// SPDX-License-Identifier: GPL-2.0-or-later
//
// See chargelimit.h

#include "chargelimit.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMetaType>
#include <QFile>
#include <QProcess>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTimer>

#include <KAuth/Action>
#include <KAuth/ActionReply>
#include <KAuth/ExecuteJob>

#include <chrono>

#include "chargelimitargs.h"
#include "dbusasync.h"
#include "servicewatch.h"

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;

namespace
{
const QString kHelperId = u"org.kde.powerdevil.chargethresholdhelper"_s;
const QString kGetAction = u"org.kde.powerdevil.chargethresholdhelper.getthreshold"_s;
const QString kSetAction = u"org.kde.powerdevil.chargethresholdhelper.setthreshold"_s;

const QString kPmService = u"org.kde.Solid.PowerManagement"_s;
const QString kPmPath = u"/org/kde/Solid/PowerManagement"_s;
const QString kPmIface = u"org.kde.Solid.PowerManagement"_s;

const QString kStopKey = u"chargeStopThreshold"_s;
const QString kStartKey = u"chargeStartThreshold"_s;

constexpr auto kPollInterval = 60s;
// The 25 s default can expire while the password dialog is still open.
constexpr auto kWriteTimeout = std::chrono::minutes(3);
constexpr auto kBusyRetryDelay = 1s;
constexpr int kMaxBusyRetries = 3;

#ifndef MC_POLKIT_RULE_SOURCE
#define MC_POLKIT_RULE_SOURCE ""
#endif
#ifndef MC_POLKIT_RULE_TARGET
#define MC_POLKIT_RULE_TARGET ""
#endif

// polkit reloads its rules when the rules directory changes; give it a
// moment before deciding the freshly installed rule did not take.
constexpr int kPermissionRecheckDelaysMs[] = {300, 1000, 2500, 5000};

// pkexec: 126 = the user dismissed the dialog / not authorized,
// 127 = authentication failed or the program could not be run.
constexpr int kPkexecDismissed = 126;

int thresholdValue(const QVariantMap &data, const QString &key)
{
    bool ok = false;
    const int v = data.value(key).toInt(&ok);
    return ok && v >= 0 && v <= 100 ? v : -1;
}
} // namespace

ChargeLimit::ChargeLimit(QObject *parent)
    : QObject(parent)
    , m_pollTimer(new QTimer(this))
{
    m_pollTimer->setInterval(kPollInterval);
    connect(m_pollTimer, &QTimer::timeout, this, &ChargeLimit::refresh);

    // 1. Synchronous sysfs pre-check (tiny reads, no D-Bus); 2. async getthreshold.
    readSysfs();
    if (m_present) {
        probe();
    }

    // Changes made through the Power Management KCM. Nothing on D-Bus
    // announces changes made elsewhere (sysfs poll while visible).
    m_pmWatch = std::make_unique<MC::ServiceWatch>(QDBusConnection::sessionBus(), QStringList{kPmService}, this, [this](const QString &, bool registered) {
        registered ? onPowerDevilUp() : onPowerDevilDown();
    });
}

ChargeLimit::~ChargeLimit()
{
    qDeleteAll(m_pmRelays);
}

bool ChargeLimit::available() const
{
    return m_available;
}

QString ChargeLimit::errorText() const
{
    return m_errorText;
}

int ChargeLimit::stopThreshold() const
{
    return m_stop;
}

int ChargeLimit::startThreshold() const
{
    return m_start;
}

bool ChargeLimit::endOnly() const
{
    return startThreshold() == -1;
}

int ChargeLimit::minimum() const
{
    return ChargeLimitArgs::Minimum;
}

int ChargeLimit::maximum() const
{
    return ChargeLimitArgs::Maximum;
}

int ChargeLimit::step() const
{
    return ChargeLimitArgs::Step;
}

bool ChargeLimit::busy() const
{
    return m_busy;
}

ChargeLimit::Permission ChargeLimit::permission() const
{
    return m_permission;
}

bool ChargeLimit::authRequired() const
{
    return m_permission == PermissionNeeded;
}

bool ChargeLimit::canInstallPermission() const
{
    static const bool can = QStringLiteral(MC_POLKIT_RULE_SOURCE).size() > 0 && QStringLiteral(MC_POLKIT_RULE_TARGET).size() > 0
        && QFile::exists(QStringLiteral(MC_POLKIT_RULE_SOURCE)) && !QStandardPaths::findExecutable(u"pkexec"_s).isEmpty()
        && !QStandardPaths::findExecutable(u"install"_s).isEmpty();
    return can;
}

