// SPDX-License-Identifier: GPL-2.0-or-later
//
// ScreenBrightness against mocked services - runs under dbus-run-session (a private
// session bus), see test/mocks/README.md. Never against the live session.

#include <QDBusConnection>
#include <QDBusMessage>
#include <QSignalSpy>
#include <QTest>

#include "dbusasync.h"
#include "mockbus.h"
#include "screenbrightness.h"

using namespace Qt::StringLiterals;

namespace
{
const QString kService = u"org.kde.ScreenBrightness"_s;
const QString kRoot = u"/org/kde/ScreenBrightness"_s;
const QString kMockIface = u"org.freedesktop.DBus.Mock"_s;

QDBusMessage callMock(const QString &service, const QString &path, const QString &method, const QVariantList &args = {})
{
    QDBusMessage msg = QDBusMessage::createMethodCall(service, path, kMockIface, method);
    msg.setArguments(args);
    return QDBusConnection::sessionBus().call(msg);
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
QList<QVariantList> methodCalls(const QString &service, const QString &path, const QString &method)
{
    QList<QVariantList> out;
    const QDBusMessage reply = callMock(service, path, u"GetMethodCalls"_s, {method});
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

void emitBrightnessChanged(const QString &name, int value, const QString &source, const QString &context)
{
    QVERIFY(mockOk(callMock(kService,
                            kRoot,
                            u"EmitSignal"_s,
                            {u"org.kde.ScreenBrightness"_s, u"BrightnessChanged"_s, u"siss"_s, QVariantList{name, value, source, context}})));
}
} // namespace

class TestScreenBrightness : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        MC_REQUIRE_PRIVATE_SESSION_BUS();
        MC_REQUIRE_DBUSMOCK();
    }

    void init()
    {
        QVERIFY2(m_brightness.start(MockBus::Session, {u"--template"_s, MockBus::mockFile(u"screenbrightness_service.py"_s)}, kService),
                 qPrintable(m_brightness.error()));
        QVERIFY2(m_pm.start(MockBus::Session, {u"--template"_s, MockBus::mockFile(u"screenbrightness_powermanagement.py"_s)}, u"org.kde.Solid.PowerManagement"_s),
                 qPrintable(m_pm.error()));
        QVERIFY2(m_accel.start(MockBus::Session, {u"--template"_s, MockBus::mockFile(u"screenbrightness_kglobalaccel.py"_s)}, u"org.kde.kglobalaccel"_s),
                 qPrintable(m_accel.error()));
        // An external monitor first, so "primary" has to pick the internal one.
        QVERIFY(mockOk(callMock(kService, kRoot, u"AddDisplay"_s, {u"display0"_s, u"DELL U2720Q"_s, false, 100, 50})));
        QVERIFY(mockOk(callMock(kService, kRoot, u"AddDisplay"_s, {u"display1"_s, u"Built-in Screen"_s, true, 10000, 2500})));
    }

    void cleanup()
    {
        m_accel.stop();
        m_pm.stop();
        m_brightness.stop();
    }

    void readsDisplays()
    {
        ScreenBrightness b;
        QTRY_VERIFY(b.available());
        QTRY_COMPARE(b.count(), 2);
        QCOMPARE(b.primaryName(), u"display1"_s);
        QCOMPARE(b.primaryPercent(), 25);

        const QVariantMap external = b.get(b.primaryRow() == 0 ? 1 : 0);
        QCOMPARE(external.value(u"label"_s).toString(), u"DELL U2720Q"_s);
        QCOMPARE(external.value(u"maxBrightness"_s).toInt(), 100);
        QCOMPARE(external.value(u"percent"_s).toInt(), 50);
        QCOMPARE(external.value(u"isInternal"_s).toBool(), false);
    }

    void followsOtherClients()
    {
        ScreenBrightness b;
        QTRY_COMPARE(b.count(), 2);
        emitBrightnessChanged(u"display1"_s, 7500, u":1.999"_s, u"powerdevil"_s);
        QTRY_COMPARE(b.primaryPercent(), 75);
    }

