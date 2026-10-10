// SPDX-License-Identifier: GPL-2.0-or-later
//
// See powerprofiles.h

#include "powerprofiles.h"

#include <QDBusConnection>
#include <QDBusError>

#include "dbusasync.h"

using namespace Qt::StringLiterals;

namespace
{
const QString kService = u"org.freedesktop.UPower.PowerProfiles"_s;
const QString kPath = u"/org/freedesktop/UPower/PowerProfiles"_s;
const QString kLegacyService = u"net.hadess.PowerProfiles"_s;
const QString kLegacyPath = u"/net/hadess/PowerProfiles"_s;

const QString kUPowerService = u"org.freedesktop.UPower"_s;
const QString kUPowerPath = u"/org/freedesktop/UPower"_s;

const QString kPmService = u"org.kde.Solid.PowerManagement"_s;
const QString kPmProfilePath = u"/org/kde/Solid/PowerManagement/Actions/PowerProfile"_s;
const QString kPmProfileIface = u"org.kde.Solid.PowerManagement.Actions.PowerProfile"_s;

// Display order; anything else a daemon might offer is ignored.
const QStringList kKnownProfiles{u"power-saver"_s, u"balanced"_s, u"performance"_s};

QString stringOf(const QVariantMap &map, const QString &key)
{
    return map.value(key).toString();
}
} // namespace

PowerProfiles::PowerProfiles(QObject *parent)
    : QObject(parent)
{
    const QDBusConnection system = QDBusConnection::systemBus();
    m_daemonWatch = std::make_unique<MC::ServiceWatch>(system, QStringList{kService, kLegacyService}, this, [this](const QString &, bool) {
        selectService();
    });

    // tuned-ppd can swap its Profiles with the power source without a
    // PropertiesChanged, and PowerDevil's configured profile depends on it.
    MC::DBus::onPropertiesChanged(system, kUPowerService, kUPowerPath, kUPowerService, this, [this](const QVariantMap &changed, const QStringList &invalidated) {
        if (changed.contains(u"OnBattery"_s) || invalidated.contains(u"OnBattery"_s)) {
            refresh();
        }
    });

    m_pmWatch = std::make_unique<MC::ServiceWatch>(QDBusConnection::sessionBus(), QStringList{kPmService}, this, [this](const QString &, bool registered) {
        if (!registered) {
            onPowerManagementDown();
            return;
        }
        ++m_pmGeneration;
        delete m_pmRelay;
        m_pmRelay = MC::DBus::onSignal(QDBusConnection::sessionBus(),
                                       kPmService,
                                       kPmProfilePath,
                                       kPmProfileIface,
                                       u"configuredProfileChanged"_s,
                                       this,
                                       [this](const QDBusMessage &msg) {
                                           if (!msg.arguments().isEmpty()) {
                                               setConfiguredProfile(msg.arguments().constFirst().toString());
                                           }
                                       });
        fetchConfiguredProfile();
    });
}

PowerProfiles::~PowerProfiles() = default;

bool PowerProfiles::available() const
{
    return m_available;
}

QString PowerProfiles::errorText() const
{
    return m_errorText;
}

QString PowerProfiles::backendService() const
{
    return m_state.service;
}

QStringList PowerProfiles::profiles() const
{
    return m_state.profiles;
}

QString PowerProfiles::activeProfile() const
{
    return m_state.activeProfile;
}

QString PowerProfiles::performanceDegraded() const
{
    return m_state.performanceDegraded;
}

QStringList PowerProfiles::degradedReasons() const
{
    QStringList reasons;
    const QStringList parts = m_state.performanceDegraded.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        const QString reason = part.trimmed();
        if (!reason.isEmpty() && !reasons.contains(reason)) {
            reasons.append(reason);
        }
    }
    return reasons;
}

QVariantList PowerProfiles::holds() const
{
    return m_state.holds;
}

