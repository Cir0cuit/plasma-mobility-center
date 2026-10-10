// SPDX-License-Identifier: GPL-2.0-or-later
//
// BatteryMonitor against mocked services - runs under dbus-run-session (a private
// session bus) plus a private system bus (MockBus::SystemBus), see
// test/mocks/README.md. Never against the live session or system bus.
//
// Mocks: test/mocks/batterymonitor_upower.py (system bus, the stock upower
// template + AddBattery) and batterymonitor_powerdevil.py (session bus).
//
// Solid's device manager is a process singleton that connects to the system
// bus once, so the private system bus and UPower mock live for the whole run;
// init() resets the mock state, and the UPower restart case runs last.

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QSignalSpy>
#include <QTest>

#include "batterymonitor.h"
#include "dbusasync.h"
#include "mockbus.h"

using namespace Qt::StringLiterals;

namespace
{
const QString kUPower = u"org.freedesktop.UPower"_s;
const QString kUPowerPath = u"/org/freedesktop/UPower"_s;
const QString kDevicePrefix = u"/org/freedesktop/UPower/devices/"_s;
const QString kDeviceIface = u"org.freedesktop.UPower.Device"_s;
const QString kPowerDevil = u"org.kde.Solid.PowerManagement"_s;
const QString kPowerDevilPath = u"/org/kde/Solid/PowerManagement"_s;
const QString kMockIface = u"org.freedesktop.DBus.Mock"_s;

QDBusConnection busFor(MockBus::Bus bus)
{
    return bus == MockBus::System ? QDBusConnection::systemBus() : QDBusConnection::sessionBus();
}

QDBusMessage callMock(MockBus::Bus bus, const QString &service, const QString &path, const QString &method, const QVariantList &args = {})
{
    QDBusMessage msg = QDBusMessage::createMethodCall(service, path, kMockIface, method);
    msg.setArguments(args);
    return busFor(bus).call(msg);
}

bool mockOk(const QDBusMessage &reply)
{
    if (reply.type() == QDBusMessage::ErrorMessage) {
        qWarning() << reply.errorName() << reply.errorMessage();
        return false;
    }
    return true;
}

// The argument lists of every call of @p method on @p path, oldest first.
QList<QVariantList> methodCalls(MockBus::Bus bus, const QString &service, const QString &path, const QString &method)
{
    QList<QVariantList> out;
    const QDBusMessage reply = callMock(bus, service, path, u"GetMethodCalls"_s, {method});
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        return out;
    }
    const QVariantList calls = MC::DBus::demarshall(reply.arguments().constFirst()).toList();
    for (const QVariant &call : calls) {
        const QVariantList fields = call.toList(); // (timestamp, args)
        if (fields.size() == 2) {
            out.append(fields.at(1).toList());
        }
    }
    return out;
}

bool setDevice(const QString &name, const QVariantMap &props)
{
    return mockOk(callMock(MockBus::System, kUPower, kUPowerPath, u"SetDeviceProperties"_s, {QVariant::fromValue(QDBusObjectPath(kDevicePrefix + name)), props}));
}

bool setOnBattery(bool onBattery)
{
    QDBusMessage set = QDBusMessage::createMethodCall(kUPower, kUPowerPath, u"org.freedesktop.DBus.Properties"_s, u"Set"_s);
    set.setArguments({kUPower, u"OnBattery"_s, QVariant::fromValue(QDBusVariant(onBattery))});
    return mockOk(QDBusConnection::systemBus().call(set));
}

bool setRemaining(qulonglong msec)
{
    return mockOk(callMock(MockBus::Session, kPowerDevil, kPowerDevilPath, u"SetSmoothedRemainingTime"_s, {QVariant::fromValue(msec)}));
}

// The defaults of batterymonitor_upower.py's AddBattery, for resetting BAT0.
QVariantMap defaultBattery()
{
    return {
        {u"IsPresent"_s, true},
        {u"State"_s, 2u},
        {u"Percentage"_s, 50.0},
        {u"Energy"_s, 30.0},
        {u"EnergyFull"_s, 60.0},
        {u"EnergyFullDesign"_s, 75.0},
        {u"EnergyRate"_s, 10.0},
        {u"Capacity"_s, 80.0},
        {u"ChargeCycles"_s, 123},
        {u"TimeToEmpty"_s, qlonglong(3 * 3600)},
        {u"TimeToFull"_s, qlonglong(0)},
    };
}
} // namespace

