// SPDX-License-Identifier: GPL-2.0-or-later
//
// ServiceWatch - "is this D-Bus name owned right now?", for backends that must
// cope with their service appearing, disappearing and restarting at runtime.
//
// Header-only and free of Q_OBJECT, so every module can compile it in without
// moc. Usage (in a backend constructor):
//
//     m_watch = std::make_unique<MC::ServiceWatch>(
//         QDBusConnection::sessionBus(), QStringList{u"org.kde.ScreenBrightness"_s}, this,
//         [this](const QString &service, bool registered) {
//             registered ? onServiceUp() : onServiceDown();
//         });
//
// The callback fires
//   * once after the asynchronous initial NameHasOwner check, if the name is
//     owned (never for "not owned" - the initial state is already "down");
//   * on every later registration and unregistration.
// Duplicate notifications are filtered, so the callback only ever sees real
// state changes (initial check vs. a racing registration signal included).
// It runs in the thread of `context` and never after `context` is destroyed.
//
// A restart of the service shows up as unregistered -> registered; backends
// must fully re-query on every registration.

#pragma once

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QStringList>

#include <functional>
#include <memory>

namespace MC
{

class ServiceWatch
{
public:
    using Callback = std::function<void(const QString &service, bool registered)>;

    ServiceWatch(const QDBusConnection &bus, const QStringList &services, QObject *context, Callback callback)
        : m_state(std::make_shared<State>())
    {
        m_state->callback = std::move(callback);

        m_watcher = new QDBusServiceWatcher(context);
        m_watcher->setConnection(bus);
        m_watcher->setWatchMode(QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration);
        m_watcher->setWatchedServices(services);

        const std::shared_ptr<State> state = m_state;
        QObject::connect(m_watcher, &QDBusServiceWatcher::serviceRegistered, context, [state](const QString &service) {
            state->update(service, true);
        });
        QObject::connect(m_watcher, &QDBusServiceWatcher::serviceUnregistered, context, [state](const QString &service) {
            state->update(service, false);
        });

        for (const QString &service : services) {
            QDBusMessage msg = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.DBus"),
                                                              QStringLiteral("/org/freedesktop/DBus"),
                                                              QStringLiteral("org.freedesktop.DBus"),
                                                              QStringLiteral("NameHasOwner"));
            msg << service;
            auto *call = new QDBusPendingCallWatcher(bus.asyncCall(msg), m_watcher);
            QObject::connect(call, &QDBusPendingCallWatcher::finished, context, [state, service](QDBusPendingCallWatcher *w) {
                w->deleteLater();
                const QDBusPendingReply<bool> reply = *w;
                // Only report "up": "down" is the initial state, and a racing
                // registration signal may already have said "up".
                if (!reply.isError() && reply.value() && !state->known.contains(service)) {
                    state->update(service, true);
                }
            });
        }
    }

    ~ServiceWatch()
    {
        // The watcher is a child of `context`; if the owner destroys us first,
        // make sure no callback can run into a half-destroyed backend.
        m_state->callback = nullptr;
        delete m_watcher.data();
    }

    ServiceWatch(const ServiceWatch &) = delete;
    ServiceWatch &operator=(const ServiceWatch &) = delete;

    /// Last known state of @p service (false until the initial check answered).
    bool isRegistered(const QString &service) const
    {
        return m_state->known.value(service, false);
    }

    /// True if any watched service is registered.
    bool anyRegistered() const
    {
        for (auto it = m_state->known.cbegin(); it != m_state->known.cend(); ++it) {
            if (it.value()) {
                return true;
            }
        }
        return false;
    }

    QDBusServiceWatcher *watcher() const
    {
        return m_watcher;
    }

private:
    struct State {
        Callback callback;
        QHash<QString, bool> known;

        void update(const QString &service, bool registered)
        {
            const auto it = known.constFind(service);
            if (it != known.cend() && it.value() == registered) {
                return;
            }
            known.insert(service, registered);
            if (callback) {
                callback(service, registered);
            }
        }
    };

    std::shared_ptr<State> m_state;
    QPointer<QDBusServiceWatcher> m_watcher;
};

} // namespace MC
