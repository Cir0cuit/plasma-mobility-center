// SPDX-License-Identifier: GPL-2.0-or-later
//
// Power profile chips (Power Save / Balanced / Performance) and, with
// showDetails, the degradation and hold reasons.
// Hidden when no profiles daemon offers real profiles.
//
// Interface used by BatteryTile.qml (keep it stable):
//   profiles      PowerProfiles   the tile's single backend instance (input)
//   showChips     bool            the chip row (collapsed card)
//   showDetails   bool            degradation / holds / configured-profile notes
//   available     bool            read

import QtQuick
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

import io.github.cir0cuit.mobilitycenter.core

import "../controls" as Controls

ColumnLayout {
    id: root

    /// The backend, shared by the collapsed chips and the details view.
    property PowerProfiles profiles: null
    property bool showChips: true
    property bool showDetails: false
    readonly property bool available: profiles !== null && profiles.available

    visible: available
    spacing: Kirigami.Units.smallSpacing

    function label(profile) {
        switch (profile) {
        case "power-saver":
            return i18nc("power profile", "Power Save");
        case "balanced":
            return i18nc("power profile", "Balanced");
        case "performance":
            return i18nc("power profile", "Performance");
        }
        return profile;
    }

    function icon(profile) {
        switch (profile) {
        case "power-saver":
            return "battery-profile-powersave";
        case "balanced":
            return "speedometer";
        case "performance":
            return "battery-profile-performance";
        }
        return "";
    }

    function reasonText(reason) {
        switch (reason) {
        case "lap-detected":
            return i18n("Performance is limited because the computer is on your lap.");
        case "high-operating-temperature":
            return i18n("Performance is limited because the computer is too hot.");
        }
        return i18n("Performance is limited by the hardware.");
    }

    Controls.ChipRow {
        Layout.fillWidth: true
        visible: root.showChips
        model: !root.available ? [] : root.profiles.profiles.map(p => ({
            id: p,
            text: root.label(p),
            icon: root.icon(p),
            active: p === root.profiles.activeProfile
        }))
        onChipClicked: id => root.profiles.setActiveProfile(id)
    }

    Repeater {
        model: root.showDetails && root.available ? root.profiles.degradedReasons : []
        delegate: Controls.StatusLine {
            required property string modelData
            Layout.fillWidth: true
            text: root.reasonText(modelData)
        }
    }

    Repeater {
        model: root.showDetails && root.available ? root.profiles.holds : []
        delegate: Controls.StatusLine {
            required property var modelData
            Layout.fillWidth: true
            readonly property string app: modelData.ApplicationId || i18n("An application")
            text: modelData.Reason
                  ? i18nc("@info %1 application, %2 power profile, %3 reason", "%1 requested %2: %3", app, root.label(modelData.Profile), modelData.Reason)
                  : i18nc("@info %1 application, %2 power profile", "%1 requested %2", app, root.label(modelData.Profile))
        }
    }

    Controls.StatusLine {
        Layout.fillWidth: true
        visible: root.showDetails && root.available && root.profiles.configuredProfile !== ""
                 && root.profiles.configuredProfile !== root.profiles.activeProfile
        text: root.available ? i18n("Power management switches back to %1 when the power source changes.", root.label(root.profiles.configuredProfile)) : ""
    }

    Controls.StatusLine {
        Layout.fillWidth: true
        text: root.available ? root.profiles.errorText : ""
    }
}
