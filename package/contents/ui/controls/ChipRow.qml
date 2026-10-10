// SPDX-License-Identifier: GPL-2.0-or-later
//
// A wrapping row of chips. model: [{ id, text, icon, active, enabled? }].

import QtQuick

import org.kde.kirigami as Kirigami

Flow {
    id: row

    property var model: []

    signal chipClicked(string id)

    spacing: Kirigami.Units.smallSpacing

    Repeater {
        model: row.model

        delegate: Chip {
            required property var modelData

            text: modelData.text
            icon.name: modelData.icon || ""
            active: !!modelData.active
            enabled: modelData.enabled === undefined || !!modelData.enabled
            onClicked: row.chipClicked(modelData.id)
        }
    }
}
