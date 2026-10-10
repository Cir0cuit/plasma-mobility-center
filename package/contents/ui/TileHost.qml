// SPDX-License-Identifier: GPL-2.0-or-later
//
// Loads one tile (tiles/<Name>Tile.qml) and reports whether it can be shown.
// A tile whose QML module is missing fails to load and simply counts as
// unavailable - the rest of the widget is unaffected.

import QtQuick

Item {
    id: host

    // Set by CardGrid.
    property string tileId
    property var entry: null
    property bool expanded: false
    property bool cardsOnly: false
    property QtObject services: null
    property bool showUnavailable: false
    property real minInlineWidth: 0
    /// The user wants this tile's details inside the card.
    property bool wantInline: false

    signal toggleRequested()

    /// The loaded Card (tile root), or null.
    readonly property Item card: loader.status === Loader.Ready ? loader.item : null
    /// The tile loaded and its backend says the hardware/service is there.
    readonly property bool available: card !== null && card.available === true
    /// Part of the grid: available, or dimmed because showUnavailable is on.
    readonly property bool shown: available || (showUnavailable && card !== null)
    /// The tile has detail content to show in the panel under its row.
    readonly property bool expandable: card !== null && card.expandable === true
    /// Width at which the card's controls do not wrap (0: any).
    readonly property real naturalWidth: card && card.naturalWidth > 0 ? card.naturalWidth : 0
    /// The tile could not be loaded (module missing or a QML error).
    readonly property bool failed: loader.status === Loader.Error

    visible: shown
    implicitHeight: card ? card.implicitHeight : 0
    /// Extra height the card's details need inside the card; 0 without
    /// details or when the user keeps them behind the arrow.
    readonly property real detailsExtent: card && wantInline && card.detailsExtent > 0 ? card.detailsExtent : 0

    Loader {
        id: loader
        anchors.fill: parent
        source: host.entry ? Qt.resolvedUrl("tiles/" + host.entry.file) : ""
    }

    Binding {
        target: loader.item
        property: "tileId"
        value: host.tileId
        when: host.card !== null
    }
    Binding {
        target: loader.item
        property: "expanded"
        value: host.expanded
        when: host.card !== null
    }
    Binding {
        target: loader.item
        property: "cardsOnly"
        value: host.cardsOnly
        when: host.card !== null
    }
    Binding {
        target: loader.item
        property: "wantInline"
        value: host.wantInline
        when: host.card !== null
    }
    Binding {
        target: loader.item
        property: "minInlineWidth"
        value: host.minInlineWidth
        when: host.card !== null
    }
    Binding {
        target: loader.item
        property: "services"
        value: host.services
        when: host.card !== null
    }
    Binding {
        target: loader.item
        property: "registryEntry"
        value: host.entry
        when: host.card !== null
    }

    Connections {
        target: host.card
        function onToggleRequested() {
            host.toggleRequested();
        }
    }
}
