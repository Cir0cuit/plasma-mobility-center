// SPDX-License-Identifier: GPL-2.0-or-later
//
// Backend objects shared by several tiles (one instance per widget):
//   * nightLight   - Display tile chip + PresentationMode's Night Light hook;
//   * chargeLimit  - the charge-limit slider in the Battery & Power details
//                    (+ BatteryMonitor.chargeStopThreshold);
//   * presentation - Presentation tile; also released by CenterView on
//                    widget removal and when its tile is hidden.
// Every other backend is created inside its own tile.

import QtQuick

import io.github.cir0cuit.mobilitycenter.core

QtObject {
    id: services

    property string nightLightEnableMode: "config"
    property bool presentationInhibitsNightLight: true

    readonly property NightLight nightLight: NightLight {
        enableMode: services.nightLightEnableMode
    }

    readonly property ChargeLimit chargeLimit: ChargeLimit {}

    readonly property PresentationMode presentation: PresentationMode {
        nightLight: services.nightLight
        inhibitNightLight: services.presentationInhibitsNightLight
    }
}