    void writesAndFiltersOwnEcho()
    {
        ScreenBrightness b;
        QTRY_COMPARE(b.count(), 2);
        const QString self = QDBusConnection::sessionBus().baseService();
        QVERIFY(mockOk(callMock(kService, kRoot, u"SetEchoSender"_s, {self})));

        b.setPercent(u"display1"_s, 40);
        QCOMPARE(b.primaryPercent(), 40); // optimistic
        const QString path = kRoot + u"/display1"_s;
        QTRY_COMPARE(methodCalls(kService, path, u"SetBrightnessWithContext"_s).size(), 1);
        const QVariantList args = methodCalls(kService, path, u"SetBrightnessWithContext"_s).constFirst();
        QCOMPARE(args.at(0).toInt(), 4000);
        QCOMPARE(args.at(1).toUInt(), 1u); // SuppressIndicator: no OSD by default
        const QString context = args.at(2).toString();
        QVERIFY(context.startsWith(u"MobilityCenter-"_s));

        // A late echo of an older value of ours must not move the slider back...
        emitBrightnessChanged(u"display1"_s, 1000, self, context);
        QTest::qWait(200);
        QCOMPARE(b.primaryPercent(), 40);
        // ...while the same client with another context (a second widget
        // instance in the same plasmashell) is honoured.
        emitBrightnessChanged(u"display1"_s, 1000, self, u"MobilityCenter-other"_s);
        QTRY_COMPARE(b.primaryPercent(), 10);
    }

    void showOsdClearsFlag()
    {
        ScreenBrightness b;
        b.setShowOsd(true);
        QTRY_COMPARE(b.count(), 2);
        b.setPercent(u"display0"_s, 80);
        const QString path = kRoot + u"/display0"_s;
        QTRY_COMPARE(methodCalls(kService, path, u"SetBrightnessWithContext"_s).size(), 1);
        const QVariantList args = methodCalls(kService, path, u"SetBrightnessWithContext"_s).constFirst();
        QCOMPARE(args.at(0).toInt(), 80);
        QCOMPARE(args.at(1).toUInt(), 0u);
    }

    void throttlesDragging()
    {
        ScreenBrightness b;
        QTRY_COMPARE(b.count(), 2);
        for (int pct = 1; pct <= 30; ++pct) {
            b.setPercent(u"display1"_s, pct);
        }
        const QString path = kRoot + u"/display1"_s;
        // Leading write right away, the last value once the window closes.
        QTRY_COMPARE(methodCalls(kService, path, u"SetBrightnessWithContext"_s).size(), 2);
        QTest::qWait(200);
        const QList<QVariantList> calls = methodCalls(kService, path, u"SetBrightnessWithContext"_s);
        QCOMPARE(calls.size(), 2);
        QCOMPARE(calls.at(0).at(0).toInt(), 100);
        QCOMPARE(calls.at(1).at(0).toInt(), 3000);
    }

    void emptySourceRequeries()
    {
        ScreenBrightness b;
        QTRY_COMPARE(b.count(), 2);
        const QString path = kRoot + u"/display0"_s;
        // Change the mock behind the backend's back (no signal)...
        QDBusMessage set = QDBusMessage::createMethodCall(kService, path, u"org.freedesktop.DBus.Properties"_s, u"Set"_s);
        set.setArguments({u"org.kde.ScreenBrightness.Display"_s, u"Label"_s, QVariant::fromValue(QDBusVariant(u"LG 27GP850"_s))});
        QVERIFY(mockOk(QDBusConnection::sessionBus().call(set)));
        // ...then announce a change with no source, as PowerDevil does for a
        // replaced display: the backend must re-read it.
        emitBrightnessChanged(u"display0"_s, 50, QString(), QString());
        QTRY_COMPARE(b.get(b.primaryRow() == 0 ? 1 : 0).value(u"label"_s).toString(), u"LG 27GP850"_s);
    }

