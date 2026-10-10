// SPDX-License-Identifier: GPL-2.0-or-later
//
// Bluetooth half of the Wireless tile - QML only, and the
// only file importing org.kde.bluezqt. Loaded by WirelessTile.qml through a
// Loader, so a missing bluez-qt only removes Bluetooth.
//
// Read-only until the user clicks: the toggle, connect and disconnect only
// run from clicks. Pairing new devices hands off to System Settings.
//
// Interface used by WirelessTile.qml:
//   available             - read
//   bluetooth             - the object AirplaneMode.qml drives (see below)
//   deviceListComponent   - the details view

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2

import org.kde.bluezqt as BluezQt
import org.kde.kcmutils as KCMUtils
import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents3

import "../controls" as Controls

Item {
    id: section

    // Manager.initialized has no NOTIFY: bind to operational, and treat
    // initError as "no Bluetooth".
    property bool initFailed: false
    // Set while switching on, so adapters that (re)appear afterwards - e.g.
    // after the rfkill unblock, asynchronous from KF 6.20 - get powered too.
    property bool pendingEnable: false

    readonly property bool available: !initFailed && BluezQt.Manager.operational
                                      && (BluezQt.Manager.bluetoothBlocked || BluezQt.Manager.adapters.length > 0)
    readonly property bool enabledState: BluezQt.Manager.bluetoothOperational
    readonly property bool hardBlocked: BluezQt.Manager.rfkill !== null
                                        && BluezQt.Manager.rfkill.state === BluezQt.Rfkill.HardBlocked

    // Paired or connected devices, Blocked last, then Connected first, then by
    // name (the bluedevil applet's rule). Rebuilt on every device change.
    property var devices: []

    implicitWidth: btChip.implicitWidth
    implicitHeight: btChip.implicitHeight

    // bluedevil's setBluetoothEnabled (identical 6.3-6.7). STATE-CHANGING:
    // only from a click or airplane mode. On KF < 6.20 the rfkill write is
    // synchronous and can block briefly, which is accepted here.
    function setBluetoothEnabled(on) {
        pendingEnable = on;
        BluezQt.Manager.bluetoothBlocked = !on;
        const adapters = BluezQt.Manager.adapters;
        for (let i = 0; i < adapters.length; ++i) {
            adapters[i].powered = on;
        }
    }

    function rebuildDevices() {
        const all = BluezQt.Manager.devices;
        const list = [];
        for (let i = 0; i < all.length; ++i) {
            if (all[i].paired || all[i].connected) {
                list.push(all[i]);
            }
        }
        list.sort((a, b) => {
            if (a.blocked !== b.blocked) {
                return a.blocked ? 1 : -1;
            }
            if (a.connected !== b.connected) {
                return a.connected ? -1 : 1;
            }
            return a.name.localeCompare(b.name);
        });
        devices = list;
    }

    // The interface AirplaneMode.qml uses. Keep it stable.
    readonly property QtObject bluetooth: QtObject {
        readonly property bool available: section.available
        readonly property bool operational: BluezQt.Manager.operational
        readonly property bool blocked: BluezQt.Manager.bluetoothBlocked

        /// [{ address, powered }] for every adapter.
        function adapters() {
            const out = [];
            const list = BluezQt.Manager.adapters;
            for (let i = 0; i < list.length; ++i) {
                out.push({ address: list[i].address, powered: list[i].powered });
            }
            return out;
        }

        /// Sets the rfkill block and each listed adapter's power.
        /// STATE-CHANGING: airplane mode only.
        function apply(blocked, poweredByAddress) {
            section.pendingEnable = !blocked;
            BluezQt.Manager.bluetoothBlocked = blocked;
            const list = BluezQt.Manager.adapters;
            for (let i = 0; i < list.length; ++i) {
                if (poweredByAddress.hasOwnProperty(list[i].address)) {
                    list[i].powered = poweredByAddress[list[i].address];
                }
            }
        }
    }

    Component.onCompleted: rebuildDevices()

    Connections {
        target: BluezQt.Manager

        function onInitError(errorText) {
            section.initFailed = true;
        }
        function onBluetoothOperationalChanged(operational) {
            if (operational) {
                section.pendingEnable = false;
            }
        }
        function onAdapterAdded(adapter) {
            if (section.pendingEnable) {
                adapter.powered = true;
            }
        }
        function onAdapterChanged(adapter) {
            if (section.pendingEnable && !adapter.powered && !BluezQt.Manager.bluetoothBlocked) {
                adapter.powered = true;
            }
        }
        function onDevicesChanged() {
            section.rebuildDevices();
        }
        function onDeviceAdded() {
            section.rebuildDevices();
        }
        function onDeviceRemoved() {
            section.rebuildDevices();
        }
        function onDeviceChanged() {
            section.rebuildDevices();
        }
    }

    Connections {
        target: BluezQt.Manager.rfkill
        function onStateChanged(state) {
            if (section.pendingEnable && state === BluezQt.Rfkill.Unblocked) {
                const adapters = BluezQt.Manager.adapters;
                for (let i = 0; i < adapters.length; ++i) {
                    adapters[i].powered = true;
                }
            }
        }
    }

    Controls.Chip {
        id: btChip
        text: i18n("Bluetooth")
        icon.name: section.enabledState ? "network-bluetooth-activated" : "network-bluetooth-inactive"
        // Bound to BluezQt's state, never to an optimistic local one.
        active: section.enabledState
        enabled: !section.hardBlocked
        onClicked: section.setBluetoothEnabled(!section.enabledState)
    }

    readonly property Component deviceListComponent: Component {
        ColumnLayout {
            id: deviceList

            // ubi -> true while a connect/disconnect call is pending.
            property var busy: ({})
            property string lastError: ""

            spacing: 0

            function setBusy(ubi, on) {
                const copy = Object.assign({}, busy);
                if (on) {
                    copy[ubi] = true;
                } else {
                    delete copy[ubi];
                }
                busy = copy;
            }

            function toggleConnection(device) {
                const ubi = device.ubi;
                if (busy[ubi]) {
                    return;
                }
                setBusy(ubi, true);
                const call = device.connected ? device.disconnectFromDevice() : device.connectToDevice();
                // Copy the result inside finished: the call deletes itself.
                call.finished.connect(() => {
                    deviceList.lastError = call.error ? call.errorText : "";
                    deviceList.setBusy(ubi, false);
                });
            }

            RowLayout {
                Layout.fillWidth: true

                Kirigami.Heading {
                    Layout.fillWidth: true
                    level: 5
                    text: i18n("Bluetooth Devices")
                    elide: Text.ElideRight
                }
                PlasmaComponents3.ToolButton {
                    icon.name: "list-add"
                    text: i18n("Pair a Device…")
                    onClicked: KCMUtils.KCMLauncher.openSystemSettings("kcm_bluetooth")
                }
            }

            Controls.StatusLine {
                Layout.fillWidth: true
                text: deviceList.lastError
            }

            Repeater {
                model: section.enabledState ? section.devices : []

                delegate: PlasmaComponents3.ItemDelegate {
                    id: deviceRow

                    required property var modelData

                    Layout.fillWidth: true
                    text: modelData.name
                    enabled: !modelData.blocked
                    onClicked: deviceList.toggleConnection(modelData)

                    contentItem: RowLayout {
                        spacing: Kirigami.Units.smallSpacing
                        Kirigami.Icon {
                            source: deviceRow.modelData.icon || "preferences-system-bluetooth"
                            Layout.preferredWidth: Kirigami.Units.iconSizes.small
                            Layout.preferredHeight: Kirigami.Units.iconSizes.small
                        }
                        PlasmaComponents3.Label {
                            Layout.fillWidth: true
                            text: deviceRow.modelData.name
                            elide: Text.ElideRight
                            textFormat: Text.PlainText
                        }
                        PlasmaComponents3.Label {
                            // Battery1 exists only while the device is connected.
                            visible: text !== ""
                            opacity: 0.7
                            text: deviceRow.modelData.battery ? i18nc("battery in percent", "%1%", deviceRow.modelData.battery.percentage) : ""
                        }
                        PlasmaComponents3.BusyIndicator {
                            visible: !!deviceList.busy[deviceRow.modelData.ubi]
                            running: visible
                            Layout.preferredWidth: Kirigami.Units.iconSizes.small
                            Layout.preferredHeight: Kirigami.Units.iconSizes.small
                        }
                        PlasmaComponents3.Label {
                            opacity: 0.7
                            text: deviceRow.modelData.connected ? i18n("Connected") : ""
                        }
                    }
                }
            }

            Controls.StatusLine {
                Layout.fillWidth: true
                visible: section.enabledState && section.devices.length === 0
                text: i18n("No paired devices")
            }
        }
    }
}
