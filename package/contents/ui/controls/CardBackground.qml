// SPDX-License-Identifier: GPL-2.0-or-later
//
// The rounded background shared by cards and the detail panel. With the
// widget background on, a subtle tint over it; in "cards only" mode an
// opaque View-coloured card with a shadow, so it reads on any wallpaper.

import QtQuick

import org.kde.kirigami as Kirigami

Item {
    id: bg

    property bool cardsOnly: false
    property bool highlighted: false
    property bool hovered: false

    readonly property color fillColor: cardsOnly
        ? Kirigami.Theme.backgroundColor
        : Kirigami.ColorUtils.tintWithAlpha(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, hovered ? 0.10 : 0.06)
    readonly property color borderColor: highlighted
        ? Kirigami.Theme.highlightColor
        : Kirigami.ColorUtils.tintWithAlpha(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, 0.15)
    readonly property real borderWidth: highlighted ? 2 : 1
    readonly property real radius: Kirigami.Units.cornerRadius * 2

    Kirigami.ShadowedRectangle {
        anchors.fill: parent
        radius: bg.radius
        color: bg.fillColor
        border.color: bg.borderColor
        border.width: bg.borderWidth
        shadow.size: bg.cardsOnly ? Kirigami.Units.gridUnit / 2 : 0
        shadow.yOffset: bg.cardsOnly ? 2 : 0
        shadow.color: Qt.rgba(0, 0, 0, 0.3)
    }
}
