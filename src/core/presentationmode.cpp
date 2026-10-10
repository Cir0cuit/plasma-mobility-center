// SPDX-License-Identifier: GPL-2.0-or-later
//
// See presentationmode.h
//
// Read-only traffic (service watch, DND support/state, PolicyAgent blocks)
// uses the shared session bus. Everything that inhibits goes through the
// private connection, opened on the first activation and closed in the
// destructor, so the daemons drop our cookies when the widget goes away.

#include "presentationmode.h"

#include <QDBusConnection>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QTimer>

#include <memory>
#include <optional>
#include <utility>

#include "dbusasync.h"
#include "privatebus.h"
#include "servicewatch.h"

using namespace Qt::StringLiterals;

namespace
{
const QString kPmService = u"org.freedesktop.PowerManagement.Inhibit"_s;
const QString kPmPath = u"/org/freedesktop/PowerManagement/Inhibit"_s;
const QString kPmIface = u"org.freedesktop.PowerManagement.Inhibit"_s;

const QString kSsService = u"org.freedesktop.ScreenSaver"_s;
const QString kSsPath = u"/ScreenSaver"_s;
const QString kSsIface = u"org.freedesktop.ScreenSaver"_s;

const QString kNotifyService = u"org.freedesktop.Notifications"_s;
const QString kNotifyPath = u"/org/freedesktop/Notifications"_s;
const QString kNotifyIface = u"org.freedesktop.Notifications"_s;

const QString kAgentService = u"org.kde.Solid.PowerManagement.PolicyAgent"_s;
const QString kAgentPath = u"/org/kde/Solid/PowerManagement/PolicyAgent"_s;
const QString kAgentIface = u"org.kde.Solid.PowerManagement.PolicyAgent"_s;

// Stable on purpose (not translated): PowerDevil remembers a "block" choice
// per (app, reason), and the same pair for PowerManagement and ScreenSaver
// lets one choice cover both. Also the Notifications desktop_entry, which
// must never be empty (plasmashell would do a blocking PID lookup).
const QString kApp = u"Mobility Center"_s;
const QString kReason = u"Presentation mode"_s;

// PowerDevil enforces (and reports) an inhibition only after 5 s.
constexpr int kBlockGraceMs = 5500;
// PowerDevil registers several names on start; react once.
constexpr int kRestartDebounceMs = 200;
// PolicyAgentInhibition::Allowed.
constexpr uint kAllowedFlag = 0x2;

QString describe(const QDBusMessage &reply)
{
    return reply.errorMessage().isEmpty() ? reply.errorName() : reply.errorMessage();
}

// "a(ss)" / "a(ssssu)" entries arrive demarshalled as QVariantList structs.
bool containsPair(const QVariantList &list, int whoIndex, int whyIndex)
{
    for (const QVariant &entry : list) {
        const QVariantList fields = entry.toList();
        if (fields.size() > qMax(whoIndex, whyIndex) && fields.at(whoIndex).toString() == kApp && fields.at(whyIndex).toString() == kReason) {
            return true;
        }
    }
    return false;
}
} // namespace

PresentationMode::PresentationMode(QObject *parent)
    : QObject(parent)
    , m_blockGrace(new QTimer(this))
    , m_powerDevilRestart(new QTimer(this))
{
    m_blockGrace->setSingleShot(true);
    m_blockGrace->setInterval(kBlockGraceMs);
    connect(m_blockGrace, &QTimer::timeout, this, &PresentationMode::updateBlockedByUser);

    m_powerDevilRestart->setSingleShot(true);
    m_powerDevilRestart->setInterval(kRestartDebounceMs);
    connect(m_powerDevilRestart, &QTimer::timeout, this, &PresentationMode::onPowerDevilRestarted);

    // Watching names is read-only; nothing is inhibited before setActive(true).
    m_watch = std::make_unique<MC::ServiceWatch>(QDBusConnection::sessionBus(),
                                                 QStringList{kPmService, kSsService, kNotifyService, kAgentService},
                                                 this,
                                                 [this](const QString &service, bool registered) {
                                                     onServiceChanged(service, registered);
                                                 });
}

