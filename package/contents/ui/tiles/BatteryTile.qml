// SPDX-License-Identifier: GPL-2.0-or-later
//
// Battery & power. Collapsed: percent, AC state, time
// remaining, power profile chips. Details: health, cycles, rate, the
// charge-limit slider, profile degradation and hold reasons.
// ChargeLimitSlider.qml belong to their own backends).

import QtQuick
import QtQuick.Layouts

import org.kde.coreaddons as KCoreAddons
import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents3

import io.github.cir0cuit.mobilitycenter.core

import ".."

Card {
    id: tile

    readonly property ChargeLimit chargeLimit: services ? services.chargeLimit : null
    readonly property bool charging: battery.chargeState === BatteryMonitor.Charging

    available: battery.available
    iconName: {
        const level = Math.round(Math.max(0, Math.min(100, battery.percent)) / 10) * 10;
        return "battery-" + String(level).padStart(3, "0") + (battery.pluggedIn ? "-charging" : "");
    }
    statusText: {
        const parts = [i18nc("battery charge in percent", "%1%", battery.percent)];
        if (battery.limitReached) {
            parts.push(i18n("Limited to %1%", battery.chargeStopThreshold));
        } else if (battery.chargeState === BatteryMonitor.FullyCharged) {
            parts.push(i18n("Fully charged"));
        } else if (tile.charging) {
            const ms = battery.timeToLimitMsec > 0 ? battery.timeToLimitMsec : battery.remainingMsec;
            parts.push(ms > 0 ? i18n("%1 until charged", tile.formatDuration(ms)) : i18n("Charging"));
        } else if (battery.pluggedIn) {
            parts.push(i18n("Plugged in, not charging"));
        } else if (battery.remainingMsec > 0) {
            parts.push(i18n("%1 remaining", tile.formatDuration(battery.remainingMsec)));
        }
        return parts.join(" · ");
    }
    detailContent: details

    onDetailsShownChanged: {
        if (detailsShown && chargeLimit && chargeLimit.available) {
            chargeLimit.refresh();
            chargeLimit.checkAuthorization();
        }
    }

    function formatDuration(ms) {
        return KCoreAddons.Format.formatDuration(ms, KCoreAddons.FormatTypes.HideSeconds);
    }

    BatteryMonitor {
        id: battery
        chargeStopThreshold: tile.chargeLimit && tile.chargeLimit.available ? tile.chargeLimit.stopThreshold : -1
    }

    // One backend for the chips here and the notes in the details.
    PowerProfiles {
        id: profilesBackend
    }

    PowerProfileChips {
        Layout.fillWidth: true
        profiles: profilesBackend
    }

    Component {
        id: details

        ColumnLayout {
            spacing: Kirigami.Units.smallSpacing

            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: Kirigami.Units.largeSpacing
                rowSpacing: 0

                PlasmaComponents3.Label {
                    text: i18n("Health:")
                    visible: battery.capacity >= 0
                }
                PlasmaComponents3.Label {
                    Layout.fillWidth: true
                    visible: battery.capacity >= 0
                    elide: Text.ElideRight
                    text: battery.energyFullDesign > 0
                          ? i18n("%1% (%2 of %3 Wh)", battery.capacity, battery.energyFull.toFixed(1), battery.energyFullDesign.toFixed(1))
                          : i18nc("battery health in percent", "%1%", battery.capacity)
                }
                PlasmaComponents3.Label {
                    text: i18n("Charge cycles:")
                    visible: battery.cycleCount >= 0
                }
                PlasmaComponents3.Label {
                    visible: battery.cycleCount >= 0
                    text: String(battery.cycleCount)
                }
                // Always there, also at 0 W: a row that comes and goes
                // with the charger would resize the card every time.
                PlasmaComponents3.Label {
                    text: tile.charging ? i18n("Charge rate:") : i18n("Discharge rate:")
                }
                PlasmaComponents3.Label {
                    text: i18nc("power in watts", "%1 W", Math.max(0, battery.energyRate).toFixed(1))
                }
                PlasmaComponents3.Label {
                    text: i18n("Battery:")
                    visible: battery.productName !== ""
                }
                PlasmaComponents3.Label {
                    Layout.fillWidth: true
                    visible: battery.productName !== ""
                    text: battery.technology !== "" ? i18nc("battery name, technology", "%1, %2", battery.productName, battery.technology)
                                                    : battery.productName
                    elide: Text.ElideRight
                }
            }

            // Several batteries: the figures above are the total; one line each.
            Repeater {
                model: battery.batteryCount > 1 ? battery.batteries : []

                PlasmaComponents3.Label {
                    required property var modelData
                    required property int index

                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    text: {
                        if (!modelData.present) {
                            return i18n("Battery %1: not present", index + 1);
                        }
                        return modelData.capacity >= 0
                               ? i18n("Battery %1: %2%, health %3%", index + 1, modelData.percent, modelData.capacity)
                               : i18n("Battery %1: %2%", index + 1, modelData.percent);
                    }
                }
            }

            ChargeLimitSlider {
                Layout.fillWidth: true
                chargeLimit: tile.chargeLimit
            }

            PowerProfileChips {
                Layout.fillWidth: true
                profiles: profilesBackend
                showDetails: true
                showChips: false
            }
        }
    }
}
