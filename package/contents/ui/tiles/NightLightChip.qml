// SPDX-License-Identifier: GPL-2.0-or-later
//
// Night Light chip, driving the shared
// Services.nightLight object. checked = running && !inhibited; switching it
// on while Night Light is disabled either enables it directly (config key
// nightLightEnableMode "config", default) or opens its settings ("kcm").

import QtQuick
import QtQuick.Controls as QQC2

import org.kde.config as KConfig
import org.kde.kcmutils as KCMUtils
import org.kde.kirigami as Kirigami

import io.github.cir0cuit.mobilitycenter.core

import "../controls" as Controls

Controls.Chip {
    id: chip

    property NightLight nightLight: null
    readonly property bool available: nightLight !== null && nightLight.available
    readonly property string statusText: {
        if (!available) {
            return "";
        }
        if (nightLight.errorText !== "") {
            return i18n("Night Light: %1", nightLight.errorText);
        }
        if (nightLight.presentationInhibited) {
            return i18n("Night Light paused for the presentation");
        }
        if (nightLight.neutral) {
            return i18n("Night Light active (no tint during daytime)");
        }
        return "";
    }
    readonly property string toolTipText: {
        if (!available) {
            return "";
        }
        if (nightLight.checked) {
            return i18n("Suspend Night Light");
        }
        if (nightLight.userInhibited || nightLight.presentationInhibited) {
            return i18n("Resume Night Light");
        }
        if (!nightLight.enabled) {
            return nightLight.canEnableDirectly ? i18n("Turn on Night Light") : i18n("Open the Night Light settings");
        }
        return "";
    }

    icon.name: active ? "redshift-status-on" : "redshift-status-off"
    text: i18n("Night Light")
    active: available && nightLight.checked
    onClicked: nightLight.setChecked(!nightLight.checked)

    QQC2.ToolTip.text: toolTipText
    QQC2.ToolTip.visible: hovered && toolTipText !== ""
    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

    Connections {
        target: chip.nightLight
        function onKcmRequested(kcm) {
            if (KConfig.KAuthorized.authorizeControlModule(kcm)) {
                KCMUtils.KCMLauncher.openSystemSettings(kcm);
            }
        }
    }
}
