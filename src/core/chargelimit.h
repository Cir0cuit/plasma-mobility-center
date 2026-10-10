// SPDX-License-Identifier: GPL-2.0-or-later
//
// ChargeLimit - battery charge stop threshold.
//
// Read: a synchronous sysfs pre-check (ChargeLimitSysfs: some
// /sys/class/power_supply/* of type Battery with an owner-writable
// charge_control_{end,start}_threshold), then the async KAuth action
// org.kde.powerdevil.chargethresholdhelper.getthreshold (polkit yes/yes, no
// prompt). An error reply hides the tile. Never poll KAuth: every call
// D-Bus-activates the root helper; poll the 0644 sysfs file instead.
//
// Write: KAuth ...setthreshold (auth_admin_keep => an administrator password
// prompt, unless install.sh's optional polkit rule is installed), arguments
// from ChargeLimitArgs::build(), timeout 3 min, job->start() (never exec()),
// jobs serialized, re-read afterwards (success or error), and on success
// call session org.kde.Solid.PowerManagement.reparseConfiguration (NOT
// refreshStatus). Never send chargeType.
//
// External changes: listen to PowerDevil chargeStopThresholdChanged(i) /
// chargeStartThresholdChanged(i), and re-read sysfs every 60 s while
// `polling` is true (the tile sets it while visible/expanded).

#pragma once

#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QQuickItem>
#include <QVariantMap>

#include <functional>
#include <memory>

#include "chargelimitsysfs.h"
#include "servicewatch.h"

class KJob;
class QTimer;
class QWindow;

class ChargeLimit : public QObject
{
    Q_OBJECT
    QML_ELEMENT

public:
    enum Permission {
        PermissionUnknown,
        PermissionGranted,
        PermissionNeeded,
    };
    Q_ENUM(Permission)

private:

    /// The battery exposes a writable stop threshold AND getthreshold
    /// succeeded. false hides the Charge limit tile and the slider in the
    /// Battery details.
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    /// Error of the last write (cancelled/denied prompt, helper error), or of
    /// the last read; empty if none. Cleared by the next successful write.
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)
    /// Current stop threshold in percent, 0..100; -1 = unknown.
    Q_PROPERTY(int stopThreshold READ stopThreshold NOTIFY thresholdsChanged)
    /// Current start threshold in percent; -1 = end-only hardware or unknown.
    Q_PROPERTY(int startThreshold READ startThreshold NOTIFY thresholdsChanged)
    /// The hardware has only a stop threshold (startThreshold == -1).
    Q_PROPERTY(bool endOnly READ endOnly NOTIFY thresholdsChanged)
    /// Slider range and step: 50..100 in 5 % steps.
    Q_PROPERTY(int minimum READ minimum CONSTANT)
    Q_PROPERTY(int maximum READ maximum CONSTANT)
    Q_PROPERTY(int step READ step CONSTANT)
    /// A KAuth job is running (the slider is disabled meanwhile).
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    /// Whether changing the limit is allowed without a password prompt
    /// (polkit CheckAuthorization without interaction, asynchronous).
    /// PermissionGranted: e.g. the optional polkit rule is installed.
    /// PermissionNeeded: setthreshold would prompt - the tile offers
    /// installPermission() first. PermissionUnknown: not checked yet or polkit
    /// unreachable; the tile then shows the slider (KAuth prompts if needed).
    Q_PROPERTY(Permission permission READ permission NOTIFY permissionChanged)
    /// Kept for compatibility: permission == PermissionNeeded.
    Q_PROPERTY(bool authRequired READ authRequired NOTIFY authRequiredChanged)
    /// installPermission() can work: pkexec exists and the rule file was
    /// installed with the widget (MC_POLKIT_RULE_SOURCE).
    Q_PROPERTY(bool canInstallPermission READ canInstallPermission CONSTANT)
    /// installPermission() is running (pkexec prompt open or copying).
    Q_PROPERTY(bool installingPermission READ installingPermission NOTIFY installingPermissionChanged)
    /// The limit was raised while the battery is Charging or FullyCharged:
    /// most firmware only resumes charging after unplugging and replugging.
    Q_PROPERTY(bool needsReplug READ needsReplug NOTIFY needsReplugChanged)
    /// INPUT from QML: true while the tile is visible/expanded; enables the
    /// 60 s sysfs re-read. Setting it to true also re-reads immediately.
    Q_PROPERTY(bool polling READ polling WRITE setPolling NOTIFY pollingChanged)

