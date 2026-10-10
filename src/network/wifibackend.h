// SPDX-License-Identifier: GPL-2.0-or-later
//
// WifiBackend - Wi-Fi state, network list and connect/forget, plus the
// NetworkManager radio switches airplane mode uses.
//
// Library: KF6::NetworkManagerQt (KF >= 6.10; do not use
// AccessPoint::bandwidth(), 6.12+). Matches the stock network applet:
//   * new connections are system-wide when allowed (plasma-nm 6.6+ default;
//     PlasmaNmBridge::systemConnectionsByDefault + permissions
//     "org.freedesktop.NetworkManager.settings.modify.system" == "yes"),
//     otherwise per user with agent-owned secrets;
//   * disconnectNetwork() = Device::disconnectInterface() (blocks autoconnect
//     until the user connects again, like plasma-nm);
//   * saved networks that are out of range are not listed.
// NM also lists the Bluetooth adapter as a device: filter on Device::Wifi.
// A kernel rfkill soft-block reads as wirelessEnabled == false while
// hardwareEnabled stays true.
//
// Secrets: the password passed to connectNew() is never logged or stored by
// us; the QML field is cleared when the card collapses. Wrong-password
// re-prompts fall through to plasma-nm's kded dialog.

#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QMetaObject>
#include <QObject>
#include <QQmlEngine>
#include <QTimer>
#include <QVariantMap>

#include "wifinetworkmodel.h"

class WifiBackend : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    /// NetworkManager is running (status() != Unknown) and at least one
    /// Device::Wifi exists.
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    /// Last error worth showing (failed activation, ...); empty if none.
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)
    /// NetworkManager is running at all (for airplane mode's WWAN half).
    Q_PROPERTY(bool networkManagerRunning READ networkManagerRunning NOTIFY availableChanged)
    /// Wi-Fi radio switch (NM WirelessEnabled; also the kernel soft-block).
    /// Writable - STATE-CHANGING: only write on user action.
    Q_PROPERTY(bool wirelessEnabled READ wirelessEnabled WRITE setWirelessEnabled NOTIFY wirelessEnabledChanged)
    /// Hardware (rfkill hard-block) switch allows Wi-Fi.
    Q_PROPERTY(bool hardwareEnabled READ hardwareEnabled NOTIFY wirelessEnabledChanged)
    /// Mobile broadband radio switch (NM WwanEnabled). Writable -
    /// STATE-CHANGING: only write on user action.
    Q_PROPERTY(bool wwanEnabled READ wwanEnabled WRITE setWwanEnabled NOTIFY wwanChanged)
    /// A modem device exists.
    Q_PROPERTY(bool wwanAvailable READ wwanAvailable NOTIFY wwanChanged)
    /// Hardware switch allows WWAN.
    Q_PROPERTY(bool wwanHardwareEnabled READ wwanHardwareEnabled NOTIFY wwanChanged)
    /// A scan is in progress (driven by lastScanChanged).
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    /// INPUT from QML: the Wireless card is expanded and visible. While true
    /// and Wi-Fi is on, requestScan() is called periodically (client throttle
    /// 10 s). Setting it false stops scanning.
    Q_PROPERTY(bool scanActive READ scanActive WRITE setScanActive NOTIFY scanActiveChanged)
    /// SSID of the active Wi-Fi connection ("" when none).
    Q_PROPERTY(QString activeSsid READ activeSsid NOTIFY activeConnectionChanged)
    /// Signal of the active access point, 0..100; 0 when none.
    Q_PROPERTY(int activeSignal READ activeSignal NOTIFY activeConnectionChanged)
    /// An activation is in progress (NM ActiveConnection state Activating).
    Q_PROPERTY(bool activating READ activating NOTIFY activeConnectionChanged)
    /// SSID being activated ("" when none).
    Q_PROPERTY(QString activatingSsid READ activatingSsid NOTIFY activeConnectionChanged)
    /// Visible networks - see WifiNetworkModel.
    Q_PROPERTY(WifiNetworkModel *networks READ networks CONSTANT)
    /// NetworkManager::permissions(): permission id -> "yes" | "no" | "auth".
    Q_PROPERTY(QVariantMap permissions READ permissions NOTIFY permissionsChanged)
    /// The user may toggle Wi-Fi without a prompt
    /// (org.freedesktop.NetworkManager.enable-disable-wifi == "yes").
    Q_PROPERTY(bool canToggleWifi READ canToggleWifi NOTIFY permissionsChanged)

