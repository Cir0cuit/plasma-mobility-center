// SPDX-License-Identifier: GPL-2.0-or-later
//
// "Adaptive" chip on the Brightness card: KWin's automatic brightness from
// the ambient light sensor (libkscreen Output::automaticBrightness, Plasma
// 6.6+). Imports the .display module, so BrightnessTile loads it through a
// Loader: without that module (or on older Plasma) the chip just is not there.

import QtQuick
import QtQuick.Controls as QQC2

import org.kde.kirigami as Kirigami

import io.github.cir0cuit.mobilitycenter.display

import "../controls" as Controls

Controls.Chip {
    id: chip

    readonly property bool available: displayConfig.autoBrightnessAvailable
    readonly property bool enabledState: displayConfig.autoBrightnessEnabled

    visible: available
    enabled: !displayConfig.busy
    icon.name: "brightness-high"
    text: i18nc("automatic screen brightness from the light sensor", "Adaptive")
    active: displayConfig.autoBrightnessEnabled
    onClicked: displayConfig.setAutoBrightnessEnabled(!displayConfig.autoBrightnessEnabled)

    QQC2.ToolTip.text: i18n("Adjust the screen brightness to the ambient light")
    QQC2.ToolTip.visible: hovered
    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

    DisplayConfig {
        id: displayConfig
    }
}
