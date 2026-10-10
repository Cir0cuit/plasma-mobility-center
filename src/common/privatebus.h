// SPDX-License-Identifier: GPL-2.0-or-later
//
// PrivateBus - a session (or system) bus connection of our own, separate from
// plasmashell's shared QDBusConnection::sessionBus().
//
// Why: inhibitions (PowerManagement, ScreenSaver,
// Notifications, KWin Night Light) are tied to the *caller's* unique bus name.
// On the shared connection
//   * Notifications.Inhibit is a loopback call into plasmashell itself, which
//     is never auto-released and can deadlock a blocking call;
//   * removing the widget would not release anything, because the shared
//     connection outlives it.
// With a private connection, destroying the owner closes the connection and
// every daemon drops our cookies on its own - also when plasmashell crashes.
//
// Usage: a backend holds `std::unique_ptr<MC::PrivateBus>` and creates it
// lazily, the first time it really needs to inhibit something (opening a bus
// connection is a synchronous socket connect + Hello).
//
// Do not keep copies of connection() (e.g. in QDBusInterface objects or
// lambda captures) beyond the PrivateBus's lifetime: Qt only closes the
// socket when the last QDBusConnection referring to it is gone.

#pragma once

#include <QDBusConnection>
#include <QString>

namespace MC
{

class PrivateBus
{
public:
    /// @param prefix  e.g. "mobilitycenter-pm"; the connection name becomes
    ///                "<prefix>-<hex address of owner>", unique per instance.
    PrivateBus(QDBusConnection::BusType type, const QString &prefix, const void *owner)
        : m_name(QStringLiteral("%1-%2").arg(prefix).arg(quintptr(owner), 0, 16))
        , m_connection(QDBusConnection::connectToBus(type, m_name))
    {
    }

    ~PrivateBus()
    {
        // Drop our own reference first, or disconnectFromBus() would only
        // forget the name and leave the socket open.
        m_connection = QDBusConnection(QStringLiteral("mobilitycenter-closed"));
        QDBusConnection::disconnectFromBus(m_name);
    }

    PrivateBus(const PrivateBus &) = delete;
    PrivateBus &operator=(const PrivateBus &) = delete;

    QDBusConnection connection() const
    {
        return m_connection;
    }

    bool isConnected() const
    {
        return m_connection.isConnected();
    }

    /// Our unique name on the bus (":1.234"), e.g. for echo filtering.
    QString baseService() const
    {
        return m_connection.baseService();
    }

    QString name() const
    {
        return m_name;
    }

private:
    QString m_name;
    QDBusConnection m_connection;
};

} // namespace MC
