// SPDX-License-Identifier: GPL-2.0-or-later
//
// PresentationMode against mocked services - runs under dbus-run-session (a private
// session bus), see test/mocks/README.md. Never against the live session.
//
// Mocks (test/mocks/presentationmode_*.py): PowerManagement.Inhibit,
// ScreenSaver, Notifications (Inhibit + Inhibited), PowerDevil's PolicyAgent
// and KWin's Night Light. The inhibitor mocks drop a cookie when its sender
// leaves the bus, like the real daemons, and report the cookies they hold.

#include <QDBusConnection>
#include <QDBusMessage>
#include <QSignalSpy>
#include <QTest>

#include <memory>

#include "dbusasync.h"
#include "mockbus.h"
#include "nightlight.h"
#include "presentationmode.h"

using namespace Qt::StringLiterals;

namespace
{
const QString kMockIface = u"org.freedesktop.DBus.Mock"_s;

const QString kPm = u"org.freedesktop.PowerManagement.Inhibit"_s;
const QString kPmPath = u"/org/freedesktop/PowerManagement/Inhibit"_s;
const QString kSs = u"org.freedesktop.ScreenSaver"_s;
const QString kSsPath = u"/ScreenSaver"_s;
const QString kNotify = u"org.freedesktop.Notifications"_s;
const QString kNotifyPath = u"/org/freedesktop/Notifications"_s;
const QString kAgent = u"org.kde.Solid.PowerManagement.PolicyAgent"_s;
const QString kAgentPath = u"/org/kde/Solid/PowerManagement/PolicyAgent"_s;
const QString kNightLight = u"org.kde.KWin.NightLight"_s;

const QString kApp = u"Mobility Center"_s;
const QString kReason = u"Presentation mode"_s;

// PowerDevil's enforcement delay is honoured before blockedByUser is shown.
constexpr int kBlockTimeoutMs = 9000;

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

// Cookies the mock currently holds; -1 if the mock did not answer.
int activeCookies(const QString &service, const QString &path)
{
    const QDBusMessage reply = callMock(service, path, u"ActiveCookies"_s);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        return -1;
    }
    return MC::DBus::demarshall(reply.arguments().constFirst()).toList().size();
}

QStringList holders(const QString &service, const QString &path)
{
    const QDBusMessage reply = callMock(service, path, u"Holders"_s);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        return {};
    }
    return MC::DBus::demarshall(reply.arguments().constFirst()).toStringList();
}

QStringList templateArgs(const QString &file, const QString &parameters = {})
{
    QStringList args{u"--template"_s, MockBus::mockFile(file)};
    if (!parameters.isEmpty()) {
        args << u"-p"_s << parameters;
    }
    return args;
}

// PolicyAgent mock (6.5): RequestedInhibitions = [ours with @p flags].
bool setRequested(uint flags)
{
    return mockOk(callMock(kAgent, kAgentPath, u"SetRequested"_s, {QStringList{kApp, kReason}, QVariant::fromValue(QList<uint>{flags})}));
}

// PolicyAgent mock (6.4): flat (who, why) lists.
bool setBlocked(const QStringList &permanent, const QStringList &temporary)
{
    return mockOk(callMock(kAgent, kAgentPath, u"SetBlocked"_s, {permanent, temporary}));
}
} // namespace

class TestPresentationMode : public QObject
{
    Q_OBJECT

private:
    bool startPm()
    {
        const bool ok = m_pm.start(MockBus::Session, templateArgs(u"presentationmode_powermanagement.py"_s), kPm);
        if (!ok) {
            qWarning() << m_pm.error();
        }
        return ok;
    }
    bool startSs()
    {
        const bool ok = m_ss.start(MockBus::Session, templateArgs(u"presentationmode_screensaver.py"_s), kSs);
        if (!ok) {
            qWarning() << m_ss.error();
        }
        return ok;
    }
    bool startNotify(const QString &parameters = {})
    {
        const bool ok = m_notify.start(MockBus::Session, templateArgs(u"presentationmode_notifications.py"_s, parameters), kNotify);
        if (!ok) {
            qWarning() << m_notify.error();
        }
        return ok;
    }
    bool startAgent(const QString &parameters = {})
    {
        const bool ok = m_agent.start(MockBus::Session, templateArgs(u"presentationmode_policyagent.py"_s, parameters), kAgent);
        if (!ok) {
            qWarning() << m_agent.error();
        }
        return ok;
    }

    // Everything up, presentation mode on, all three cookies held.
    void activate(PresentationMode &pm)
    {
        QTRY_VERIFY(pm.available());
        QTRY_VERIFY(pm.dndAvailable());
        pm.setActive(true);
        QVERIFY(pm.active());
        QTRY_VERIFY(pm.sleepBlocked());
        QTRY_VERIFY(pm.lockBlocked());
        QTRY_VERIFY(pm.dndActive());
        QTRY_COMPARE(activeCookies(kNotify, kNotifyPath), 1);
    }

private Q_SLOTS:
    void initTestCase()
    {
        MC_REQUIRE_PRIVATE_SESSION_BUS();
        MC_REQUIRE_DBUSMOCK();
    }

