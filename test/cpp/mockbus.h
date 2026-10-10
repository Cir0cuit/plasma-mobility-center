// SPDX-License-Identifier: GPL-2.0-or-later
//
// MockBus - private D-Bus buses and python-dbusmock services for backend
// tests, so that no test ever talks to (let alone changes) the live session.
// Shared by every tst_<backend>.cpp; checked by tst_mockbus.cpp.
//
// Typical use (a test registered with DBUS in test/CMakeLists.txt, so it
// already runs on a private session bus):
//
//     void initTestCase()
//     {
//         MC_REQUIRE_PRIVATE_SESSION_BUS();
//         QVERIFY2(m_system.start(), qPrintable(m_system.error()));   // before ANY systemBus() use
//         MC_REQUIRE_PRIVATE_SYSTEM_BUS();
//         QVERIFY2(m_upower.start(MockBus::System, {u"--template"_s, u"upower"_s}, u"org.freedesktop.UPower"_s),
//                  qPrintable(m_upower.error()));
//     }
//     MockBus::SystemBus m_system;   // declared BEFORE the mocks: destroyed after them
//     MockBus::Mock m_upower;
//
// Custom templates live in test/mocks/<backend>_*.py and are passed as
// {"--template", MockBus::mockFile("screenbrightness_mock.py")}.
//
// Rules: start the private system bus before the first
// QDBusConnection::systemBus() call in the process (Qt caches that
// connection); never fall back to a real bus - QSKIP/QFAIL instead.

#pragma once

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

namespace MockBus
{

enum Bus {
    Session,
    System,
};

/// "" when @p address is a usable PRIVATE bus address, else why not. An
/// empty address means "the default bus", i.e. the live one.
inline QString checkPrivateAddress(const QByteArray &address, Bus bus)
{
    if (address.isEmpty()) {
        return QStringLiteral("the %1 bus address is empty (would mean the live bus)").arg(bus == System ? QStringLiteral("system") : QStringLiteral("session"));
    }
    const QList<QByteArray> live = bus == System
        ? QList<QByteArray>{"/run/dbus/system_bus_socket", "/var/run/dbus/system_bus_socket"}
        : QList<QByteArray>{"/run/user/", "/var/run/user/"};
    for (const QByteArray &marker : live) {
        if (address.contains(marker)) {
            return QStringLiteral("%1 points at the live bus").arg(QString::fromLocal8Bit(address));
        }
    }
    return {};
}

/// "" when this process's session bus is a private one (dbus-run-session).
inline QString checkPrivateSessionBus()
{
    return checkPrivateAddress(qgetenv("DBUS_SESSION_BUS_ADDRESS"), Session);
}

/// "" when this process's system bus is a private one (MockBus::SystemBus).
inline QString checkPrivateSystemBus()
{
    const QString err = checkPrivateAddress(qgetenv("DBUS_SYSTEM_BUS_ADDRESS"), System);
    if (!err.isEmpty()) {
        return err;
    }
    if (qEnvironmentVariableIsEmpty("MC_MOCKBUS_SYSTEM_BUS")) {
        return QStringLiteral("no private system bus started (MockBus::SystemBus)");
    }
    return {};
}

/// Absolute path of test/mocks/@p name (MC_TEST_MOCKS_DIR is set by CTest).
inline QString mockFile(const QString &name)
{
    return QDir(qEnvironmentVariable("MC_TEST_MOCKS_DIR")).absoluteFilePath(name);
}

/// Waits until @p service is owned on the bus at @p address.
inline bool waitForName(const QString &address, const QString &service, int timeoutMs = 10000)
{
    const QString connName = QStringLiteral("mockbus-wait-%1").arg(quintptr(&service), 0, 16);
    bool owned = false;
    {
        QDBusConnection bus = QDBusConnection::connectToBus(address, connName);
        QDeadlineTimer deadline(timeoutMs);
        while (bus.isConnected() && !deadline.hasExpired()) {
            if (bus.interface()->isServiceRegistered(service)) {
                owned = true;
                break;
            }
            QTest::qWait(50);
        }
    }
    QDBusConnection::disconnectFromBus(connName);
    return owned;
}

/// A private dbus-daemon of type "system" (same configuration as
/// python-dbusmock's DBusTestCase.start_system_bus()). start() points
/// DBUS_SYSTEM_BUS_ADDRESS at it; the destructor kills it.
class SystemBus
{
public:
    SystemBus() = default;
    SystemBus(const SystemBus &) = delete;
    SystemBus &operator=(const SystemBus &) = delete;

    ~SystemBus()
    {
        stop();
    }

    bool start()
    {
        if (m_daemon.state() != QProcess::NotRunning) {
            return true;
        }
        if (!m_dir.isValid()) {
            m_error = QStringLiteral("cannot create a temporary directory");
            return false;
        }
        const QString config = m_dir.filePath(QStringLiteral("system.conf"));
        QFile file(config);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            m_error = QStringLiteral("cannot write %1").arg(config);
            return false;
        }
        // Plain literals: moc (which also reads this header) cannot parse raw strings here.
        const QString xml = QStringLiteral(
                                "<!DOCTYPE busconfig PUBLIC \"-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN\"\n"
                                " \"http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd\">\n"
                                "<busconfig>\n"
                                "  <type>system</type>\n"
                                "  <keep_umask/>\n"
                                "  <listen>unix:path=%1</listen>\n"
                                "  <servicedir>%2</servicedir>\n"
                                "  <policy context=\"default\">\n"
                                "    <allow send_destination=\"*\" eavesdrop=\"true\"/>\n"
                                "    <allow eavesdrop=\"true\"/>\n"
                                "    <allow own=\"*\"/>\n"
                                "  </policy>\n"
                                "</busconfig>\n")
                                .arg(m_dir.filePath(QStringLiteral("system_bus_socket")), m_dir.path());
        file.write(xml.toUtf8());
        file.close();

