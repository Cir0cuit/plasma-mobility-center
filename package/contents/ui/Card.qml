// SPDX-License-Identifier: GPL-2.0-or-later
//
// Card - the root type of every tiles/<Name>Tile.qml.
//
// A tile sets:
//   available       bool       its backend has hardware/service to control
//                              (false hides the card unless "show unavailable")
//   statusText      string     short status under the title ("72 %", "Connected to X")
//   dimmed          bool       busy/connecting look (still interactive)
//   detailContent   Component  the details; null = the card has none.
//                              Shown INSIDE the card when the user wants
//                              that (wantInline) and there is room - as
//                              many whole rows as fit - and in the details
//                              flyout beside the widget, opened with the
//                              arrow, which stays while anything is cut
//                              off. Created in the tile's own context (ids
//                              resolve).
//   headerControl   Component  optional trailing control in the header (a switch)
//   title, iconName, kcm       default to the tileRegistry.js entry; override if needed
//   children                   the collapsed body (a ColumnLayout: set
//                              Layout.fillWidth on wide controls). Non-visual
//                              children (backend objects) are fine too.
//
// The host sets (read them, do not write): tileId, registryEntry, expanded,
// cardsOnly, wantInline, minInlineWidth, services (Services.qml: nightLight,
// chargeLimit, presentation).
//
// Clicking the header (or the arrow) emits toggleRequested; the grid flips
// `expanded` and opens or closes the flyout. Tiles that start work while
// details are on screen (scans, re-reads) watch `detailsShown`; `hovered` is
// for cheap re-reads.
//
// Whether details are inline is applied outside the layout pass and at most
// a few times per card size (see `internal`): a decision that feeds back
// into itself inside one pass never terminates and freezes the whole shell.

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2

import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents3

import "controls" as Controls

