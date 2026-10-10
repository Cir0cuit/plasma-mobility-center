// SPDX-License-Identifier: GPL-2.0-or-later
//
// The libkscreen half of the Display tile - the only file importing the
// .display module. Loaded by DisplayTile.qml through a Loader.
//
// Interface used by DisplayTile.qml:
//   available, statusText, projectionAvailable    - read
// The collapsed chips (refresh rate, HDR) are this item itself (a Row).
//   projectionComponent                           - the details view

import QtQuick
import QtQuick.Layouts

import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents3

import io.github.cir0cuit.mobilitycenter.display

import "../controls" as Controls

Row {
    id: section

    readonly property bool available: displayConfig.available
                                      && (displayConfig.refreshToggleAvailable || displayConfig.projectionAvailable || displayConfig.hdrAvailable)
    readonly property bool projectionAvailable: displayConfig.available && displayConfig.projectionAvailable
    readonly property string statusText: displayConfig.available && displayConfig.currentRefreshRate > 0 && displayConfig.refreshToggleAvailable
                                         ? i18nc("refresh rate", "%1 Hz", displayConfig.currentRefreshRate) : ""

    spacing: Kirigami.Units.smallSpacing

    DisplayConfig {
        id: displayConfig
    }

    Controls.Chip {
        id: refreshChip
        visible: displayConfig.refreshToggleAvailable
        enabled: !displayConfig.busy
        icon.name: "video-display"
        text: displayConfig.alternateRefreshRate > 0 ? i18nc("switch to refresh rate", "Switch to %1 Hz", displayConfig.alternateRefreshRate) : ""
        active: displayConfig.refreshRates.length > 0 && displayConfig.currentRefreshRate === displayConfig.refreshRates[0]
        onClicked: displayConfig.toggleRefreshRate()
    }

    Controls.Chip {
        id: hdrChip
        visible: displayConfig.hdrAvailable
        enabled: !displayConfig.busy
        icon.name: "color-management"
        text: i18nc("high dynamic range", "HDR")
        active: displayConfig.hdrEnabled
        onClicked: displayConfig.setHdrEnabled(!displayConfig.hdrEnabled)
    }

    readonly property Component projectionComponent: Component {
        ColumnLayout {
            spacing: Kirigami.Units.smallSpacing

            Kirigami.Heading {
                level: 5
                text: i18n("Projection")
            }

            Controls.ChipRow {
                Layout.fillWidth: true
                enabled: !displayConfig.busy
                model: [
                    { id: String(DisplayConfig.InternalOnly), text: i18n("Laptop screen only"), icon: "computer-laptop",
                      active: displayConfig.projectionMode === DisplayConfig.InternalOnly },
                    { id: String(DisplayConfig.Mirror), text: i18n("Mirror"), icon: "video-display",
                      active: displayConfig.projectionMode === DisplayConfig.Mirror },
                    { id: String(DisplayConfig.Extend), text: i18n("Extend"), icon: "video-display-add",
                      active: displayConfig.projectionMode === DisplayConfig.Extend },
                    { id: String(DisplayConfig.ExternalOnly), text: i18n("External screen only"), icon: "monitor",
                      active: displayConfig.projectionMode === DisplayConfig.ExternalOnly }
                ]
                onChipClicked: id => displayConfig.setProjectionMode(Number(id))
            }

            Controls.StatusLine {
                Layout.fillWidth: true
                text: displayConfig.errorText !== "" ? displayConfig.errorText
                     : (displayConfig.mirrorEmulated ? i18n("Mirroring shows both screens at a common resolution.") : "")
            }

            PlasmaComponents3.Button {
                icon.name: "preferences-desktop-display"
                text: i18n("More Options…")
                onClicked: displayConfig.showKdeSwitcher()
            }
        }
    }
}
