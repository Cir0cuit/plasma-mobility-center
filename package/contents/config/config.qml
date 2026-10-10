// SPDX-License-Identifier: GPL-2.0-or-later

import QtQuick
import org.kde.plasma.configuration

ConfigModel {
    ConfigCategory {
        name: i18n("Tiles")
        icon: "view-grid"
        source: "ConfigTiles.qml"
    }
    ConfigCategory {
        name: i18n("Behavior")
        icon: "preferences-desktop"
        source: "ConfigBehavior.qml"
    }
}
