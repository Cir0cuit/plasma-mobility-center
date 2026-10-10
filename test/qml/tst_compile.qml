// SPDX-License-Identifier: GPL-2.0-or-later
//
// Every QML file of the package compiles against the built modules
// (-import build/qml): catches typos, wrong imports and API drift between a
// tile and its backend header. Compile only - nothing is instantiated, so no
// backend starts and no D-Bus traffic happens.

import QtQuick
import QtTest

import io.github.cir0cuit.mobilitycenter.core

TestCase {
    name: "Compile"

    readonly property string ui: Qt.resolvedUrl("../../package/contents/ui/")

    // Files that import an optional module; skipped when it was not built
    // (their tiles then fail to load at runtime and stay hidden, by design).
    readonly property var needs: ({
        "tiles/VolumeTile.qml": "audio",
        "tiles/WifiSection.qml": "network",
        "tiles/DisplaySection.qml": "display"
    })

    function test_compile_data() {
        return [
            "main.qml", "CenterView.qml", "Services.qml", "CardGrid.qml", "TileHost.qml", "Card.qml",
            "ConfigTiles.qml", "ConfigBehavior.qml", "../config/config.qml",
            "controls/CardBackground.qml", "controls/Chip.qml", "controls/ChipRow.qml", "controls/LevelSelector.qml",
            "controls/PercentSlider.qml", "controls/SettingsButton.qml", "controls/StatusLine.qml",
            "tiles/BrightnessTile.qml", "tiles/KeyboardBacklightTile.qml", "tiles/VolumeTile.qml",
            "tiles/BatteryTile.qml", "tiles/PowerProfileChips.qml", "tiles/ChargeLimitSlider.qml",
            "tiles/DisplayTile.qml", "tiles/DisplaySection.qml", "tiles/AdaptiveBrightnessChip.qml", "tiles/NightLightChip.qml",
            "tiles/WirelessTile.qml", "tiles/WifiSection.qml", "tiles/AirplaneMode.qml", "tiles/BluetoothSection.qml",
            "tiles/PresentationTile.qml"
        ].map(file => ({ tag: file, file: file, needs: needs[file] || "core" }));
    }

    function test_compile(data) {
        if (ModuleInfo.builtModules.indexOf(data.needs) === -1) {
            skip("the ." + data.needs + " module was not built");
        }
        const component = Qt.createComponent(ui + data.file, Component.PreferSynchronous);
        tryVerify(() => component.status !== Component.Loading, 5000);
        verify(component.status === Component.Ready, data.file + ": " + component.errorString());
    }
}
