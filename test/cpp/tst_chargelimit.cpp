// SPDX-License-Identifier: GPL-2.0-or-later
//
// ChargeLimit against mocked services - runs under dbus-run-session (a private
// session bus), see test/mocks/README.md. Never against the live session.
//
// No KAuth helper is ever contacted: the job factory is replaced by FakeJob
// (scripted replies), and sysfs is a fake tree in a QTemporaryDir. PowerDevil
// (reparseConfiguration, chargeStopThresholdChanged) is a dbusmock template.

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDir>
#include <QFile>
#include <QPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <KAuth/ActionReply>
#include <KJob>

#include "chargelimit.h"
#include "chargelimitsysfs.h"
#include "dbusasync.h"
#include "mockbus.h"

using namespace Qt::StringLiterals;

namespace
{
const QString kGet = u"org.kde.powerdevil.chargethresholdhelper.getthreshold"_s;
const QString kSet = u"org.kde.powerdevil.chargethresholdhelper.setthreshold"_s;
const QString kPmService = u"org.kde.Solid.PowerManagement"_s;
const QString kPmPath = u"/org/kde/Solid/PowerManagement"_s;
const QString kMockIface = u"org.freedesktop.DBus.Mock"_s;

/// What a FakeJob answers.
struct Reply {
    int error = 0;
    QString errorText;
    QVariantMap data;
    /// Runs right before the result (e.g. the "helper" writing sysfs).
    std::function<void()> sideEffect;
    /// Wait for FakeJob::finish() instead of finishing on its own.
    bool held = false;
};

class FakeJob : public KJob
{
    Q_OBJECT
public:
    explicit FakeJob(const Reply &reply)
        : m_reply(reply)
    {
    }

    void start() override
    {
        if (!m_reply.held) {
            QTimer::singleShot(10, this, &FakeJob::finish);
        }
    }

    void finish()
    {
        if (m_reply.sideEffect) {
            m_reply.sideEffect();
        }
        setProperty("replyData", m_reply.data);
        setError(m_reply.error);
        setErrorText(m_reply.errorText);
        emitResult();
    }

private:
    Reply m_reply;
};

struct Request {
    QString action;
    QVariantMap args;
};

bool writeFile(const QString &path, const QByteArray &content, QFileDevice::Permissions perms = QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup | QFileDevice::ReadOther)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    f.write(content);
    f.close();
    return f.setPermissions(perms);
}

QDBusMessage callMock(const QString &method, const QVariantList &args = {})
{
    QDBusMessage msg = QDBusMessage::createMethodCall(kPmService, kPmPath, kMockIface, method);
    msg.setArguments(args);
    return QDBusConnection::sessionBus().call(msg);
}

int reparseCalls()
{
    const QDBusMessage reply = callMock(u"GetMethodCalls"_s, {u"reparseConfiguration"_s});
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        return -1;
    }
    return MC::DBus::demarshall(reply.arguments().constFirst()).toList().size();
}
} // namespace

class TestChargeLimit : public QObject
{
    Q_OBJECT

private:
    QString m_root; ///< fake /sys/class/power_supply
    std::unique_ptr<QTemporaryDir> m_dir;
    QList<Request> m_requests;
    QList<Reply> m_replies; ///< consumed in order; empty => default success
    QList<QPointer<FakeJob>> m_held;
    MockBus::Mock m_pm;

    // A battery directory under the fake root.
    void addBattery(const QString &name, int stop, int start = -1, const QByteArray &status = "Discharging", bool writable = true)
    {
        const QString dir = m_root + u'/' + name;
        QVERIFY(QDir().mkpath(dir));
        QVERIFY(writeFile(dir + u"/type"_s, "Battery\n"));
        QVERIFY(writeFile(dir + u"/status"_s, status + '\n'));
        const QFileDevice::Permissions ro = QFileDevice::ReadOwner | QFileDevice::ReadGroup | QFileDevice::ReadOther;
        const QFileDevice::Permissions rw = ro | QFileDevice::WriteOwner;
        if (stop >= 0) {
            QVERIFY(writeFile(dir + u"/charge_control_end_threshold"_s, QByteArray::number(stop) + '\n', writable ? rw : ro));
        }
        if (start >= 0) {
            QVERIFY(writeFile(dir + u"/charge_control_start_threshold"_s, QByteArray::number(start) + '\n', writable ? rw : ro));
        }
    }

