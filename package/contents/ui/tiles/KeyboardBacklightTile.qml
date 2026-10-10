// SPDX-License-Identifier: GPL-2.0-or-later
//
// Keyboard backlight: a slider that snaps to the levels,
// with a tick per level. No details.

import QtQuick
import QtQuick.Layouts

import io.github.cir0cuit.mobilitycenter.core

import ".."
import "../controls" as Controls

Card {
    id: tile

    available: kbd.available
    statusText: kbd.errorText !== "" ? kbd.errorText
              : kbd.brightness === 0 ? i18nc("keyboard backlight", "Off")
              : i18nc("keyboard backlight level", "Level %1 of %2", kbd.brightness, kbd.maxBrightness)

    // UPower 1.91.4 does not signal Fn-key changes: re-read when shown or hovered.
    onHoveredChanged: if (hovered && kbd.rereadOnShow) kbd.refresh()
    onVisibleChanged: if (visible && kbd.rereadOnShow) kbd.refresh()

    KeyboardBacklight {
        id: kbd
    }

    Controls.LevelSelector {
        Layout.fillWidth: true
        level: kbd.brightness
        steps: kbd.steps
        // Always silent: the card already shows the level, so PowerDevil's
        // OSD would only repeat it (as for screen brightness).
        onLevelSelected: (level, committed) => kbd.setLevel(level, true)
    }
}