Item {
    id: card

    // ---- set by the tile -------------------------------------------------
    property bool available: false
    property string title: registryEntry ? i18n(registryEntry.title) : ""
    property string iconName: registryEntry ? registryEntry.icon : ""
    property string kcm: registryEntry ? registryEntry.kcm : ""
    property string statusText: ""
    property string unavailableText: i18n("Not available on this device")
    property bool dimmed: false
    property Component detailContent: null
    property Component headerControl: null
    default property alias content: body.data

    // ---- set by TileHost ---------------------------------------------------
    property string tileId: ""
    property var registryEntry: null
    property bool expanded: false
    property bool cardsOnly: false
    property QtObject services: null
    /// The user wants this card's details inside the card (settings). By
    /// default they are behind the arrow.
    property bool wantInline: false
    /// Narrower than this, the details stay behind the arrow however tall
    /// the card is: lists and labels need the width.
    property real minInlineWidth: 0

    // ---- read-only for tiles ---------------------------------------------
    /// Details are shown inside the card (all of them, or the first rows).
    readonly property bool detailsInline: internal.inlineNow
    /// Height the details take inside the card, with the gap and the
    /// separator above them; 0 without details. The grid tries to give a
    /// card this much more than implicitHeight when the user wants them
    /// inline.
    readonly property real detailsExtent: available && inlineDetails.item !== null
        ? Math.ceil(inlineDetails.item.implicitHeight) + detailsMargin : 0
    /// Height of the details that fits under the controls, cut between two
    /// rows; 0 when not even the first one fits or they are not wanted here.
    readonly property real detailsCut: wantInline && available && inlineDetails.item !== null
                                       && width + 0.5 >= minInlineWidth
        ? cutHeight(inlineDetails.item, height - implicitHeight - detailsMargin) : 0
    /// Width at which the controls need not wrap: the widest row of chips
    /// laid end to end, with the card's padding; 0 without any.
    readonly property real naturalWidth: {
        const widest = available ? unwrappedWidth(body) : 0;
        return widest > 0 ? Math.ceil(widest + 2 * padding) : 0;
    }
    /// Part of the details does not fit inside the card.
    readonly property bool detailsCutOff: detailsInline && inlineDetails.item !== null
                                          && detailsCut + 0.5 < inlineDetails.item.implicitHeight
    /// The arrow is offered: there are details that are not all on the card.
    readonly property bool expandable: detailContent !== null && available && (!detailsInline || detailsCutOff)
    /// Details are on screen, inline or in the flyout.
    readonly property bool detailsShown: detailsInline || expanded
    readonly property alias hovered: hover.hovered
    readonly property real padding: Kirigami.Units.largeSpacing

    signal toggleRequested()

    implicitWidth: Kirigami.Units.gridUnit * 10
    // The COLLAPSED height (header + controls). The grid shares the widget
    // between the cards from it and from detailsExtent. Neither may depend on
    // detailsInline, or the grid and the cards could push each other back
    // and forth.
    implicitHeight: 2 * padding + header.implicitHeight + (available ? column.spacing + body.implicitHeight : 0)

    // Gap and separator between the controls and inline details.
    readonly property real detailsMargin: 2 * column.spacing + Kirigami.Units.largeSpacing + 1

    /// The height of @p item's top part that fits in @p limit without
    /// cutting a row: column layouts (and loaders of them) are cut between
    /// their children, anything else is one piece.
    function cutHeight(item, limit) {
        if (!item || limit <= 0) {
            return 0;
        }
        if (item.implicitHeight <= limit + 0.5) {
            return item.implicitHeight;
        }
        const part = cutInside(item, limit);
        // A lone heading is not worth showing.
        return part >= Kirigami.Units.gridUnit * 3 ? part : 0;
    }

    function cutInside(item, limit) {
        if (item instanceof Loader) {
            return item.item ? cutInside(item.item, limit) : 0;
        }
        if (!(item instanceof ColumnLayout)) {
            return 0;
        }
        let end = 0;
        // Where the cut falls back to when it would come right after a
        // heading: a heading without its first row says nothing.
        let beforeHeading = -1;
        const kids = item.children;
        for (let i = 0; i < kids.length; ++i) {
            const kid = kids[i];
            if (!kid.visible || kid.height <= 0) {
                continue;
            }
            if (kid.y + kid.height <= limit + 0.5) {
                beforeHeading = isHeading(kid) ? end : -1;
                end = Math.max(end, kid.y + kid.height);
                continue;
            }
            if (kid.y < limit) {
                const part = cutInside(kid, limit - kid.y);
                if (part > 0) {
                    return Math.max(end, kid.y + part);
                }
            }
            return beforeHeading >= 0 ? beforeHeading : end;
        }
        return end;
    }

    /// The width @p item's flows would have in one row (their children do
    /// not depend on the card's width, so neither does this).
    function unwrappedWidth(item) {
        if (item instanceof Loader) {
            return item.item ? unwrappedWidth(item.item) : 0;
        }
        const kids = item.children;
        let width = 0;
        if (item instanceof Flow) {
            for (let i = 0; i < kids.length; ++i) {
                if (kids[i].visible && kids[i].width > 0) {
                    width += kids[i].width + (width > 0 ? item.spacing : 0);
                }
            }
        } else if (item instanceof ColumnLayout) {
            for (let j = 0; j < kids.length; ++j) {
                if (kids[j].visible) {
                    width = Math.max(width, unwrappedWidth(kids[j]));
                }
            }
        }
        return width;
    }

    function isHeading(item) {
        if (item instanceof RowLayout) {
            return item.children.length > 0 && isHeading(item.children[0]);
        }
        return item.level !== undefined && item.text !== undefined;
    }

    onDetailsCutChanged: internal.schedule()
    QtObject {
        id: internal

        property bool inlineNow: false
        // Times the details went inline in quick succession. Content whose
        // height depends on being shown would otherwise flip for ever;
        // after two tries the card stays with the arrow, which always
        // works. A change of the card's size or of the user's choice, or a
        // quiet second, starts the count again.
        property int inlineTries: 0
        readonly property int maxInlineTries: 2

        function startOver() {
            inlineTries = 0;
            schedule();
        }

        function changedFromOutside() {
            calm.interval = 1000;
            startOver();
        }

        // Never from inside the layout pass that measured the details: a
        // zero timer fires from the event loop.
        function schedule() {
            applyTimer.start();
        }

        function apply() {
            const want = card.detailsCut > 0;
            if (want === inlineNow) {
                return;
            }
            if (want) {
                if (inlineTries >= maxInlineTries) {
                    return;
                }
                ++inlineTries;
            }
            inlineNow = want;
            calm.restart();
        }
    }

    onWidthChanged: internal.changedFromOutside()
    onHeightChanged: internal.changedFromOutside()
    onWantInlineChanged: internal.changedFromOutside()

    Timer {
        id: applyTimer
        interval: 0
        onTriggered: internal.apply()
    }

    // The mode has been still for a while: whatever made it flip is over.
    // If the card gave up on inline meanwhile, it tries again - but waits
    // four times longer after every such round, so content that really
    // contradicts itself ends up flipping a few times an hour at most.
    Timer {
        id: calm
        interval: 1000
        onTriggered: {
            if (card.detailsCut > 0 && !internal.inlineNow) {
                interval = Math.min(interval * 4, 1200000);
            }
            internal.startOver();
        }
    }

    // The grid fits cards to the widget; in a widget sized too small for
    // their content they clip instead of overflowing into each other.
    clip: true

    Kirigami.Theme.colorSet: Kirigami.Theme.View
    Kirigami.Theme.inherit: false

    HoverHandler {
        id: hover
    }

    Controls.CardBackground {
        anchors.fill: parent
        cardsOnly: card.cardsOnly
        highlighted: card.expanded
        hovered: card.hovered && card.expandable
    }

    ColumnLayout {
        id: column
        anchors.fill: parent
        anchors.margins: card.padding
        spacing: Kirigami.Units.smallSpacing
        opacity: card.available ? (card.dimmed ? 0.6 : 1) : 0.5

        RowLayout {
            id: header
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            Kirigami.Icon {
                source: card.iconName
                Layout.preferredWidth: Kirigami.Units.iconSizes.smallMedium
                Layout.preferredHeight: Kirigami.Units.iconSizes.smallMedium
                Layout.alignment: Qt.AlignTop
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0

                PlasmaComponents3.Label {
                    Layout.fillWidth: true
                    text: card.title
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                }
                // One line, whatever the text: a status that wraps when
                // it changes would resize the card and move its neighbours.
                Controls.StatusLine {
                    id: status
                    Layout.fillWidth: true
                    maximumLineCount: 1
                    text: card.available ? card.statusText : card.unavailableText

                    HoverHandler {
                        id: statusHover
                    }
                    QQC2.ToolTip.text: status.text
                    QQC2.ToolTip.visible: statusHover.hovered && status.truncated
                    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                }
            }

            Loader {
                Layout.alignment: Qt.AlignTop
                active: card.available && card.headerControl !== null
                sourceComponent: card.headerControl
                visible: active
            }

            Controls.SettingsButton {
                Layout.alignment: Qt.AlignTop
                kcm: card.kcm
            }

            PlasmaComponents3.ToolButton {
                Layout.alignment: Qt.AlignTop
                // Keeps its place while the details are inline, so the
                // header (and with it the card's height) stays the same.
                visible: card.available && card.detailContent !== null
                opacity: card.expandable ? 1 : 0
                enabled: card.expandable
                icon.name: "arrow-right"
                checkable: true
                checked: card.expanded
                display: QQC2.AbstractButton.IconOnly
                text: card.expanded ? i18n("Hide details") : i18n("Show details")
                QQC2.ToolTip.text: text
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                onClicked: card.toggleRequested()
            }

            TapHandler {
                enabled: card.expandable
                onTapped: card.toggleRequested()
            }
        }

        // Cards stretch to fill the widget: centre the controls in the space
        // under the header instead of leaving it all empty at the bottom.
        Item {
            Layout.fillHeight: true
            visible: !card.detailsInline
        }

        ColumnLayout {
            id: body
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing
            visible: card.available
        }

        Kirigami.Separator {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.smallSpacing
            visible: card.detailsInline
        }

        // Where the inline details go (the details themselves: below).
        Item {
            id: inlineSlot
            Layout.fillWidth: true
            Layout.preferredHeight: card.detailsCut
            visible: card.detailsInline
        }

        Item {
            Layout.fillHeight: true
        }
    }

    // The details, always instantiated when the card has any so their height
    // is known. They are outside the layout and stay visible - transparent,
    // disabled and parked under the card's clipped edge - while not shown:
    // Plasma's controls only report their real size when visible, so details
    // measured hidden looked shorter than they are. Shown, they are clipped
    // at detailsCut.
    Item {
        x: column.x
        y: card.detailsInline ? column.y + inlineSlot.y : card.height
        width: column.width
        height: card.detailsInline ? card.detailsCut : inlineDetails.height
        clip: true
        opacity: card.detailsInline ? column.opacity : 0
        enabled: card.detailsInline

        Loader {
            id: inlineDetails
            width: parent.width
            active: card.available && card.detailContent !== null
            sourceComponent: card.detailContent
        }
    }
}