PresentationMode::~PresentationMode()
{
    m_watch.reset();
    // The NightLight object can outlive us: give its inhibition back.
    if (m_nightLightHook) {
        m_nightLightHook->setPresentationInhibited(false);
    }
    // Pending calls hold references to the private connection; drop them so
    // closing it really closes the socket (and the daemons drop our cookies).
    const auto watchers = findChildren<QDBusPendingCallWatcher *>(Qt::FindDirectChildrenOnly);
    qDeleteAll(watchers);
    m_bus.reset();
}

bool PresentationMode::active() const
{
    return m_active;
}

void PresentationMode::setActive(bool active)
{
    if (m_active == active) {
        return;
    }
    if (active && !m_available) {
        return;
    }
    m_active = active;
    setErrorText({});

    if (active) {
        if (m_dndRevokedByUser) {
            m_dndRevokedByUser = false;
            Q_EMIT dndChanged();
        }
        for (int kind = 0; kind < KindCount; ++kind) {
            inhibit(Kind(kind));
        }
        m_blockGrace->start();
    } else {
        m_blockGrace->stop();
        m_powerDevilRestart->stop();
        for (int kind = 0; kind < KindCount; ++kind) {
            uninhibit(Kind(kind));
        }
        if (m_dndRevokedByUser) {
            m_dndRevokedByUser = false;
            Q_EMIT dndChanged();
        }
    }
    applyNightLightHook();
    updateBlockedByUser();
    Q_EMIT activeChanged();
}

bool PresentationMode::available() const
{
    return m_available;
}

QString PresentationMode::errorText() const
{
    return m_errorText;
}

bool PresentationMode::sleepBlocked() const
{
    return m_inhibitions[PowerKind].held;
}

bool PresentationMode::lockBlocked() const
{
    return m_inhibitions[ScreenSaverKind].held;
}

bool PresentationMode::dndAvailable() const
{
    return m_dndAvailable;
}

bool PresentationMode::dndActive() const
{
    return m_dndActive;
}

bool PresentationMode::dndRevokedByUser() const
{
    return m_dndRevokedByUser;
}

bool PresentationMode::blockedByUser() const
{
    return m_blockedByUser;
}

bool PresentationMode::nightLightInhibited() const
{
    return !m_nightLightHook.isNull();
}

NightLight *PresentationMode::nightLight() const
{
    return m_nightLight;
}

void PresentationMode::setNightLight(NightLight *nightLight)
{
    if (m_nightLight == nightLight) {
        return;
    }
    m_nightLight = nightLight;
    Q_EMIT nightLightChanged();
    applyNightLightHook();
}

bool PresentationMode::inhibitNightLight() const
{
    return m_inhibitNightLight;
}

void PresentationMode::setInhibitNightLight(bool inhibit)
{
    if (m_inhibitNightLight == inhibit) {
        return;
    }
    m_inhibitNightLight = inhibit;
    Q_EMIT nightLightChanged();
    applyNightLightHook();
}

void PresentationMode::refresh()
{
    updateAvailable();
    if (m_watch->isRegistered(kNotifyService)) {
        fetchDndState();
    }
    if (m_watch->isRegistered(kAgentService)) {
        fetchBlockedState();
    }
}

// ---- services --------------------------------------------------------------

