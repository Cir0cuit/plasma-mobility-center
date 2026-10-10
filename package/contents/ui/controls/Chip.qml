// SPDX-License-Identifier: GPL-2.0-or-later
//
// A quick-settings toggle chip. `active` is the backend state (bind it);
// clicking emits `clicked` and leaves the decision to the backend, so the
// chip never shows an optimistic state the system did not take.

import QtQuick
import QtQuick.Controls as QQC2

import org.kde.plasma.components as PlasmaComponents3

PlasmaComponents3.Button {
    id: chip

    property bool active: false

    checkable: false
    checked: active
    display: QQC2.AbstractButton.TextBesideIcon
}
