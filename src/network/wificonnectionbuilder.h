// SPDX-License-Identifier: GPL-2.0-or-later
//
// WifiConnectionBuilder - pure construction of the settings map for
// NetworkManager::addAndActivateConnection(), unit-testable
// without NetworkManager.
//
//   * ConnectionSettings(Wireless): id = SSID, new UUID, autoconnect on;
//   * WirelessSetting: setInitialized(true), setSsid, and when secured
//     setSecurity("802-11-wireless-security");
//   * WirelessSecuritySetting (secured only): setInitialized(true),
//     setKeyMgmt WpaPsk | SAE | Wep | OWE, setPsk or setWepKey0;
//   * ad-hoc networks: Adhoc mode + WpaNone;
//   * without setInitialized(true) NM silently drops the groups;
//   * secret ownership (plasma-nm UiUtils::setConnectionDefaultPermissions):
//     systemWide => no permissions, secret flags None;
//     else addToPermissions(loginName) and AgentOwned.
// Private helper of WifiBackend (not a QML type).

#pragma once

#include <NetworkManagerQt/GenericTypes>

#include <QString>

namespace WifiConnectionBuilder
{

struct Params {
    QString ssid; ///< raw SSID
    QString password; ///< PSK / SAE password / WEP key; empty for open and OWE
    int securityType = 0; ///< WifiNetworkModel::SecurityType
    bool adhoc = false; ///< the network is an ad-hoc (IBSS) network
    bool systemWide = true; ///< system-owned connection (see header comment)
    QString loginName; ///< user to add to permissions when !systemWide
};

/// Builds the connection map; empty for unsupported (enterprise) security.
NMVariantMapMap build(const Params &params);

/// EAP, LEAP, Dynamic WEP, Suite-B - and Unknown (no security the device
/// supports): hand off to kcm_networkmanagement.
bool isEnterprise(int securityType);

/// The user has to type a password (not open, not OWE).
bool needsPassword(int securityType);

} // namespace WifiConnectionBuilder
