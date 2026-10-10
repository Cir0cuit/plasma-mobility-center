// SPDX-License-Identifier: GPL-2.0-or-later
//
// The NetworkManager half of the Wireless tile - the only file importing the
// .network module. Loaded by WirelessTile.qml through a Loader.
//
// Interface used by WirelessTile.qml / AirplaneMode.qml:
//   available, wwanAvailable, statusText     - read
//   expanded                                  - written by the tile (scanning)
//   backend (WifiBackend), nmBridge (PlasmaNmBridge)
//   networkListComponent                      - the details view

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2

import org.kde.kcmutils as KCMUtils
import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents3

import io.github.cir0cuit.mobilitycenter.network

import "../controls" as Controls

Item {
    id: section

    property bool expanded: false

    readonly property QtObject backend: wifi
    readonly property QtObject nmBridge: bridge
    readonly property bool available: wifi.available
    readonly property bool wwanAvailable: wifi.wwanAvailable
    readonly property string statusText: {
        if (!wifi.available) {
            return "";
        }
        if (!wifi.hardwareEnabled) {
            return i18n("Wi-Fi is switched off by a hardware switch");
        }
        if (!wifi.wirelessEnabled) {
            return i18n("Wi-Fi is off");
        }
        if (wifi.activating) {
            return i18n("Connecting to %1…", wifi.activatingSsid);
        }
        if (wifi.activeSsid !== "") {
            return i18n("Connected to %1", wifi.activeSsid);
        }
        return i18n("Not connected");
    }

    implicitWidth: wifiChip.implicitWidth
    implicitHeight: wifiChip.implicitHeight

    WifiBackend {
        id: wifi
        scanActive: section.expanded && wirelessEnabled
    }

    PlasmaNmBridge {
        id: bridge
    }

    Controls.Chip {
        id: wifiChip
        text: i18n("Wi-Fi")
        icon.name: wifi.wirelessEnabled ? "network-wireless-on" : "network-wireless-off"
        active: wifi.wirelessEnabled
        enabled: wifi.hardwareEnabled
        onClicked: wifi.wirelessEnabled = !wifi.wirelessEnabled
    }

    function signalIcon(signal, secured) {
        const bucket = signal >= 90 ? 100 : signal >= 70 ? 80 : signal >= 50 ? 60 : signal >= 30 ? 40 : signal >= 10 ? 20 : 0;
        return "network-wireless-" + bucket + (secured ? "-locked" : "");
    }

    readonly property Component networkListComponent: Component {
        ColumnLayout {
            id: list

            // SSID whose password field is open; "" for none. The field and
            // its text disappear with the details panel.
            property string passwordFor: ""

            spacing: 0

            RowLayout {
                Layout.fillWidth: true

                Kirigami.Heading {
                    Layout.fillWidth: true
                    level: 5
                    text: i18n("Wi-Fi Networks")
                    elide: Text.ElideRight
                }
                PlasmaComponents3.BusyIndicator {
                    visible: wifi.scanning
                    running: visible
                    Layout.preferredHeight: Kirigami.Units.iconSizes.small
                    Layout.preferredWidth: Kirigami.Units.iconSizes.small
                }
                PlasmaComponents3.ToolButton {
                    visible: wifi.activeSsid !== ""
                    icon.name: "network-disconnect"
                    text: i18n("Disconnect")
                    onClicked: wifi.disconnectNetwork()
                }
            }

            Controls.StatusLine {
                Layout.fillWidth: true
                text: wifi.errorText
            }

            Repeater {
                model: wifi.wirelessEnabled ? wifi.networks : null

                delegate: ColumnLayout {
                    id: row

                    required property string ssid
                    required property string displaySsid
                    required property int signal
                    required property bool secured
                    required property bool enterprise
                    required property bool saved
                    required property string connectionUuid
                    required property bool active
                    required property bool activating

                    Layout.fillWidth: true
                    spacing: 0

                    PlasmaComponents3.ItemDelegate {
                        Layout.fillWidth: true
                        text: row.displaySsid
                        highlighted: row.active

                        onClicked: {
                            if (row.active || row.activating) {
                                return;
                            }
                            if (row.saved) {
                                wifi.connectSaved(row.ssid);
                            } else if (row.enterprise) {
                                KCMUtils.KCMLauncher.openSystemSettings("kcm_networkmanagement");
                            } else if (row.secured) {
                                list.passwordFor = list.passwordFor === row.ssid ? "" : row.ssid;
                            } else {
                                wifi.connectNew(row.ssid, "");
                            }
                        }

                        contentItem: RowLayout {
                            spacing: Kirigami.Units.smallSpacing
                            Kirigami.Icon {
                                source: section.signalIcon(row.signal, row.secured)
                                Layout.preferredWidth: Kirigami.Units.iconSizes.small
                                Layout.preferredHeight: Kirigami.Units.iconSizes.small
                            }
                            PlasmaComponents3.Label {
                                Layout.fillWidth: true
                                text: row.displaySsid
                                elide: Text.ElideRight
                                textFormat: Text.PlainText
                            }
                            PlasmaComponents3.Label {
                                opacity: 0.7
                                text: row.active ? i18n("Connected") : row.activating ? i18n("Connecting…") : row.saved ? i18n("Saved") : ""
                            }
                            PlasmaComponents3.ToolButton {
                                visible: row.saved && !row.active
                                icon.name: "edit-delete-remove"
                                display: QQC2.AbstractButton.IconOnly
                                text: i18n("Forget this network")
                                QQC2.ToolTip.text: text
                                QQC2.ToolTip.visible: hovered
                                onClicked: wifi.forget(row.ssid)
                            }
                        }
                    }

                    Kirigami.PasswordField {
                        id: password
                        Layout.fillWidth: true
                        visible: list.passwordFor === row.ssid
                        placeholderText: i18n("Password")
                        onVisibleChanged: {
                            if (visible) {
                                forceActiveFocus();
                            } else {
                                text = "";
                            }
                        }
                        onAccepted: {
                            wifi.connectNew(row.ssid, text);
                            text = "";
                            list.passwordFor = "";
                        }
                    }
                }
            }

            Controls.StatusLine {
                Layout.fillWidth: true
                visible: wifi.wirelessEnabled && wifi.networks.count === 0
                text: i18n("No networks found")
            }
        }
    }
}
