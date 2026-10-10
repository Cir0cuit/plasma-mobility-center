// SPDX-License-Identifier: GPL-2.0-or-later
//
// See chargelimitsysfs.h. Read-only: this file never opens anything for
// writing.

#include "chargelimitsysfs.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

using namespace Qt::StringLiterals;

namespace
{

/// First line of @p path, trimmed; null if it cannot be read.
QString readLine(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return QString::fromUtf8(file.readLine(64)).trimmed();
}

/// A threshold file the kernel exposes as writable (owner-write bit) - the
/// helper can then change it. Its value in percent, or -1.
bool writableThreshold(const QString &path, int *value)
{
    const QFileInfo info(path);
    if (!info.exists() || !(info.permissions() & QFileDevice::WriteOwner)) {
        return false;
    }
    bool ok = false;
    const int v = readLine(path).toInt(&ok);
    *value = ok && v >= 0 && v <= 100 ? v : -1;
    return true;
}

} // namespace

namespace ChargeLimitSysfs
{

ChargeThresholds read(const QString &root)
{
    ChargeThresholds result;
    const QDir dir(root);
    // Entries in /sys/class/power_supply are symlinks to directories, which
    // QDir::Dirs includes (no NoSymLinks).
    const QStringList names = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &name : names) {
        const QString base = dir.filePath(name) + u'/';
        if (readLine(base + u"type"_s) != u"Battery"_s) {
            continue;
        }
        // Peripheral batteries (mice, headsets) report scope "Device".
        if (readLine(base + u"scope"_s).compare(u"Device"_s, Qt::CaseInsensitive) == 0) {
            continue;
        }
        int stop = -1;
        if (!writableThreshold(base + u"charge_control_end_threshold"_s, &stop)) {
            continue;
        }
        int start = -1;
        if (!writableThreshold(base + u"charge_control_start_threshold"_s, &start)) {
            start = -1;
        }
        result.present = true;
        result.batteryName = name;
        result.stop = stop;
        result.start = start;
        return result;
    }
    return result;
}

QString status(const QString &batteryName, const QString &root)
{
    if (batteryName.isEmpty()) {
        return {};
    }
    return readLine(QDir(root).filePath(batteryName) + u"/status"_s);
}

} // namespace ChargeLimitSysfs