bool PowerProfiles::hasBatteryAware() const
{
    return m_state.hasBatteryAware;
}

bool PowerProfiles::batteryAware() const
{
    return m_state.hasBatteryAware && m_state.batteryAware;
}

QString PowerProfiles::configuredProfile() const
{
    return m_configuredProfile;
}

void PowerProfiles::setActiveProfile(const QString &profile)
{
    if (!m_available || !m_state.profiles.contains(profile)) {
        return;
    }
    // Re-sending the active profile still matters while something holds it:
    // a manual choice releases the holds.
    if (profile == m_state.activeProfile && m_state.holds.isEmpty()) {
        return;
    }
    const quint64 generation = m_generation;
    MC::DBus::set(QDBusConnection::systemBus(), m_state.service, path(), interface(), u"ActiveProfile"_s, profile, this, [this, generation](const QDBusError &error) {
        if (generation != m_generation) {
            return;
        }
        setErrorText(error.isValid() ? error.message() : QString());
        if (error.isValid()) {
            // The chips never moved, but re-read in case the daemon changed
            // anyway or knows a different profile list now.
            fetchAll();
        }
    });
}

void PowerProfiles::refresh()
{
    fetchAll();
    fetchConfiguredProfile();
}

void PowerProfiles::selectService()
{
    QString service;
    if (m_daemonWatch->isRegistered(kService)) {
        service = kService;
    } else if (m_daemonWatch->isRegistered(kLegacyService)) {
        service = kLegacyService;
    }

    // A restart shows up as down -> up, i.e. as two switches, so the new
    // instance always gets a full re-query. ppd owns both names; the second
    // one appearing must not reset what the first one already delivered.
    if (service == m_state.service) {
        return;
    }
    ++m_generation;
    delete m_daemonRelay;
    m_daemonRelay = nullptr;

    State state;
    state.service = service;
    setState(state);
    if (service.isEmpty()) {
        return;
    }

    m_daemonRelay = MC::DBus::onPropertiesChanged(QDBusConnection::systemBus(),
                                                  service,
                                                  path(),
                                                  interface(),
                                                  this,
                                                  [this](const QVariantMap &changed, const QStringList &invalidated) {
                                                      if (!invalidated.isEmpty()) {
                                                          fetchAll();
                                                      }
                                                      if (changed.isEmpty() || !m_state.loaded) {
                                                          // Before GetAll answers, its reply carries the news.
                                                          return;
                                                      }
                                                      State state = m_state;
                                                      applyProperties(changed, state);
                                                      setState(state);
                                                  });
    fetchAll();
}

void PowerProfiles::fetchAll()
{
    if (m_state.service.isEmpty()) {
        return;
    }
    const quint64 generation = m_generation;
    MC::DBus::getAll(QDBusConnection::systemBus(),
                     m_state.service,
                     path(),
                     interface(),
                     this,
                     [this, generation](const QVariantMap &props, const QDBusError &error) {
                         if (generation != m_generation) {
                             return;
                         }
                         if (error.isValid()) {
                             setErrorText(error.message());
                             return;
                         }
                         State state;
                         state.service = m_state.service;
                         state.loaded = true;
                         applyProperties(props, state);
                         setState(state);
                     });
}

