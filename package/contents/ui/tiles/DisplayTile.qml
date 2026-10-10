// SPDX-License-Identifier: GPL-2.0-or-later
//
// Display / external screen. Collapsed: refresh-rate
// toggle (only when the panel offers >= 2 rates) and the Night Light chip.
// Details: projection mode (internal only / mirror / extend / external only)
// when exactly two screens are connected.
//
// This file imports neither .display nor .core: the libkscreen half lives in
// DisplaySection.qml behind a Loader, so a missing .display module only
// removes those controls and the Night Light chip keeps working.

import QtQuick
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

import ".."

Card {
    id: tile

    readonly property Item section: sectionLoader.status === Loader.Ready ? sectionLoader.item : null
    readonly property bool displayAvailable: section !== null && section.available

    available: displayAvailable || nightChip.available
    statusText: {
        const parts = [];
        if (section && section.statusText !== "") {
            parts.push(section.statusText);
        }
        if (nightChip.available && nightChip.statusText !== "") {
            parts.push(nightChip.statusText);
        }
        return parts.join(" · ");
    }
    detailContent: section !== null && section.projectionAvailable ? details : null

    Flow {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        Loader {
            id: sectionLoader
            source: Qt.resolvedUrl("DisplaySection.qml")
            visible: tile.displayAvailable
        }

        NightLightChip {
            id: nightChip
            nightLight: tile.services ? tile.services.nightLight : null
            visible: available
        }
    }

    Component {
        id: details

        Loader {
            sourceComponent: tile.section ? tile.section.projectionComponent : null
        }
    }
}