class TestBatteryMonitor : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        MC_REQUIRE_PRIVATE_SESSION_BUS();
        MC_REQUIRE_DBUSMOCK();
        // Before ANY systemBus() use (Solid, ServiceWatch).
        QVERIFY2(m_system.start(), qPrintable(m_system.error()));
        MC_REQUIRE_PRIVATE_SYSTEM_BUS();
        QVERIFY2(m_upower.start(MockBus::System, {u"--template"_s, MockBus::mockFile(u"batterymonitor_upower.py"_s)}, kUPower),
                 qPrintable(m_upower.error()));
        QVERIFY(mockOk(callMock(MockBus::System, kUPower, kUPowerPath, u"AddBattery"_s, {u"BAT0"_s, QVariantMap()})));
        // A non-battery power supply must not count.
        QVERIFY(mockOk(callMock(MockBus::System, kUPower, kUPowerPath, u"AddAC"_s, {u"AC"_s, u"Mains"_s})));
    }

    void init()
    {
        QVERIFY2(m_powerDevil.start(MockBus::Session, {u"--template"_s, MockBus::mockFile(u"batterymonitor_powerdevil.py"_s)}, kPowerDevil),
                 qPrintable(m_powerDevil.error()));
        QVERIFY(setDevice(u"BAT0"_s, defaultBattery()));
        QVERIFY(setOnBattery(false));
    }

    void cleanup()
    {
        m_powerDevil.stop();
    }

    void readsBattery()
    {
        BatteryMonitor b;
        QTRY_VERIFY(b.available());
        QCOMPARE(b.batteryCount(), 1);
        QCOMPARE(b.percent(), 50);
        QCOMPARE(b.chargeState(), BatteryMonitor::Discharging);
        QCOMPARE(b.energy(), 30.0);
        QCOMPARE(b.energyFull(), 60.0);
        QCOMPARE(b.energyFullDesign(), 75.0);
        QCOMPARE(b.energyRate(), 10.0);
        QCOMPARE(b.capacity(), 80);
        QCOMPARE(b.cycleCount(), 123);
        QCOMPARE(b.technology(), u"Lithium ion"_s);
        QCOMPARE(b.productName(), u"ACME Battery BAT0"_s);
        QCOMPARE(b.errorText(), QString());

        const QVariantMap bat = b.batteries().constFirst().toMap();
        QCOMPARE(bat.value(u"udi"_s).toString(), kDevicePrefix + u"BAT0"_s);
        QCOMPARE(bat.value(u"vendor"_s).toString(), u"ACME"_s);
        QCOMPARE(bat.value(u"product"_s).toString(), u"Battery BAT0"_s);
        QCOMPARE(bat.value(u"percent"_s).toInt(), 50);
        QCOMPARE(bat.value(u"chargeState"_s).toInt(), int(BatteryMonitor::Discharging));
        QCOMPARE(bat.value(u"capacity"_s).toInt(), 80);
        QCOMPARE(bat.value(u"energyFullDesign"_s).toDouble(), 75.0);
        QCOMPARE(bat.value(u"cycleCount"_s).toInt(), 123);
        QCOMPARE(bat.value(u"present"_s).toBool(), true);
    }

    void clampsEnergyFull()
    {
        BatteryMonitor b;
        QTRY_VERIFY(b.available());
        // A fresh battery reporting more than its design capacity.
        QVERIFY(setDevice(u"BAT0"_s, {{u"EnergyFull"_s, 80.0}, {u"Capacity"_s, 0.0}, {u"ChargeCycles"_s, -1}}));
        QTRY_COMPARE(b.energyFull(), 75.0);
        QCOMPARE(b.capacity(), 100);
        QCOMPARE(b.cycleCount(), -1);
    }

    void followsBatteryChanges()
    {
        BatteryMonitor b;
        QTRY_VERIFY(b.available());
        QSignalSpy percent(&b, &BatteryMonitor::percentChanged);
        QVERIFY(setDevice(u"BAT0"_s, {{u"Percentage"_s, 64.0}, {u"Energy"_s, 38.4}}));
        QTRY_COMPARE(b.percent(), 64);
        QTRY_COMPARE(b.energy(), 38.4);
        QCOMPARE(percent.count(), 1); // one batch, one signal

        QVERIFY(setDevice(u"BAT0"_s, {{u"State"_s, 1u}}));
        QTRY_COMPARE(b.chargeState(), BatteryMonitor::Charging);
        QVERIFY(setDevice(u"BAT0"_s, {{u"State"_s, 4u}}));
        QTRY_COMPARE(b.chargeState(), BatteryMonitor::FullyCharged);
    }

    void absentBatteryIsUnavailable()
    {
        BatteryMonitor b;
        QTRY_VERIFY(b.available());
        QVERIFY(setDevice(u"BAT0"_s, {{u"IsPresent"_s, false}}));
        QTRY_VERIFY(!b.available());
        QCOMPARE(b.batteryCount(), 1);
        QCOMPARE(b.batteries().constFirst().toMap().value(u"present"_s).toBool(), false);
        QVERIFY(setDevice(u"BAT0"_s, {{u"IsPresent"_s, true}}));
        QTRY_VERIFY(b.available());
    }

    void pluggedInFollowsOnBattery()
    {
        BatteryMonitor b;
        QTRY_VERIFY(b.pluggedIn());
        QVERIFY(setOnBattery(true));
        QTRY_VERIFY(!b.pluggedIn());
        QVERIFY(setOnBattery(false));
        QTRY_VERIFY(b.pluggedIn());
    }

    void remainingFromPowerDevil()
    {
        BatteryMonitor b;
        QTRY_VERIFY(b.available());
        QVERIFY(setRemaining(5400000));
        QTRY_COMPARE(b.remainingMsec(), qint64(5400000));
        QVERIFY(setRemaining(3600000));
        QTRY_COMPARE(b.remainingMsec(), qint64(3600000));

        // Neither charging nor discharging: no time to show.
        QVERIFY(setDevice(u"BAT0"_s, {{u"State"_s, 4u}}));
        QTRY_COMPARE(b.remainingMsec(), qint64(0));
    }

    void remainingFallsBackToUPower()
    {
        BatteryMonitor b;
        QTRY_VERIFY(b.available());
        QVERIFY(setRemaining(5400000));
        QTRY_COMPARE(b.remainingMsec(), qint64(5400000));

        // PowerDevil gone: UPower's raw TimeToEmpty.
        m_powerDevil.stop();
        QTRY_COMPARE(b.remainingMsec(), qint64(3 * 3600 * 1000));

        // PowerDevil back (restart): re-queried, not stuck on the fallback.
        QVERIFY2(m_powerDevil.start(MockBus::Session, {u"--template"_s, MockBus::mockFile(u"batterymonitor_powerdevil.py"_s), u"--parameters"_s, u"{\"remaining\": 1200000}"_s}, kPowerDevil),
                 qPrintable(m_powerDevil.error()));
        QTRY_COMPARE(b.remainingMsec(), qint64(1200000));
    }

    void chargeLimit()
    {
        BatteryMonitor b;
        QTRY_VERIFY(b.available());
        QTRY_VERIFY(b.pluggedIn());
        // Charging at 12 W from 42 of 60 Wh towards an 80 % limit (48 Wh):
        // 6 Wh / 12 W = 30 min.
        QVERIFY(setDevice(u"BAT0"_s, {{u"State"_s, 1u}, {u"Percentage"_s, 70.0}, {u"Energy"_s, 42.0}, {u"EnergyRate"_s, 12.0}}));
        QTRY_COMPARE(b.chargeState(), BatteryMonitor::Charging);
        QCOMPARE(b.timeToLimitMsec(), qint64(0)); // no limit yet
        QVERIFY(!b.limitReached());

        QSignalSpy thresholdSpy(&b, &BatteryMonitor::chargeStopThresholdChanged);
        b.setChargeStopThreshold(80);
        QCOMPARE(thresholdSpy.count(), 1);
        QCOMPARE(b.timeToLimitMsec(), qint64(30 * 60 * 1000));
        QVERIFY(!b.limitReached());

        // Held at the limit (UPower: pending-charge = 5).
        QVERIFY(setDevice(u"BAT0"_s, {{u"State"_s, 5u}, {u"Percentage"_s, 79.0}, {u"Energy"_s, 47.4}, {u"EnergyRate"_s, 0.0}}));
        QTRY_VERIFY(b.limitReached());
        QCOMPARE(b.chargeState(), BatteryMonitor::NoCharge);
        QCOMPARE(b.timeToLimitMsec(), qint64(0));

        QVERIFY(setOnBattery(true));
        QTRY_VERIFY(!b.limitReached());
        QVERIFY(setOnBattery(false));
        QTRY_VERIFY(b.limitReached());

        // No limit (ChargeLimit unavailable), and 100 % is no limit either.
        b.setChargeStopThreshold(-1);
        QVERIFY(!b.limitReached());
        b.setChargeStopThreshold(100);
        QVERIFY(!b.limitReached());
        b.setChargeStopThreshold(250); // out of range = none
        QCOMPARE(b.chargeStopThreshold(), -1);
    }

    void multipleBatteries()
    {
        BatteryMonitor b;
        QTRY_COMPARE(b.batteryCount(), 1);
        QVERIFY(mockOk(callMock(MockBus::System,
                                kUPower,
                                kUPowerPath,
                                u"AddBattery"_s,
                                {u"BAT1"_s,
                                 QVariantMap{{u"Vendor"_s, u"Other"_s},
                                             {u"Energy"_s, 20.0},
                                             {u"EnergyFull"_s, 20.0},
                                             {u"EnergyFullDesign"_s, 25.0},
                                             {u"Percentage"_s, 100.0},
                                             {u"Technology"_s, 2u}}})));
        QTRY_COMPARE(b.batteryCount(), 2);
        // Before the DisplayDevice says anything: UPower-style sums.
        QTRY_COMPARE(b.energyFull(), 80.0);
        QCOMPARE(b.energyFullDesign(), 100.0);
        QCOMPARE(b.capacity(), 80);
        QCOMPARE(b.cycleCount(), -1);
        QCOMPARE(b.percent(), 63); // 50 / 80 Wh
        QCOMPARE(b.productName(), u"ACME Battery BAT0"_s); // the first battery

        // The DisplayDevice is the aggregate the rest of Plasma shows.
        QVERIFY(mockOk(callMock(MockBus::System,
                                kUPower,
                                kUPowerPath,
                                u"SetupDisplayDevice"_s,
                                {2u, 1u, 66.0, 52.8, 80.0, 15.0, qlonglong(0), qlonglong(1800), true, u"battery"_s, 1u})));
        QTRY_COMPARE(b.percent(), 66);
        QCOMPARE(b.chargeState(), BatteryMonitor::Charging);
        QCOMPARE(b.energyRate(), 15.0);
        QCOMPARE(b.energy(), 52.8);

        QVERIFY(mockOk(callMock(MockBus::System, kUPower, kUPowerPath, u"RemoveDevice"_s, {QVariant::fromValue(QDBusObjectPath(kDevicePrefix + u"BAT1"_s))})));
        QTRY_COMPARE(b.batteryCount(), 1);
        QTRY_COMPARE(b.percent(), 50); // the single battery again, not the DisplayDevice
        QCOMPARE(b.cycleCount(), 123);
    }

    void neverRefreshesDevices()
    {
        {
            BatteryMonitor b;
            QTRY_VERIFY(b.available());
            b.refresh();
            QTRY_VERIFY(b.pluggedIn());
        }
        // Read-only: UPower's Refresh (a state-changing poll) is never sent.
        QCOMPARE(methodCalls(MockBus::System, kUPower, kDevicePrefix + u"BAT0"_s, u"Refresh"_s).size(), 0);
        QCOMPARE(methodCalls(MockBus::System, kUPower, kDevicePrefix + u"DisplayDevice"_s, u"Refresh"_s).size(), 0);
    }

    // Last: restarting UPower leaves Solid's singleton without devices.
    void upowerRestart()
    {
        BatteryMonitor b;
        QTRY_VERIFY(b.pluggedIn());
        m_upower.stop();
        QTRY_VERIFY(!b.pluggedIn()); // unknown => false
        QVERIFY2(m_upower.start(MockBus::System, {u"--template"_s, MockBus::mockFile(u"batterymonitor_upower.py"_s)}, kUPower),
                 qPrintable(m_upower.error()));
        QTRY_VERIFY(b.pluggedIn()); // re-queried on registration
        QVERIFY(setOnBattery(true));
        QTRY_VERIFY(!b.pluggedIn()); // and the new instance is followed
    }

private:
    MockBus::SystemBus m_system; // declared BEFORE the mocks: destroyed after them
    MockBus::Mock m_upower;
    MockBus::Mock m_powerDevil;
};

QTEST_GUILESS_MAIN(TestBatteryMonitor)
#include "tst_batterymonitor.moc"