    void init()
    {
        QVERIFY(startPm());
        QVERIFY(startSs());
        QVERIFY(startNotify());
        QVERIFY(startAgent());
        QVERIFY2(m_kwinNl.start(MockBus::Session, templateArgs(u"presentationmode_nightlight.py"_s), kNightLight), qPrintable(m_kwinNl.error()));
    }

    void cleanup()
    {
        m_kwinNl.stop();
        m_agent.stop();
        m_notify.stop();
        m_ss.stop();
        m_pm.stop();
    }

    void availability()
    {
        PresentationMode pm;
        QTRY_VERIFY(pm.available());
        QVERIFY(!pm.active());
        QTRY_VERIFY(pm.dndAvailable());
        QVERIFY(!pm.dndActive());
        QVERIFY(!pm.blockedByUser());

        // Either daemon is enough for the tile.
        m_pm.stop();
        QTest::qWait(200);
        QVERIFY(pm.available());
        m_ss.stop();
        QTRY_VERIFY(!pm.available());

        // Writing true while unavailable is ignored and sends nothing.
        pm.setActive(true);
        QVERIFY(!pm.active());

        QVERIFY(startSs());
        QTRY_VERIFY(pm.available());
    }

    void constructionIsReadOnly()
    {
        {
            PresentationMode pm;
            QTRY_VERIFY(pm.dndAvailable());
            QTest::qWait(200);
        }
        QVERIFY(methodCalls(kPm, kPmPath, u"Inhibit"_s).isEmpty());
        QVERIFY(methodCalls(kSs, kSsPath, u"Inhibit"_s).isEmpty());
        QVERIFY(methodCalls(kNotify, kNotifyPath, u"Inhibit"_s).isEmpty());
    }

    void activateInhibitsAll()
    {
        PresentationMode pm;
        QSignalSpy activeSpy(&pm, &PresentationMode::activeChanged);
        activate(pm);
        QCOMPARE(activeSpy.count(), 1);
        QVERIFY(pm.errorText().isEmpty());

        QCOMPARE(activeCookies(kPm, kPmPath), 1);
        QCOMPARE(activeCookies(kSs, kSsPath), 1);

        // The same stable (app, reason) for PowerManagement and ScreenSaver.
        const QVariantList pmArgs = methodCalls(kPm, kPmPath, u"Inhibit"_s).constFirst();
        const QVariantList ssArgs = methodCalls(kSs, kSsPath, u"Inhibit"_s).constFirst();
        QCOMPARE(pmArgs, (QVariantList{kApp, kReason}));
        QCOMPARE(ssArgs, pmArgs);
        // Notifications: a non-empty desktop_entry.
        const QVariantList notifyArgs = methodCalls(kNotify, kNotifyPath, u"Inhibit"_s).constFirst();
        QVERIFY(!notifyArgs.constFirst().toString().isEmpty());

        // Sent from one private connection, not the shared one.
        const QStringList pmHolders = holders(kPm, kPmPath);
        QCOMPARE(pmHolders.size(), 1);
        QVERIFY(pmHolders.constFirst() != QDBusConnection::sessionBus().baseService());
        QCOMPARE(holders(kSs, kSsPath), pmHolders);
        QCOMPARE(holders(kNotify, kNotifyPath), pmHolders);
    }

    void deactivateReleases()
    {
        PresentationMode pm;
        activate(pm);
        const uint pmCookie = methodCalls(kPm, kPmPath, u"Inhibit"_s).size(); // the mock counts from 1

        pm.setActive(false);
        QVERIFY(!pm.active());
        QVERIFY(!pm.sleepBlocked());
        QVERIFY(!pm.lockBlocked());
        QTRY_COMPARE(activeCookies(kPm, kPmPath), 0);
        QTRY_COMPARE(activeCookies(kSs, kSsPath), 0);
        QTRY_COMPARE(activeCookies(kNotify, kNotifyPath), 0);
        QTRY_VERIFY(!pm.dndActive());
        QCOMPARE(methodCalls(kPm, kPmPath, u"UnInhibit"_s).size(), 1);
        QCOMPARE(methodCalls(kPm, kPmPath, u"UnInhibit"_s).constFirst().constFirst().toUInt(), pmCookie);
        QVERIFY(!pm.dndRevokedByUser());
    }

