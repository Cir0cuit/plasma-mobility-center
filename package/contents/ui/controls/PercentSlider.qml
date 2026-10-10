// SPDX-License-Identifier: GPL-2.0-or-later
//
// A slider for backend values that change under our feet. It follows
// `percent` except while the user drags it, emits throttled `percentMoved`
// while dragging (for live preview), and `percentCommitted` on release.

import QtQuick

import org.kde.plasma.components as PlasmaComponents3

PlasmaComponents3.Slider {
    id: slider

    /// The backend value (bind it). The handle follows it unless pressed.
    property int percent: 0
    /// Minimum time between two percentMoved emissions, in ms.
    property int throttleInterval: 50

    signal percentMoved(int percent)
    signal percentCommitted(int percent)

    from: 0
    to: 100
    stepSize: 1

    Binding {
        target: slider
        property: "value"
        value: slider.percent
        when: !slider.pressed
        restoreMode: Binding.RestoreNone
    }

    onMoved: {
        if (throttle.running) {
            throttle.pending = true;
        } else {
            slider.percentMoved(Math.round(slider.value));
            throttle.start();
        }
    }

    onPressedChanged: {
        if (!pressed) {
            throttle.stop();
            throttle.pending = false;
            slider.percentCommitted(Math.round(slider.value));
        }
    }

    Timer {
        id: throttle
        property bool pending: false
        interval: slider.throttleInterval
        onTriggered: {
            if (pending) {
                pending = false;
                slider.percentMoved(Math.round(slider.value));
                start();
            }
        }
    }
}
