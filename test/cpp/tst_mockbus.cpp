// SPDX-License-Identifier: GPL-2.0-or-later
//
// Self-test of the shared mock-bus helper (mockbus.h) and of the safety net
// in test/CMakeLists.txt: the test runs on private buses only, a private
// system bus can be started, and a python-dbusmock template appears on it.
// Nothing here talks to the live session or system bus.

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QTest>

#include "mockbus.h"

using namespace Qt::StringLiterals;

class TestMockBus : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void addressChecks()
    {
        QVERIFY(!MockBus::checkPrivateAddress({}, MockBus::Session).isEmpty());
        QVERIFY(!MockBus::checkPrivateAddress("unix:path=/run/user/1000/bus", MockBus::Session).isEmpty());
        QVERIFY(!MockBus::checkPrivateAddress("unix:path=/run/dbus/system_bus_socket", MockBus::System).isEmpty());
        QVERIFY(!MockBus::checkPrivateAddress({}, MockBus::System).isEmpty());
        QVERIFY(MockBus::checkPrivateAddress("unix:path=/tmp/dbus-abc,guid=1", MockBus::Session).isEmpty());
    }

    void environmentIsIsolated()
    {
        // Set by CTest (test/CMakeLists.txt); without them a stray backend
        // could write ~/.config/kwinrc or reach the live system bus.
        MC_REQUIRE_PRIVATE_SESSION_BUS();
        QVERIFY(qEnvironmentVariable("XDG_CONFIG_HOME").contains(u"test-home"_s));
        QVERIFY(qEnvironmentVariable("DBUS_SYSTEM_BUS_ADDRESS").contains(u"no-such-bus-socket"_s));
        QVERIFY(!qEnvironmentVariableIsEmpty("PULSE_SERVER"));
        QCOMPARE(qEnvironmentVariable("KSCREEN_BACKEND"), u"Fake"_s);
        // The default system bus is unusable until a test starts its own.
        QVERIFY(!MockBus::checkPrivateSystemBus().isEmpty());
    }

    void privateSystemBusWithDbusmock()
    {
        MockBus::SystemBus system;
        QVERIFY2(system.start(), qPrintable(system.error()));
        MC_REQUIRE_PRIVATE_SYSTEM_BUS();

        // The process-wide system bus now is the private one: it has no
        // logind or systemd, which every real system bus has.
        QDBusConnection bus = QDBusConnection::systemBus();
        QVERIFY(bus.isConnected());
        QVERIFY(!bus.interface()->isServiceRegistered(u"org.freedesktop.login1"_s));
        QVERIFY(!bus.interface()->isServiceRegistered(u"org.freedesktop.systemd1"_s));

        MC_REQUIRE_DBUSMOCK();
        MockBus::Mock upower;
        QVERIFY2(upower.start(MockBus::System, {u"--template"_s, u"upower"_s}, u"org.freedesktop.UPower"_s), qPrintable(upower.error()));
        QVERIFY(bus.interface()->isServiceRegistered(u"org.freedesktop.UPower"_s));

        upower.stop();
        QTRY_VERIFY(!bus.interface()->isServiceRegistered(u"org.freedesktop.UPower"_s));
    }
};

QTEST_GUILESS_MAIN(TestMockBus)
#include "tst_mockbus.moc"