    void destructionReleasesThroughDisconnect()
    {
        {
            PresentationMode pm;
            activate(pm);
        }
        // No UnInhibit was sent: closing the private connection is what
        // releases (also after a plasmashell crash).
        QTRY_COMPARE(activeCookies(kPm, kPmPath), 0);
        QTRY_COMPARE(activeCookies(kSs, kSsPath), 0);
        QTRY_COMPARE(activeCookies(kNotify, kNotifyPath), 0);
        QVERIFY(methodCalls(kPm, kPmPath, u"UnInhibit"_s).isEmpty());
        QVERIFY(methodCalls(kSs, kSsPath, u"UnInhibit"_s).isEmpty());
    }

    void quickToggleLeaksNothing()
    {
        PresentationMode pm;
        QTRY_VERIFY(pm.available());
        QTRY_VERIFY(pm.dndAvailable());
        // Off again before any reply arrived: late cookies are handed back.
        pm.setActive(true);
        pm.setActive(false);
        QTRY_COMPARE(methodCalls(kPm, kPmPath, u"UnInhibit"_s).size(), 1);
        QTRY_COMPARE(activeCookies(kPm, kPmPath), 0);
        QTRY_COMPARE(activeCookies(kSs, kSsPath), 0);
        QTRY_COMPARE(activeCookies(kNotify, kNotifyPath), 0);
        QVERIFY(!pm.sleepBlocked());
        QVERIFY(!pm.lockBlocked());
    }

    void powerDevilRestartReinhibitsBoth()
    {
        PresentationMode pm;
        activate(pm);

        m_pm.stop();
        m_agent.stop();
        QTRY_VERIFY(!pm.sleepBlocked());
        QVERIFY(pm.active());
        QVERIFY(pm.lockBlocked());

        QVERIFY(startPm());
        QVERIFY(startAgent());
        QTRY_VERIFY(pm.sleepBlocked());
        QTRY_COMPARE(activeCookies(kPm, kPmPath), 1);
        // The ScreenSaver cookie is handed back and taken again, so that
        // kscreenlocker forwards it to the new PowerDevil.
        QTRY_COMPARE(methodCalls(kSs, kSsPath, u"Inhibit"_s).size(), 2);
        QCOMPARE(methodCalls(kSs, kSsPath, u"UnInhibit"_s).size(), 1);
        QTRY_VERIFY(pm.lockBlocked());
        QTRY_COMPARE(activeCookies(kSs, kSsPath), 1);
        QTest::qWait(300);
        QCOMPARE(methodCalls(kSs, kSsPath, u"Inhibit"_s).size(), 2); // once per restart
        QCOMPARE(methodCalls(kPm, kPmPath, u"Inhibit"_s).size(), 1); // new mock instance
    }

    void kwinRestartReinhibitsScreenSaver()
    {
        PresentationMode pm;
        activate(pm);

        m_ss.stop();
        QTRY_VERIFY(!pm.lockBlocked());
        QVERIFY(pm.sleepBlocked());
        QVERIFY(startSs());
        QTRY_VERIFY(pm.lockBlocked());
        QCOMPARE(activeCookies(kSs, kSsPath), 1);
        QCOMPARE(methodCalls(kPm, kPmPath, u"Inhibit"_s).size(), 1);
        QVERIFY(methodCalls(kPm, kPmPath, u"UnInhibit"_s).isEmpty());
    }

    void dndRevokedByUser()
    {
        PresentationMode pm;
        activate(pm);
        QVERIFY(!pm.dndRevokedByUser());

        // The user switches Do Not Disturb off in the notifications applet.
        QVERIFY(mockOk(callMock(kNotify, kNotifyPath, u"ClearInhibitions"_s)));
        QTRY_VERIFY(!pm.dndActive());
        QVERIFY(pm.dndRevokedByUser());
        QVERIFY(pm.active());
        QVERIFY(pm.sleepBlocked());
        QTest::qWait(300);
        QCOMPARE(methodCalls(kNotify, kNotifyPath, u"Inhibit"_s).size(), 1); // not re-inhibited

        pm.setActive(false);
        QVERIFY(!pm.dndRevokedByUser());
        QCOMPARE(methodCalls(kNotify, kNotifyPath, u"UnInhibit"_s).size(), 0); // nothing left to release

        // Next time it inhibits again.
        pm.setActive(true);
        QTRY_VERIFY(pm.dndActive());
        QCOMPARE(methodCalls(kNotify, kNotifyPath, u"Inhibit"_s).size(), 2);
    }

    void dndNeedsPlasma()
    {
        QVERIFY(startNotify(u"{\"name\": \"dunst\"}"_s));
        PresentationMode pm;
        QTRY_VERIFY(pm.available());
        QTest::qWait(300);
        QVERIFY(!pm.dndAvailable());
        pm.setActive(true);
        QTRY_VERIFY(pm.sleepBlocked());
        QTRY_VERIFY(pm.lockBlocked());
        QTest::qWait(200);
        QVERIFY(methodCalls(kNotify, kNotifyPath, u"Inhibit"_s).isEmpty());
        QVERIFY(pm.errorText().isEmpty());
    }

