// SPDX-License-Identifier: GPL-2.0-or-later
//
// See wifinetworkmodel.h. Filled by WifiBackend through setNetworks().

#include "wifinetworkmodel.h"

#include "wificonnectionbuilder.h"

WifiNetworkModel::WifiNetworkModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

WifiNetworkModel::~WifiNetworkModel() = default;

int WifiNetworkModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_networks.size();
}

QVariant WifiNetworkModel::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid)) {
        return {};
    }
    const Network &n = m_networks.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case DisplaySsidRole:
        return n.displaySsid;
    case SsidRole:
        return n.ssid;
    case SignalRole:
        return n.signal;
    case SecurityTypeRole:
        return n.securityType;
    case SecuredRole:
        return WifiConnectionBuilder::needsPassword(n.securityType);
    case EnterpriseRole:
        return WifiConnectionBuilder::isEnterprise(n.securityType);
    case SavedRole:
        return n.saved;
    case ConnectionUuidRole:
        return n.connectionUuid;
    case ActiveRole:
        return n.active;
    case ActivatingRole:
        return n.activating;
    case FrequencyRole:
        return n.frequency;
    case DeviceUniRole:
        return n.deviceUni;
    }
    return {};
}

QHash<int, QByteArray> WifiNetworkModel::roleNames() const
{
    return {
        {SsidRole, "ssid"},
        {DisplaySsidRole, "displaySsid"},
        {SignalRole, "signal"},
        {SecurityTypeRole, "securityType"},
        {SecuredRole, "secured"},
        {EnterpriseRole, "enterprise"},
        {SavedRole, "saved"},
        {ConnectionUuidRole, "connectionUuid"},
        {ActiveRole, "active"},
        {ActivatingRole, "activating"},
        {FrequencyRole, "frequency"},
        {DeviceUniRole, "deviceUni"},
    };
}

int WifiNetworkModel::count() const
{
    return rowCount();
}

QVariantMap WifiNetworkModel::get(int row) const
{
    QVariantMap map;
    if (row < 0 || row >= rowCount()) {
        return map;
    }
    const QModelIndex idx = index(row, 0);
    const auto roles = roleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it) {
        map.insert(QString::fromUtf8(it.value()), data(idx, it.key()));
    }
    return map;
}

bool WifiNetworkModel::lessThan(const Network &a, const Network &b)
{
    if (a.active != b.active) {
        return a.active;
    }
    if (a.saved != b.saved) {
        return a.saved;
    }
    if (a.signal != b.signal) {
        return a.signal > b.signal;
    }
    const int byName = QString::localeAwareCompare(a.displaySsid, b.displaySsid);
    if (byName != 0) {
        return byName < 0;
    }
    return a.deviceUni < b.deviceUni;
}

int WifiNetworkModel::indexOf(const QString &ssid, const QString &deviceUni, int from) const
{
    for (int i = from; i < m_networks.size(); ++i) {
        if (m_networks.at(i).ssid == ssid && m_networks.at(i).deviceUni == deviceUni) {
            return i;
        }
    }
    return -1;
}

void WifiNetworkModel::setNetworks(const QList<Network> &networks)
{
    const int oldCount = m_networks.size();

    // 1. Drop rows that are gone (bottom up).
    for (int i = m_networks.size() - 1; i >= 0; --i) {
        const Network &old = m_networks.at(i);
        const bool kept = std::any_of(networks.cbegin(), networks.cend(), [&old](const Network &n) {
            return n.ssid == old.ssid && n.deviceUni == old.deviceUni;
        });
        if (!kept) {
            beginRemoveRows({}, i, i);
            m_networks.removeAt(i);
            endRemoveRows();
        }
    }

    // 2. Walk the new order: update in place, move up, or insert.
    for (int i = 0; i < networks.size(); ++i) {
        const Network &n = networks.at(i);
        const int at = indexOf(n.ssid, n.deviceUni, i);
        if (at < 0) {
            beginInsertRows({}, i, i);
            m_networks.insert(i, n);
            endInsertRows();
            continue;
        }
        if (at != i) {
            beginMoveRows({}, at, at, {}, i);
            m_networks.move(at, i);
            endMoveRows();
        }
        if (!(m_networks.at(i) == n)) {
            m_networks[i] = n;
            const QModelIndex idx = index(i, 0);
            Q_EMIT dataChanged(idx, idx);
        }
    }

    if (m_networks.size() != oldCount) {
        Q_EMIT countChanged();
    }
}

#include <NetworkManagerQt/Utils>

// SecurityType must stay value-compatible with NetworkManagerQt.
static_assert(int(WifiNetworkModel::UnknownSecurity) == int(NetworkManager::UnknownSecurity));
static_assert(int(WifiNetworkModel::NoSecurity) == int(NetworkManager::NoneSecurity));
static_assert(int(WifiNetworkModel::StaticWep) == int(NetworkManager::StaticWep));
static_assert(int(WifiNetworkModel::DynamicWep) == int(NetworkManager::DynamicWep));
static_assert(int(WifiNetworkModel::Leap) == int(NetworkManager::Leap));
static_assert(int(WifiNetworkModel::WpaPsk) == int(NetworkManager::WpaPsk));
static_assert(int(WifiNetworkModel::WpaEap) == int(NetworkManager::WpaEap));
static_assert(int(WifiNetworkModel::Wpa2Psk) == int(NetworkManager::Wpa2Psk));
static_assert(int(WifiNetworkModel::Wpa2Eap) == int(NetworkManager::Wpa2Eap));
static_assert(int(WifiNetworkModel::SAE) == int(NetworkManager::SAE));
static_assert(int(WifiNetworkModel::Wpa3SuiteB192) == int(NetworkManager::Wpa3SuiteB192));
static_assert(int(WifiNetworkModel::OWE) == int(NetworkManager::OWE));
