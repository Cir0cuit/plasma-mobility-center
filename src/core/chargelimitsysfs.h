// SPDX-License-Identifier: GPL-2.0-or-later
//
// ChargeLimitSysfs - read-only probe of the kernel's charge thresholds.
//
// Looks for a /sys/class/power_supply/<name> with type == "Battery" that has
// charge_control_end_threshold (and optionally charge_control_start_threshold)
// with the owner-write bit set (the kernel only exposes them writable when
// the firmware supports them). The files are mode 0644, so reading needs no
// privileges - this is what the 60 s poll uses. Synchronous: tiny sysfs
// reads, no D-Bus.
//
// `root` is a parameter so tests can point it at a fake directory tree.

#pragma once

#include <QString>

struct ChargeThresholds {
    bool present = false; ///< a battery with a writable end threshold exists
    QString batteryName; ///< e.g. "BAT0"; empty if !present
    int stop = -1; ///< charge_control_end_threshold, 0..100; -1 unknown
    int start = -1; ///< charge_control_start_threshold; -1 absent (end-only) or unknown
};

namespace ChargeLimitSysfs
{

/// Probes @p root (default /sys/class/power_supply). First matching battery wins.
ChargeThresholds read(const QString &root = QStringLiteral("/sys/class/power_supply"));

/// The kernel's charging status of @p batteryName under @p root ("Charging",
/// "Discharging", "Full", "Not charging", ...); empty if unreadable.
QString status(const QString &batteryName, const QString &root = QStringLiteral("/sys/class/power_supply"));

} // namespace ChargeLimitSysfs
