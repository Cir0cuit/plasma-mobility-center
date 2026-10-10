// SPDX-License-Identifier: GPL-2.0-or-later
//
// NightLight - the Night Light chip of the Display tile.
//
// Source: session bus org.kde.KWin.NightLight /org/kde/KWin/NightLight,
// interface org.kde.KWin.NightLight: GetAll + PropertiesChanged; methods
// inhibit() -> u, uninhibit(u).
//
// Semantics of the chip:
//   checked = running && !inhibited.
//   Turning it OFF while Night Light is enabled => we take a user inhibition.
//   Turning it ON:
//     * if we hold the user inhibition => release it;
//     * if Night Light is disabled in its settings => depending on
//       enableMode, either write kwinrc [NightColor] Active=true with
//       KConfig::Notify (honouring isEntryImmutable("Active")) - "config",
//       the default - or emit kcmRequested("kcm_nightlight") - "kcm".
//
// Inhibitions are sent on a PRIVATE bus connection (MC::PrivateBus,
// "mobilitycenter-nl-<this>"), so widget removal or a plasmashell crash
// releases them; KWin drops cookies when their sender disconnects. (The
// design said "the same connection as presentation mode"; one private
// connection per object gives the same release guarantee without coupling.)
// Drop all cookies when KWin unregisters.
//
// Never interpret `mode` without a version check (0..3 before Plasma 6.5,
// 0..1 from 6.5) - it is exposed raw only.

#pragma once

#include <QObject>
#include <QQmlEngine>

#include <memory>
#include <optional>

namespace MC
{
class PrivateBus;
class ServiceWatch;
}

class NightLight : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    /// KWin's Night Light D-Bus interface is reachable (KWin registered and
    /// GetAll answered with "available" == true).
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    /// Last error worth showing (e.g. kwinrc immutable, inhibit failed).
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)
    /// Night Light is enabled in its settings (D-Bus "enabled").
    Q_PROPERTY(bool enabled READ enabled NOTIFY stateChanged)
    /// Night Light is running (D-Bus "running").
    Q_PROPERTY(bool running READ running NOTIFY stateChanged)
    /// Someone (we or another client) inhibits it (D-Bus "inhibited").
    Q_PROPERTY(bool inhibited READ inhibited NOTIFY stateChanged)
    /// What the chip shows: running && !inhibited.
    Q_PROPERTY(bool checked READ checked NOTIFY stateChanged)
    /// Current colour temperature in Kelvin (6500 = neutral); 0 unknown.
    Q_PROPERTY(int currentTemperature READ currentTemperature NOTIFY temperatureChanged)
    /// Target colour temperature in Kelvin; 0 unknown.
    Q_PROPERTY(int targetTemperature READ targetTemperature NOTIFY temperatureChanged)
    /// checked && currentTemperature == 6500: the subtitle says
    /// "active (no tint during daytime)".
    Q_PROPERTY(bool neutral READ neutral NOTIFY temperatureChanged)
    /// Raw D-Bus "mode" (meaning changed in Plasma 6.5; do not interpret).
    Q_PROPERTY(int mode READ mode NOTIFY stateChanged)
    /// We hold the chip's own (user) inhibition cookie.
    Q_PROPERTY(bool userInhibited READ userInhibited NOTIFY inhibitionsChanged)
    /// INPUT, written by PresentationMode only (QML must not bind it): hold a
    /// separate inhibition while presentation mode is on. Independent of the
    /// user inhibition; Night Light comes back only when both are released.
    /// The stored value changes at once (so
    /// PresentationMode tests can assert it); the KWin call follows async.
    /// Setting it while !available is remembered and applied when KWin
    /// appears.
    Q_PROPERTY(bool presentationInhibited READ presentationInhibited WRITE setPresentationInhibited NOTIFY inhibitionsChanged)
    /// INPUT from config nightLightEnableMode: "config" (default; write
    /// kwinrc directly) or "kcm" (open the Night Light settings page).
    Q_PROPERTY(QString enableMode READ enableMode WRITE setEnableMode NOTIFY enableModeChanged)
    /// Enabling directly is possible: enableMode == "config" and kwinrc
    /// [NightColor] Active is not immutable (Kiosk).
    Q_PROPERTY(bool canEnableDirectly READ canEnableDirectly NOTIFY enableModeChanged)

public:
    explicit NightLight(QObject *parent = nullptr);
    ~NightLight() override;

    bool available() const;
    QString errorText() const;
    bool enabled() const;
    bool running() const;
    bool inhibited() const;
    bool checked() const;
    int currentTemperature() const;
    int targetTemperature() const;
    bool neutral() const;
    int mode() const;
    bool userInhibited() const;
    bool presentationInhibited() const;
    void setPresentationInhibited(bool inhibit);
    QString enableMode() const;
    void setEnableMode(const QString &mode);
    bool canEnableDirectly() const;

    /// The chip was toggled to @p on - see the class comment for the rules.
    /// setChecked(false) while presentationInhibited only takes the user
    /// inhibition (the chip then stays off after the presentation ends);
    /// setChecked(true) releases the user inhibition but never the
    /// presentation one. STATE-CHANGING: only call on user action.
    Q_INVOKABLE void setChecked(bool on);

    /// Re-reads all properties (GetAll). Read-only.
    Q_INVOKABLE void refresh();

Q_SIGNALS:
    void availableChanged();
    void errorTextChanged();
    void stateChanged();
    void temperatureChanged();
    void inhibitionsChanged();
    void enableModeChanged();
    /// setChecked(true) with Night Light disabled and enableMode "kcm" (or
    /// kwinrc immutable): QML opens @p kcm via KCMLauncher.
    void kcmRequested(const QString &kcm);

private:
    // One KWin inhibition we may hold. `wanted` is the request; `cookie` is
    // set once KWin answered; `pending` while an inhibit() call is in flight.
    struct Inhibition {
        bool wanted = false;
        bool pending = false;
        std::optional<uint> cookie;
    };

    void onServiceUp();
    void onServiceDown();
    void fetchAll();
    void applyProperties(const QVariantMap &props);
    void syncInhibition(Inhibition &inhibition);
    void dropCookies();
    void enableNightLight();
    void updateKioskState();
    void setAvailable(bool available);
    void setErrorText(const QString &text);
    bool kwinReady() const;

    bool m_available = false;
    bool m_serviceUp = false;
    bool m_enabled = false;
    bool m_running = false;
    bool m_inhibited = false;
    int m_mode = 0;
    int m_currentTemperature = 0;
    int m_targetTemperature = 0;
    bool m_activeImmutable = false;
    QString m_errorText;
    Inhibition m_user;
    Inhibition m_presentation;
    bool m_presentationInhibited = false;
    QString m_enableMode = QStringLiteral("config");
    // Bumped on every KWin (re)registration and loss: replies from an earlier
    // KWin instance are dropped.
    quint64 m_generation = 0;
    std::unique_ptr<MC::ServiceWatch> m_watch;
    QObject *m_relay = nullptr;
    // Context of every call on the private bus; deleted before the bus so no
    // pending-call watcher outlives the connection.
    QObject *m_privateCalls = nullptr;
    std::unique_ptr<MC::PrivateBus> m_privateBus;
};
