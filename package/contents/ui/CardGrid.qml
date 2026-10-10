// SPDX-License-Identifier: GPL-2.0-or-later
//
// The cards, always filling the widget (geometry: layout.js), plus the
// details flyout.
//
// The widget never scrolls. Cards share it in proportion to their content:
// they are arranged in nested rows and columns, a card with more to show
// gets more room, and light cards can sit side by side. Details are shown
// inside a card when there is room; otherwise they open in a Plasma popup
// window next to the card, which never moves or resizes anything in the
// widget.
// Only one flyout is open at a time; it closes on Esc, on clicking elsewhere,
// or with the card's button.

import QtQuick
import QtQuick.Layouts

import org.kde.kirigami as Kirigami
import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.components as PlasmaComponents3

import "layout.js" as Geometry
import "tileRegistry.js" as Registry

Item {
    id: grid

    property var tileIds: []
    property real minCardWidth: Kirigami.Units.gridUnit * 10
    property real spacing: Kirigami.Units.largeSpacing
    property bool cardsOnly: false
    property QtObject services: null
    property bool showUnavailable: false
    /// Tiles whose details the user wants inside the card (settings); the
    /// others keep them behind the arrow.
    property var inlineTiles: []
    /// Log every layout pass and its cause (hidden setting debugLayout).
    property bool debug: false

    /// Id of the tile whose details flyout is open, "" for none. Reset when
    /// that tile disappears, becomes unavailable or loses its details.
    property string expandedId: ""

    /// Number of cards currently in the grid.
    readonly property int shownCount: d.shownHosts.length
    /// Ids of tiles that are loaded/attempted but not available.
    readonly property var unavailableIds: d.unavailableIds
    /// The first layout pass has run (placeholders wait for it).
    readonly property bool settled: d.settled

    /// Narrower than this, a card keeps its details behind the arrow.
    property real minInlineWidth: minCardWidth * 4 / 3
    /// The widest a card can make use of; more counts as empty space when
    /// the layout is chosen.
    property real prefCardWidth: minCardWidth * 2.2

    /// @p cause: "content" when a card's measured content changed, "state"
    /// when only a flyout or a card's mode did; otherwise the change came
    /// from outside (see d.relayout for why that matters).
    function scheduleLayout(cause) {
        if (cause === "content") {
            d.contentChange = true;
        } else if (cause !== "state") {
            d.outsideChange = true;
        }
        if (!d.pending) {
            d.pending = true;
            Qt.callLater(d.relayout);
        }
    }

    onWidthChanged: d.sizeChanged()
    onHeightChanged: d.sizeChanged()
    onTileIdsChanged: scheduleLayout()
    onMinCardWidthChanged: scheduleLayout()
    onPrefCardWidthChanged: scheduleLayout()
    onMinInlineWidthChanged: scheduleLayout()
    onSpacingChanged: scheduleLayout()
    onShowUnavailableChanged: scheduleLayout()
    onInlineTilesChanged: scheduleLayout()
    onExpandedIdChanged: scheduleLayout("state")

    QtObject {
        id: d

        property var shownHosts: []
        property var unavailableIds: []
        property Item expandedHost: null
        property bool pending: false
        property bool settled: false
        property bool animate: false

        // When the layout may change. The cards must not move by
        // themselves, so there are two phases:
        //
        //   settling - after a change from outside (widget size, tiles,
        //     settings, a card appearing). Card sizes follow the cards'
        //     content and the content's height follows the cards' width
        //     (chips flow, lists re-wrap), so a few rounds are needed. Should
        //     they not end, the arrangement and all widths are held after
        //     maxContentRounds and only the heights follow, which cannot
        //     loop. The phase ends when a second passes without a round.
        //     Nothing is animated meanwhile: content measured in a card
        //     that is still on its way to its size would be wrong.
        //   quiet - afterwards. The arrangement stays. A card's needs only
        //     count upwards (`laid`), and the sizes are worked out again
        //     only when a card's content no longer fits its place, or when a
        //     card lost most of its content for good (Wi-Fi switched off).
        //     A list that gains and loses a row now and then moves nothing.
        property bool outsideChange: true
        property bool contentChange: false
        property bool settling: true
        property int contentRounds: 0
        property var held: null
        property var last: null
        // By tile id: the needs and the height of the layout on screen.
        property var laid: ({})
        // By tile id: the largest needs measured in this settling.
        property var peak: null
        property bool shrinkConfirmed: false
        readonly property int maxContentRounds: 10

        // While the widget is being resized the cards keep their arrangement
        // and only stretch (cheap, and nothing jumps around under the
        // pointer); the best arrangement for the new size is looked up when
        // the size has been still for a moment - or at once, when the
        // current one no longer fits.
        property bool resizing: false

        function sizeChanged() {
            if (settled) {
                resizing = true;
                resizeDone.restart();
            }
            grid.scheduleLayout();
        }

        function relayout() {
            pending = false;
            const hosts = [];
            const unavailable = [];
            for (let i = 0; i < repeater.count; ++i) {
                const host = repeater.itemAt(i);
                if (!host) {
                    continue;
                }
                if (host.shown) {
                    hosts.push(host);
                }
                if (!host.available) {
                    unavailable.push(host.tileId);
                }
            }

            const expandedIndex = Geometry.resolveExpanded(hosts.map(h => ({
                id: h.tileId,
                available: h.available,
                expandable: h.expandable
            })), grid.expandedId);
            if (expandedIndex === -1 && grid.expandedId !== "") {
                // Will re-run through onExpandedIdChanged.
                grid.expandedId = "";
            }
            expandedHost = expandedIndex >= 0 ? hosts[expandedIndex] : null;

            if (JSON.stringify(unavailable) !== JSON.stringify(unavailableIds)) {
                unavailableIds = unavailable;
            }

            const current = hosts.map(h => ({ collapsed: h.implicitHeight, details: h.detailsExtent, natural: h.naturalWidth }));
            const cause = (outsideChange ? "outside " : "") + (contentChange ? "content " : "") + (resizing ? "resizing " : "");
            if (outsideChange || last === null) {
                settling = true;
                contentRounds = 0;
                held = null;
            }

            let needs = current;
            let keep = null;
            if (settling) {
                settleDone.restart();
                shrinkCheck.stop();
                if (outsideChange || last === null) {
                    // These needs were measured in the old layout.
                    peak = null;
                } else if (contentChange) {
                    // A card at a wrapping threshold needs less when it is
                    // given more and more when it is given less; taking the
                    // needs as measured would go back and forth. Within one
                    // settling they only count upwards, so this ends.
                    const tops = {};
                    needs = hosts.map((h, k) => {
                        const top = peak !== null ? peak[h.tileId] : undefined;
                        const now = current[k];
                        const most = top ? { collapsed: Math.max(now.collapsed, top.collapsed), details: Math.max(now.details, top.details),
                                             natural: now.natural } : now;
                        tops[h.tileId] = most;
                        return most;
                    });
                    peak = tops;
                    if (++contentRounds > maxContentRounds && held === null && last !== null) {
                        held = last;
                    }
                }
                keep = held !== null ? held : (resizing && last !== null ? { key: last.key } : null);
            } else {
                let outgrown = false;
                let shrunk = false;
                needs = hosts.map((h, k) => {
                    const was = laid[h.tileId];
                    const now = current[k];
                    if (!was) {
                        outgrown = true;
                        return now;
                    }
                    if (now.collapsed + now.details > was.collapsed + was.details + 0.5
                            && now.collapsed + now.details > was.height + 0.5) {
                        outgrown = true;
                    }
                    const lost = was.collapsed + was.details - now.collapsed - now.details;
                    if (lost >= Math.max(Kirigami.Units.gridUnit * 6, 0.4 * (was.collapsed + was.details))) {
                        shrunk = true;
                    }
                    return { collapsed: Math.max(now.collapsed, was.collapsed), details: Math.max(now.details, was.details),
                             natural: now.natural };
                });
                const giveBack = shrunk && shrinkConfirmed;
                shrinkConfirmed = false;
                if (giveBack) {
                    needs = current;
                } else if (shrunk) {
                    shrinkCheck.start();
                } else {
                    shrinkCheck.stop();
                }
                if (!outgrown && !giveBack) {
                    if (grid.debug && contentChange) {
                        console.info("MCLAYOUT quiet, nothing to do:", hosts.map((h, k) => h.tileId.substr(0, 4) + ":"
                            + Math.round(current[k].collapsed) + "+" + Math.round(current[k].details)).join(" "));
                    }
                    outsideChange = false;
                    contentChange = false;
                    return;
                }
                keep = { key: last.key };
            }
            outsideChange = false;
            contentChange = false;

            const params = {
                width: grid.width,
                height: grid.height,
                spacing: grid.spacing,
                cards: needs,
                minCardWidth: grid.minCardWidth,
                minInlineWidth: grid.minInlineWidth,
                prefCardWidth: grid.prefCardWidth,
                current: last !== null ? last.key : "",
                keep: keep
            };
            let r = Geometry.compute(params);
            if (!r.fits && keep !== null && held === null) {
                params.keep = null;
                r = Geometry.compute(params);
            }
            if (grid.debug) {
                console.info("MCLAYOUT", settling ? "settling" : "quiet", cause, held !== null ? "held" : "", "round", contentRounds,
                             grid.width + "x" + grid.height, (last !== null && last.key !== r.key ? "REARRANGED " : "") + r.key,
                             hosts.map((h, k) => h.tileId.substr(0, 4) + ":" + Math.round(needs[k].collapsed) + "+" + Math.round(needs[k].details)
                                       + "w" + Math.round(needs[k].natural) + "@" + r.cards[k].x + "," + r.cards[k].y + " " + r.cards[k].width + "x" + r.cards[k].height).join(" "));
            }
            if (grid.debug) {
                debugState.restart();
            }
            last = { key: r.key, widths: r.widths };
            const nowLaid = {};
            for (let k = 0; k < hosts.length; ++k) {
                const c = r.cards[k];
                hosts[k].x = c.x;
                hosts[k].y = c.y;
                hosts[k].width = c.width;
                hosts[k].height = c.height;
                nowLaid[hosts[k].tileId] = { collapsed: needs[k].collapsed, details: needs[k].details, height: c.height };
            }
            laid = nowLaid;
            shownHosts = hosts;
            if (!settled) {
                settled = true;
                // Animate moves only after the first layout.
                Qt.callLater(() => { d.animate = true; });
            }
        }
    }

    Timer {
        id: resizeDone
        interval: 150
        onTriggered: {
            d.resizing = false;
            grid.scheduleLayout();
        }
    }

    Timer {
        id: debugState
        interval: 1500
        onTriggered: console.info("MCSTATE", d.shownHosts.map(h => h.tileId.substr(0, 4) + " " + h.width + "x" + h.height
            + " collapsed=" + Math.round(h.card.implicitHeight) + " details=" + Math.round(h.card.detailsExtent)
            + " cut=" + Math.round(h.card.detailsCut) + (h.card.detailsInline ? " inline" : "") + (h.card.expandable ? " arrow" : "")).join(" | "))
    }

    // A second without a layout round: the cards have settled.
    Timer {
        id: settleDone
        interval: 1000
        onTriggered: d.settling = false
    }

    // A card that lost most of its content gives the room back, but only
    // when it stays that way.
    Timer {
        id: shrinkCheck
        interval: 2500
        onTriggered: {
            d.shrinkConfirmed = true;
            grid.scheduleLayout("content");
        }
    }

    Repeater {
        id: repeater
        model: grid.tileIds

        delegate: TileHost {
            id: tileHost

            required property string modelData

            tileId: modelData
            entry: Registry.byId(modelData)
            expanded: grid.expandedId === modelData
            cardsOnly: grid.cardsOnly
            services: grid.services
            showUnavailable: grid.showUnavailable
            minInlineWidth: grid.minInlineWidth
            wantInline: grid.inlineTiles.indexOf(modelData) !== -1

            onToggleRequested: grid.expandedId = (grid.expandedId === modelData ? "" : modelData)
            onShownChanged: grid.scheduleLayout()
            onAvailableChanged: grid.scheduleLayout()
            onExpandableChanged: grid.scheduleLayout("state")
            onFailedChanged: grid.scheduleLayout()
            onImplicitHeightChanged: grid.scheduleLayout("content")
            onDetailsExtentChanged: grid.scheduleLayout("content")
            onNaturalWidthChanged: grid.scheduleLayout("content")
            Component.onCompleted: grid.scheduleLayout()
            Component.onDestruction: grid.scheduleLayout()

            Behavior on x {
                enabled: d.animate && !d.settling
                NumberAnimation { duration: Kirigami.Units.longDuration; easing.type: Easing.InOutQuad }
            }
            Behavior on y {
                enabled: d.animate && !d.settling
                NumberAnimation { duration: Kirigami.Units.longDuration; easing.type: Easing.InOutQuad }
            }
            Behavior on width {
                enabled: d.animate && !d.settling
                NumberAnimation { duration: Kirigami.Units.longDuration; easing.type: Easing.InOutQuad }
            }
            Behavior on height {
                enabled: d.animate && !d.settling
                NumberAnimation { duration: Kirigami.Units.longDuration; easing.type: Easing.InOutQuad }
            }
        }
    }

    // The details flyout of the expanded card.
    PlasmaCore.Dialog {
        id: flyout

        readonly property Item card: d.expandedHost ? d.expandedHost.card : null
        // Wide enough for a network list; long lists scroll inside the flyout,
        // never the widget.
        readonly property real contentWidth: Kirigami.Units.gridUnit * 22
        readonly property real maxContentHeight: Kirigami.Units.gridUnit * 26

        visualParent: card
        location: PlasmaCore.Types.Floating
        type: PlasmaCore.Dialog.PopupMenu
        hideOnWindowDeactivate: true
        visible: card !== null && card.available && detailLoader.status === Loader.Ready
        flags: Qt.WindowStaysOnTopHint

        onVisibleChanged: {
            // Closed by Esc or a click elsewhere: forget it, so the card's
            // button state matches.
            if (!visible && card !== null) {
                grid.expandedId = "";
            }
        }

        mainItem: FocusScope {
            id: flyoutContent

            width: flyout.contentWidth
            height: Math.min(flyout.maxContentHeight, header.implicitHeight + Kirigami.Units.smallSpacing + scroll.contentHeight)

            Kirigami.Theme.colorSet: Kirigami.Theme.View
            Kirigami.Theme.inherit: false

            Keys.onEscapePressed: grid.expandedId = ""

            RowLayout {
                id: header
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                spacing: Kirigami.Units.smallSpacing

                Kirigami.Icon {
                    source: flyout.card ? flyout.card.iconName : ""
                    Layout.preferredWidth: Kirigami.Units.iconSizes.smallMedium
                    Layout.preferredHeight: Kirigami.Units.iconSizes.smallMedium
                }
                Kirigami.Heading {
                    Layout.fillWidth: true
                    level: 4
                    text: flyout.card ? flyout.card.title : ""
                    elide: Text.ElideRight
                }
                PlasmaComponents3.ToolButton {
                    icon.name: "window-close"
                    display: PlasmaComponents3.AbstractButton.IconOnly
                    text: i18n("Close")
                    onClicked: grid.expandedId = ""
                }
            }

            PlasmaComponents3.ScrollView {
                id: scroll
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: header.bottom
                anchors.topMargin: Kirigami.Units.smallSpacing
                anchors.bottom: parent.bottom
                contentWidth: availableWidth
                contentHeight: detailLoader.item ? detailLoader.item.implicitHeight : 0

                Loader {
                    id: detailLoader
                    width: scroll.availableWidth
                    // Created in the tile's own context, so its ids and backends resolve.
                    sourceComponent: flyout.card && flyout.card.available ? flyout.card.detailContent : null
                    onItemChanged: {
                        if (item) {
                            item.width = Qt.binding(() => detailLoader.width);
                        }
                    }
                }
            }
        }
    }
}