    void inhibitErrorShown()
    {
        m_ss.stop();
        // A ScreenSaver that does not implement Inhibit.
        QVERIFY2(m_ss.start(MockBus::Session, {kSs, kSsPath, kSs}, kSs), qPrintable(m_ss.error()));
        PresentationMode pm;
        QTRY_VERIFY(pm.available());
        pm.setActive(true);
        QTRY_VERIFY(pm.sleepBlocked());
        QTRY_VERIFY(!pm.errorText().isEmpty());
        QVERIFY(!pm.lockBlocked());
        pm.setActive(false);
        QVERIFY(pm.errorText().isEmpty());
    }

    void nightLightHook()
    {
        NightLight nl;
        NightLight other;
        PresentationMode pm;
        pm.setNightLight(&nl);
        QVERIFY(pm.inhibitNightLight()); // default
        QVERIFY(!nl.presentationInhibited());

        activate(pm);
        QVERIFY(nl.presentationInhibited());
        QVERIFY(pm.nightLightInhibited());

        pm.setInhibitNightLight(false);
        QVERIFY(!nl.presentationInhibited());
        QVERIFY(!pm.nightLightInhibited());
        pm.setInhibitNightLight(true);
        QVERIFY(nl.presentationInhibited());

        // Moving the hook to another object.
        pm.setNightLight(&other);
        QVERIFY(!nl.presentationInhibited());
        QVERIFY(other.presentationInhibited());
        pm.setNightLight(nullptr);
        QVERIFY(!other.presentationInhibited());
        QVERIFY(!pm.nightLightInhibited());
        pm.setNightLight(&nl);
        QVERIFY(nl.presentationInhibited());

        pm.setActive(false);
        QVERIFY(!nl.presentationInhibited());
        QVERIFY(!pm.nightLightInhibited());
        QVERIFY(!other.presentationInhibited());
    }

    void nightLightReleasedOnDestruction()
    {
        NightLight nl;
        {
            PresentationMode pm;
            pm.setNightLight(&nl);
            activate(pm);
            QVERIFY(nl.presentationInhibited());
        }
        QVERIFY(!nl.presentationInhibited());

        // And the other way round: the NightLight object goes first.
        PresentationMode pm;
        auto shortLived = std::make_unique<NightLight>();
        pm.setNightLight(shortLived.get());
        activate(pm);
        QVERIFY(pm.nightLightInhibited());
        shortLived.reset();
        QVERIFY(!pm.nightLightInhibited());
        QCOMPARE(pm.nightLight(), nullptr);
    }

    void blockedByUser()
    {
        PresentationMode pm;
        activate(pm);
        // Allowed (flags Active|Allowed): not blocked.
        QVERIFY(setRequested(0x3));
        QTest::qWait(500);
        QVERIFY(!pm.blockedByUser());

        // Blocked in the battery applet: flags without Allowed. Not shown
        // before PowerDevil's ~5 s enforcement delay has passed.
        QVERIFY(setRequested(0x0));
        QTest::qWait(500);
        QVERIFY(!pm.blockedByUser());
        QTRY_VERIFY_WITH_TIMEOUT(pm.blockedByUser(), kBlockTimeoutMs);

        // Allowed again.
        QVERIFY(setRequested(0x3));
        QTRY_VERIFY(!pm.blockedByUser());

        // Never shown while off.
        QVERIFY(setRequested(0x0));
        pm.setActive(false);
        QTest::qWait(300);
        QVERIFY(!pm.blockedByUser());
    }

    void blockedByUserLegacy()
    {
        QVERIFY(startAgent(u"{\"api\": \"6.4\"}"_s));
        PresentationMode pm;
        activate(pm);

        const QStringList someoneElse{u"Firefox"_s, u"Playing video"_s};
        QVERIFY(setBlocked(someoneElse, {}));
        QTest::qWait(500);
        QVERIFY(!pm.blockedByUser());

        // Our pair among the temporarily blocked ones.
        QVERIFY(setBlocked(someoneElse, someoneElse + QStringList{kApp, kReason}));
        QTRY_VERIFY_WITH_TIMEOUT(pm.blockedByUser(), kBlockTimeoutMs);

        QVERIFY(setBlocked({}, {}));
        QTRY_VERIFY(!pm.blockedByUser());
    }

private:
    MockBus::Mock m_pm;
    MockBus::Mock m_ss;
    MockBus::Mock m_notify;
    MockBus::Mock m_agent;
    MockBus::Mock m_kwinNl;
};

QTEST_GUILESS_MAIN(TestPresentationMode)
#include "tst_presentationmode.moc"
