// SPDX-License-Identifier: GPL-2.0-or-later
//
// Mobility Center - laptop quick controls as a Plasma 6 desktop widget.
//
// The controls need compiled QML modules (io.github.cir0cuit.mobilitycenter.*)
// that live on Qt's import path, which a widget archive cannot install by
// itself. CenterView.qml is therefore loaded through a Loader: with the .core
// module missing the widget says how to install it, instead of taking the
// whole plasmoid down with Plasma's generic "error loading QML file" box.

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.plasma.plasmoid
import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.extras as PlasmaExtras
import org.kde.kirigami as Kirigami

PlasmoidItem {
    id: root

    Plasmoid.icon: "computer-laptop"
    Plasmoid.backgroundHints: PlasmaCore.Types.DefaultBackground | PlasmaCore.Types.ConfigurableBackground

    // "Cards only": the user switched the widget background off. The cards
    // then draw their own opaque backgrounds and shadows.
    readonly property bool cardsOnly: !(Plasmoid.effectiveBackgroundHints
                                        & (PlasmaCore.Types.StandardBackground | PlasmaCore.Types.TranslucentBackground))

    // Desktop widget (Planar) and plasmawindowed (Application) get the full
    // view. In a panel the stock icon + popup stays as a harmless fallback;
    // it is not a supported mode.
    preferredRepresentation: (Plasmoid.formFactor === PlasmaCore.Types.Planar
                              || Plasmoid.formFactor === PlasmaCore.Types.Application) ? fullRepresentation : compactRepresentation

    toolTipMainText: i18n("Mobility Center")
    toolTipSubText: i18n("Brightness, volume, battery, wireless and display controls")

    fullRepresentation: Item {
        id: full

        // Fixed default size in grid units, never bound to the content: a
        // desktop widget must not shrink or grow by itself.
        Layout.preferredWidth: Kirigami.Units.gridUnit * 30
        Layout.preferredHeight: Kirigami.Units.gridUnit * 24
        Layout.minimumWidth: Kirigami.Units.gridUnit * 10
        Layout.minimumHeight: Kirigami.Units.gridUnit * 8

        Loader {
            id: viewLoader
            anchors.fill: parent
            source: Qt.resolvedUrl("CenterView.qml")
        }

        Binding {
            target: viewLoader.item
            property: "cardsOnly"
            value: root.cardsOnly
            when: viewLoader.status === Loader.Ready
        }

        PlasmaExtras.PlaceholderMessage {
            anchors.centerIn: parent
            width: parent.width - Kirigami.Units.gridUnit * 2
            visible: viewLoader.status === Loader.Error
            iconName: "computer-laptop"
            text: i18n("Mobility Center backend not installed")
            explanation: i18n("The controls talk to the system through compiled QML modules that have to be built and installed once, system-wide; a widget package cannot do that on its own. The project page has the commands.")

            helpfulAction: QQC2.Action {
                icon.name: "internet-services"
                text: i18n("Open the project page")
                onTriggered: Qt.openUrlExternally("https://github.com/Cir0cuit/plasma-mobility-center#build-and-install")
            }
        }
    }
}