        m_daemon.setProgram(QStringLiteral("dbus-daemon"));
        m_daemon.setArguments({QStringLiteral("--config-file=%1").arg(config), QStringLiteral("--nofork"), QStringLiteral("--print-address=1")});
        m_daemon.setProcessChannelMode(QProcess::SeparateChannels);
        m_daemon.start();
        if (!m_daemon.waitForStarted(5000)) {
            m_error = QStringLiteral("dbus-daemon did not start: %1").arg(m_daemon.errorString());
            return false;
        }
        QDeadlineTimer deadline(5000);
        while (!m_daemon.canReadLine() && !deadline.hasExpired() && m_daemon.state() == QProcess::Running) {
            m_daemon.waitForReadyRead(100);
        }
        m_address = QString::fromLocal8Bit(m_daemon.readLine()).trimmed();
        const QString check = checkPrivateAddress(m_address.toLocal8Bit(), System);
        if (m_address.isEmpty() || !check.isEmpty()) {
            m_error = m_address.isEmpty() ? QStringLiteral("dbus-daemon printed no address") : check;
            stop();
            return false;
        }
        qputenv("DBUS_SYSTEM_BUS_ADDRESS", m_address.toLocal8Bit());
        qputenv("MC_MOCKBUS_SYSTEM_BUS", "1");
        return true;
    }

    void stop()
    {
        if (m_daemon.state() != QProcess::NotRunning) {
            m_daemon.terminate();
            if (!m_daemon.waitForFinished(3000)) {
                m_daemon.kill();
                m_daemon.waitForFinished(1000);
            }
        }
    }

    QString address() const
    {
        return m_address;
    }

    QString error() const
    {
        return m_error;
    }

private:
    QTemporaryDir m_dir;
    QProcess m_daemon;
    QString m_address;
    QString m_error;
};

/// One `python3 -m dbusmock` process on the private session or system bus.
/// The destructor stops it (stopping it is also how a test simulates the
/// service going away; start() again for a restart).
class Mock
{
public:
    Mock() = default;
    Mock(const Mock &) = delete;
    Mock &operator=(const Mock &) = delete;

    ~Mock()
    {
        stop();
    }

    /// @p args: dbusmock arguments without the bus switch, e.g.
    /// {"--template", "upower"} or {"org.example.Name", "/path", "org.example.Iface"}.
    /// @p busName: the name to wait for.
    bool start(Bus bus, const QStringList &args, const QString &busName, int timeoutMs = 10000)
    {
        const QString check = bus == System ? checkPrivateSystemBus() : checkPrivateSessionBus();
        if (!check.isEmpty()) {
            m_error = QStringLiteral("refusing to start a mock: %1").arg(check);
            return false;
        }
        stop();
        QStringList argv{QStringLiteral("-m"), QStringLiteral("dbusmock"), bus == System ? QStringLiteral("--system") : QStringLiteral("--session")};
        argv += args;
        m_process.setProgram(QStringLiteral("python3"));
        m_process.setArguments(argv);
        m_process.setProcessChannelMode(QProcess::ForwardedErrorChannel);
        m_process.start();
        if (!m_process.waitForStarted(5000)) {
            m_error = QStringLiteral("python3 -m dbusmock did not start: %1").arg(m_process.errorString());
            return false;
        }
        const QString address = qEnvironmentVariable(bus == System ? "DBUS_SYSTEM_BUS_ADDRESS" : "DBUS_SESSION_BUS_ADDRESS");
        if (!waitForName(address, busName, timeoutMs)) {
            m_error = QStringLiteral("%1 did not appear on the private bus (is python3-dbusmock installed?)").arg(busName);
            stop();
            return false;
        }
        return true;
    }

    void stop()
    {
        if (m_process.state() != QProcess::NotRunning) {
            m_process.terminate();
            if (!m_process.waitForFinished(3000)) {
                m_process.kill();
                m_process.waitForFinished(1000);
            }
        }
    }

    bool isRunning() const
    {
        return m_process.state() == QProcess::Running;
    }

    QString error() const
    {
        return m_error;
    }

private:
    QProcess m_process;
    QString m_error;
};

/// python3 can import dbusmock.
inline bool dbusmockAvailable()
{
    QProcess p;
    p.start(QStringLiteral("python3"), {QStringLiteral("-c"), QStringLiteral("import dbusmock")});
    return p.waitForFinished(10000) && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

} // namespace MockBus

/// Fails the test unless the session bus is private (dbus-run-session).
#define MC_REQUIRE_PRIVATE_SESSION_BUS()                                                                                                                       \
    do {                                                                                                                                                       \
        const QString err_ = MockBus::checkPrivateSessionBus();                                                                                                \
        QVERIFY2(err_.isEmpty(), qPrintable(err_));                                                                                                            \
    } while (false)

/// Fails the test unless a private system bus was started (MockBus::SystemBus).
#define MC_REQUIRE_PRIVATE_SYSTEM_BUS()                                                                                                                        \
    do {                                                                                                                                                       \
        const QString err_ = MockBus::checkPrivateSystemBus();                                                                                                 \
        QVERIFY2(err_.isEmpty(), qPrintable(err_));                                                                                                            \
    } while (false)

/// Skips the test when python3-dbusmock is not installed.
#define MC_REQUIRE_DBUSMOCK()                                                                                                                                  \
    do {                                                                                                                                                       \
        if (!MockBus::dbusmockAvailable()) {                                                                                                                   \
            QSKIP("python3-dbusmock is not installed");                                                                                                        \
        }                                                                                                                                                      \
    } while (false)
