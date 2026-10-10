// SPDX-License-Identifier: GPL-2.0-or-later
//
// The widget's content: the card grid. Loaded by main.qml through a Loader,
// because importing the .core module fails when the C++ half is missing.

import QtQuick
import QtQuick.Layouts

import org.kde.plasma.plasmoid
import org.kde.plasma.components as PlasmaComponents3
import org.kde.plasma.extras as PlasmaExtras
import org.kde.kirigami as Kirigami

import io.github.cir0cuit.mobilitycenter.core

import "tileRegistry.js" as Registry

Item {
    id: view

    // The C++ <-> QML contract this package was written against
    // (MC_API_VERSION in CMakeLists.txt).
    readonly property int expectedApiVersion: 2
    readonly property bool apiMatches: ModuleInfo.apiVersion === expectedApiVersion

    // Set by main.qml.
    property bool cardsOnly: false

    readonly property var cfg: Plasmoid.configuration
    readonly property var tileIds: Registry.visibleIds(cfg.tileOrder, cfg.hiddenTiles)

    Services {
        id: sharedServices
        nightLightEnableMode: view.cfg.nightLightEnableMode
        presentationInhibitsNightLight: view.cfg.presentationInhibitsNightLight
    }

    // Presentation mode is never left on without a way to switch it off:
    // release it when the widget is removed (deferred and undoable, but we do
    // not re-inhibit on Undo) and when its tile is hidden.
    Connections {
        target: Plasmoid
        function onDestroyedChanged(destroyed) {
            if (destroyed) {
                sharedServices.presentation.active = false;
            }
        }
    }
    onTileIdsChanged: {
        if (tileIds.indexOf("presentation") === -1 && sharedServices.presentation.active) {
            sharedServices.presentation.active = false;
        }
    }

    // Fills the widget; never scrolls (details open in a flyout).
    CardGrid {
        id: grid
        anchors.fill: parent
        visible: view.apiMatches
        tileIds: view.tileIds
        minCardWidth: Kirigami.Units.gridUnit * Math.max(7, view.cfg.minCardWidth)
        cardsOnly: view.cardsOnly
        services: sharedServices
        showUnavailable: view.cfg.showUnavailable
        inlineTiles: Registry.toArray(view.cfg.inlineTiles)
        debug: view.cfg.debugLayout
    }

    PlasmaExtras.PlaceholderMessage {
        anchors.centerIn: parent
        width: parent.width - Kirigami.Units.gridUnit * 2
        visible: view.apiMatches && grid.settled && grid.shownCount === 0
        iconName: "computer-laptop"
        text: view.tileIds.length === 0 ? i18n("All tiles are hidden") : i18n("No controls available on this device")
        explanation: view.tileIds.length === 0 ? i18n("Choose the tiles to show in the widget's settings.") : ""
    }

    PlasmaExtras.PlaceholderMessage {
        anchors.centerIn: parent
        width: parent.width - Kirigami.Units.gridUnit * 2
        visible: !view.apiMatches
        iconName: "dialog-warning"
        text: i18n("The installed backend does not match this widget version")
        explanation: i18n("Reinstall with install.sh and restart plasmashell (backend API %1, widget expects %2).",
                          ModuleInfo.apiVersion, view.expectedApiVersion)
    }

    // Remember which tiles were unavailable, so the settings page can grey
    // them out. Debounced, and written only when the set changes.
    Timer {
        id: unavailableWriter
        interval: 5000
        onTriggered: {
            const ids = grid.unavailableIds.slice().sort();
            const stored = Array.from(view.cfg.unavailableTiles || []).sort();
            if (JSON.stringify(ids) !== JSON.stringify(stored)) {
                view.cfg.unavailableTiles = ids;
            }
        }
    }
    Connections {
        target: grid
        function onUnavailableIdsChanged() {
            unavailableWriter.restart();
        }
    }
}
