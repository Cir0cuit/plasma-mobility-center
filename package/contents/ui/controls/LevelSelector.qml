// SPDX-License-Identifier: GPL-2.0-or-later
//
// Discrete level picker, 0..steps: a slider that snaps to the levels, with a
// tick per level and, while they fit, the levels' names under the ticks.

import QtQuick
import QtQuick.Controls as QQC2

import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents3

Item {
    id: selector

    /// Current level (bind it).
    property int level: 0
    /// Highest level; 0 disables the control.
    property int steps: 0
    /// Name of level 0 under its tick.
    property string offText: i18nc("lowest level of a backlight", "Off")

    /// @p committed is false while the slider is being dragged.
    signal levelSelected(int level, bool committed)

    implicitHeight: slider.implicitHeight + (labels.visible ? labels.height : 0)
    implicitWidth: slider.implicitWidth

    PercentSlider {
        id: slider

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top

        enabled: selector.steps > 0
        // The Plasma slider draws a tick per step (up to 20 of them).
        to: Math.max(1, selector.steps)
        stepSize: 1
        snapMode: QQC2.Slider.SnapAlways
        percent: selector.level
        onPercentMoved: value => selector.levelSelected(value, false)
        onPercentCommitted: value => selector.levelSelected(value, true)
    }

    Item {
        id: labels

        // Where the ticks are: the handle's centre travels this range.
        readonly property real handleWidth: slider.handle ? slider.handle.width : 0
        readonly property real first: slider.leftPadding + handleWidth / 2
        readonly property real span: slider.availableWidth - handleWidth

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: slider.bottom
        height: metrics.height
        visible: selector.steps > 0 && selector.steps <= 10
                 && span / selector.steps >= metrics.averageCharacterWidth * 3

        FontMetrics {
            id: metrics
            font: Kirigami.Theme.smallFont
        }

        Repeater {
            model: labels.visible ? selector.steps + 1 : 0

            delegate: PlasmaComponents3.Label {
                required property int index

                // Centred under its tick, but never outside the control.
                x: Math.max(0, Math.min(labels.width - width,
                                        labels.first + labels.span * index / selector.steps - width / 2))
                font: Kirigami.Theme.smallFont
                opacity: index === selector.level ? 1 : 0.6
                text: index === 0 ? selector.offText : String(index)
                textFormat: Text.PlainText
            }
        }
    }
}
