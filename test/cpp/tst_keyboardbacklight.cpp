// SPDX-License-Identifier: GPL-2.0-or-later
//
// KeyboardBacklight against mocked services - runs under dbus-run-session (a
// private session bus) plus a private system bus (MockBus::SystemBus), see
// test/mocks/README.md. Never against the live session or system bus.
//
// Mocks: test/mocks/keyboardbacklight_powerdevil.py (session) and
// test/mocks/keyboardbacklight_upower.py (system); each test starts the ones
// it needs with its own parameters.

#include <QDBusConnection>
#include <QDBusMessage>
#include <QSignalSpy>
#include <QTest>

#include "dbusasync.h"
#include "keyboardbacklight.h"
#include "mockbus.h"

using namespace Qt::StringLiterals;

namespace
{
const QString kPdService = u"org.kde.Solid.PowerManagement"_s;
const QString kPdRoot = u"/org/kde/Solid/PowerManagement"_s;
const QString kPdKbd = u"/org/kde/Solid/PowerManagement/Actions/KeyboardBrightnessControl"_s;
const QString kUpService = u"org.freedesktop.UPower"_s;
const QString kUpRoot = u"/org/freedesktop/UPower"_s;
const QString kUpKbd = u"/org/freedesktop/UPower/KbdBacklight"_s;
const QString kMockIface = u"org.freedesktop.DBus.Mock"_s;

QDBusConnection busOf(MockBus::Bus bus)
{
    return bus == MockBus::System ? QDBusConnection::systemBus() : QDBusConnection::sessionBus();
}

QDBusMessage callMock(MockBus::Bus bus, const QString &service, const QString &path, const QString &method, const QVariantList &args = {})
{
    QDBusMessage msg = QDBusMessage::createMethodCall(service, path, kMockIface, method);
    msg.setArguments(args);
    return busOf(bus).call(msg);
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

QList<QVariantList> pdCalls(const QString &method)
{
    return methodCalls(MockBus::Session, kPdService, kPdKbd, method);
}

QList<QVariantList> upCalls(const QString &method)
{
    return methodCalls(MockBus::System, kUpService, kUpKbd, method);
}
} // namespace

class TestKeyboardBacklight : public QObject
{
    Q_OBJECT

private:
    bool startPowerDevil(const QString &params = u"{}"_s)
    {
        const bool ok = m_pd.start(MockBus::Session, {u"--template"_s, MockBus::mockFile(u"keyboardbacklight_powerdevil.py"_s), u"-p"_s, params}, kPdService);
        if (!ok) {
            qWarning() << m_pd.error();
        }
        return ok;
    }

    bool startUPower(const QString &params = u"{}"_s)
    {
        const bool ok = m_upower.start(MockBus::System, {u"--template"_s, MockBus::mockFile(u"keyboardbacklight_upower.py"_s), u"-p"_s, params}, kUpService);
        if (!ok) {
            qWarning() << m_upower.error();
        }
        return ok;
    }

private Q_SLOTS:
    void initTestCase()
    {
        MC_REQUIRE_PRIVATE_SESSION_BUS();
        MC_REQUIRE_DBUSMOCK();
        // Before the first QDBusConnection::systemBus() use in this process.
        QVERIFY2(m_system.start(), qPrintable(m_system.error()));
        MC_REQUIRE_PRIVATE_SYSTEM_BUS();
    }

    void cleanup()
    {
        m_pd.stop();
        m_upower.stop();
    }

    void readsPowerDevil()
    {
        QVERIFY(startPowerDevil(u"{\"max\": 3, \"brightness\": 1}"_s));
        QVERIFY(startUPower(u"{\"max\": 3, \"brightness\": 1}"_s));
        KeyboardBacklight k;
        QTRY_VERIFY(k.available());
        QTRY_VERIFY(!k.viaUPower());
        QCOMPARE(k.maxBrightness(), 3);
        QCOMPARE(k.steps(), 3);
        QCOMPARE(k.brightness(), 1);
        QCOMPARE(k.percent(), 33);
        QVERIFY(!k.rereadOnShow());
        QVERIFY(k.errorText().isEmpty());
    }

    void followsPowerDevilSignals()
    {
        QVERIFY(startPowerDevil());
        KeyboardBacklight k;
        QTRY_VERIFY(k.available());
        QSignalSpy maxSpy(&k, &KeyboardBacklight::maxBrightnessChanged);
        QVERIFY(mockOk(callMock(MockBus::Session, kPdService, kPdRoot, u"EmitBrightness"_s, {2})));
        QTRY_COMPARE(k.brightness(), 2);
        QCOMPARE(k.percent(), 67);
        // keyboardBrightnessMaxChanged comes with every change: idempotent.
        QTest::qWait(100);
        QCOMPARE(maxSpy.count(), 0);
        QCOMPARE(k.maxBrightness(), 3);
    }