bool ChargeLimit::installingPermission() const
{
    return m_installingPermission;
}

bool ChargeLimit::needsReplug() const
{
    return m_needsReplug;
}

bool ChargeLimit::polling() const
{
    return m_polling;
}

void ChargeLimit::setPolling(bool polling)
{
    if (m_polling == polling) {
        return;
    }
    m_polling = polling;
    Q_EMIT pollingChanged();
    if (m_polling) {
        refresh();
        m_pollTimer->start();
    } else {
        m_pollTimer->stop();
    }
}

void ChargeLimit::setStopThreshold(int percent, QQuickItem *anchor)
{
    if (!m_available) {
        return;
    }
    const int stop = ChargeLimitArgs::snap(percent);
    QWindow *window = anchor ? anchor->window() : nullptr;
    if (m_busy) {
        // Jobs are serialized: remember the latest wish, send it afterwards.
        m_pendingStop = stop;
        m_pendingParent = window;
        return;
    }
    m_busyRetries = 0;
    startWrite(stop, window);
}

void ChargeLimit::refresh()
{
    readSysfs();
    // The battery (or the kernel interface) appeared later: ask the helper
    // once. Never repeatedly - every call activates the root helper.
    if (m_present && !m_helperOk && !m_probing && !m_probed) {
        probe();
    }
}

void ChargeLimit::checkAuthorization()
{
    // polkit's CheckAuthorization without interaction: never prompts, and
    // answers asynchronously, so plasmashell never blocks on it. The subject
    // is this process's system bus connection, as KAuth's own polkit
    // backend uses for setthreshold.
    QDBusConnection bus = QDBusConnection::systemBus();
    if (!bus.isConnected()) {
        setPermission(PermissionUnknown);
        return;
    }
    static const bool registered = [] {
        qDBusRegisterMetaType<QMap<QString, QString>>();
        return true;
    }();
    Q_UNUSED(registered)

    QDBusArgument subject;
    subject.beginStructure();
    subject << u"system-bus-name"_s << QVariantMap{{u"name"_s, bus.baseService()}};
    subject.endStructure();

    const QDBusMessage msg = MC::DBus::methodCall(u"org.freedesktop.PolicyKit1"_s,
                                                  u"/org/freedesktop/PolicyKit1/Authority"_s,
                                                  u"org.freedesktop.PolicyKit1.Authority"_s,
                                                  u"CheckAuthorization"_s,
                                                  {QVariant::fromValue(subject),
                                                   kSetAction,
                                                   QVariant::fromValue(QMap<QString, QString>()),
                                                   uint(0), // no user interaction
                                                   QString()});
    const quint64 generation = ++m_permissionGeneration;
    MC::DBus::call(bus, msg, this, [this, generation](const QDBusMessage &reply) {
        if (generation != m_permissionGeneration) {
            return;
        }
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
            setPermission(PermissionUnknown);
            return;
        }
        // (bba{ss}): is_authorized, is_challenge, details
        const QVariantList result = MC::DBus::demarshall(reply.arguments().constFirst()).toList();
        if (result.size() < 2) {
            setPermission(PermissionUnknown);
            return;
        }
        setPermission(result.at(0).toBool() ? PermissionGranted : PermissionNeeded);
    });
}

void ChargeLimit::installPermission()
{
    if (m_installingPermission || !canInstallPermission()) {
        return;
    }
    setInstallingPermission(true);
    setErrorText({});

    auto *process = new QProcess(this);
    process->setProgram(QStandardPaths::findExecutable(u"pkexec"_s));
    process->setArguments({QStandardPaths::findExecutable(u"install"_s),
                           u"-D"_s,
                           u"-m"_s,
                           u"0644"_s,
                           QStringLiteral(MC_POLKIT_RULE_SOURCE),
                           QStringLiteral(MC_POLKIT_RULE_TARGET)});
    process->setProcessChannelMode(QProcess::MergedChannels);
    connect(process, &QProcess::finished, this, [this, process](int exitCode, QProcess::ExitStatus status) {
        process->deleteLater();
        setInstallingPermission(false);
        const bool ok = status == QProcess::NormalExit && exitCode == 0;
        if (ok) {
            checkAuthorization();
            // polkit picks the new rule up within moments; re-check until it does.
            for (const int delay : kPermissionRecheckDelaysMs) {
                QTimer::singleShot(delay, this, [this] {
                    if (m_permission != PermissionGranted) {
                        checkAuthorization();
                    }
                });
            }
        } else if (status == QProcess::NormalExit && exitCode == kPkexecDismissed) {
            setErrorText({}); // cancelled
        } else {
            const QString output = QString::fromLocal8Bit(process->readAll()).trimmed();
            setErrorText(output.isEmpty() ? tr("Could not install the permission (pkexec exit code %1)").arg(exitCode) : output);
        }
        Q_EMIT permissionInstallFinished(ok);
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) {
            return;
        }
        process->deleteLater();
        setInstallingPermission(false);
        setErrorText(process->errorString());
        Q_EMIT permissionInstallFinished(false);
    });
    process->start();
}