void PresentationMode::onServiceChanged(const QString &service, bool registered)
{
    if (service == kPmService || service == kSsService) {
        const Kind kind = service == kPmService ? PowerKind : ScreenSaverKind;
        Inhibition &inh = m_inhibitions[kind];
        ++inh.instance;
        ++inh.request;
        inh.pending = false;
        if (inh.held) {
            // The daemon is gone and so is our cookie.
            inh.held = false;
            Q_EMIT inhibitionsChanged();
        }
        updateAvailable();
        if (registered && m_active) {
            if (kind == PowerKind) {
                // PowerDevil restart: the PowerManagement cookie is re-issued
                // and the ScreenSaver one re-sent (see onPowerDevilRestarted).
                m_powerDevilRestart->start();
            } else {
                // KWin restart: re-issue the ScreenSaver cookie.
                inhibit(ScreenSaverKind);
            }
        }
        return;
    }

    if (service == kNotifyService) {
        ++m_notificationsGeneration;
        qDeleteAll(m_notificationRelays);
        m_notificationRelays.clear();
        Inhibition &inh = m_inhibitions[NotificationsKind];
        ++inh.instance;
        ++inh.request;
        inh.pending = false;
        if (inh.held) {
            inh.held = false;
            Q_EMIT inhibitionsChanged();
        }
        if (!registered) {
            const bool changed = m_dndAvailable || m_dndActive;
            m_dndAvailable = false;
            m_dndActive = false;
            if (changed) {
                Q_EMIT dndChanged();
            }
            return;
        }
        m_notificationRelays << MC::DBus::onPropertiesChanged(QDBusConnection::sessionBus(),
                                                              kNotifyService,
                                                              kNotifyPath,
                                                              kNotifyIface,
                                                              this,
                                                              [this](const QVariantMap &changed, const QStringList &invalidated) {
                                                                  if (changed.contains(u"Inhibited"_s)) {
                                                                      setDndActive(changed.value(u"Inhibited"_s).toBool());
                                                                  } else if (invalidated.contains(u"Inhibited"_s)) {
                                                                      fetchDndState();
                                                                  }
                                                              });
        // Re-inhibits (if active and not revoked) once support is confirmed.
        fetchDndState();
        return;
    }

    if (service == kAgentService) {
        ++m_agentGeneration;
        qDeleteAll(m_agentRelays);
        m_agentRelays.clear();
        if (!registered) {
            m_rawBlocked = false;
            updateBlockedByUser();
            return;
        }
        if (m_active) {
            m_powerDevilRestart->start();
        }
        const QDBusConnection bus = QDBusConnection::sessionBus();
        // 6.5+: PowerDevil invalidates RequestedInhibitions (no value).
        m_agentRelays << MC::DBus::onPropertiesChanged(bus, kAgentService, kAgentPath, kAgentIface, this, [this](const QVariantMap &changed, const QStringList &invalidated) {
            if (changed.contains(u"RequestedInhibitions"_s) || invalidated.contains(u"RequestedInhibitions"_s)) {
                fetchBlockedState();
            }
        });
        // 6.3/6.4.
        for (const QString &signal : {u"PermanentlyBlockedInhibitionsChanged"_s, u"TemporarilyBlockedInhibitionsChanged"_s}) {
            m_agentRelays << MC::DBus::onSignal(bus, kAgentService, kAgentPath, kAgentIface, signal, this, [this](const QDBusMessage &) {
                fetchBlockedState();
            });
        }
        m_agentLegacy = false;
        fetchBlockedState();
        return;
    }
}

bool PresentationMode::isRegistered(Kind kind) const
{
    switch (kind) {
    case PowerKind:
        return m_watch->isRegistered(kPmService);
    case ScreenSaverKind:
        return m_watch->isRegistered(kSsService);
    case NotificationsKind:
        return m_watch->isRegistered(kNotifyService) && m_dndAvailable;
    case KindCount:
        break;
    }
    return false;
}

void PresentationMode::updateAvailable()
{
    const bool available = m_watch->isRegistered(kPmService) || m_watch->isRegistered(kSsService);
    if (m_available == available) {
        return;
    }
    m_available = available;
    Q_EMIT availableChanged();
}

void PresentationMode::onPowerDevilRestarted()
{
    if (!m_active) {
        return;
    }
    inhibit(PowerKind);
    // kscreenlocker forwards its inhibitions to PowerDevil only once, so the
    // new PowerDevil does not know ours: hand the cookie back and ask again.
    const Inhibition &ss = m_inhibitions[ScreenSaverKind];
    if (ss.held || ss.pending) {
        uninhibit(ScreenSaverKind);
        inhibit(ScreenSaverKind);
    }
    m_blockGrace->start();
    updateBlockedByUser();
}

// ---- inhibitions -----------------------------------------------------------

QDBusConnection PresentationMode::privateBus()
{
    if (!m_bus) {
        m_bus = std::make_unique<MC::PrivateBus>(QDBusConnection::SessionBus, u"mobilitycenter-pm"_s, this);
    }
    return m_bus->connection();
}

