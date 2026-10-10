// SPDX-License-Identifier: GPL-2.0-or-later
//
// Settings page: which tiles to show, in which order, and whether a tile's
// details are shown inside its card or kept behind its arrow.
//
// Every edit assigns NEW arrays to cfg_tileOrder / cfg_hiddenTiles /
// cfg_inlineTiles: on
// Plasma 6.3 the dialog's saveConfig() runs after writeConfig(), so nothing
// may rely on it, and mutating an array in place does not mark the page as
// changed.

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2

import org.kde.kcmutils as KCM
import org.kde.kirigami as Kirigami
import org.kde.plasma.plasmoid

import "tileRegistry.js" as Registry

KCM.ScrollViewKCM {
    id: page

    property var cfg_tileOrder: []
    property var cfg_hiddenTiles: []
    property var cfg_inlineTiles: []
    property int cfg_minCardWidth
    property bool cfg_showUnavailable

    readonly property var order: Registry.mergeOrder(cfg_tileOrder)
    // Written by the widget (CenterView.qml); read-only here.
    readonly property var unavailable: {
        try {
            return Registry.toArray(Plasmoid.configuration.unavailableTiles);
        } catch (e) {
            return [];
        }
    }

    function isHidden(id) {
        return Registry.toArray(cfg_hiddenTiles).indexOf(id) !== -1;
    }

    /// cfg_hiddenTiles with @p id hidden or shown, as a NEW array.
    function setHiddenFor(id, hide) {
        return Registry.setHidden(cfg_hiddenTiles, id, hide);
    }

    /// Details are behind the arrow unless the user put them in the card.
    function detailsInCard(id) {
        return Registry.toArray(cfg_inlineTiles).indexOf(id) !== -1;
    }

    /// cfg_inlineTiles with @p id in the card or behind the arrow, as a NEW array.
    function setInCardFor(id, inCard) {
        return Registry.setHidden(cfg_inlineTiles, id, inCard);
    }

    // The list shows a ListModel rather than `order` itself: dragging with
    // Kirigami.ListItemDragHandle emits moveRequested on every step, and
    // resetting a JS-array model there would destroy the dragged delegate.
    // Edits move the ListModel row and then store a NEW array; the model is
    // only rebuilt when the order changes from outside (defaults, reset).
    ListModel {
        id: tileModel
    }

    function modelIds() {
        const ids = [];
        for (let i = 0; i < tileModel.count; ++i) {
            ids.push(tileModel.get(i).tileId);
        }
        return ids;
    }

    function syncModel() {
        const wanted = order;
        if (JSON.stringify(wanted) === JSON.stringify(modelIds())) {
            return;
        }
        tileModel.clear();
        for (const id of wanted) {
            tileModel.append({ tileId: id });
        }
    }

    function moveTile(from, to) {
        if (from === to || from < 0 || to < 0 || from >= tileModel.count || to >= tileModel.count) {
            return;
        }
        tileModel.move(from, to, 1);
        cfg_tileOrder = modelIds();
    }

    onOrderChanged: syncModel()
    Component.onCompleted: syncModel()

    header: ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        Kirigami.FormLayout {
            Layout.fillWidth: true

            QQC2.SpinBox {
                Kirigami.FormData.label: i18n("Minimum card width:")
                from: 7
                to: 30
                value: page.cfg_minCardWidth
                onValueModified: page.cfg_minCardWidth = value
                textFromValue: (value, locale) => i18ncp("card width in grid units", "%1 unit", "%1 units", value)
                valueFromText: (text, locale) => parseInt(text)
            }
            QQC2.CheckBox {
                text: i18n("Show dimmed cards for missing hardware")
                checked: page.cfg_showUnavailable
                onToggled: page.cfg_showUnavailable = checked
            }
        }

        Kirigami.InlineMessage {
            objectName: "detailsNote"
            Layout.fillWidth: true
            visible: true
            type: Kirigami.MessageType.Information
            text: i18n("A tile’s details open with the arrow on its card. They can be shown in the card instead, but that needs room: the widget has to be large enough. A card shows as many of its details as fit, and its arrow opens the rest. If details are missing, make the widget larger or leave other tiles’ details behind the arrow.")
        }
    }

    view: ListView {
        id: tileListView
        objectName: "tileList"

        model: tileModel
        reuseItems: false

        delegate: QQC2.ItemDelegate {
            id: delegate

            required property string tileId
            required property int index

            readonly property var entry: Registry.byId(tileId)
            readonly property bool unavailable: page.unavailable.indexOf(tileId) !== -1

            width: ListView.view.width
            down: false
            hoverEnabled: false

            contentItem: RowLayout {
                spacing: Kirigami.Units.smallSpacing

                Kirigami.ListItemDragHandle {
                    listItem: delegate
                    listView: tileListView
                    onMoveRequested: (oldIndex, newIndex) => page.moveTile(oldIndex, newIndex)
                }

                QQC2.CheckBox {
                    checked: !page.isHidden(delegate.tileId)
                    onToggled: page.cfg_hiddenTiles = page.setHiddenFor(delegate.tileId, !checked)
                    QQC2.ToolTip.text: checked ? i18n("Hide this tile") : i18n("Show this tile")
                    QQC2.ToolTip.visible: hovered
                }

                Kirigami.Icon {
                    source: delegate.entry ? delegate.entry.icon : ""
                    Layout.preferredWidth: Kirigami.Units.iconSizes.smallMedium
                    Layout.preferredHeight: Kirigami.Units.iconSizes.smallMedium
                    opacity: delegate.unavailable ? 0.5 : 1
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    QQC2.Label {
                        Layout.fillWidth: true
                        text: delegate.entry ? i18n(delegate.entry.title) : delegate.tileId
                        elide: Text.ElideRight
                        opacity: delegate.unavailable ? 0.5 : 1
                    }
                    QQC2.Label {
                        Layout.fillWidth: true
                        visible: delegate.unavailable
                        text: i18n("Not available on this device")
                        font: Kirigami.Theme.smallFont
                        opacity: 0.6
                        elide: Text.ElideRight
                    }
                }

                QQC2.ComboBox {
                    objectName: "detailsMode-" + delegate.tileId
                    visible: Registry.hasDetails(delegate.tileId)
                    enabled: !page.isHidden(delegate.tileId)
                    // Wide enough for the longer of the two choices.
                    implicitContentWidthPolicy: QQC2.ComboBox.WidestText
                    model: [i18nc("where a tile's details are shown", "Details behind the arrow"),
                            i18nc("where a tile's details are shown", "Details in the card")]
                    currentIndex: page.detailsInCard(delegate.tileId) ? 1 : 0
                    onActivated: index => page.cfg_inlineTiles = page.setInCardFor(delegate.tileId, index === 1)
                    QQC2.ToolTip.text: i18n("In the card, details are shown as far as there is room; the arrow opens the rest.")
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                }

                QQC2.ToolButton {
                    icon.name: "arrow-up"
                    display: QQC2.AbstractButton.IconOnly
                    text: i18n("Move up")
                    enabled: delegate.index > 0
                    onClicked: page.moveTile(delegate.index, delegate.index - 1)
                    QQC2.ToolTip.text: text
                    QQC2.ToolTip.visible: hovered
                }
                QQC2.ToolButton {
                    icon.name: "arrow-down"
                    display: QQC2.AbstractButton.IconOnly
                    text: i18n("Move down")
                    enabled: delegate.index < tileModel.count - 1
                    onClicked: page.moveTile(delegate.index, delegate.index + 1)
                    QQC2.ToolTip.text: text
                    QQC2.ToolTip.visible: hovered
                }
            }
        }
    }
}