public:
    explicit WifiBackend(QObject *parent = nullptr);
    ~WifiBackend() override;

    bool available() const;
    QString errorText() const;
    bool networkManagerRunning() const;
    bool wirelessEnabled() const;
    void setWirelessEnabled(bool enabled);
    bool hardwareEnabled() const;
    bool wwanEnabled() const;
    void setWwanEnabled(bool enabled);
    bool wwanAvailable() const;
    bool wwanHardwareEnabled() const;
    bool scanning() const;
    bool scanActive() const;
    void setScanActive(bool active);
    QString activeSsid() const;
    int activeSignal() const;
    bool activating() const;
    QString activatingSsid() const;
    WifiNetworkModel *networks() const;
    QVariantMap permissions() const;
    bool canToggleWifi() const;

    /// Requests a scan on every Wi-Fi device. Throttled to once per 10 s;
    /// NotAllowed and auth errors are ignored silently. Only meaningful while
    /// Wi-Fi is on. (A scan is harmless but costs power; the QML side only
    /// triggers it through scanActive.)
    Q_INVOKABLE void requestScan();

    // The following are STATE-CHANGING: only call them on user action.

    /// Activates the saved profile for @p ssid on the device that sees it
    /// (activateConnection(conn, dev, apUni)).
    Q_INVOKABLE void connectSaved(const QString &ssid);
    /// Creates and activates a new profile for @p ssid
    /// (addAndActivateConnection, settings from WifiConnectionBuilder).
    /// @p password may be empty for open/OWE networks.
    Q_INVOKABLE void connectNew(const QString &ssid, const QString &password);
    /// Disconnects the Wi-Fi device (Device::disconnectInterface()).
    Q_INVOKABLE void disconnectNetwork();
    /// Deletes the saved profile of @p ssid (Connection::remove()).
    Q_INVOKABLE void forget(const QString &ssid);

    /// @p ssid uses EAP/LEAP/DynamicWEP/Suite-B: the UI hands off to
    /// kcm_networkmanagement instead of showing a password field.
    Q_INVOKABLE bool needsEnterprise(const QString &ssid) const;
    /// UUID of the saved profile for @p ssid (newest timestamp wins among
    /// duplicates); "" when none. For kcm_networkmanagement
    /// ["--args", "Uuid=" + uuid].
    Q_INVOKABLE QString savedConnectionUuid(const QString &ssid) const;

    /// Re-reads devices, radio state and the network list. Read-only.
    Q_INVOKABLE void refresh();

Q_SIGNALS:
    void availableChanged();
    void errorTextChanged();
    void wirelessEnabledChanged();
    void wwanChanged();
    void scanningChanged();
    void scanActiveChanged();
    void activeConnectionChanged();
    void permissionsChanged();
    /// An activation for @p ssid failed; @p reason is a human readable
    /// NetworkManager reason (also put into errorText).
    void connectionFailed(const QString &ssid, const QString &reason);

private:
    struct SavedProfile {
        QString uuid;
        QString path;
        qint64 timestamp = 0;
        int securityType = -1;
    };

    void onServiceAppeared();
    void onServiceDisappeared();
    void reloadDevices();
    void scheduleRebuild();
    void rebuild();
    void reloadSaved();
    void fetchPermissions();
    void updateAvailable();
    void updateScanTimer();
    void setScanning(bool scanning);
    void setErrorText(const QString &text);
    void setRadio(const QString &property, bool enabled);
    void reportFailure(const QString &ssid, const QString &reason);
    /// Newest saved profile for @p rawSsid; nullptr if none.
    const SavedProfile *savedFor(const QByteArray &rawSsid) const;
    /// Row index in m_networks->m_networks for @p ssid (best signal wins).
    int rowFor(const QString &ssid) const;

    WifiNetworkModel *m_networks = nullptr;
    bool m_scanActive = false;

    bool m_available = false;
    bool m_nmRunning = false;
    QString m_errorText;
    QVariantMap m_permissions;
    bool m_scanning = false;
    QString m_activeSsid;
    int m_activeSignal = 0;
    bool m_activating = false;
    QString m_activatingSsid;

    QStringList m_wifiDevices; ///< unis of Device::Wifi devices
    QHash<QByteArray, QList<SavedProfile>> m_saved; ///< raw SSID -> profiles, newest first
    QHash<QString, QString> m_activatingByDevice; ///< device uni -> SSID being activated

    QList<QMetaObject::Connection> m_deviceConnections;
    QList<QMetaObject::Connection> m_dynamicConnections;
    QHash<QString, QMetaObject::Connection> m_profileConnections;

    QTimer m_rebuildTimer;
    QTimer m_scanTimer;
    QTimer m_scanningTimeout;
    QHash<QString, QElapsedTimer> m_lastScanRequest; ///< device uni -> last RequestScan
};
