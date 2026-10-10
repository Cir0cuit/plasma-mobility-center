// SPDX-License-Identifier: GPL-2.0-or-later
//
// PowerProfiles against mocked services - runs under dbus-run-session (a private
// session bus) plus a private system bus (MockBus::SystemBus), see
// test/mocks/README.md. Never against the live session or system bus.

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>

#include "dbusasync.h"
#include "mockbus.h"
#include "powerprofiles.h"

using namespace Qt::StringLiterals;

namespace
{
const QString kService = u"org.freedesktop.UPower.PowerProfiles"_s;
const QString kPath = u"/org/freedesktop/UPower/PowerProfiles"_s;
const QString kLegacyService = u"net.hadess.PowerProfiles"_s;
const QString kLegacyPath = u"/net/hadess/PowerProfiles"_s;
const QString kUPower = u"org.freedesktop.UPower"_s;
const QString kPm = u"org.kde.Solid.PowerManagement"_s;
const QString kPmPath = u"/org/kde/Solid/PowerManagement/Actions/PowerProfile"_s;
const QString kMockIface = u"org.freedesktop.DBus.Mock"_s;

QDBusMessage callOn(const QDBusConnection &bus, const QString &service, const QString &path, const QString &iface, const QString &method, const QVariantList &args)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(service, path, iface, method);
    msg.setArguments(args);
    return bus.call(msg);
}

QDBusMessage callMock(const QString &service, const QString &path, const QString &method, const QVariantList &args = {})
{
    return callOn(QDBusConnection::systemBus(), service, path, kMockIface, method, args);
}

bool mockOk(const QDBusMessage &reply)
{
    if (reply.type() == QDBusMessage::ErrorMessage) {
        qWarning() << reply.errorName() << reply.errorMessage();
        return false;
    }
    return true;
}

QStringList templateArgs(const QString &file, const QJsonObject &parameters = {})
{
    QStringList args{u"--template"_s, MockBus::mockFile(file)};
    if (!parameters.isEmpty()) {
        args << u"--parameters"_s << QString::fromUtf8(QJsonDocument(parameters).toJson(QJsonDocument::Compact));
    }
    return args;
}

// Updates properties of the daemon mock and emits PropertiesChanged.
bool updateDaemon(const QVariantMap &props, const QString &service = kService, const QString &path = kPath)
{
    return mockOk(callMock(service, path, u"UpdateProperties"_s, {service, props}));
}

// Properties.Set calls the daemon mock received, as (name, value) pairs.
QList<QPair<QString, QVariant>> writes(const QString &service = kService, const QString &path = kPath)
{
    QList<QPair<QString, QVariant>> out;
    const QDBusMessage reply = callMock(service, path, u"GetWrites"_s);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        return out;
    }
    const QVariantList rows = MC::DBus::demarshall(reply.arguments().constFirst()).toList();
    for (const QVariant &row : rows) {
        const QVariantList fields = row.toList();
        if (fields.size() == 2) {
            out.append({fields.at(0).toString(), fields.at(1)});
        }
    }
    return out;
}

QString daemonActiveProfile(const QString &service = kService, const QString &path = kPath)
{
    const QDBusMessage reply =
        callOn(QDBusConnection::systemBus(), service, path, u"org.freedesktop.DBus.Properties"_s, u"Get"_s, {service, u"ActiveProfile"_s});
    return reply.arguments().isEmpty() ? QString() : MC::DBus::demarshall(reply.arguments().constFirst()).toString();
}

QVariantList holdProfile(const QString &profile, const QString &reason, const QString &app)
{
    return {profile, reason, app};
}
} // namespace

