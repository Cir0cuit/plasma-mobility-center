// SPDX-License-Identifier: GPL-2.0-or-later
//
// See nightlight.h

#include "nightlight.h"

#include <KConfigGroup>
#include <KSharedConfig>

#include <QDBusConnection>
#include <QDBusError>

#include "dbusasync.h"
#include "privatebus.h"
#include "servicewatch.h"

using namespace Qt::StringLiterals;

namespace
{
const QString kService = u"org.kde.KWin.NightLight"_s;
const QString kPath = u"/org/kde/KWin/NightLight"_s;
const QString kIface = u"org.kde.KWin.NightLight"_s;

const QString kKcm = u"kcm_nightlight"_s;
const QString kConfigFile = u"kwinrc"_s;
const QString kConfigGroup = u"NightColor"_s;
const char kConfigKey[] = "Active";

constexpr int kNeutralTemperature = 6500;
} // namespace

NightLight::NightLight(QObject *parent)
    : QObject(parent)
    , m_privateCalls(new QObject(this))
{
    updateKioskState();
    m_watch = std::make_unique<MC::ServiceWatch>(QDBusConnection::sessionBus(), QStringList{kService}, this, [this](const QString &, bool registered) {
        registered ? onServiceUp() : onServiceDown();
    });
}

NightLight::~NightLight()
{
    // Pending calls on the private bus go first, then the bus itself: closing
    // the connection makes KWin drop every cookie we still hold.
    delete m_privateCalls;
    m_privateCalls = nullptr;
    m_privateBus.reset();
    m_watch.reset();
}

bool NightLight::available() const
{
    return m_available;
}

QString NightLight::errorText() const
{
    return m_errorText;
}

bool NightLight::enabled() const
{
    return m_enabled;
}

bool NightLight::running() const
{
    return m_running;
}

bool NightLight::inhibited() const
{
    return m_inhibited;
}

bool NightLight::checked() const
{
    return running() && !inhibited();
}

int NightLight::currentTemperature() const
{
    return m_currentTemperature;
}

int NightLight::targetTemperature() const
{
    return m_targetTemperature;
}

bool NightLight::neutral() const
{
    return checked() && currentTemperature() == kNeutralTemperature;
}

int NightLight::mode() const
{
    return m_mode;
}

bool NightLight::userInhibited() const
{
    return m_user.wanted;
}

bool NightLight::presentationInhibited() const
{
    return m_presentationInhibited;
}

void NightLight::setPresentationInhibited(bool inhibit)
{
    if (m_presentationInhibited == inhibit) {
        return;
    }
    m_presentationInhibited = inhibit;
    m_presentation.wanted = inhibit;
    Q_EMIT inhibitionsChanged();
    syncInhibition(m_presentation);
}

QString NightLight::enableMode() const
{
    return m_enableMode;
}

void NightLight::setEnableMode(const QString &mode)
{
    const QString normalized = mode == QLatin1String("kcm") ? mode : QStringLiteral("config");
    if (m_enableMode == normalized) {
        return;
    }
    m_enableMode = normalized;
    Q_EMIT enableModeChanged();
}

bool NightLight::canEnableDirectly() const
{
    return m_enableMode == QLatin1String("config") && !m_activeImmutable;
}

void NightLight::setChecked(bool on)
{
    if (!m_available) {
        return;
    }
    if (!on) {
        // Only Night Light that is switched on can be suspended; our own
        // inhibition is the one the chip controls.
        if (m_enabled && !m_user.wanted) {
            m_user.wanted = true;
            Q_EMIT inhibitionsChanged();
            syncInhibition(m_user);
        }
        return;
    }

    if (m_user.wanted) {
        m_user.wanted = false;
        Q_EMIT inhibitionsChanged();
        syncInhibition(m_user);
    }
    if (!m_enabled) {
        enableNightLight();
    }
}

void NightLight::refresh()
{
    updateKioskState();
    if (m_serviceUp) {
        fetchAll();
    }
}

