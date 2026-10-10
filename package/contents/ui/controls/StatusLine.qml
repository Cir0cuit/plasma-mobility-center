// SPDX-License-Identifier: GPL-2.0-or-later
//
// Secondary, one-or-two-line status text. Hidden when empty.

import QtQuick

import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents3

PlasmaComponents3.Label {
    visible: text !== ""
    opacity: 0.75
    elide: Text.ElideRight
    wrapMode: Text.Wrap
    maximumLineCount: 2
    textFormat: Text.PlainText
    font: Kirigami.Theme.smallFont
}
