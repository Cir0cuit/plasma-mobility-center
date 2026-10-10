// SPDX-License-Identifier: GPL-2.0-or-later
//
// Display brightness. On the card: the primary display's
// slider, then one row with the Adaptive chip and "Turn Screen Off".
// Details: one slider per display when there are several.

import QtQuick
import QtQuick.Layouts

import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents3
import org.kde.plasma.plasmoid

import io.github.cir0cuit.mobilitycenter.core

import ".."
import "../controls" as Controls

Card {
    id: tile

    available: brightness.available && brightness.count > 0
    statusText: brightness.primaryPercent >= 0 ? i18nc("brightness in percent", "%1%", brightness.primaryPercent) : ""
    detailContent: brightness.count > 1 ? details : null

    ScreenBrightness {
        id: brightness
        showOsd: Plasmoid.configuration.brightnessShowOsd
    }

    Controls.PercentSlider {
        Layout.fillWidth: true
        percent: Math.max(0, brightness.primaryPercent)
        onPercentMoved: value => brightness.setPercent(brightness.primaryName, value)
        onPercentCommitted: value => brightness.setPercent(brightness.primaryName, value)
    }

    Flow {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing
        // Test the conditions, not the children's `visible`: a child of an
        // invisible item reads visible == false, which would keep the row
        // hidden for good.
        readonly property bool adaptiveAvailable: adaptiveLoader.status === Loader.Ready && adaptiveLoader.item.available
        visible: adaptiveAvailable || brightness.screenOffAvailable

        // Adaptive (ambient light) brightness - needs the .display module
        // and Plasma 6.6+; absent otherwise.
        Loader {
            id: adaptiveLoader
            source: Qt.resolvedUrl("AdaptiveBrightnessChip.qml")
            visible: parent.adaptiveAvailable
        }

        PlasmaComponents3.Button {
            id: screenOff
            visible: brightness.screenOffAvailable
            icon.name: "system-suspend-hibernate"
            text: i18n("Turn Screen Off")
            // clicked fires on release; the backend waits ~400 ms more,
            // because any input event wakes the screen again.
            onClicked: brightness.turnScreenOff()
        }
    }

    Component {
        id: details

        ColumnLayout {
            spacing: Kirigami.Units.smallSpacing

            Repeater {
                model: brightness.count > 1 ? brightness : null

                delegate: ColumnLayout {
                    id: displayRow

                    required property string name
                    required property string label
                    required property int percent

                    Layout.fillWidth: true
                    spacing: 0

                    RowLayout {
                        Layout.fillWidth: true
                        PlasmaComponents3.Label {
                            Layout.fillWidth: true
                            text: displayRow.label
                            elide: Text.ElideRight
                        }
                        PlasmaComponents3.Label {
                            text: i18nc("brightness in percent", "%1%", displayRow.percent)
                        }
                    }
                    Controls.PercentSlider {
                        Layout.fillWidth: true
                        percent: displayRow.percent
                        onPercentMoved: value => brightness.setPercent(displayRow.name, value)
                        onPercentCommitted: value => brightness.setPercent(displayRow.name, value)
                    }
                }
            }
        }
    }
}