public:
    explicit ChargeLimit(QObject *parent = nullptr);
    ~ChargeLimit() override;

    bool available() const;
    QString errorText() const;
    int stopThreshold() const;
    int startThreshold() const;
    bool endOnly() const;
    int minimum() const;
    int maximum() const;
    int step() const;
    bool busy() const;
    Permission permission() const;
    bool authRequired() const;
    bool canInstallPermission() const;
    bool installingPermission() const;
    bool needsReplug() const;
    bool polling() const;
    void setPolling(bool polling);

    /// Writes stop threshold @p percent (snapped to `step`, clamped to
    /// minimum..maximum) through the KAuth helper. Call on slider RELEASE
    /// only. @p anchor (any item of the widget) parents the password dialog
    /// to the right window; may be null. Emits writeFinished.
    /// STATE-CHANGING (firmware setting, root helper): only on user action.
    Q_INVOKABLE void setStopThreshold(int percent, QQuickItem *anchor = nullptr);

    /// Re-reads the thresholds from sysfs (cheap, no KAuth, no prompt).
    Q_INVOKABLE void refresh();

    /// Re-checks `permission` with polkit (asynchronous, never prompts).
    Q_INVOKABLE void checkAuthorization();

    /// Installs the optional polkit rule that lets the active local user
    /// change the charge limit without a password: pkexec copies the
    /// root-owned rule file shipped with the widget into polkit's rules
    /// directory. pkexec asks for the administrator password once. On success
    /// `permission` turns PermissionGranted; on failure errorText says why
    /// (empty when the user cancelled). Emits permissionInstallFinished.
    /// STATE-CHANGING (system file, root): only on user action.
    Q_INVOKABLE void installPermission();

Q_SIGNALS:
    void availableChanged();
    void errorTextChanged();
    void thresholdsChanged();
    void busyChanged();
    void authRequiredChanged();
    void permissionChanged();
    void installingPermissionChanged();
    void permissionInstallFinished(bool ok);
    void needsReplugChanged();
    void pollingChanged();
    /// A setStopThreshold() job ended. @p ok false => errorText says why
    /// (an empty errorText with ok == false means the user cancelled).
    void writeFinished(bool ok);

private:
    friend class TestChargeLimit;

    /// Creates (does not start) the KAuth job for @p actionId with @p args.
    /// Tests replace it so no KAuth helper is ever contacted; the fake job
    /// carries its reply data in the dynamic property "replyData".
    using JobFactory = std::function<KJob *(const QString &actionId, const QVariantMap &args, QWindow *parent)>;

    void probe();
    void onProbeResult(KJob *job, quint64 generation);
    void startWrite(int stop, QWindow *parent);
    void onWriteResult(KJob *job, int stop, int previousStop, QPointer<QWindow> parent);
    void startQueued();
    void readSysfs();
    void onPowerDevilUp();
    void onPowerDevilDown();
    void setThresholds(int stop, int start);
    void setAvailable(bool available);
    void setBusy(bool busy);
    void setErrorText(const QString &text);
    void setPermission(Permission permission);
    void setInstallingPermission(bool installing);
    void setNeedsReplug(bool needs);
    KJob *createJob(const QString &actionId, const QVariantMap &args, QWindow *parent) const;
    QString sysfsRoot() const;
    static QVariantMap jobData(KJob *job);

    // Test seams (TestChargeLimit only): a fake sysfs tree and a fake job
    // factory. Set before constructing a ChargeLimit.
    inline static QString s_sysfsRootOverride;
    inline static JobFactory s_jobFactoryOverride;

    bool m_polling = false;
    bool m_available = false;
    bool m_present = false; ///< sysfs pre-check passed
    bool m_helperOk = false; ///< getthreshold answered
    bool m_probing = false;
    bool m_probed = false; ///< getthreshold ran since the last PowerDevil (re)start
    bool m_busy = false;
    Permission m_permission = PermissionUnknown;
    bool m_installingPermission = false;
    quint64 m_permissionGeneration = 0;
    bool m_needsReplug = false;
    int m_stop = -1;
    int m_start = -1;
    int m_pendingStop = -1; ///< queued write while a job runs; last value wins
    QPointer<QWindow> m_pendingParent;
    int m_busyRetries = 0;
    QString m_batteryName;
    QString m_errorText;
    quint64 m_generation = 0;
    QTimer *m_pollTimer = nullptr;
    std::unique_ptr<MC::ServiceWatch> m_pmWatch;
    QList<QObject *> m_pmRelays;
};
