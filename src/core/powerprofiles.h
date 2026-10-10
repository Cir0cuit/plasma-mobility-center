// SPDX-License-Identifier: GPL-2.0-or-later
//
// PowerProfiles - power-saver / balanced / performance chips.
//
// Source: system bus org.freedesktop.UPower.PowerProfiles
// /org/freedesktop/UPower/PowerProfiles; if that name is not registered,
// net.hadess.PowerProfiles /net/hadess/PowerProfiles (older ppd, tuned-ppd).
// GetAll, then PropertiesChanged. tuned-ppd can change `Profiles` with the AC
// state without a signal, so re-read on UPower OnBattery changes.
//
// Never call PowerDevil refreshStatus. PowerDevil re-applies its configured
// profile on AC/battery switches and session re-activation - the UI may hint
// at that with configuredProfile.

#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <memory>

#include "servicewatch.h"

class PowerProfiles : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    /// A profiles daemon is reachable and offers real profiles: the list is
    /// non-empty and Profiles[0].Driver != "placeholder".
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    /// Last error worth showing (e.g. a refused Set); empty if none.
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)
    /// Bus name in use: "org.freedesktop.UPower.PowerProfiles",
    /// "net.hadess.PowerProfiles", or "" when none.
    Q_PROPERTY(QString backendService READ backendService NOTIFY availableChanged)
    /// Profile ids in display order, a subset of
    /// ["power-saver", "balanced", "performance"] (only `Profile` and `Driver`
    /// of each Profiles entry are relied on).
    Q_PROPERTY(QStringList profiles READ profiles NOTIFY profilesChanged)
    /// Active profile id, one of `profiles`; "" when unknown.
    Q_PROPERTY(QString activeProfile READ activeProfile NOTIFY activeProfileChanged)
    /// Raw PerformanceDegraded string ("" = not degraded), comma separated.
    Q_PROPERTY(QString performanceDegraded READ performanceDegraded NOTIFY performanceDegradedChanged)
    /// PerformanceDegraded split on ",": known tokens "lap-detected" and
    /// "high-operating-temperature"; QML shows generic text for others.
    Q_PROPERTY(QStringList degradedReasons READ degradedReasons NOTIFY performanceDegradedChanged)
    /// ActiveProfileHolds: one map per hold, keys Profile (QString),
    /// Reason (QString), ApplicationId (QString) - as ppd sends them.
    Q_PROPERTY(QVariantList holds READ holds NOTIFY holdsChanged)
    /// The daemon exposes BatteryAware (ppd >= 0.30; tuned-ppd does not).
    Q_PROPERTY(bool hasBatteryAware READ hasBatteryAware NOTIFY profilesChanged)
    /// Value of BatteryAware; false when !hasBatteryAware.
    Q_PROPERTY(bool batteryAware READ batteryAware NOTIFY profilesChanged)
    /// Optional hint: PowerDevil's Actions.PowerProfile configuredProfile()
    /// for the current power state, which PowerDevil re-applies on AC/battery
    /// switches. "" when PowerDevil does not tell.
    Q_PROPERTY(QString configuredProfile READ configuredProfile NOTIFY configuredProfileChanged)

public:
    explicit PowerProfiles(QObject *parent = nullptr);
    ~PowerProfiles() override;

    bool available() const;
    QString errorText() const;
    QString backendService() const;
    QStringList profiles() const;
    QString activeProfile() const;
    QString performanceDegraded() const;
    QStringList degradedReasons() const;
    QVariantList holds() const;
    bool hasBatteryAware() const;
    bool batteryAware() const;
    QString configuredProfile() const;

    /// Properties.Set(ActiveProfile, @p profile). polkit switch-profile is
    /// allow_active=yes, so no prompt. A manual set cancels holds.
    /// STATE-CHANGING: only call on user action.
    Q_INVOKABLE void setActiveProfile(const QString &profile);

    /// Re-reads all properties (GetAll). Read-only.
    Q_INVOKABLE void refresh();

Q_SIGNALS:
    void availableChanged();
    void errorTextChanged();
    void profilesChanged();
    void activeProfileChanged();
    void performanceDegradedChanged();
    void holdsChanged();
    void configuredProfileChanged();

private:
    // Everything read from the daemon; compared as a whole so each change
    // emits exactly the signals it affects.
    struct State {
        QString service;
        bool loaded = false; // GetAll answered for `service`
        bool placeholder = false;
        QStringList profiles;
        QString activeProfile;
        QString performanceDegraded;
        QVariantList holds;
        bool hasBatteryAware = false;
        bool batteryAware = false;
    };

    void selectService();
    void fetchAll();
    void applyProperties(const QVariantMap &props, State &state) const;
    void setState(const State &state);
    void fetchConfiguredProfile();
    void onPowerManagementDown();
    void setConfiguredProfile(const QString &profile);
    void setErrorText(const QString &text);
    QString path() const;
    QString interface() const;

    State m_state;
    bool m_available = false;
    QString m_errorText;
    QString m_configuredProfile;
    // Bumped whenever the daemon in use changes (switch, loss, restart) so
    // replies from an earlier instance are dropped.
    quint64 m_generation = 0;
    quint64 m_pmGeneration = 0;
    std::unique_ptr<MC::ServiceWatch> m_daemonWatch;
    std::unique_ptr<MC::ServiceWatch> m_pmWatch;
    QObject *m_daemonRelay = nullptr;
    QObject *m_pmRelay = nullptr;
};
