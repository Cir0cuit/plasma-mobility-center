// SPDX-License-Identifier: GPL-2.0-or-later
//
// PresentationMode - keep the laptop awake and quiet while presenting
//.
//
// While `active`, on a PRIVATE session bus connection (MC::PrivateBus,
// "mobilitycenter-pm-<this>"; all calls async):
//   1. org.freedesktop.PowerManagement.Inhibit Inhibit(app, reason) -> u
//   2. org.freedesktop.ScreenSaver /ScreenSaver Inhibit(app, reason) -> u
//      (no auto-lock; forwarded to PowerDevil: no dimming, no DPMS)
//   3. org.freedesktop.Notifications Inhibit(desktop_entry, reason, {}) -> u
//      (Do Not Disturb; desktop_entry must be non-empty)
//   4. nightLight->setPresentationInhibited(true), if nightLight is set and
//      inhibitNightLight is true (decision: presentation mode also suspends
//      Night Light).
// Same stable (app, reason) strings for 1 and 2, so one "block" choice in the
// battery applet covers both.
//
// Release: active = false => UnInhibit all + Night Light hook off;
// destruction => the private bus closes (daemons drop the cookies) AND the
// destructor calls nightLight->setPresentationInhibited(false) if the hook is
// on (the NightLight object can outlive us). The QML side sets active = false
// on Plasmoid.destroyedChanged(true) and when the tile is hidden. `active` is
// never persisted.
//
// Night Light hook while active: reassigning `nightLight` moves the hook
// (old object released, new one inhibited); switching `inhibitNightLight`
// applies at once. nightLightInhibited mirrors whether the hook is on. The
// hook only calls NightLight's public setter; NightLight owns the actual
// KWin inhibition (and its own private bus), so either class can be
// implemented and tested without the other.
//
// Re-registration while active: PowerDevil restart => re-issue the
// PowerManagement cookie AND UnInhibit+Inhibit the ScreenSaver cookie; KWin
// restart => re-issue the ScreenSaver cookie.
//
// User blocking: Plasma >= 6.5 PolicyAgent RequestedInhibitions
// ((flags & 0x2) == 0 for our (who, why) = blocked); 6.3/6.4
// List{Permanently,Temporarily}BlockedInhibitions + signals. Ignore the first
// ~5 s after inhibiting (PowerDevil delays enforcement).

#pragma once

#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QStringList>

#include <array>
#include <memory>

#include "nightlight.h"

class QDBusConnection;
class QTimer;

namespace MC
{
class PrivateBus;
class ServiceWatch;
}

class PresentationMode : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    /// Presentation mode is on. Writing true inhibits, false releases.
    /// Becomes true as soon as the request is made (the tile's switch shows
    /// it at once); sleepBlocked/lockBlocked/dndActive report what the
    /// daemons actually granted. Writing true while !available is ignored.
    /// STATE-CHANGING: only write on user action (or false on teardown).
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    /// org.freedesktop.ScreenSaver or org.freedesktop.PowerManagement.Inhibit
    /// is registered - the tile shows.
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    /// Last error worth showing (an Inhibit call failed); empty if none.
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)
    /// We hold the PowerManagement cookie (automatic sleep is blocked).
    Q_PROPERTY(bool sleepBlocked READ sleepBlocked NOTIFY inhibitionsChanged)
    /// We hold the ScreenSaver cookie (auto-lock and dimming are blocked).
    Q_PROPERTY(bool lockBlocked READ lockBlocked NOTIFY inhibitionsChanged)
    /// Do Not Disturb can be used: Notifications GetServerInformation() name
    /// is "Plasma".
    Q_PROPERTY(bool dndAvailable READ dndAvailable NOTIFY dndChanged)
    /// Notifications are inhibited right now (PropertiesChanged{Inhibited}).
    Q_PROPERTY(bool dndActive READ dndActive NOTIFY dndChanged)
    /// While active, the user switched DND off elsewhere (Inhibited went
    /// true -> false with the server still there). Shown as "DND overridden";
    /// we do not re-inhibit. Reset when active goes false.
    Q_PROPERTY(bool dndRevokedByUser READ dndRevokedByUser NOTIFY dndChanged)
    /// The user blocked our inhibition in the battery applet / power settings.
    Q_PROPERTY(bool blockedByUser READ blockedByUser NOTIFY blockedByUserChanged)
    /// Night Light is currently suspended on our behalf.
    Q_PROPERTY(bool nightLightInhibited READ nightLightInhibited NOTIFY inhibitionsChanged)
    /// INPUT from QML: the shared NightLight object to suspend while active
    /// (may be null).
    Q_PROPERTY(NightLight *nightLight READ nightLight WRITE setNightLight NOTIFY nightLightChanged)
    /// INPUT: also suspend Night Light while active. Default true.
    Q_PROPERTY(bool inhibitNightLight READ inhibitNightLight WRITE setInhibitNightLight NOTIFY nightLightChanged)