    void setSysfs(const QString &name, const QString &file, const QByteArray &value)
    {
        QVERIFY(writeFile(m_root + u'/' + name + u'/' + file, value + '\n'));
    }

    static Reply thresholds(int stop, int start)
    {
        Reply r;
        r.data = {{u"chargeStopThreshold"_s, stop}, {u"chargeStartThreshold"_s, start}};
        return r;
    }

    QList<Request> requestsFor(const QString &action) const
    {
        QList<Request> out;
        for (const Request &r : m_requests) {
            if (r.action == action) {
                out.append(r);
            }
        }
        return out;
    }

private Q_SLOTS:
    void initTestCase()
    {
        MC_REQUIRE_PRIVATE_SESSION_BUS();
        MC_REQUIRE_DBUSMOCK();
        ChargeLimit::s_jobFactoryOverride = [this](const QString &action, const QVariantMap &args, QWindow *) -> KJob * {
            m_requests.append({action, args});
            const Reply reply = m_replies.isEmpty() ? Reply{} : m_replies.takeFirst();
            auto *job = new FakeJob(reply);
            if (reply.held) {
                m_held.append(job);
            }
            return job;
        };
    }

    void cleanupTestCase()
    {
        ChargeLimit::s_jobFactoryOverride = nullptr;
        ChargeLimit::s_sysfsRootOverride.clear();
    }

    void init()
    {
        m_dir = std::make_unique<QTemporaryDir>();
        QVERIFY(m_dir->isValid());
        m_root = m_dir->filePath(u"power_supply"_s);
        QVERIFY(QDir().mkpath(m_root));
        ChargeLimit::s_sysfsRootOverride = m_root;
        m_requests.clear();
        m_replies.clear();
        m_held.clear();
        QVERIFY2(m_pm.start(MockBus::Session, {u"--template"_s, MockBus::mockFile(u"chargelimit_powermanagement.py"_s)}, kPmService),
                 qPrintable(m_pm.error()));
    }

    void cleanup()
    {
        m_pm.stop();
        m_dir.reset();
    }

    // --- sysfs probe -------------------------------------------------------

    void sysfsEndOnly()
    {
        addBattery(u"BAT0"_s, 80);
        const ChargeThresholds t = ChargeLimitSysfs::read(m_root);
        QVERIFY(t.present);
        QCOMPARE(t.batteryName, u"BAT0"_s);
        QCOMPARE(t.stop, 80);
        QCOMPARE(t.start, -1);
    }

    void sysfsStartAndEnd()
    {
        addBattery(u"BAT1"_s, 90, 75);
        const ChargeThresholds t = ChargeLimitSysfs::read(m_root);
        QVERIFY(t.present);
        QCOMPARE(t.stop, 90);
        QCOMPARE(t.start, 75);
    }

    void sysfsNotWritable()
    {
        addBattery(u"BAT0"_s, 80, 70, "Charging", false);
        QVERIFY(!ChargeLimitSysfs::read(m_root).present);
    }

    void sysfsNoBattery()
    {
        QVERIFY(!ChargeLimitSysfs::read(m_root).present);
        QVERIFY(!ChargeLimitSysfs::read(m_root + u"/missing"_s).present);
        // A battery without thresholds, a mains adapter, a peripheral battery.
        addBattery(u"BAT0"_s, -1);
        QVERIFY(QDir().mkpath(m_root + u"/ADP1"_s));
        QVERIFY(writeFile(m_root + u"/ADP1/type"_s, "Mains\n"));
        addBattery(u"hidpp_battery_0"_s, 80);
        QVERIFY(writeFile(m_root + u"/hidpp_battery_0/scope"_s, "Device\n"));
        QVERIFY(!ChargeLimitSysfs::read(m_root).present);
    }

    void sysfsSymlinkedDevice()
    {
        // The real /sys/class/power_supply entries are symlinks.
        const QString real = m_dir->filePath(u"devices/BAT1"_s);
        QVERIFY(QDir().mkpath(real));
        QVERIFY(writeFile(real + u"/type"_s, "Battery\n"));
        QVERIFY(writeFile(real + u"/charge_control_end_threshold"_s, "85\n"));
        QVERIFY(QFile::link(real, m_root + u"/BAT1"_s));
        const ChargeThresholds t = ChargeLimitSysfs::read(m_root);
        QVERIFY(t.present);
        QCOMPARE(t.batteryName, u"BAT1"_s);
        QCOMPARE(t.stop, 85);
    }