void PresentationMode::inhibit(Kind kind)
{
    Inhibition &inh = m_inhibitions[kind];
    if (!m_active || inh.held || inh.pending || !isRegistered(kind)) {
        return;
    }
    if (kind == NotificationsKind && m_dndRevokedByUser) {
        return;
    }

    QDBusMessage msg;
    QString what;
    switch (kind) {
    case PowerKind:
        msg = MC::DBus::methodCall(kPmService, kPmPath, kPmIface, u"Inhibit"_s, {kApp, kReason});
        what = u"Could not prevent automatic sleep: %1"_s;
        break;
    case ScreenSaverKind:
        msg = MC::DBus::methodCall(kSsService, kSsPath, kSsIface, u"Inhibit"_s, {kApp, kReason});
        what = u"Could not prevent screen locking: %1"_s;
        break;
    case NotificationsKind:
        msg = MC::DBus::methodCall(kNotifyService, kNotifyPath, kNotifyIface, u"Inhibit"_s, {kApp, kReason, QVariantMap()});
        what = u"Could not turn on Do Not Disturb: %1"_s;
        break;
    case KindCount:
        return;
    }

    const QDBusConnection bus = privateBus();
    if (!bus.isConnected()) {
        setErrorText(what.arg(bus.lastError().message()));
        return;
    }

    const quint64 request = ++inh.request;
    const quint64 instance = inh.instance;
    inh.pending = true;
    MC::DBus::call(bus, msg, this, [this, kind, request, instance, what](const QDBusMessage &reply) {
        Inhibition &inh = m_inhibitions[kind];
        if (instance != inh.instance) {
            // The service went away meanwhile; its cookies died with it.
            return;
        }
        const bool ok = reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty();
        const uint cookie = ok ? reply.arguments().constFirst().toUInt() : 0;
        if (request != inh.request) {
            // Switched off (or re-requested) while the call was in flight.
            if (ok) {
                sendUninhibit(kind, cookie);
            }
            return;
        }
        inh.pending = false;
        if (!ok) {
            setErrorText(what.arg(describe(reply)));
            return;
        }
        inh.held = true;
        inh.cookie = cookie;
        Q_EMIT inhibitionsChanged();
    });
}

void PresentationMode::uninhibit(Kind kind)
{
    Inhibition &inh = m_inhibitions[kind];
    ++inh.request;
    inh.pending = false;
    if (!inh.held) {
        return;
    }
    inh.held = false;
    sendUninhibit(kind, inh.cookie);
    Q_EMIT inhibitionsChanged();
}

void PresentationMode::sendUninhibit(Kind kind, uint cookie)
{
    if (!m_bus) {
        return;
    }
    QDBusMessage msg;
    switch (kind) {
    case PowerKind:
        msg = MC::DBus::methodCall(kPmService, kPmPath, kPmIface, u"UnInhibit"_s, {cookie});
        break;
    case ScreenSaverKind:
        msg = MC::DBus::methodCall(kSsService, kSsPath, kSsIface, u"UnInhibit"_s, {cookie});
        break;
    case NotificationsKind:
        msg = MC::DBus::methodCall(kNotifyService, kNotifyPath, kNotifyIface, u"UnInhibit"_s, {cookie});
        break;
    case KindCount:
        return;
    }
    MC::DBus::call(m_bus->connection(), msg, this, {});
}

// ---- Do Not Disturb --------------------------------------------------------

void PresentationMode::fetchDndState()
{
    const quint64 generation = m_notificationsGeneration;
    const QDBusConnection bus = QDBusConnection::sessionBus();
    const QDBusMessage info = MC::DBus::methodCall(kNotifyService, kNotifyPath, kNotifyIface, u"GetServerInformation"_s);
    MC::DBus::call(bus, info, this, [this, generation](const QDBusMessage &reply) {
        if (generation != m_notificationsGeneration) {
            return;
        }
        const bool plasma = reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()
            && reply.arguments().constFirst().toString() == u"Plasma"_s;
        if (plasma != m_dndAvailable) {
            m_dndAvailable = plasma;
            Q_EMIT dndChanged();
        }
        if (plasma) {
            inhibit(NotificationsKind);
        }
    });
    MC::DBus::get(bus, kNotifyService, kNotifyPath, kNotifyIface, u"Inhibited"_s, this, [this, generation](const QVariant &value, const QDBusError &error) {
        if (generation != m_notificationsGeneration || error.isValid()) {
            return;
        }
        setDndActive(value.toBool());
    });
}

void PresentationMode::setDndActive(bool active)
{
    if (m_dndActive == active) {
        return;
    }
    const bool wasActive = m_dndActive;
    m_dndActive = active;
    Inhibition &inh = m_inhibitions[NotificationsKind];
    if (wasActive && !active && m_active && inh.held && m_watch->isRegistered(kNotifyService)) {
        // Switched off in the notifications applet: plasmashell dropped every
        // inhibition, ours included. Respect it and do not re-inhibit.
        ++inh.request;
        inh.held = false;
        m_dndRevokedByUser = true;
        Q_EMIT inhibitionsChanged();
    }
    Q_EMIT dndChanged();
}