class TestPowerProfiles : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        MC_REQUIRE_PRIVATE_SESSION_BUS();
        MC_REQUIRE_DBUSMOCK();
        QVERIFY2(m_system.start(), qPrintable(m_system.error())); // before ANY systemBus() use
        MC_REQUIRE_PRIVATE_SYSTEM_BUS();
    }

    void cleanup()
    {
        m_pm.stop();
        m_upower.stop();
        m_legacy.stop();
        m_daemon.stop();
    }

    void noDaemon()
    {
        PowerProfiles p;
        QTest::qWait(300);
        QVERIFY(!p.available());
        QCOMPARE(p.backendService(), QString());
        QVERIFY(p.profiles().isEmpty());
        QCOMPARE(p.activeProfile(), QString());
        p.setActiveProfile(u"performance"_s); // no-op, no crash
        p.refresh();
    }

    void readsDaemon()
    {
        startDaemon({{u"ActiveProfile"_s, u"performance"_s}, {u"BatteryAware"_s, true}});
        PowerProfiles p;
        QTRY_VERIFY(p.available());
        QCOMPARE(p.backendService(), kService);
        QCOMPARE(p.profiles(), QStringList({u"power-saver"_s, u"balanced"_s, u"performance"_s}));
        QCOMPARE(p.activeProfile(), u"performance"_s);
        QCOMPARE(p.performanceDegraded(), QString());
        QVERIFY(p.degradedReasons().isEmpty());
        QVERIFY(p.holds().isEmpty());
        QVERIFY(p.hasBatteryAware());
        QVERIFY(p.batteryAware());
        QCOMPARE(p.errorText(), QString());
    }

    void noBatteryAwareOnOlderDaemons()
    {
        startDaemon(); // like tuned-ppd / ppd < 0.30: no BatteryAware property
        PowerProfiles p;
        QTRY_VERIFY(p.available());
        QVERIFY(!p.hasBatteryAware());
        QVERIFY(!p.batteryAware());
    }

    void placeholderDriverIsUnavailable()
    {
        startDaemon({{u"Profiles"_s, QJsonArray({QJsonArray({u"power-saver"_s, u"placeholder"_s}), QJsonArray({u"balanced"_s, u"placeholder"_s})})}});
        PowerProfiles p;
        QTRY_COMPARE(p.backendService(), kService);
        QTest::qWait(300);
        QVERIFY(!p.available());

        // A real driver showing up later (PropertiesChanged) makes it available.
        QVERIFY(mockOk(callMock(kService, kPath, u"SetProfiles"_s, {QStringList{u"power-saver:intel_pstate"_s, u"balanced:intel_pstate"_s}, true})));
        QTRY_VERIFY(p.available());
        QCOMPARE(p.profiles(), QStringList({u"power-saver"_s, u"balanced"_s}));
    }

    void emptyProfilesIsUnavailable()
    {
        startDaemon({{u"Profiles"_s, QJsonArray()}});
        PowerProfiles p;
        QTRY_COMPARE(p.backendService(), kService);
        QTest::qWait(300);
        QVERIFY(!p.available());
    }

    void degradedReasons()
    {
        startDaemon();
        PowerProfiles p;
        QTRY_VERIFY(p.available());
        QSignalSpy spy(&p, &PowerProfiles::performanceDegradedChanged);
        QVERIFY(updateDaemon({{u"PerformanceDegraded"_s, u"lap-detected,high-operating-temperature"_s}}));
        QTRY_COMPARE(p.degradedReasons(), QStringList({u"lap-detected"_s, u"high-operating-temperature"_s}));
        QCOMPARE(p.performanceDegraded(), u"lap-detected,high-operating-temperature"_s);
        QCOMPARE(spy.count(), 1);

        QVERIFY(updateDaemon({{u"PerformanceDegraded"_s, QString()}}));
        QTRY_VERIFY(p.degradedReasons().isEmpty());
    }

    void degradedAtStartup()
    {
        startDaemon({{u"PerformanceDegraded"_s, u"high-operating-temperature"_s}});
        PowerProfiles p;
        QTRY_VERIFY(p.available());
        QCOMPARE(p.degradedReasons(), QStringList({u"high-operating-temperature"_s}));
    }

    void holds()
    {
        startDaemon();
        PowerProfiles p;
        QTRY_VERIFY(p.available());
        QSignalSpy spy(&p, &PowerProfiles::holdsChanged);

        const QDBusMessage reply = callOn(QDBusConnection::systemBus(), kService, kPath, kService, u"HoldProfile"_s,
                                          holdProfile(u"performance"_s, u"Compiling"_s, u"org.kde.kdevelop"_s));
        QVERIFY(mockOk(reply));
        const uint cookie = reply.arguments().constFirst().toUInt();

        QTRY_COMPARE(p.holds().size(), 1);
        const QVariantMap hold = p.holds().constFirst().toMap();
        QCOMPARE(hold.value(u"Profile"_s).toString(), u"performance"_s);
        QCOMPARE(hold.value(u"Reason"_s).toString(), u"Compiling"_s);
        QCOMPARE(hold.value(u"ApplicationId"_s).toString(), u"org.kde.kdevelop"_s);
        QTRY_COMPARE(p.activeProfile(), u"performance"_s);
        QVERIFY(spy.count() >= 1);

        QVERIFY(mockOk(callOn(QDBusConnection::systemBus(), kService, kPath, kService, u"ReleaseProfile"_s, {cookie})));
        QTRY_VERIFY(p.holds().isEmpty());
    }

    void setsProfile()
    {
        startDaemon();
        PowerProfiles p;
        QTRY_VERIFY(p.available());
        QCOMPARE(p.activeProfile(), u"balanced"_s);

        p.setActiveProfile(u"power-saver"_s);
        QCOMPARE(p.activeProfile(), u"balanced"_s); // never optimistic: the daemon decides
        QTRY_COMPARE(p.activeProfile(), u"power-saver"_s);
        QCOMPARE(daemonActiveProfile(), u"power-saver"_s);
        const auto w = writes();
        QCOMPARE(w.size(), 1);
        QCOMPARE(w.constFirst().first, u"ActiveProfile"_s);
        QCOMPARE(w.constFirst().second.toString(), u"power-saver"_s);
        QCOMPARE(p.errorText(), QString());
    }

    void ignoresUnknownAndUnchangedProfiles()
    {
        startDaemon({{u"Profiles"_s, QJsonArray({QJsonArray({u"power-saver"_s, u"x"_s}), QJsonArray({u"balanced"_s, u"x"_s})})}});
        PowerProfiles p;
        QTRY_VERIFY(p.available());
        p.setActiveProfile(u"performance"_s); // not offered
        p.setActiveProfile(u"turbo"_s);
        p.setActiveProfile(u"balanced"_s); // already active, no holds
        QTest::qWait(300);
        QVERIFY(writes().isEmpty());
    }

    void manualSetWhileHeldCancelsHolds()
    {
        startDaemon();
        PowerProfiles p;
        QTRY_VERIFY(p.available());
        QVERIFY(mockOk(callOn(QDBusConnection::systemBus(), kService, kPath, kService, u"HoldProfile"_s,
                              holdProfile(u"power-saver"_s, u"Low battery"_s, u"org.gnome.Shell"_s))));
        QTRY_COMPARE(p.holds().size(), 1);
        QTRY_COMPARE(p.activeProfile(), u"power-saver"_s);

        // Clicking the already active chip still goes out: it releases the hold.
        p.setActiveProfile(u"power-saver"_s);
        QTRY_COMPARE(writes().size(), 1);
        QTRY_VERIFY(p.holds().isEmpty());
    }

    void refusedSetShowsError()
    {
        startDaemon();
        PowerProfiles p;
        QTRY_VERIFY(p.available());
        QVERIFY(mockOk(callMock(kService, kPath, u"DenyWrites"_s, {u"Not authorized"_s})));
        p.setActiveProfile(u"performance"_s);
        QTRY_VERIFY(p.errorText().contains(u"Not authorized"_s));
        QCOMPARE(p.activeProfile(), u"balanced"_s);

        QVERIFY(mockOk(callMock(kService, kPath, u"DenyWrites"_s, {QString()})));
        p.setActiveProfile(u"performance"_s);
        QTRY_COMPARE(p.activeProfile(), u"performance"_s);
        QCOMPARE(p.errorText(), QString());
    }

    void followsExternalChanges()
    {
        startDaemon();
        PowerProfiles p;
        QTRY_VERIFY(p.available());
        QSignalSpy spy(&p, &PowerProfiles::activeProfileChanged);
        QVERIFY(updateDaemon({{u"ActiveProfile"_s, u"performance"_s}}));
        QTRY_COMPARE(p.activeProfile(), u"performance"_s);
        QCOMPARE(spy.count(), 1);
    }

    void fallsBackToLegacyName()
    {
        QVERIFY2(m_legacy.start(MockBus::System, templateArgs(u"powerprofiles_legacy.py"_s, {{u"ActiveProfile"_s, u"power-saver"_s}}), kLegacyService),
                 qPrintable(m_legacy.error()));
        PowerProfiles p;
        QTRY_VERIFY(p.available());
        QCOMPARE(p.backendService(), kLegacyService);
        QCOMPARE(p.activeProfile(), u"power-saver"_s);

        // Writes and change signals use the legacy path and interface.
        p.setActiveProfile(u"performance"_s);
        QTRY_COMPARE(p.activeProfile(), u"performance"_s);
        QCOMPARE(daemonActiveProfile(kLegacyService, kLegacyPath), u"performance"_s);
        QVERIFY(updateDaemon({{u"PerformanceDegraded"_s, u"lap-detected"_s}}, kLegacyService, kLegacyPath));
        QTRY_COMPARE(p.degradedReasons(), QStringList({u"lap-detected"_s}));
    }

    void prefersNewNameAndSwitches()
    {
        QVERIFY2(m_legacy.start(MockBus::System, templateArgs(u"powerprofiles_legacy.py"_s, {{u"ActiveProfile"_s, u"power-saver"_s}}), kLegacyService),
                 qPrintable(m_legacy.error()));
        PowerProfiles p;
        QTRY_COMPARE(p.backendService(), kLegacyService);
        QTRY_VERIFY(p.available());

        startDaemon({{u"ActiveProfile"_s, u"performance"_s}});
        QTRY_COMPARE(p.backendService(), kService);
        QTRY_VERIFY(p.available());
        QCOMPARE(p.activeProfile(), u"performance"_s);

        m_daemon.stop();
        QTRY_COMPARE(p.backendService(), kLegacyService);
        QTRY_VERIFY(p.available());
        QCOMPARE(p.activeProfile(), u"power-saver"_s);
    }

    void survivesDaemonRestart()
    {
        startDaemon();
        PowerProfiles p;
        QTRY_VERIFY(p.available());
        m_daemon.stop();
        QTRY_VERIFY(!p.available());
        QCOMPARE(p.backendService(), QString());
        QVERIFY(p.profiles().isEmpty());
        QCOMPARE(p.activeProfile(), QString());

        startDaemon({{u"ActiveProfile"_s, u"power-saver"_s}, {u"PerformanceDegraded"_s, u"lap-detected"_s}});
        QTRY_VERIFY(p.available());
        QCOMPARE(p.activeProfile(), u"power-saver"_s);
        QCOMPARE(p.degradedReasons(), QStringList({u"lap-detected"_s}));
    }

    void rereadsOnPowerSourceChange()
    {
        startDaemon();
        QVERIFY2(m_upower.start(MockBus::System, templateArgs(u"powerprofiles_upower.py"_s), kUPower), qPrintable(m_upower.error()));
        PowerProfiles p;
        QTRY_VERIFY(p.available());
        QCOMPARE(p.profiles().size(), 3);
        // tuned-ppd: the profile list changes with AC, without a signal...
        QVERIFY(mockOk(callMock(kService, kPath, u"SetProfiles"_s, {QStringList{u"power-saver:tuned"_s, u"balanced:tuned"_s}, false})));
        QTest::qWait(200);
        QCOMPARE(p.profiles().size(), 3);
        // ...so UPower's OnBattery change triggers a re-read.
        QVERIFY(mockOk(callMock(kUPower, u"/org/freedesktop/UPower"_s, u"SetOnBattery"_s, {true})));
        QTRY_COMPARE(p.profiles(), QStringList({u"power-saver"_s, u"balanced"_s}));
    }

    void configuredProfileHint()
    {
        startDaemon();
        QVERIFY2(m_pm.start(MockBus::Session, templateArgs(u"powerprofiles_powerdevil.py"_s, {{u"configuredProfile"_s, u"balanced"_s}}), kPm),
                 qPrintable(m_pm.error()));
        PowerProfiles p;
        QTRY_COMPARE(p.configuredProfile(), u"balanced"_s);

        QSignalSpy spy(&p, &PowerProfiles::configuredProfileChanged);
        QVERIFY(mockOk(callOn(QDBusConnection::sessionBus(), kPm, kPmPath, kMockIface, u"SetConfiguredProfile"_s, {u"power-saver"_s})));
        QTRY_COMPARE(p.configuredProfile(), u"power-saver"_s);
        QCOMPARE(spy.count(), 1);

        // PowerDevil configured "leave it alone".
        QVERIFY(mockOk(callOn(QDBusConnection::sessionBus(), kPm, kPmPath, kMockIface, u"SetConfiguredProfile"_s, {QString()})));
        QTRY_COMPARE(p.configuredProfile(), QString());

        QVERIFY(mockOk(callOn(QDBusConnection::sessionBus(), kPm, kPmPath, kMockIface, u"SetConfiguredProfile"_s, {u"performance"_s})));
        QTRY_COMPARE(p.configuredProfile(), u"performance"_s);
        m_pm.stop();
        QTRY_COMPARE(p.configuredProfile(), QString());
    }

    void noConfiguredProfileWithoutAction()
    {
        // PowerDevil without the PowerProfile action object (or an old one).
        QVERIFY2(m_pm.start(MockBus::Session, {kPm, u"/org/kde/Solid/PowerManagement"_s, kPm}, kPm), qPrintable(m_pm.error()));
        PowerProfiles p;
        QTest::qWait(300);
        QCOMPARE(p.configuredProfile(), QString());
    }

private:
    void startDaemon(const QJsonObject &parameters = {})
    {
        QVERIFY2(m_daemon.start(MockBus::System, templateArgs(u"powerprofiles_daemon.py"_s, parameters), kService), qPrintable(m_daemon.error()));
    }

    MockBus::SystemBus m_system; // declared before the mocks: destroyed after them
    MockBus::Mock m_daemon;
    MockBus::Mock m_legacy;
    MockBus::Mock m_upower;
    MockBus::Mock m_pm;
};

QTEST_GUILESS_MAIN(TestPowerProfiles)
#include "tst_powerprofiles.moc"
