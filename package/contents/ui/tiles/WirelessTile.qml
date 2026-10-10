// SPDX-License-Identifier: GPL-2.0-or-later
//
// Wireless. Collapsed: Wi-Fi and Bluetooth toggles,
// the current network, airplane mode. Details: the Wi-Fi network list and
// the paired Bluetooth devices.
//
// This file imports neither .network nor org.kde.bluezqt. Each half sits
// behind its own Loader (WifiSection.qml, BluetoothSection.qml), so a
// missing NetworkManagerQt module or bluez-qt only removes that half.
// AirplaneMode.qml talks to both through duck-typed interfaces.
// own owner).

import QtQuick
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

import ".."

Card {
    id: tile

    readonly property Item wifiSection: wifiLoader.status === Loader.Ready ? wifiLoader.item : null
    readonly property Item btSection: btLoader.status === Loader.Ready ? btLoader.item : null
    readonly property bool wifiAvailable: wifiSection !== null && wifiSection.available
    readonly property bool btAvailable: btSection !== null && btSection.available

    available: wifiAvailable || (wifiSection !== null && wifiSection.wwanAvailable) || btAvailable
    statusText: airplaneChip.engaged && airplaneChip.visible ? i18n("Airplane mode is on")
              : wifiSection !== null ? wifiSection.statusText : ""
    detailContent: wifiAvailable || btAvailable ? details : null

    Flow {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        Loader {
            id: wifiLoader
            source: Qt.resolvedUrl("WifiSection.qml")
            visible: tile.wifiAvailable
        }
        Binding {
            target: tile.wifiSection
            property: "expanded"
            // Scan while the flyout is open; inline (always on screen) only
            // while hovered, so a large widget does not scan all day.
            value: tile.expanded || (tile.detailsInline && tile.hovered)
            when: tile.wifiSection !== null
        }

        Loader {
            id: btLoader
            source: Qt.resolvedUrl("BluetoothSection.qml")
            visible: tile.btAvailable
        }

        AirplaneMode {
            id: airplaneChip
            wifi: tile.wifiSection ? tile.wifiSection.backend : null
            nmBridge: tile.wifiSection ? tile.wifiSection.nmBridge : null
            bluetooth: tile.btSection ? tile.btSection.bluetooth : null
        }
    }

    Component {
        id: details

        ColumnLayout {
            spacing: Kirigami.Units.largeSpacing

            Loader {
                Layout.fillWidth: true
                active: tile.wifiAvailable
                sourceComponent: tile.wifiSection ? tile.wifiSection.networkListComponent : null
            }
            Loader {
                Layout.fillWidth: true
                active: tile.btAvailable
                sourceComponent: tile.btSection ? tile.btSection.deviceListComponent : null
            }
        }
    }
}