    void sysfsStatus()
    {
        addBattery(u"BAT0"_s, 80, -1, "Not charging");
        QCOMPARE(ChargeLimitSysfs::status(u"BAT0"_s, m_root), u"Not charging"_s);
        QCOMPARE(ChargeLimitSysfs::status(u"BAT9"_s, m_root), QString());
    }

    // --- presence ----------------------------------------------------------

    void availableAfterGetThreshold()
    {
        addBattery(u"BAT0"_s, 80);
        m_replies << thresholds(80, -1);
        ChargeLimit c;
        QVERIFY(!c.available()); // only after the async getthreshold
        QTRY_VERIFY(c.available());
        QCOMPARE(c.stopThreshold(), 80);
        QCOMPARE(c.startThreshold(), -1);
        QVERIFY(c.endOnly());
        QCOMPARE(m_requests.size(), 1);
        QCOMPARE(m_requests.constFirst().action, kGet);
        QVERIFY(m_requests.constFirst().args.isEmpty());
        QVERIFY(c.errorText().isEmpty());
        QVERIFY(!c.busy());
        QCOMPARE(c.minimum(), 50);
        QCOMPARE(c.maximum(), 100);
        QCOMPARE(c.step(), 5);
    }

    void startAndStopHardware()
    {
        addBattery(u"BAT0"_s, 90, 75);
        m_replies << thresholds(90, 75);
        ChargeLimit c;
        QTRY_VERIFY(c.available());
        QCOMPARE(c.stopThreshold(), 90);
        QCOMPARE(c.startThreshold(), 75);
        QVERIFY(!c.endOnly());
    }

    void noSysfsMeansNoHelperCall()
    {
        ChargeLimit c;
        QTest::qWait(200);
        QVERIFY(!c.available());
        QVERIFY(m_requests.isEmpty());
        c.refresh();
        QVERIFY(m_requests.isEmpty());
    }

    void helperErrorHides()
    {
        addBattery(u"BAT0"_s, 80);
        Reply r;
        r.error = KAuth::ActionReply::NoSuchActionError;
        r.errorText = u"no such action"_s;
        m_replies << r;
        ChargeLimit c;
        QTRY_COMPARE(m_requests.size(), 1);
        QTest::qWait(100);
        QVERIFY(!c.available());
        // KAuth is never polled: neither refresh() nor the poll timer asks again.
        c.refresh();
        c.setPolling(true);
        QTest::qWait(100);
        QCOMPARE(m_requests.size(), 1);
        // Writes are refused while unavailable.
        c.setStopThreshold(60, nullptr);
        QCOMPARE(m_requests.size(), 1);
    }

    void powerDevilRestartRetriesFailedProbe()
    {
        addBattery(u"BAT0"_s, 80);
        Reply r;
        r.error = KAuth::ActionReply::DBusError;
        m_replies << r;
        ChargeLimit c;
        QTRY_COMPARE(m_requests.size(), 1);
        QTest::qWait(100);
        QVERIFY(!c.available());

        // One more getthreshold per PowerDevil (re)start, not more.
        m_pm.stop();
        m_replies << thresholds(80, -1);
        QVERIFY2(m_pm.start(MockBus::Session, {u"--template"_s, MockBus::mockFile(u"chargelimit_powermanagement.py"_s)}, kPmService),
                 qPrintable(m_pm.error()));
        QTRY_VERIFY(c.available());
        QCOMPARE(requestsFor(kGet).size(), 2);
        c.refresh();
        QCOMPARE(requestsFor(kGet).size(), 2);
    }

    void helperWithoutThresholdsHides()
    {
        addBattery(u"BAT0"_s, 80);
        m_replies << Reply{}; // success, but no data
        ChargeLimit c;
        QTRY_COMPARE(m_requests.size(), 1);
        QTest::qWait(100);
        QVERIFY(!c.available());
    }

    // --- writes ------------------------------------------------------------