void ChargeLimit::probe()
{
    const quint64 generation = ++m_generation;
    m_probing = true;
    m_probed = true;
    KJob *job = createJob(kGetAction, {}, nullptr);
    connect(job, &KJob::result, this, [this, job, generation] {
        onProbeResult(job, generation);
    });
    job->start();
}

void ChargeLimit::onProbeResult(KJob *job, quint64 generation)
{
    if (generation != m_generation) {
        return;
    }
    m_probing = false;
    const QVariantMap data = jobData(job);
    const int stop = thresholdValue(data, kStopKey);
    if (job->error() != 0 || stop < 0) {
        // No helper, no polkit, or the helper sees no thresholds: hide.
        m_helperOk = false;
        setAvailable(false);
        return;
    }
    m_helperOk = true;
    if (data.contains(kStartKey)) {
        setThresholds(stop, thresholdValue(data, kStartKey));
    } else {
        setThresholds(stop, m_start);
    }
    // sysfs stays the source of truth for later reads.
    readSysfs();
    setAvailable(m_present && m_helperOk);
}

void ChargeLimit::startWrite(int stop, QWindow *parent)
{
    const int previousStop = m_stop;
    const QVariantMap args = ChargeLimitArgs::build(stop, m_start);
    KJob *job = createJob(kSetAction, args, parent);
    setBusy(true);
    const QPointer<QWindow> parentGuard(parent);
    connect(job, &KJob::result, this, [this, job, stop, previousStop, parentGuard] {
        onWriteResult(job, stop, previousStop, parentGuard);
    });
    job->start(); // never exec() inside plasmashell
}

void ChargeLimit::onWriteResult(KJob *job, int stop, int previousStop, QPointer<QWindow> parent)
{
    const int error = job->error();
    const bool ok = error == 0;

    if (error == KAuth::ActionReply::HelperBusyError && m_busyRetries < kMaxBusyRetries) {
        ++m_busyRetries;
        QTimer::singleShot(kBusyRetryDelay, this, [this, stop, parent] {
            // A newer value queued meanwhile replaces this one.
            if (m_pendingStop >= 0) {
                startQueued();
            } else {
                startWrite(stop, parent);
            }
        });
        return;
    }
    m_busyRetries = 0;

    if (ok) {
        setErrorText({});
        // Most firmware only resumes charging after a replug when the limit
        // is raised while the battery is charging or full (the KCM's rule).
        const QString status = ChargeLimitSysfs::status(m_batteryName, sysfsRoot());
        if (stop > previousStop && previousStop >= 0) {
            setNeedsReplug(status == u"Charging"_s || status == u"Full"_s);
        } else if (stop < previousStop) {
            setNeedsReplug(false);
        }
        // Let PowerDevil (and the battery applet) pick up the new values.
        // Not refreshStatus: that would re-apply power profiles.
        if (m_pmWatch && m_pmWatch->isRegistered(kPmService)) {
            MC::DBus::call(QDBusConnection::sessionBus(), MC::DBus::methodCall(kPmService, kPmPath, kPmIface, u"reparseConfiguration"_s), this, nullptr);
        }
    } else if (error == KAuth::ActionReply::UserCancelledError) {
        setErrorText({});
    } else {
        QString text = job->errorText();
        if (text.isEmpty()) {
            text = u"Could not change the charge limit (error %1)"_s.arg(error);
        }
        setErrorText(text);
    }

    // Always re-read, success or error.
    readSysfs();

    if (m_pendingStop >= 0) {
        startQueued();
    } else {
        setBusy(false);
    }
    Q_EMIT writeFinished(ok);
}

