// SPDX-License-Identifier: GPL-2.0-or-later
//
// Opens a System Settings page. Hidden when the module is not allowed by
// Kiosk (KAuthorized) or no module is set.

import QtQuick
import QtQuick.Controls as QQC2

import org.kde.config as KConfig
import org.kde.kcmutils as KCMUtils
import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents3

PlasmaComponents3.ToolButton {
    id: button

    /// e.g. "kcm_pulseaudio".
    property string kcm: ""
    /// Extra arguments for the module, e.g. ["--args", "Uuid=..."].
    property var args: []

    visible: kcm !== "" && KConfig.KAuthorized.authorizeControlModule(kcm)
    icon.name: "configure"
    display: QQC2.AbstractButton.IconOnly
    text: i18n("Open System Settings…")
    QQC2.ToolTip.text: text
    QQC2.ToolTip.visible: hovered
    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

    onClicked: KCMUtils.KCMLauncher.openSystemSettings(kcm, args)
}
