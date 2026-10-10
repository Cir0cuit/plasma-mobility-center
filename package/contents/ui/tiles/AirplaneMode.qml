// SPDX-License-Identifier: GPL-2.0-or-later
//
// Airplane mode chip. Snapshot/restore planning is in
// airplane.js (unit-tested); this file gathers the live state and applies
// the plan. The snapshot lives in the applet config (airplaneRestore*).
//
// Inputs (duck-typed so this file imports no optional module):
//   wifi      - WifiBackend: available, wwanAvailable, wirelessEnabled (rw),
//               wwanEnabled (rw)
//   nmBridge  - PlasmaNmBridge: setAirplaneFlag(bool)
//   bluetooth - BluetoothSection.bluetooth: available, operational,
//               blocked, adapters() -> [{address, powered}],
//               apply(blocked, {address: powered})

import QtQuick

import org.kde.plasma.plasmoid

import "../controls" as Controls
import "airplane.js" as Airplane

Controls.Chip {
    id: airplane

    property QtObject wifi: null
    property QtObject nmBridge: null
    property QtObject bluetooth: null

    readonly property bool available: (wifi !== null && (wifi.available || wifi.wwanAvailable))
                                      || (bluetooth !== null && bluetooth.available)
    readonly property bool engaged: Plasmoid.configuration.airplaneActive
    // Switching on waits (briefly) for BluezQt to become operational, or the
    // adapter list - and with it the snapshot - would be empty.
    property bool waitingForBluetooth: false

    visible: available
    enabled: !waitingForBluetooth
    text: i18n("Airplane Mode")
    icon.name: engaged ? "network-flightmode-on" : "network-flightmode-off"
    active: engaged
    onClicked: engaged ? switchOff() : switchOn()

    function liveState() {
        const bt = bluetooth !== null && bluetooth.available && bluetooth.operational;
        return {
            wifiEnabled: wifi !== null && wifi.wirelessEnabled,
            wwanEnabled: wifi !== null && wifi.wwanEnabled,
            wwanAvailable: wifi !== null && wifi.wwanAvailable,
            btAvailable: bt,
            btBlocked: bt && bluetooth.blocked,
            adapters: bt ? bluetooth.adapters() : []
        };
    }

    function applyPlan(plan) {
        if (wifi !== null && wifi.networkManagerRunning) {
            if (wifi.wirelessEnabled !== plan.wifi) {
                wifi.wirelessEnabled = plan.wifi;
            }
            if (plan.wwan !== null && wifi.wwanEnabled !== plan.wwan) {
                wifi.wwanEnabled = plan.wwan;
            }
        }
        if (bluetooth !== null && plan.btBlocked !== null) {
            bluetooth.apply(plan.btBlocked, plan.adapters);
        }
    }

    function switchOn() {
        if (bluetooth !== null && !bluetooth.operational && !waitingForBluetooth) {
            waitingForBluetooth = true;
            bluetoothWait.restart();
            return;
        }
        waitingForBluetooth = false;
        bluetoothWait.stop();

        const cfg = Plasmoid.configuration;
        const state = liveState();
        const snap = Airplane.snapshot(state);
        cfg.airplaneRestoreWifi = snap.wifi;
        cfg.airplaneRestoreWwan = snap.wwan;
        cfg.airplaneRestoreBtBlocked = snap.btBlocked;
        cfg.airplaneRestoreAdapters = snap.adapters;
        cfg.airplaneActive = true;
        applyPlan(Airplane.planOn(state));
        rememberRadios();
        if (nmBridge !== null) {
            nmBridge.setAirplaneFlag(true);
        }
    }

    function switchOff() {
        const cfg = Plasmoid.configuration;
        const snap = {
            wifi: cfg.airplaneRestoreWifi,
            wwan: cfg.airplaneRestoreWwan,
            btBlocked: cfg.airplaneRestoreBtBlocked,
            adapters: Array.from(cfg.airplaneRestoreAdapters || [])
        };
        // Clear the flag first: the radio changes below must not be taken
        // for "the user re-enabled a radio elsewhere".
        cfg.airplaneActive = false;
        applyPlan(Airplane.planOff(snap, liveState()));
        if (nmBridge !== null) {
            nmBridge.setAirplaneFlag(false);
        }
    }

    // Our airplane mode also ends when the user turns any radio back on
    // elsewhere (tray, Fn key, System Settings): forget the snapshot - each
    // side restores only what it saved - and clear plasma-nm's flag, or the
    // tray keeps hiding its Wi-Fi toggle. Only real off -> on transitions
    // count (backends may re-announce an unchanged state), so the last seen
    // values are kept here.
    property bool lastWifiOn: false
    property bool lastWwanOn: false
    property bool lastBtUnblocked: false

    function rememberRadios() {
        lastWifiOn = wifi !== null && wifi.wirelessEnabled;
        lastWwanOn = wifi !== null && wifi.wwanAvailable && wifi.wwanEnabled;
        lastBtUnblocked = bluetooth !== null && bluetooth.operational && !bluetooth.blocked;
    }

    function radiosChanged() {
        const wifiOn = wifi !== null && wifi.wirelessEnabled;
        const wwanOn = wifi !== null && wifi.wwanAvailable && wifi.wwanEnabled;
        const btUnblocked = bluetooth !== null && bluetooth.operational && !bluetooth.blocked;
        const turnedOn = (wifiOn && !lastWifiOn) || (wwanOn && !lastWwanOn) || (btUnblocked && !lastBtUnblocked);
        lastWifiOn = wifiOn;
        lastWwanOn = wwanOn;
        lastBtUnblocked = btUnblocked;
        if (turnedOn && engaged) {
            Plasmoid.configuration.airplaneActive = false;
            if (nmBridge !== null) {
                nmBridge.setAirplaneFlag(false);
            }
        }
    }

    Component.onCompleted: rememberRadios()
    onWifiChanged: rememberRadios()
    onBluetoothChanged: rememberRadios()

    Timer {
        id: bluetoothWait
        interval: 2000
        // BluezQt never became operational (bluetoothd not running): go on
        // with Wi-Fi and WWAN only.
        onTriggered: airplane.switchOn()
    }

    Connections {
        target: airplane.bluetooth
        ignoreUnknownSignals: true
        function onOperationalChanged() {
            if (airplane.waitingForBluetooth && airplane.bluetooth.operational) {
                airplane.switchOn();
            }
            airplane.radiosChanged();
        }
        function onBlockedChanged() {
            airplane.radiosChanged();
        }
    }

    Connections {
        target: airplane.wifi
        ignoreUnknownSignals: true
        function onWirelessEnabledChanged() {
            airplane.radiosChanged();
        }
        function onWwanChanged() {
            airplane.radiosChanged();
        }
    }
}
