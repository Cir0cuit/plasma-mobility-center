// SPDX-License-Identifier: GPL-2.0-or-later
//
// Settings page: small behaviour switches of individual tiles.

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2

import org.kde.kcmutils as KCM
import org.kde.kirigami as Kirigami

KCM.SimpleKCM {
    id: page

    property bool cfg_brightnessShowOsd
    property bool cfg_volumeFeedback
    property string cfg_nightLightEnableMode
    property bool cfg_presentationInhibitsNightLight

    Kirigami.FormLayout {
        anchors.left: parent.left
        anchors.right: parent.right

        QQC2.CheckBox {
            Kirigami.FormData.label: i18n("Brightness:")
            text: i18n("Show the on-screen display when changing it")
            checked: page.cfg_brightnessShowOsd
            onToggled: page.cfg_brightnessShowOsd = checked
        }

        QQC2.CheckBox {
            Kirigami.FormData.label: i18n("Volume:")
            text: i18n("Play a feedback sound after changing it")
            checked: page.cfg_volumeFeedback
            onToggled: page.cfg_volumeFeedback = checked
        }
        QQC2.Label {
            Layout.fillWidth: true
            text: i18n("Only when the widget was built with libcanberra, and when audio feedback is enabled in the Sound settings.")
            font: Kirigami.Theme.smallFont
            wrapMode: Text.Wrap
            opacity: 0.7
        }

        QQC2.ButtonGroup {
            id: nightLightGroup
        }
        QQC2.RadioButton {
            Kirigami.FormData.label: i18n("Night Light disabled:")
            text: i18n("The chip switches it on")
            QQC2.ButtonGroup.group: nightLightGroup
            checked: page.cfg_nightLightEnableMode !== "kcm"
            onToggled: if (checked) page.cfg_nightLightEnableMode = "config"
        }
        QQC2.RadioButton {
            text: i18n("The chip opens the Night Light settings")
            QQC2.ButtonGroup.group: nightLightGroup
            checked: page.cfg_nightLightEnableMode === "kcm"
            onToggled: if (checked) page.cfg_nightLightEnableMode = "kcm"
        }

        QQC2.CheckBox {
            Kirigami.FormData.label: i18n("Presentation mode:")
            text: i18n("Also pause Night Light")
            checked: page.cfg_presentationInhibitsNightLight
            onToggled: page.cfg_presentationInhibitsNightLight = checked
        }
    }
}