    void writesPowerDevil()
    {
        QVERIFY(startPowerDevil());
        QVERIFY(startUPower());
        KeyboardBacklight k;
        QTRY_VERIFY(k.available());
        QTRY_VERIFY(!k.viaUPower());

        k.setLevel(3); // silent by default
        QCOMPARE(k.brightness(), 3); // optimistic
        QTRY_COMPARE(pdCalls(u"setKeyboardBrightnessSilent"_s).size(), 1);
        QCOMPARE(pdCalls(u"setKeyboardBrightnessSilent"_s).constFirst().constFirst().toInt(), 3);

        k.setLevel(0, false); // with OSD
        QTRY_COMPARE(pdCalls(u"setKeyboardBrightness"_s).size(), 1);
        QCOMPARE(pdCalls(u"setKeyboardBrightness"_s).constFirst().constFirst().toInt(), 0);
        QTRY_COMPARE(k.brightness(), 0);

        k.setLevel(42); // clamped to max
        QTRY_COMPARE(pdCalls(u"setKeyboardBrightnessSilent"_s).size(), 2);
        QCOMPARE(pdCalls(u"setKeyboardBrightnessSilent"_s).at(1).constFirst().toInt(), 3);
        k.setLevel(-5);
        QTRY_COMPARE(pdCalls(u"setKeyboardBrightnessSilent"_s).size(), 3);
        QCOMPARE(pdCalls(u"setKeyboardBrightnessSilent"_s).at(2).constFirst().toInt(), 0);

        QTest::qWait(100);
        QVERIFY(upCalls(u"SetBrightness"_s).isEmpty()); // never both paths
        QVERIFY(k.errorText().isEmpty());
    }

    void supportedWithMaxZeroIsUnavailable()
    {
        // PowerDevil 6.3.0-6.7.5 + UPower >= 1.91.4 on a machine without a
        // backlight: "supported", max 0.
        QVERIFY(startPowerDevil(u"{\"max\": 0, \"brightness\": 0}"_s));
        QVERIFY(startUPower(u"{\"kbd\": false, \"version\": \"1.91.4\"}"_s));
        KeyboardBacklight k;
        QTRY_VERIFY(k.rereadOnShow()); // everything has answered by now
        QTRY_VERIFY(methodCalls(MockBus::Session, kPdService, kPdKbd, u"keyboardBrightnessMax"_s).size() >= 1);
        QTest::qWait(200);
        QVERIFY(!k.available());
        QCOMPARE(k.maxBrightness(), 0);
        QCOMPARE(k.percent(), 0);

        k.setLevel(1); // no-op
        QTest::qWait(100);
        QVERIFY(pdCalls(u"setKeyboardBrightnessSilent"_s).isEmpty());
    }

    void fallsBackToUPowerWhenUnsupported()
    {
        QVERIFY(startPowerDevil(u"{\"supported\": false}"_s));
        QVERIFY(startUPower(u"{\"max\": 3, \"brightness\": 2}"_s));
        KeyboardBacklight k;
        QTRY_VERIFY(k.available());
        QVERIFY(k.viaUPower());
        QCOMPARE(k.brightness(), 2);
        QCOMPARE(k.maxBrightness(), 3);

        k.setLevel(1, false); // UPower has no OSD variant; must still go to UPower
        QTRY_COMPARE(upCalls(u"SetBrightness"_s).size(), 1);
        QCOMPARE(upCalls(u"SetBrightness"_s).constFirst().constFirst().toInt(), 1);
        QVERIFY(pdCalls(u"setKeyboardBrightness"_s).isEmpty());
        QCOMPARE(k.brightness(), 1);

        // A signalled Fn-key change.
        QVERIFY(mockOk(callMock(MockBus::System, kUpService, kUpRoot, u"PressFnKey"_s, {3, true})));
        QTRY_COMPARE(k.brightness(), 3);

        // PowerDevil starts supporting the action (6.3.0 start-up race resolved).
        QVERIFY(mockOk(callMock(MockBus::Session, kPdService, kPdRoot, u"SetHardware"_s, {3, 3})));
        QVERIFY(mockOk(callMock(MockBus::Session, kPdService, kPdRoot, u"SetSupported"_s, {true})));
        QTRY_VERIFY(!k.viaUPower());
        QVERIFY(k.available());
        QCOMPARE(k.brightness(), 3);
    }

