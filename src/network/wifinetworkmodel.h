// SPDX-License-Identifier: GPL-2.0-or-later
//
// WifiNetworkModel - one row per visible Wi-Fi network (SSID per device),
// owned and filled by WifiBackend.
//
// Rebuilt on networkAppeared/Disappeared, signalStrengthChanged,
// activeAccessPointChanged, Settings::notifier() connectionAdded/Removed and
// activeConnectionsChanged. Sorted: active first, then saved, then by signal
// (descending), then by SSID. Saved networks out of range are not listed.

#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QQmlEngine>
#include <QString>

class WifiBackend;

class WifiNetworkModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by WifiBackend.networks")

    /// Number of rows.
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    /// Mirrors NetworkManager::WirelessSecurityType (findBestWirelessSecurity).
    enum SecurityType {
        UnknownSecurity = -1,
        NoSecurity = 0,
        StaticWep,
        DynamicWep, ///< enterprise hand-off
        Leap, ///< enterprise hand-off
        WpaPsk,
        WpaEap, ///< enterprise hand-off
        Wpa2Psk,
        Wpa2Eap, ///< enterprise hand-off
        SAE, ///< WPA3 personal
        Wpa3SuiteB192, ///< enterprise hand-off
        OWE, ///< enhanced open: no password, encrypted
    };
    Q_ENUM(SecurityType)

    enum Roles {
        SsidRole = Qt::UserRole + 1, ///< "ssid": QString, raw SSID (what the invokables take)
        DisplaySsidRole, ///< "displaySsid": QString, printable form (fallback for non-UTF-8)
        SignalRole, ///< "signal": int 0..100
        SecurityTypeRole, ///< "securityType": int SecurityType
        SecuredRole, ///< "secured": bool, needs a password or enterprise credentials
        EnterpriseRole, ///< "enterprise": bool, hand off to kcm_networkmanagement
        SavedRole, ///< "saved": bool, a matching profile exists
        ConnectionUuidRole, ///< "connectionUuid": QString, the saved profile's UUID or ""
        ActiveRole, ///< "active": bool, currently connected
        ActivatingRole, ///< "activating": bool, connection in progress
        FrequencyRole, ///< "frequency": int MHz of the best AP (2412.., 5180.., 5955..); 0 unknown
        DeviceUniRole, ///< "deviceUni": QString, NM device path that sees the network
    };
    Q_ENUM(Roles)

    explicit WifiNetworkModel(QObject *parent = nullptr);
    ~WifiNetworkModel() override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const;

    /// Row @p row as a map keyed by role name; {} if out of range.
    Q_INVOKABLE QVariantMap get(int row) const;

Q_SIGNALS:
    void countChanged();

private:
    friend class WifiBackend;

    struct Network {
        QString ssid;
        QString displaySsid;
        int signal = 0;
        int securityType = UnknownSecurity;
        bool saved = false;
        QString connectionUuid;
        bool active = false;
        bool activating = false;
        int frequency = 0;
        QString deviceUni;
        // Not exposed as roles; used by WifiBackend.
        QByteArray rawSsid;
        QString apUni; ///< reference access point
        bool adhoc = false;

        bool operator==(const Network &other) const = default;
    };

    /// Replaces the rows with @p networks (already sorted). Rows are matched
    /// by (ssid, deviceUni) and updated, moved, inserted or removed in place -
    /// never reset - so delegates (an open password field) survive.
    void setNetworks(const QList<Network> &networks);
    /// Sort order: active first, then saved, then signal (desc), then SSID.
    static bool lessThan(const Network &a, const Network &b);
    int indexOf(const QString &ssid, const QString &deviceUni, int from) const;

    QList<Network> m_networks;
};
