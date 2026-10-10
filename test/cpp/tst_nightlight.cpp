// SPDX-License-Identifier: GPL-2.0-or-later
//
// NightLight against mocked services - runs under dbus-run-session (a private
// session bus), see test/mocks/README.md. Never against the live session.
//
// KWin is test/mocks/nightlight_kwin.py. kwinrc writes go to a QTemporaryDir
// set as XDG_CONFIG_HOME before the first KConfig use (and checked not to be
// the real ~/.config).

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <KConfigGroup>
#include <KSharedConfig>

#include "dbusasync.h"
#include "mockbus.h"
#include "nightlight.h"

using namespace Qt::StringLiterals;

namespace
{
const QString kService = u"org.kde.KWin.NightLight"_s;
const QString kPath = u"/org/kde/KWin/NightLight"_s;
const QString kIface = u"org.kde.KWin.NightLight"_s;
const QString kMockIface = u"org.freedesktop.DBus.Mock"_s;

QDBusMessage callMock(const QString &method, const QVariantList &args = {})
{
    QDBusMessage msg = QDBusMessage::createMethodCall(kService, kPath, kMockIface, method);
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

struct Inhibitor {
    uint cookie;
    QString sender;
};

QList<Inhibitor> inhibitors()
{
    QList<Inhibitor> out;
    const QDBusMessage reply = callMock(u"GetInhibitors"_s);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        return out;
    }
    const QVariantList list = MC::DBus::demarshall(reply.arguments().constFirst()).toList();
    for (const QVariant &entry : list) {
        const QVariantList fields = entry.toList();
        if (fields.size() == 2) {
            out.append({fields.at(0).toUInt(), fields.at(1).toString()});
        }
    }
    return out;
}

// The argument lists of every call of @p method, oldest first.
QList<QVariantList> methodCalls(const QString &method)
{
    QList<QVariantList> out;
    const QDBusMessage reply = callMock(u"GetMethodCalls"_s, {method});
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

void updateProperties(const QVariantMap &props)
{
    QVERIFY(mockOk(callMock(u"UpdateProperties"_s, {kIface, props})));
}

QString kwinrcPath()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)).filePath(u"kwinrc"_s);
}

bool kwinrcActive()
{
    KSharedConfig::Ptr config = KSharedConfig::openConfig(u"kwinrc"_s, KConfig::NoGlobals);
    config->reparseConfiguration();
    return KConfigGroup(config, u"NightColor"_s).readEntry("Active", false);
}

// Receives org.kde.kconfig.notify ConfigChanged for kwinrc.
class NotifySpy : public QObject
{
    Q_OBJECT
public:
    NotifySpy()
    {
        connected = QDBusConnection::sessionBus().connect(QString(), u"/kwinrc"_s, u"org.kde.kconfig.notify"_s, u"ConfigChanged"_s, this, SLOT(onChanged(QDBusMessage)));
    }
    bool connected = false;
    int count = 0;
public Q_SLOTS:
    void onChanged(const QDBusMessage &)
    {
        ++count;
    }
};
} // namespace