    void fallsBackToUPowerWithoutPowerDevil()
    {
        QVERIFY(startUPower(u"{\"max\": 2, \"brightness\": 0}"_s));
        KeyboardBacklight k;
        QTRY_VERIFY(k.available());
        QVERIFY(k.viaUPower());
        QCOMPARE(k.maxBrightness(), 2);
        QCOMPARE(k.brightness(), 0);
    }

    void survivesRestarts()
    {
        QVERIFY(startPowerDevil(u"{\"max\": 3, \"brightness\": 1}"_s));
        QVERIFY(startUPower(u"{\"max\": 3, \"brightness\": 1}"_s));
        KeyboardBacklight k;
        QTRY_VERIFY(k.available());
        QTRY_VERIFY(!k.viaUPower());

        m_pd.stop();
        QTRY_VERIFY(k.viaUPower());
        QVERIFY(k.available());

        // A hot-plugged keyboard shows up after a PowerDevil restart.
        QVERIFY(startPowerDevil(u"{\"max\": 5, \"brightness\": 4}"_s));
        QTRY_VERIFY(!k.viaUPower());
        QTRY_COMPARE(k.maxBrightness(), 5);
        QCOMPARE(k.brightness(), 4);

        m_pd.stop();
        m_upower.stop();
        QTRY_VERIFY(!k.available());
        QCOMPARE(k.brightness(), 0);
        QCOMPARE(k.maxBrightness(), 0);
        QVERIFY(!k.viaUPower());

        QVERIFY(startUPower(u"{\"max\": 3, \"brightness\": 3}"_s));
        QTRY_VERIFY(k.available());
        QVERIFY(k.viaUPower());
        QCOMPARE(k.brightness(), 3);
    }

    void upower1914RereadsOnRefresh()
    {
        QVERIFY(startUPower(u"{\"version\": \"1.91.4\", \"max\": 3, \"brightness\": 1}"_s));
        KeyboardBacklight k;
        QTRY_VERIFY(k.available());
        QTRY_VERIFY(k.rereadOnShow());
        QCOMPARE(k.brightness(), 1);

        // Fn key on UPower 1.91.4: nothing is signalled...
        QVERIFY(mockOk(callMock(MockBus::System, kUpService, kUpRoot, u"PressFnKey"_s, {3, false})));
        QTest::qWait(200);
        QCOMPARE(k.brightness(), 1);
        // ...so the tile calls refresh() when the card is shown or hovered.
        k.refresh();
        QTRY_COMPARE(k.brightness(), 3);
    }

    void upower1914OverridesStalePowerDevil()
    {
        // PowerDevil caches the level and UPower 1.91.4 does not tell it about
        // Fn-key changes either: refresh() takes the level from UPower.
        QVERIFY(startPowerDevil(u"{\"max\": 3, \"brightness\": 1}"_s));
        QVERIFY(startUPower(u"{\"version\": \"1.91.4\", \"max\": 3, \"brightness\": 1}"_s));
        KeyboardBacklight k;
        QTRY_VERIFY(k.available());
        QTRY_VERIFY(k.rereadOnShow());
        QTRY_VERIFY(!k.viaUPower());

        QVERIFY(mockOk(callMock(MockBus::System, kUpService, kUpRoot, u"PressFnKey"_s, {2, false})));
        k.refresh();
        QTRY_COMPARE(k.brightness(), 2);
        QTest::qWait(200);
        QCOMPARE(k.brightness(), 2); // not overwritten by PowerDevil's stale 1
        QVERIFY(!k.viaUPower());

        // Writes still go through PowerDevil.
        k.setLevel(3);
        QTRY_COMPARE(pdCalls(u"setKeyboardBrightnessSilent"_s).size(), 1);
        QVERIFY(upCalls(u"SetBrightness"_s).isEmpty());
    }

    void otherVersionsDoNotNeedReread()
    {
        QVERIFY(startUPower(u"{\"version\": \"1.91.5\"}"_s));
        KeyboardBacklight k;
        QTRY_VERIFY(k.available());
        QTest::qWait(100);
        QVERIFY(!k.rereadOnShow());
    }

private:
    MockBus::SystemBus m_system; // declared before the mocks: destroyed after them
    MockBus::Mock m_pd;
    MockBus::Mock m_upower;
};

QTEST_GUILESS_MAIN(TestKeyboardBacklight)
#include "tst_keyboardbacklight.moc"