    void displaysComeAndGo()
    {
        ScreenBrightness b;
        QTRY_COMPARE(b.count(), 2);
        QVERIFY(mockOk(callMock(kService, kRoot, u"RemoveDisplay"_s, {u"display1"_s})));
        QTRY_COMPARE(b.count(), 1);
        QCOMPARE(b.primaryName(), u"display0"_s); // no internal panel left: row 0
        QVERIFY(mockOk(callMock(kService, kRoot, u"AddDisplay"_s, {u"display2"_s, u"Built-in Screen"_s, true, 10000, 10000})));
        QTRY_COMPARE(b.count(), 2);
        QCOMPARE(b.primaryName(), u"display2"_s);
        QCOMPARE(b.primaryPercent(), 100);
    }

    void survivesPowerDevilRestart()
    {
        ScreenBrightness b;
        QTRY_COMPARE(b.count(), 2);
        m_brightness.stop();
        QTRY_VERIFY(!b.available());
        QCOMPARE(b.count(), 0);
        QCOMPARE(b.primaryRow(), -1);
        QCOMPARE(b.primaryPercent(), -1);

        QVERIFY2(m_brightness.start(MockBus::Session, {u"--template"_s, MockBus::mockFile(u"screenbrightness_service.py"_s)}, kService),
                 qPrintable(m_brightness.error()));
        QTRY_VERIFY(b.available());
        QVERIFY(mockOk(callMock(kService, kRoot, u"AddDisplay"_s, {u"display0"_s, u"Built-in Screen"_s, true, 10000, 5000})));
        QTRY_COMPARE(b.count(), 1);
        QCOMPARE(b.primaryPercent(), 50);
    }

    void turnScreenOff()
    {
        ScreenBrightness b;
        QTRY_VERIFY(b.screenOffAvailable());
        b.turnScreenOff();
        const QString component = u"/component/org_kde_powerdevil"_s;
        QTest::qWait(150);
        QVERIFY(methodCalls(u"org.kde.kglobalaccel"_s, component, u"invokeShortcut"_s).isEmpty()); // delayed past the click
        QTRY_COMPARE(methodCalls(u"org.kde.kglobalaccel"_s, component, u"invokeShortcut"_s).size(), 1);
        QCOMPARE(methodCalls(u"org.kde.kglobalaccel"_s, component, u"invokeShortcut"_s).constFirst().constFirst().toString(), u"Turn Off Screen"_s);
    }

    void screenOffFollowsPowerDevil()
    {
        ScreenBrightness b;
        QTRY_VERIFY(b.screenOffAvailable());
        QVERIFY(mockOk(callMock(u"org.kde.Solid.PowerManagement"_s, u"/org/kde/Solid/PowerManagement"_s, u"SetDpmsSupported"_s, {false})));
        QTRY_VERIFY(!b.screenOffAvailable());
        m_pm.stop();
        QVERIFY2(m_pm.start(MockBus::Session, {u"--template"_s, MockBus::mockFile(u"screenbrightness_powermanagement.py"_s)}, u"org.kde.Solid.PowerManagement"_s),
                 qPrintable(m_pm.error()));
        QTRY_VERIFY(b.screenOffAvailable());
        m_pm.stop();
        QTRY_VERIFY(!b.screenOffAvailable());
        b.turnScreenOff(); // no-op now
        QTest::qWait(600);
        QVERIFY(methodCalls(u"org.kde.kglobalaccel"_s, u"/component/org_kde_powerdevil"_s, u"invokeShortcut"_s).isEmpty());
    }

private:
    MockBus::Mock m_brightness;
    MockBus::Mock m_pm;
    MockBus::Mock m_accel;
};

QTEST_GUILESS_MAIN(TestScreenBrightness)
#include "tst_screenbrightness.moc"