class TestNightLight : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        MC_REQUIRE_PRIVATE_SESSION_BUS();
        MC_REQUIRE_DBUSMOCK();

        // kwinrc must never be the user's: point XDG_CONFIG_HOME at a fresh
        // directory before anything opens a KConfig, and check it took.
        QVERIFY(m_configHome.isValid());
        qputenv("XDG_CONFIG_HOME", m_configHome.path().toLocal8Bit());
        const QString configDir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
        QCOMPARE(QDir(configDir).canonicalPath(), QDir(m_configHome.path()).canonicalPath());
        QVERIFY(QDir(configDir).canonicalPath() != QDir(QDir::homePath() + u"/.config"_s).canonicalPath());
    }

    void init()
    {
        QFile::remove(kwinrcPath());
        startKWin();
    }

    void cleanup()
    {
        m_kwin.stop();
        QFile::remove(kwinrcPath());
    }

    void readsState()
    {
        NightLight nl;
        QTRY_VERIFY(nl.available());
        QVERIFY(nl.enabled());
        QVERIFY(nl.running());
        QVERIFY(!nl.inhibited());
        QVERIFY(nl.checked());
        QCOMPARE(nl.currentTemperature(), 4500);
        QCOMPARE(nl.targetTemperature(), 4500);
        QVERIFY(!nl.neutral());
        QCOMPARE(nl.mode(), 0);
        QVERIFY(!nl.userInhibited());
        QVERIFY(nl.canEnableDirectly());
        QVERIFY(nl.errorText().isEmpty());
        // Reading never inhibits anything.
        QTest::qWait(100);
        QVERIFY(methodCalls(u"inhibit"_s).isEmpty());
    }

    void followsKWin()
    {
        NightLight nl;
        QTRY_VERIFY(nl.checked());
        QSignalSpy temp(&nl, &NightLight::temperatureChanged);

        updateProperties({{u"currentTemperature"_s, QVariant::fromValue(6500u)}, {u"mode"_s, QVariant::fromValue(1u)}});
        QTRY_VERIFY(nl.neutral()); // "active (no tint during daytime)"
        QCOMPARE(nl.mode(), 1);
        QVERIFY(!temp.isEmpty());

        QVERIFY(mockOk(callMock(u"SetEnabled"_s, {false})));
        QTRY_VERIFY(!nl.enabled());
        QVERIFY(!nl.running());
        QVERIFY(!nl.checked());
        QVERIFY(!nl.neutral());

        updateProperties({{u"available"_s, false}});
        QTRY_VERIFY(!nl.available());
        updateProperties({{u"available"_s, true}});
        QTRY_VERIFY(nl.available());
    }

    void userToggleInhibitsOnPrivateBus()
    {
        NightLight nl;
        QTRY_VERIFY(nl.checked());

        nl.setChecked(false);
        QVERIFY(nl.userInhibited());
        QTRY_COMPARE(inhibitors().size(), 1);
        QTRY_VERIFY(!nl.checked());
        QVERIFY(nl.inhibited());
        // Sent on our own connection, not the shared one: removing the
        // widget or a plasmashell crash must release it.
        const Inhibitor held = inhibitors().constFirst();
        QVERIFY(held.sender != QDBusConnection::sessionBus().baseService());

        nl.setChecked(false); // already off: no second cookie
        QTest::qWait(100);
        QCOMPARE(methodCalls(u"inhibit"_s).size(), 1);

        nl.setChecked(true);
        QVERIFY(!nl.userInhibited());
        QTRY_VERIFY(inhibitors().isEmpty());
        QTRY_VERIFY(nl.checked());
        const QList<QVariantList> released = methodCalls(u"uninhibit"_s);
        QCOMPARE(released.size(), 1);
        QCOMPARE(released.constFirst().constFirst().toUInt(), held.cookie);
        // Enabled already: nothing written.
        QVERIFY(!QFile::exists(kwinrcPath()));
    }

    void quickToggleSettles()
    {
        NightLight nl;
        QTRY_VERIFY(nl.checked());
        // Released before KWin's reply arrives: the cookie is handed back as
        // soon as it lands.
        nl.setChecked(false);
        nl.setChecked(true);
        QTRY_COMPARE(methodCalls(u"uninhibit"_s).size(), 1);
        QVERIFY(inhibitors().isEmpty());
        QTRY_VERIFY(nl.checked());
        QVERIFY(!nl.userInhibited());
    }

    void userAndPresentationAreIndependent()
    {
        NightLight nl;
        QTRY_VERIFY(nl.checked());
        QSignalSpy inhibitions(&nl, &NightLight::inhibitionsChanged);

        nl.setPresentationInhibited(true);
        QVERIFY(nl.presentationInhibited()); // at once
        QCOMPARE(inhibitions.size(), 1);
        QTRY_COMPARE(inhibitors().size(), 1);
        QTRY_VERIFY(!nl.checked());

        // Chip switched off during the presentation: a second cookie.
        nl.setChecked(false);
        QTRY_COMPARE(inhibitors().size(), 2);
        const QList<Inhibitor> both = inhibitors();
        QVERIFY(both.at(0).cookie != both.at(1).cookie);

        // Presentation ends: the user's choice keeps Night Light off.
        nl.setPresentationInhibited(false);
        QTRY_COMPARE(inhibitors().size(), 1);
        QCOMPARE(inhibitors().constFirst().cookie, both.at(1).cookie);
        QTest::qWait(100);
        QVERIFY(!nl.checked());

        // And the chip never releases presentation mode's inhibition.
        nl.setPresentationInhibited(true);
        QTRY_COMPARE(inhibitors().size(), 2);
        nl.setChecked(true);
        QTRY_COMPARE(inhibitors().size(), 1);
        QVERIFY(nl.presentationInhibited());
        QVERIFY(!nl.userInhibited());
        QTest::qWait(100);
        QVERIFY(!nl.checked());

        nl.setPresentationInhibited(false);
        QTRY_VERIFY(inhibitors().isEmpty());
        QTRY_VERIFY(nl.checked());
    }

    void presentationWaitsForKWin()
    {
        m_kwin.stop();
        NightLight nl;
        QTest::qWait(100);
        QVERIFY(!nl.available());
        nl.setPresentationInhibited(true);
        QVERIFY(nl.presentationInhibited());
        nl.setChecked(false); // nothing to suspend without KWin
        QVERIFY(!nl.userInhibited());

        startKWin();
        QTRY_VERIFY(nl.available());
        QTRY_COMPARE(inhibitors().size(), 1);
        QTRY_VERIFY(nl.inhibited());
        QVERIFY(!nl.checked());
    }

    void kwinRestartDropsCookies()
    {
        NightLight nl;
        QTRY_VERIFY(nl.checked());
        nl.setPresentationInhibited(true);
        nl.setChecked(false);
        QTRY_COMPARE(inhibitors().size(), 2);

        m_kwin.stop();
        QTRY_VERIFY(!nl.available());
        QVERIFY(!nl.enabled());
        QVERIFY(!nl.checked());
        QVERIFY(!nl.userInhibited()); // the user's suspension died with KWin
        QVERIFY(nl.presentationInhibited()); // an input: kept, re-applied

        startKWin();
        QTRY_VERIFY(nl.available());
        QTRY_COMPARE(inhibitors().size(), 1);
        QTest::qWait(100);
        QCOMPARE(methodCalls(u"inhibit"_s).size(), 1);
        QVERIFY(!nl.userInhibited());

        nl.setPresentationInhibited(false);
        QTRY_VERIFY(inhibitors().isEmpty());
        QTRY_VERIFY(nl.checked());
    }

    void destructionReleases()
    {
        {
            NightLight nl;
            QTRY_VERIFY(nl.checked());
            nl.setChecked(false);
            nl.setPresentationInhibited(true);
            QTRY_COMPARE(inhibitors().size(), 2);
        }
        // No uninhibit() needed: closing the private connection is enough.
        QTRY_VERIFY(inhibitors().isEmpty());
        QCOMPARE(methodCalls(u"uninhibit"_s).size(), 0);
    }

    void destructionWithCallInFlight()
    {
        {
            NightLight nl;
            QTRY_VERIFY(nl.checked());
            nl.setChecked(false);
        } // inhibit() reply still pending
        QTest::qWait(200);
        QTRY_VERIFY(inhibitors().isEmpty());
    }

    void enablesThroughKwinrc()
    {
        QVERIFY(mockOk(callMock(u"SetEnabled"_s, {false})));
        NightLight nl;
        QTRY_VERIFY(nl.available());
        QVERIFY(!nl.enabled());
        QVERIFY(!nl.checked());
        QCOMPARE(nl.enableMode(), u"config"_s);
        QVERIFY(nl.canEnableDirectly());
        QSignalSpy kcm(&nl, &NightLight::kcmRequested);
        NotifySpy notify;
        QVERIFY(notify.connected);

        // The directory really is the test one (never ~/.config/kwinrc).
        QVERIFY(kwinrcPath().startsWith(m_configHome.path()));
        nl.setChecked(true);
        QVERIFY(QFile::exists(kwinrcPath()));
        QVERIFY(kwinrcActive());
        QTRY_COMPARE(notify.count, 1); // KConfig::Notify - KWin's watcher reacts to it
        QVERIFY(kcm.isEmpty());
        QVERIFY(methodCalls(u"inhibit"_s).isEmpty());
        QVERIFY(nl.errorText().isEmpty());

        // KWin follows the setting; the chip follows KWin.
        QVERIFY(mockOk(callMock(u"SetEnabled"_s, {true})));
        QTRY_VERIFY(nl.checked());
    }

    void kcmModeOpensSettings()
    {
        QVERIFY(mockOk(callMock(u"SetEnabled"_s, {false})));
        NightLight nl;
        QSignalSpy modeChanged(&nl, &NightLight::enableModeChanged);
        nl.setEnableMode(u"kcm"_s);
        QCOMPARE(nl.enableMode(), u"kcm"_s);
        QCOMPARE(modeChanged.size(), 1);
        QVERIFY(!nl.canEnableDirectly());
        QTRY_VERIFY(nl.available());

        QSignalSpy kcm(&nl, &NightLight::kcmRequested);
        nl.setChecked(true);
        QCOMPARE(kcm.size(), 1);
        QCOMPARE(kcm.constFirst().constFirst().toString(), u"kcm_nightlight"_s);
        QVERIFY(!QFile::exists(kwinrcPath()));

        nl.setEnableMode(u"bogus"_s); // anything else means the default
        QCOMPARE(nl.enableMode(), u"config"_s);
        QVERIFY(nl.canEnableDirectly());
    }

    void immutableKwinrcOpensSettings()
    {
        QFile file(kwinrcPath());
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("[NightColor]\nActive[$i]=false\n");
        file.close();

        QVERIFY(mockOk(callMock(u"SetEnabled"_s, {false})));
        NightLight nl;
        QVERIFY(!nl.canEnableDirectly());
        QTRY_VERIFY(nl.available());
        QSignalSpy kcm(&nl, &NightLight::kcmRequested);
        nl.setChecked(true);
        QCOMPARE(kcm.size(), 1);
        QVERIFY(!kwinrcActive());
    }

    void unavailableIgnoresToggles()
    {
        cleanup();
        QVERIFY2(m_kwin.start(MockBus::Session, {u"--template"_s, MockBus::mockFile(u"nightlight_kwin.py"_s), u"-p"_s, u"{\"available\": false, \"enabled\": false}"_s}, kService),
                 qPrintable(m_kwin.error()));
        NightLight nl;
        QSignalSpy kcm(&nl, &NightLight::kcmRequested);
        QTest::qWait(200);
        QVERIFY(!nl.available());
        nl.setChecked(true);
        nl.setChecked(false);
        QTest::qWait(100);
        QVERIFY(kcm.isEmpty());
        QVERIFY(!QFile::exists(kwinrcPath()));
        QVERIFY(methodCalls(u"inhibit"_s).isEmpty());
    }

private:
    void startKWin()
    {
        QVERIFY2(m_kwin.start(MockBus::Session, {u"--template"_s, MockBus::mockFile(u"nightlight_kwin.py"_s)}, kService), qPrintable(m_kwin.error()));
    }

    QTemporaryDir m_configHome;
    MockBus::Mock m_kwin;
};

QTEST_GUILESS_MAIN(TestNightLight)
#include "tst_nightlight.moc"