    void writeSnapsSendsStopOnlyAndReparses()
    {
        addBattery(u"BAT0"_s, 80);
        m_replies << thresholds(80, -1);
        ChargeLimit c;
        QTRY_VERIFY(c.available());
        QTRY_COMPARE(reparseCalls(), 0);

        Reply ok;
        ok.held = true;
        ok.sideEffect = [this] {
            setSysfs(u"BAT0"_s, u"charge_control_end_threshold"_s, "65");
        };
        m_replies << ok;
        QSignalSpy finished(&c, &ChargeLimit::writeFinished);
        c.setStopThreshold(63, nullptr);
        QVERIFY(c.busy());

        const QList<Request> sets = requestsFor(kSet);
        QCOMPARE(sets.size(), 1);
        QCOMPARE(sets.constFirst().args, (QVariantMap{{u"chargeStopThreshold"_s, 65}}));

        QCOMPARE(m_held.size(), 1);
        m_held.constFirst()->finish();
        QCOMPARE(finished.size(), 1);
        QCOMPARE(finished.constFirst().constFirst().toBool(), true);
        QVERIFY(!c.busy());
        QCOMPARE(c.stopThreshold(), 65); // re-read from sysfs
        QVERIFY(c.errorText().isEmpty());
        QTRY_COMPARE(reparseCalls(), 1);
        // getthreshold ran once at start-up and never again.
        QCOMPARE(requestsFor(kGet).size(), 1);
    }

    void writeLowersStartToo()
    {
        addBattery(u"BAT0"_s, 80, 75);
        m_replies << thresholds(80, 75);
        ChargeLimit c;
        QTRY_VERIFY(c.available());
        c.setStopThreshold(60, nullptr);
        QTRY_COMPARE(requestsFor(kSet).size(), 1);
        QCOMPARE(requestsFor(kSet).constFirst().args,
                 (QVariantMap{{u"chargeStopThreshold"_s, 60}, {u"chargeStartThreshold"_s, 60}}));
        QTRY_VERIFY(!c.busy());
    }

    void cancelledLeavesNoError()
    {
        addBattery(u"BAT0"_s, 80);
        m_replies << thresholds(80, -1);
        ChargeLimit c;
        QTRY_VERIFY(c.available());
        Reply cancel;
        cancel.error = KAuth::ActionReply::UserCancelledError;
        cancel.errorText = u"cancelled"_s;
        m_replies << cancel;
        QSignalSpy finished(&c, &ChargeLimit::writeFinished);
        c.setStopThreshold(90, nullptr);
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(finished.constFirst().constFirst().toBool(), false);
        QVERIFY(c.errorText().isEmpty());
        QCOMPARE(c.stopThreshold(), 80);
        QVERIFY(!c.busy());
        QTest::qWait(100);
        QCOMPARE(reparseCalls(), 0);
    }

    void deniedShowsErrorUntilNextSuccess()
    {
        addBattery(u"BAT0"_s, 80);
        m_replies << thresholds(80, -1);
        ChargeLimit c;
        QTRY_VERIFY(c.available());
        Reply denied;
        denied.error = KAuth::ActionReply::AuthorizationDeniedError;
        denied.errorText = u"Not authorized"_s;
        m_replies << denied;
        QSignalSpy finished(&c, &ChargeLimit::writeFinished);
        c.setStopThreshold(70, nullptr);
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(finished.constFirst().constFirst().toBool(), false);
        QCOMPARE(c.errorText(), u"Not authorized"_s);
        QVERIFY(c.available()); // a failed write does not hide the tile

        // A helper error without text still produces something to show.
        Reply bare;
        bare.error = -1;
        m_replies << bare;
        c.setStopThreshold(70, nullptr);
        QTRY_COMPARE(finished.size(), 2);
        QVERIFY(!c.errorText().isEmpty());

        c.setStopThreshold(70, nullptr); // default reply: success
        QTRY_COMPARE(finished.size(), 3);
        QCOMPARE(finished.constLast().constFirst().toBool(), true);
        QVERIFY(c.errorText().isEmpty());
    }

    void helperBusyIsRetried()
    {
        addBattery(u"BAT0"_s, 80);
        m_replies << thresholds(80, -1);
        ChargeLimit c;
        QTRY_VERIFY(c.available());
        Reply busy;
        busy.error = KAuth::ActionReply::HelperBusyError;
        m_replies << busy;
        QSignalSpy finished(&c, &ChargeLimit::writeFinished);
        c.setStopThreshold(85, nullptr);
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
        QCOMPARE(finished.constFirst().constFirst().toBool(), true);
        const QList<Request> sets = requestsFor(kSet);
        QCOMPARE(sets.size(), 2);
        QCOMPARE(sets.constLast().args.value(u"chargeStopThreshold"_s).toInt(), 85);
    }

