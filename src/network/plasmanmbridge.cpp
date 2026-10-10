// SPDX-License-Identifier: GPL-2.0-or-later
//
// See plasmanmbridge.h. Uses the same KSharedConfig("plasma-nm") instance as
// plasma-nm's own Configuration class (same plasmashell process), so a write
// here is seen by the tray immediately; KConfig::Notify reaches its
// KConfigWatcher (Plasma >= 6.5) and other processes.

#include "plasmanmbridge.h"

#include <KConfigGroup>

using namespace Qt::StringLiterals;

namespace
{
const QString kFile = u"plasma-nm"_s;
const QString kGroup = u"General"_s;
const QString kAirplaneKey = u"AirplaneModeEnabled"_s;
const QString kSystemKey = u"SystemConnectionsByDefault"_s;
} // namespace

PlasmaNmBridge::PlasmaNmBridge(QObject *parent)
    : QObject(parent)
    , m_config(KSharedConfig::openConfig(kFile))
{
    m_watcher = KConfigWatcher::create(m_config);
    connect(m_watcher.data(), &KConfigWatcher::configChanged, this, [this](const KConfigGroup &group, const QByteArrayList &) {
        if (group.name() == kGroup) {
            read();
        }
    });
    read();
}

PlasmaNmBridge::~PlasmaNmBridge() = default;

bool PlasmaNmBridge::available() const
{
    return m_config != nullptr;
}

QString PlasmaNmBridge::errorText() const
{
    return m_errorText;
}

bool PlasmaNmBridge::airplaneFlag() const
{
    return m_airplaneFlag;
}

bool PlasmaNmBridge::systemConnectionsByDefault() const
{
    return m_systemConnectionsByDefault;
}

void PlasmaNmBridge::setAirplaneFlag(bool on)
{
    KConfigGroup group(m_config, kGroup);
    if (group.isEntryImmutable(kAirplaneKey) || !m_config->isConfigWritable(false)) {
        setErrorText(u"The plasma-nm configuration is read-only"_s);
        return;
    }
    if (group.readEntry(kAirplaneKey, false) == on) {
        // Nothing to change (also avoids creating the file just to say false).
        read();
        return;
    }
    group.writeEntry(kAirplaneKey, on, KConfig::Notify);
    if (!m_config->sync()) {
        setErrorText(u"Could not write the plasma-nm configuration"_s);
    } else {
        setErrorText({});
    }
    read();
}

void PlasmaNmBridge::refresh()
{
    m_config->reparseConfiguration();
    read();
}

void PlasmaNmBridge::read()
{
    const KConfigGroup group(m_config, kGroup);
    const bool airplane = group.readEntry(kAirplaneKey, false);
    const bool system = group.readEntry(kSystemKey, true);
    if (airplane != m_airplaneFlag) {
        m_airplaneFlag = airplane;
        Q_EMIT airplaneFlagChanged();
    }
    if (system != m_systemConnectionsByDefault) {
        m_systemConnectionsByDefault = system;
        Q_EMIT systemConnectionsByDefaultChanged();
    }
}

void PlasmaNmBridge::setErrorText(const QString &text)
{
    if (m_errorText == text) {
        return;
    }
    m_errorText = text;
    Q_EMIT errorTextChanged();
}
