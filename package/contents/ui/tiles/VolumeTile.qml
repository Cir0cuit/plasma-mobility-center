// SPDX-License-Identifier: GPL-2.0-or-later
//
// Volume. Collapsed: output volume slider + mute.
// Details: output device picker, input device picker, mic volume + mute.

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2

import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents3
import org.kde.plasma.plasmoid

import io.github.cir0cuit.mobilitycenter.audio

import ".."
import "../controls" as Controls

Card {
    id: tile

    readonly property bool canRetry: AudioBackend.failed && !AudioBackend.autoConnecting

    available: AudioBackend.available || AudioBackend.connecting || canRetry
    dimmed: AudioBackend.connecting
    iconName: AudioBackend.outputIconName
    statusText: {
        if (AudioBackend.connecting) {
            return i18n("Connecting…");
        }
        if (canRetry) {
            return i18n("The sound server is not responding");
        }
        if (AudioBackend.outputMuted) {
            return i18n("Muted");
        }
        return i18nc("volume in percent", "%1%", AudioBackend.outputVolumePercent);
    }
    detailContent: AudioBackend.available ? details : null

    Binding {
        target: AudioBackend
        property: "feedbackEnabled"
        value: Plasmoid.configuration.volumeFeedback
    }

    RowLayout {
        Layout.fillWidth: true
        visible: AudioBackend.available

        PlasmaComponents3.ToolButton {
            icon.name: AudioBackend.outputIconName
            display: QQC2.AbstractButton.IconOnly
            text: AudioBackend.outputMuted ? i18n("Unmute") : i18n("Mute")
            checked: AudioBackend.outputMuted
            QQC2.ToolTip.text: text
            QQC2.ToolTip.visible: hovered
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
            onClicked: AudioBackend.toggleOutputMute()
        }
        Controls.PercentSlider {
            Layout.fillWidth: true
            to: AudioBackend.maxVolumePercent
            // Wheel and arrow keys move by plasma-pa's VolumeStep; dragging
            // stays at 1 % resolution (no snapping to the step).
            stepSize: AudioBackend.volumeStep
            snapMode: QQC2.Slider.NoSnap
            percent: AudioBackend.outputVolumePercent
            onPercentMoved: value => AudioBackend.setOutputVolumePercent(value)
            onPercentCommitted: value => {
                AudioBackend.setOutputVolumePercent(value);
                AudioBackend.playFeedback();
            }
        }
    }

    PlasmaComponents3.Button {
        visible: tile.canRetry
        icon.name: "view-refresh"
        text: i18n("Retry")
        onClicked: AudioBackend.reconnect()
    }

    Component {
        id: details

        ColumnLayout {
            spacing: Kirigami.Units.smallSpacing

            Kirigami.Heading {
                level: 5
                text: i18n("Output")
            }
            // Each picker in its own layout: RadioDelegate's autoExclusive
            // groups by parent, outputs and inputs must not share one group.
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0

                Repeater {
                    model: AudioBackend.outputs
                    delegate: DeviceDelegate {
                        onActivated: name => AudioBackend.setDefaultOutput(name)
                    }
                }
            }

            Kirigami.Heading {
                level: 5
                visible: AudioBackend.inputAvailable
                text: i18n("Input")
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                visible: AudioBackend.inputAvailable

                Repeater {
                    model: AudioBackend.inputAvailable ? AudioBackend.inputs : null
                    delegate: DeviceDelegate {
                        onActivated: name => AudioBackend.setDefaultInput(name)
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                visible: AudioBackend.inputAvailable

                PlasmaComponents3.ToolButton {
                    icon.name: AudioBackend.inputMuted ? "microphone-sensitivity-muted" : "audio-input-microphone"
                    display: QQC2.AbstractButton.IconOnly
                    text: AudioBackend.inputMuted ? i18n("Unmute microphone") : i18n("Mute microphone")
                    checked: AudioBackend.inputMuted
                    QQC2.ToolTip.text: text
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                    onClicked: AudioBackend.toggleInputMute()
                }
                Controls.PercentSlider {
                    Layout.fillWidth: true
                    to: AudioBackend.maxVolumePercent
                    stepSize: AudioBackend.volumeStep
                    snapMode: QQC2.Slider.NoSnap
                    percent: AudioBackend.inputVolumePercent
                    onPercentMoved: value => AudioBackend.setInputVolumePercent(value)
                    onPercentCommitted: value => AudioBackend.setInputVolumePercent(value)
                }
                PlasmaComponents3.Label {
                    text: i18nc("volume in percent", "%1%", AudioBackend.inputVolumePercent)
                    textFormat: Text.PlainText
                }
            }
        }
    }

    component DeviceDelegate: PlasmaComponents3.RadioDelegate {
        id: delegate

        required property var model

        /// The user picked this device (its PulseAudio name).
        signal activated(string name)

        Layout.fillWidth: true
        text: model.label || model.Name || ""
        checked: !!model.Default
        onClicked: {
            delegate.activated(model.Name);
            // The click toggled `checked` locally; follow the server again.
            checked = Qt.binding(() => !!delegate.model.Default);
        }
    }
}