    void writesAreSerializedLastWins()
    {
        addBattery(u"BAT0"_s, 80);
        m_replies << thresholds(80, -1);
        ChargeLimit c;
        QTRY_VERIFY(c.available());
        Reply first;
        first.held = true;
        m_replies << first;
        QSignalSpy finished(&c, &ChargeLimit::writeFinished);
        c.setStopThreshold(70, nullptr);
        c.setStopThreshold(55, nullptr);
        c.setStopThreshold(95, nullptr);
        QCOMPARE(requestsFor(kSet).size(), 1); // nothing sent while one runs
        m_held.constFirst()->finish();
        QVERIFY(c.busy()); // the queued one started right away
        QTRY_COMPARE(finished.size(), 2);
        const QList<Request> sets = requestsFor(kSet);
        QCOMPARE(sets.size(), 2);
        QCOMPARE(sets.at(0).args.value(u"chargeStopThreshold"_s).toInt(), 70);
        QCOMPARE(sets.at(1).args.value(u"chargeStopThreshold"_s).toInt(), 95);
        QVERIFY(!c.busy());
    }

    void needsReplug_data()
    {
        QTest::addColumn<QByteArray>("status");
        QTest::addColumn<int>("newStop");
        QTest::addColumn<bool>("expected");

        QTest::newRow("raised while charging") << QByteArray("Charging") << 90 << true;
        QTest::newRow("raised while full") << QByteArray("Full") << 100 << true;
        QTest::newRow("raised while discharging") << QByteArray("Discharging") << 90 << false;
        QTest::newRow("lowered while charging") << QByteArray("Charging") << 60 << false;
    }

    void needsReplug()
    {
        QFETCH(QByteArray, status);
        QFETCH(int, newStop);
        QFETCH(bool, expected);

        addBattery(u"BAT0"_s, 80, -1, status);
        m_replies << thresholds(80, -1);
        ChargeLimit c;
        QTRY_VERIFY(c.available());
        Reply ok;
        ok.sideEffect = [this, newStop] {
            setSysfs(u"BAT0"_s, u"charge_control_end_threshold"_s, QByteArray::number(newStop));
        };
        m_replies << ok;
        QSignalSpy finished(&c, &ChargeLimit::writeFinished);
        c.setStopThreshold(newStop, nullptr);
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(c.needsReplug(), expected);

        if (expected) {
            // Unplugging clears the hint on the next read.
            setSysfs(u"BAT0"_s, u"status"_s, "Discharging");
            c.refresh();
            QVERIFY(!c.needsReplug());
        }
    }

    // --- external changes ----------------------------------------------------

    void followsPowerDevilSignals()
    {
        addBattery(u"BAT0"_s, 80);
        m_replies << thresholds(80, -1);
        ChargeLimit c;
        QTRY_VERIFY(c.available());
        QTest::qWait(100); // let the service watch subscribe
        setSysfs(u"BAT0"_s, u"charge_control_end_threshold"_s, "70");
        QSignalSpy changed(&c, &ChargeLimit::thresholdsChanged);
        QVERIFY(callMock(u"EmitThresholdChanged"_s, {u"stop"_s, 70}).type() == QDBusMessage::ReplyMessage);
        QTRY_COMPARE(c.stopThreshold(), 70);
        QCOMPARE(changed.size(), 1);
        QCOMPARE(requestsFor(kGet).size(), 1);
    }

    void pollingRereadsSysfs()
    {
        addBattery(u"BAT0"_s, 80);
        m_replies << thresholds(80, -1);
        ChargeLimit c;
        QTRY_VERIFY(c.available());
        setSysfs(u"BAT0"_s, u"charge_control_end_threshold"_s, "60");
        QCOMPARE(c.stopThreshold(), 80); // nothing notifies
        QSignalSpy pollingSpy(&c, &ChargeLimit::pollingChanged);
        c.setPolling(true); // immediate re-read
        QCOMPARE(pollingSpy.size(), 1);
        QVERIFY(c.polling());
        QCOMPARE(c.stopThreshold(), 60);
        c.setPolling(false);
        QCOMPARE(pollingSpy.size(), 2);
        QCOMPARE(requestsFor(kGet).size(), 1);
    }

    void batteryDisappears()
    {
        addBattery(u"BAT0"_s, 80);
        m_replies << thresholds(80, -1);
        ChargeLimit c;
        QTRY_VERIFY(c.available());
        QVERIFY(QDir(m_root + u"/BAT0"_s).removeRecursively());
        c.refresh();
        QVERIFY(!c.available());
        QCOMPARE(c.stopThreshold(), -1);
    }
};

QTEST_GUILESS_MAIN(TestChargeLimit)
#include "tst_chargelimit.moc"