void ChargeLimit::startQueued()
{
    const int stop = m_pendingStop;
    const QPointer<QWindow> parent = m_pendingParent;
    m_pendingStop = -1;
    m_pendingParent = nullptr;
    if (stop == m_stop || !m_available) {
        setBusy(false);
        return;
    }
    startWrite(stop, parent);
}

void ChargeLimit::readSysfs()
{
    const ChargeThresholds t = ChargeLimitSysfs::read(sysfsRoot());
    m_present = t.present;
    m_batteryName = t.batteryName;
    if (t.present) {
        setThresholds(t.stop, t.start);
        if (m_needsReplug && ChargeLimitSysfs::status(m_batteryName, sysfsRoot()) == u"Discharging"_s) {
            setNeedsReplug(false); // unplugged
        }
    } else {
        setThresholds(-1, -1);
        setNeedsReplug(false);
    }
    setAvailable(m_present && m_helperOk);
}

void ChargeLimit::onPowerDevilUp()
{
    qDeleteAll(m_pmRelays);
    m_pmRelays.clear();
    const QDBusConnection bus = QDBusConnection::sessionBus();
    // The values in these signals are PowerDevil's view; sysfs is the truth.
    m_pmRelays << MC::DBus::onSignal(bus, kPmService, kPmPath, kPmIface, u"chargeStopThresholdChanged"_s, this, [this](const QDBusMessage &) {
        refresh();
    });
    m_pmRelays << MC::DBus::onSignal(bus, kPmService, kPmPath, kPmIface, u"chargeStartThresholdChanged"_s, this, [this](const QDBusMessage &) {
        refresh();
    });
    // A new PowerDevil instance may come with a reinstalled helper: allow one
    // more getthreshold if the last one failed.
    if (!m_helperOk && !m_probing) {
        m_probed = false;
    }
    refresh();
}

void ChargeLimit::onPowerDevilDown()
{
    qDeleteAll(m_pmRelays);
    m_pmRelays.clear();
}

void ChargeLimit::setThresholds(int stop, int start)
{
    if (m_stop == stop && m_start == start) {
        return;
    }
    m_stop = stop;
    m_start = start;
    Q_EMIT thresholdsChanged();
}

void ChargeLimit::setAvailable(bool available)
{
    if (m_available == available) {
        return;
    }
    m_available = available;
    Q_EMIT availableChanged();
    if (m_available) {
        checkAuthorization();
    }
}

void ChargeLimit::setBusy(bool busy)
{
    if (m_busy == busy) {
        return;
    }
    m_busy = busy;
    Q_EMIT busyChanged();
}

void ChargeLimit::setErrorText(const QString &text)
{
    if (m_errorText == text) {
        return;
    }
    m_errorText = text;
    Q_EMIT errorTextChanged();
}

void ChargeLimit::setPermission(Permission permission)
{
    if (m_permission == permission) {
        return;
    }
    const bool wasRequired = authRequired();
    m_permission = permission;
    Q_EMIT permissionChanged();
    if (authRequired() != wasRequired) {
        Q_EMIT authRequiredChanged();
    }
}

void ChargeLimit::setInstallingPermission(bool installing)
{
    if (m_installingPermission == installing) {
        return;
    }
    m_installingPermission = installing;
    Q_EMIT installingPermissionChanged();
}

void ChargeLimit::setNeedsReplug(bool needs)
{
    if (m_needsReplug == needs) {
        return;
    }
    m_needsReplug = needs;
    Q_EMIT needsReplugChanged();
}

KJob *ChargeLimit::createJob(const QString &actionId, const QVariantMap &args, QWindow *parent) const
{
    if (s_jobFactoryOverride) {
        return s_jobFactoryOverride(actionId, args, parent);
    }
    KAuth::Action action(actionId);
    action.setHelperId(kHelperId);
    action.setArguments(args);
    if (parent) {
        action.setParentWindow(parent);
    }
    if (actionId == kSetAction) {
        action.setTimeout(kWriteTimeout);
    }
    return action.execute();
}

QString ChargeLimit::sysfsRoot() const
{
    return s_sysfsRootOverride.isEmpty() ? u"/sys/class/power_supply"_s : s_sysfsRootOverride;
}

QVariantMap ChargeLimit::jobData(KJob *job)
{
    if (auto *executeJob = qobject_cast<KAuth::ExecuteJob *>(job)) {
        return executeJob->data();
    }
    return job->property("replyData").toMap();
}