void NightLight::onServiceUp()
{
    ++m_generation;
    m_serviceUp = true;
    delete m_relay;
    m_relay = MC::DBus::onPropertiesChanged(QDBusConnection::sessionBus(),
                                            kService,
                                            kPath,
                                            kIface,
                                            this,
                                            [this](const QVariantMap &changed, const QStringList &invalidated) {
                                                applyProperties(changed);
                                                if (!invalidated.isEmpty()) {
                                                    fetchAll();
                                                }
                                            });
    fetchAll();
}

void NightLight::onServiceDown()
{
    ++m_generation;
    m_serviceUp = false;
    delete m_relay;
    m_relay = nullptr;

    // KWin is gone and with it every inhibition it granted.
    dropCookies();

    const bool stateDiffers = m_enabled || m_running || m_inhibited || m_mode != 0;
    const bool tempDiffers = m_currentTemperature != 0 || m_targetTemperature != 0;
    m_enabled = m_running = m_inhibited = false;
    m_mode = 0;
    m_currentTemperature = m_targetTemperature = 0;
    if (stateDiffers) {
        Q_EMIT stateChanged();
    }
    if (stateDiffers || tempDiffers) {
        Q_EMIT temperatureChanged();
    }
    setAvailable(false);
}

void NightLight::fetchAll()
{
    const quint64 generation = m_generation;
    MC::DBus::getAll(QDBusConnection::sessionBus(), kService, kPath, kIface, this, [this, generation](const QVariantMap &props, const QDBusError &error) {
        if (generation != m_generation) {
            return;
        }
        if (error.isValid()) {
            setErrorText(error.message());
            setAvailable(false);
            return;
        }
        applyProperties(props);
        if (!props.contains(u"available"_s)) {
            // An older KWin without the property: the interface answering
            // is all we can go by.
            setAvailable(true);
        }
    });
}

void NightLight::applyProperties(const QVariantMap &props)
{
    const bool wasChecked = checked();
    const bool wasNeutral = neutral();
    bool stateDirty = false;
    bool tempDirty = false;

    auto takeBool = [&props](const QString &key, bool &field, bool &dirty) {
        const auto it = props.constFind(key);
        if (it != props.cend() && it->toBool() != field) {
            field = it->toBool();
            dirty = true;
        }
    };
    auto takeInt = [&props](const QString &key, int &field, bool &dirty) {
        const auto it = props.constFind(key);
        if (it != props.cend() && it->toInt() != field) {
            field = it->toInt();
            dirty = true;
        }
    };

    takeBool(u"enabled"_s, m_enabled, stateDirty);
    takeBool(u"running"_s, m_running, stateDirty);
    takeBool(u"inhibited"_s, m_inhibited, stateDirty);
    takeInt(u"mode"_s, m_mode, stateDirty);
    takeInt(u"currentTemperature"_s, m_currentTemperature, tempDirty);
    takeInt(u"targetTemperature"_s, m_targetTemperature, tempDirty);

    if (stateDirty) {
        Q_EMIT stateChanged();
    }
    if (tempDirty || wasNeutral != neutral() || (stateDirty && wasChecked != checked())) {
        Q_EMIT temperatureChanged();
    }

    const auto it = props.constFind(u"available"_s);
    if (it != props.cend()) {
        setAvailable(it->toBool());
    }
}

bool NightLight::kwinReady() const
{
    return m_serviceUp && m_available;
}