void PowerProfiles::applyProperties(const QVariantMap &props, State &state) const
{
    if (props.contains(u"Profiles"_s)) {
        const QVariantList entries = props.value(u"Profiles"_s).toList();
        QStringList offered;
        for (const QVariant &entry : entries) {
            offered.append(stringOf(entry.toMap(), u"Profile"_s));
        }
        state.profiles.clear();
        for (const QString &profile : kKnownProfiles) {
            if (offered.contains(profile)) {
                state.profiles.append(profile);
            }
        }
        state.placeholder = !entries.isEmpty() && stringOf(entries.constFirst().toMap(), u"Driver"_s) == u"placeholder"_s;
    }
    if (props.contains(u"ActiveProfile"_s)) {
        state.activeProfile = props.value(u"ActiveProfile"_s).toString();
    }
    if (props.contains(u"PerformanceDegraded"_s)) {
        state.performanceDegraded = props.value(u"PerformanceDegraded"_s).toString();
    }
    if (props.contains(u"ActiveProfileHolds"_s)) {
        state.holds.clear();
        const QVariantList holds = props.value(u"ActiveProfileHolds"_s).toList();
        for (const QVariant &hold : holds) {
            const QVariantMap map = hold.toMap();
            state.holds.append(QVariantMap{
                {u"Profile"_s, stringOf(map, u"Profile"_s)},
                {u"Reason"_s, stringOf(map, u"Reason"_s)},
                {u"ApplicationId"_s, stringOf(map, u"ApplicationId"_s)},
            });
        }
    }
    if (props.contains(u"BatteryAware"_s)) {
        state.hasBatteryAware = true;
        state.batteryAware = props.value(u"BatteryAware"_s).toBool();
    }
}

void PowerProfiles::setState(const State &next)
{
    const State old = m_state;
    m_state = next;
    // The active profile is only meaningful when it is one of the chips.
    if (!m_state.profiles.contains(m_state.activeProfile)) {
        m_state.activeProfile.clear();
    }

    const bool available = m_state.loaded && !m_state.profiles.isEmpty() && !m_state.placeholder;
    const bool notifyAvailable = available != m_available || old.service != m_state.service;
    m_available = available;

    if (old.profiles != m_state.profiles || old.hasBatteryAware != m_state.hasBatteryAware || old.batteryAware != m_state.batteryAware) {
        Q_EMIT profilesChanged();
    }
    if (old.activeProfile != m_state.activeProfile) {
        Q_EMIT activeProfileChanged();
    }
    if (old.performanceDegraded != m_state.performanceDegraded) {
        Q_EMIT performanceDegradedChanged();
    }
    if (old.holds != m_state.holds) {
        Q_EMIT holdsChanged();
    }
    if (notifyAvailable) {
        Q_EMIT availableChanged();
    }
    if (old.service != m_state.service) {
        // Errors from a daemon that is gone are not worth showing.
        setErrorText({});
    }
}

void PowerProfiles::fetchConfiguredProfile()
{
    if (!m_pmWatch->isRegistered(kPmService)) {
        return;
    }
    const quint64 generation = m_pmGeneration;
    const QDBusMessage msg = MC::DBus::methodCall(kPmService, kPmProfilePath, kPmProfileIface, u"configuredProfile"_s);
    MC::DBus::call(QDBusConnection::sessionBus(), msg, this, [this, generation](const QDBusMessage &reply) {
        if (generation != m_pmGeneration) {
            return;
        }
        // Older PowerDevil, or no profile action loaded: no hint.
        const bool ok = reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty();
        setConfiguredProfile(ok ? reply.arguments().constFirst().toString() : QString());
    });
}

void PowerProfiles::onPowerManagementDown()
{
    ++m_pmGeneration;
    delete m_pmRelay;
    m_pmRelay = nullptr;
    setConfiguredProfile({});
}

void PowerProfiles::setConfiguredProfile(const QString &profile)
{
    const QString value = kKnownProfiles.contains(profile) ? profile : QString();
    if (m_configuredProfile == value) {
        return;
    }
    m_configuredProfile = value;
    Q_EMIT configuredProfileChanged();
}

void PowerProfiles::setErrorText(const QString &text)
{
    if (m_errorText == text) {
        return;
    }
    m_errorText = text;
    Q_EMIT errorTextChanged();
}

QString PowerProfiles::path() const
{
    return m_state.service == kLegacyService ? kLegacyPath : kPath;
}

QString PowerProfiles::interface() const
{
    return m_state.service == kLegacyService ? kLegacyService : kService;
}
