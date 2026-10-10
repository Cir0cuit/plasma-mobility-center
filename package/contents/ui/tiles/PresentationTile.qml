// SPDX-License-Identifier: GPL-2.0-or-later
//
// Presentation mode: one switch. While on: no automatic
// sleep, dimming or screen lock, Do Not Disturb, and Night Light suspended.
// The shared Services.presentation object is released by CenterView on
// widget removal and when this tile is hidden.

import QtQuick
import QtQuick.Layouts

import org.kde.plasma.components as PlasmaComponents3

import io.github.cir0cuit.mobilitycenter.core

import ".."
import "../controls" as Controls

Card {
    id: tile

    readonly property PresentationMode pm: services ? services.presentation : null

    available: pm !== null && pm.available
    statusText: {
        if (!pm || !pm.active) {
            return i18nc("presentation mode", "Off");
        }
        if (pm.blockedByUser) {
            return i18n("Blocked in the power management settings");
        }
        if (pm.dndActive && !pm.dndRevokedByUser) {
            return i18n("Sleep, screen lock and notifications paused");
        }
        return i18n("Sleep and screen lock paused");
    }

    headerControl: Component {
        PlasmaComponents3.Switch {
            checked: tile.pm !== null && tile.pm.active
            onToggled: {
                tile.pm.active = checked;
                // Show what the backend did, not what was clicked.
                checked = Qt.binding(() => tile.pm !== null && tile.pm.active);
            }
        }
    }

    Controls.StatusLine {
        Layout.fillWidth: true
        visible: tile.pm !== null && tile.pm.active
        text: {
            if (!tile.pm) {
                return "";
            }
            const notes = [];
            if (tile.pm.dndRevokedByUser) {
                notes.push(i18n("Do Not Disturb was switched off elsewhere."));
            } else if (tile.pm.dndAvailable && !tile.pm.dndActive) {
                notes.push(i18n("Do Not Disturb is not active."));
            }
            if (tile.pm.nightLightInhibited) {
                notes.push(i18n("Night Light is suspended."));
            }
            notes.push(i18n("Prevents automatic sleep only; the lid and power button still suspend."));
            if (tile.pm.errorText !== "") {
                notes.push(tile.pm.errorText);
            }
            return notes.join(" ");
        }
    }
}