// ---- user blocks (PolicyAgent) ---------------------------------------------

void PresentationMode::fetchBlockedState()
{
    const quint64 generation = m_agentGeneration;
    const QDBusConnection bus = QDBusConnection::sessionBus();

    if (!m_agentLegacy) {
        MC::DBus::get(bus, kAgentService, kAgentPath, kAgentIface, u"RequestedInhibitions"_s, this, [this, generation](const QVariant &value, const QDBusError &error) {
            if (generation != m_agentGeneration) {
                return;
            }
            if (error.isValid()) {
                // Plasma 6.3/6.4: no RequestedInhibitions yet.
                m_agentLegacy = true;
                fetchBlockedState();
                return;
            }
            applyBlockedLists(value.toList(), {}, {});
        });
        return;
    }

    // Both lists, then evaluate once. Shared state lives in the lambdas.
    auto lists = std::make_shared<std::pair<std::optional<QVariantList>, std::optional<QVariantList>>>();
    auto finish = [this, generation, lists] {
        if (generation != m_agentGeneration || !lists->first || !lists->second) {
            return;
        }
        applyBlockedLists({}, *lists->first, *lists->second);
    };
    auto listCall = [&](const QString &method, bool first) {
        const QDBusMessage msg = MC::DBus::methodCall(kAgentService, kAgentPath, kAgentIface, method);
        MC::DBus::call(bus, msg, this, [lists, finish, first](const QDBusMessage &reply) {
            QVariantList list;
            if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
                list = MC::DBus::demarshall(reply.arguments().constFirst()).toList();
            }
            (first ? lists->first : lists->second) = list;
            finish();
        });
    };
    listCall(u"ListPermanentlyBlockedInhibitions"_s, true);
    listCall(u"ListTemporarilyBlockedInhibitions"_s, false);
}

void PresentationMode::applyBlockedLists(const QVariantList &requested, const QVariantList &permanent, const QVariantList &temporary)
{
    bool blocked = false;
    // (what, who, why, mode, flags): ours, without the Allowed flag.
    for (const QVariant &entry : requested) {
        const QVariantList f = entry.toList();
        if (f.size() >= 5 && f.at(1).toString() == kApp && f.at(2).toString() == kReason && (f.at(4).toUInt() & kAllowedFlag) == 0) {
            blocked = true;
            break;
        }
    }
    // (who, why)
    blocked = blocked || containsPair(permanent, 0, 1) || containsPair(temporary, 0, 1);
    m_rawBlocked = blocked;
    updateBlockedByUser();
}

void PresentationMode::updateBlockedByUser()
{
    // PowerDevil only decides after ~5 s; do not report anything before.
    const bool blocked = m_active && !m_blockGrace->isActive() && m_rawBlocked;
    if (m_blockedByUser == blocked) {
        return;
    }
    m_blockedByUser = blocked;
    Q_EMIT blockedByUserChanged();
}

// ---- Night Light -------------------------------------------------------------

void PresentationMode::applyNightLightHook()
{
    const bool wasHooked = !m_nightLightHook.isNull();
    const bool want = m_active && m_inhibitNightLight && m_nightLight;

    if (m_nightLightHook && (!want || m_nightLightHook != m_nightLight)) {
        disconnect(m_nightLightHook, &QObject::destroyed, this, nullptr);
        m_nightLightHook->setPresentationInhibited(false);
        m_nightLightHook.clear();
    }
    if (want && !m_nightLightHook) {
        m_nightLightHook = m_nightLight;
        m_nightLightHook->setPresentationInhibited(true);
        connect(m_nightLightHook, &QObject::destroyed, this, [this] {
            // QPointer is already null here.
            Q_EMIT inhibitionsChanged();
        });
    }
    if (wasHooked != !m_nightLightHook.isNull()) {
        Q_EMIT inhibitionsChanged();
    }
}

void PresentationMode::setErrorText(const QString &text)
{
    if (m_errorText == text) {
        return;
    }
    m_errorText = text;
    Q_EMIT errorTextChanged();
}