void NightLight::syncInhibition(Inhibition &inhibition)
{
    if (inhibition.pending) {
        // Settled when the reply in flight lands.
        return;
    }

    if (!inhibition.wanted) {
        if (inhibition.cookie && m_privateBus && m_serviceUp) {
            const QDBusMessage msg = MC::DBus::methodCall(kService, kPath, kIface, u"uninhibit"_s, {*inhibition.cookie});
            inhibition.cookie.reset();
            MC::DBus::call(m_privateBus->connection(), msg, m_privateCalls, [this](const QDBusMessage &reply) {
                if (reply.type() == QDBusMessage::ErrorMessage) {
                    setErrorText(reply.errorMessage());
                }
            });
        }
        inhibition.cookie.reset();
        return;
    }

    if (inhibition.cookie || !kwinReady()) {
        // Held already, or applied when KWin (re)appears.
        return;
    }

    if (!m_privateBus) {
        // Created on first use only: a private connection is a synchronous
        // connect + Hello, and most widget instances never inhibit anything.
        m_privateBus = std::make_unique<MC::PrivateBus>(QDBusConnection::SessionBus, u"mobilitycenter-nl"_s, this);
    }
    if (!m_privateBus->isConnected()) {
        setErrorText(m_privateBus->connection().lastError().message());
        m_privateBus.reset();
        const bool isUser = &inhibition == &m_user;
        if (isUser) {
            inhibition.wanted = false;
            Q_EMIT inhibitionsChanged();
        }
        return;
    }

    inhibition.pending = true;
    const quint64 generation = m_generation;
    Inhibition *target = &inhibition;
    const QDBusMessage msg = MC::DBus::methodCall(kService, kPath, kIface, u"inhibit"_s);
    MC::DBus::call(m_privateBus->connection(), msg, m_privateCalls, [this, generation, target](const QDBusMessage &reply) {
        if (generation != m_generation) {
            // Sent to a KWin that has gone since; its cookie died with it.
            // dropCookies() already cleared `pending`, and the new instance
            // gets a fresh request once it reports itself available.
            return;
        }
        target->pending = false;
        if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().isEmpty()) {
            setErrorText(reply.errorMessage());
            if (target == &m_user && m_user.wanted) {
                // The chip must not claim an inhibition KWin refused.
                m_user.wanted = false;
                Q_EMIT inhibitionsChanged();
            }
            return;
        }
        setErrorText({});
        target->cookie = reply.arguments().constFirst().toUInt();
        // Released while the call was in flight: give it back at once.
        syncInhibition(*target);
    });
}

void NightLight::dropCookies()
{
    m_user.cookie.reset();
    m_presentation.cookie.reset();
    // A pending inhibit() reply is ignored by the generation check.
    m_user.pending = false;
    m_presentation.pending = false;
    if (m_user.wanted) {
        // The user inhibition does not survive a KWin restart; presentation
        // mode's request is an input and is re-applied when KWin returns.
        m_user.wanted = false;
        Q_EMIT inhibitionsChanged();
    }
}

void NightLight::updateKioskState()
{
    const bool wasDirect = canEnableDirectly();
    KSharedConfig::Ptr config = KSharedConfig::openConfig(kConfigFile, KConfig::NoGlobals);
    config->reparseConfiguration();
    m_activeImmutable = config->isImmutable() || KConfigGroup(config, kConfigGroup).isEntryImmutable(kConfigKey);
    if (wasDirect != canEnableDirectly()) {
        Q_EMIT enableModeChanged();
    }
}

void NightLight::enableNightLight()
{
    updateKioskState();
    if (!canEnableDirectly()) {
        Q_EMIT kcmRequested(kKcm);
        return;
    }

    // What the Night Light KCM does: KWin's KConfigWatcher picks up the
    // notification and switches Night Light on; "enabled" then follows over
    // D-Bus.
    KSharedConfig::Ptr config = KSharedConfig::openConfig(kConfigFile, KConfig::NoGlobals);
    KConfigGroup group(config, kConfigGroup);
    group.writeEntry(kConfigKey, true, KConfig::Notify | KConfig::Persistent);
    if (!config->sync()) {
        setErrorText(tr("Could not write the Night Light setting."));
        return;
    }
    setErrorText({});
}

void NightLight::setAvailable(bool available)
{
    if (m_available == available) {
        return;
    }
    m_available = available;
    Q_EMIT availableChanged();
    if (available) {
        syncInhibition(m_user);
        syncInhibition(m_presentation);
    }
}

void NightLight::setErrorText(const QString &text)
{
    if (m_errorText == text) {
        return;
    }
    m_errorText = text;
    Q_EMIT errorTextChanged();
}