public:
    explicit PresentationMode(QObject *parent = nullptr);
    ~PresentationMode() override;

    bool active() const;
    void setActive(bool active);
    bool available() const;
    QString errorText() const;
    bool sleepBlocked() const;
    bool lockBlocked() const;
    bool dndAvailable() const;
    bool dndActive() const;
    bool dndRevokedByUser() const;
    bool blockedByUser() const;
    bool nightLightInhibited() const;
    NightLight *nightLight() const;
    void setNightLight(NightLight *nightLight);
    bool inhibitNightLight() const;
    void setInhibitNightLight(bool inhibit);

    /// Re-checks service availability, DND support and user blocks. Read-only.
    Q_INVOKABLE void refresh();

Q_SIGNALS:
    void activeChanged();
    void availableChanged();
    void errorTextChanged();
    void inhibitionsChanged();
    void dndChanged();
    void blockedByUserChanged();
    void nightLightChanged();

private:
    // The three inhibitions we hold while active.
    enum Kind {
        PowerKind, // org.freedesktop.PowerManagement.Inhibit
        ScreenSaverKind, // org.freedesktop.ScreenSaver
        NotificationsKind, // org.freedesktop.Notifications (DND)
        KindCount,
    };
    struct Inhibition {
        // Bumped on every request and release: a reply to an older request
        // is released at once instead of being stored.
        quint64 request = 0;
        // Bumped whenever the service (un)registers: cookies of an earlier
        // instance are meaningless and must never be sent to the new one.
        quint64 instance = 0;
        bool pending = false;
        bool held = false;
        uint cookie = 0;
    };

    void onServiceChanged(const QString &service, bool registered);
    bool isRegistered(Kind kind) const;
    void inhibit(Kind kind);
    void uninhibit(Kind kind);
    void sendUninhibit(Kind kind, uint cookie);
    void onPowerDevilRestarted();
    void fetchDndState();
    void setDndActive(bool active);
    void fetchBlockedState();
    void applyBlockedLists(const QVariantList &requested, const QVariantList &permanent, const QVariantList &temporary);
    void updateBlockedByUser();
    void applyNightLightHook();
    void updateAvailable();
    void setErrorText(const QString &text);
    QDBusConnection privateBus();

    QPointer<NightLight> m_nightLight;
    bool m_inhibitNightLight = true;

    bool m_active = false;
    bool m_available = false;
    QString m_errorText;
    std::array<Inhibition, KindCount> m_inhibitions;

    bool m_dndAvailable = false;
    bool m_dndActive = false;
    bool m_dndRevokedByUser = false;
    quint64 m_notificationsGeneration = 0;

    // PolicyAgent: 6.5+ RequestedInhibitions, or the 6.3/6.4 blocked lists.
    bool m_agentLegacy = false;
    bool m_rawBlocked = false;
    bool m_blockedByUser = false;
    quint64 m_agentGeneration = 0;
    QTimer *m_blockGrace = nullptr;

    // Debounces the two names PowerDevil registers on a restart.
    QTimer *m_powerDevilRestart = nullptr;

    // The NightLight object we currently hold presentationInhibited on.
    QPointer<NightLight> m_nightLightHook;

    std::unique_ptr<MC::ServiceWatch> m_watch;
    QList<QObject *> m_notificationRelays;
    QList<QObject *> m_agentRelays;
    std::unique_ptr<MC::PrivateBus> m_bus;
};
