// SPDX-License-Identifier: GPL-2.0-or-later
//
// Charge limit in the Battery & Power details, driving the shared
// Services.chargeLimit object.
//
// Changing the limit goes through PowerDevil's root helper, which asks for
// an administrator password on every change. So the slider is gated: while
// polkit says a change would prompt, the section offers a one-time
// "Allow" step instead (installs the optional polkit rule through pkexec).
// Once allowed, the slider (50-100 % in 5 % steps, committed on release)
// appears and changes apply without prompts. If the permission cannot be
// installed (no pkexec, rule file missing) the slider is shown anyway and
// each change prompts.

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2

import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents3

import io.github.cir0cuit.mobilitycenter.core

import "../controls" as Controls

ColumnLayout {
    id: root

    property ChargeLimit chargeLimit: null
    readonly property bool available: chargeLimit !== null && chargeLimit.available
    // The value being written: the handle stays there while the job runs
    // (password prompt) instead of jumping back to the old limit.
    property int pendingValue: -1

    // Polkit says a change would prompt and we can fix that once.
    readonly property bool needsPermission: available && chargeLimit.permission === ChargeLimit.PermissionNeeded
                                            && chargeLimit.canInstallPermission

    visible: available
    spacing: Kirigami.Units.smallSpacing

    PlasmaComponents3.Label {
        Layout.fillWidth: true
        text: i18n("Charge limit")
        font.weight: Font.DemiBold
    }

    // ---- step 1: permission ------------------------------------------------
    PlasmaComponents3.Label {
        Layout.fillWidth: true
        visible: root.needsPermission
        wrapMode: Text.WordWrap
        text: i18n("Changing the charge limit needs a one-time permission. Without it, every change asks for the administrator password.")
    }
    RowLayout {
        Layout.fillWidth: true
        visible: root.needsPermission
        spacing: Kirigami.Units.smallSpacing

        PlasmaComponents3.Button {
            icon.name: "security-high"
            text: i18n("Allow Changing the Limit…")
            enabled: !root.chargeLimit.installingPermission
            onClicked: root.chargeLimit.installPermission()
        }
        PlasmaComponents3.BusyIndicator {
            visible: root.available && root.chargeLimit.installingPermission
            running: visible
            Layout.preferredHeight: Kirigami.Units.iconSizes.small
            Layout.preferredWidth: Kirigami.Units.iconSizes.small
        }
    }

    // ---- step 2: the limit ---------------------------------------------------
    RowLayout {
        Layout.fillWidth: true
        visible: !root.needsPermission

        PlasmaComponents3.Label {
            Layout.fillWidth: true
            text: i18n("Stop charging at")
        }
        PlasmaComponents3.BusyIndicator {
            visible: root.available && root.chargeLimit.busy
            running: visible
            Layout.preferredHeight: Kirigami.Units.iconSizes.small
            Layout.preferredWidth: Kirigami.Units.iconSizes.small
        }
        PlasmaComponents3.Label {
            text: root.available ? i18nc("charge limit in percent", "%1%", Math.round(slider.value)) : ""
        }
    }

    Controls.PercentSlider {
        id: slider
        Layout.fillWidth: true
        visible: !root.needsPermission
        from: root.available ? root.chargeLimit.minimum : 50
        to: root.available ? root.chargeLimit.maximum : 100
        stepSize: root.available ? root.chargeLimit.step : 5
        snapMode: QQC2.Slider.SnapAlways
        enabled: root.available && !root.chargeLimit.busy
        percent: {
            if (!root.available) {
                return 100;
            }
            if (root.pendingValue >= 0) {
                return root.pendingValue;
            }
            return Math.max(from, root.chargeLimit.stopThreshold);
        }
        onPercentCommitted: value => {
            if (root.available && value !== root.chargeLimit.stopThreshold) {
                root.pendingValue = value;
                root.chargeLimit.setStopThreshold(value, slider);
            }
        }
    }

    Connections {
        target: root.chargeLimit
        function onWriteFinished() {
            if (!root.chargeLimit.busy) {
                root.pendingValue = -1;
            }
        }
        function onAvailableChanged() {
            root.pendingValue = -1;
        }
    }

    Controls.StatusLine {
        Layout.fillWidth: true
        visible: text !== ""
        text: {
            if (!root.available) {
                return "";
            }
            if (root.chargeLimit.errorText !== "") {
                return root.chargeLimit.errorText;
            }
            if (root.chargeLimit.needsReplug && !root.needsPermission) {
                return i18n("Unplug and replug the charger to charge up to the new limit.");
            }
            return "";
        }
    }
}
