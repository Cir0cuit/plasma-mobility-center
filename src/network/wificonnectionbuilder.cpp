// SPDX-License-Identifier: GPL-2.0-or-later
//
// See wificonnectionbuilder.h. Mirrors plasma-nm's
// Handler::addAndActivateConnection for the non-enterprise cases and
// UiUtils::setConnectionDefaultPermissions for secret ownership.

#include "wificonnectionbuilder.h"

#include <NetworkManagerQt/ConnectionSettings>
#include <NetworkManagerQt/Setting>
#include <NetworkManagerQt/Utils>
#include <NetworkManagerQt/WirelessSecuritySetting>
#include <NetworkManagerQt/WirelessSetting>

using namespace Qt::StringLiterals;

namespace WifiConnectionBuilder
{

bool isEnterprise(int securityType)
{
    switch (static_cast<NetworkManager::WirelessSecurityType>(securityType)) {
    case NetworkManager::NoneSecurity:
    case NetworkManager::StaticWep:
    case NetworkManager::WpaPsk:
    case NetworkManager::Wpa2Psk:
    case NetworkManager::SAE:
    case NetworkManager::OWE:
        return false;
    case NetworkManager::UnknownSecurity:
    case NetworkManager::DynamicWep:
    case NetworkManager::Leap:
    case NetworkManager::WpaEap:
    case NetworkManager::Wpa2Eap:
    case NetworkManager::Wpa3SuiteB192:
        return true;
    }
    return true;
}

bool needsPassword(int securityType)
{
    const auto type = static_cast<NetworkManager::WirelessSecurityType>(securityType);
    return type != NetworkManager::NoneSecurity && type != NetworkManager::OWE;
}

NMVariantMapMap build(const Params &params)
{
    if (params.ssid.isEmpty() || isEnterprise(params.securityType)) {
        return {};
    }
    const auto type = static_cast<NetworkManager::WirelessSecurityType>(params.securityType);

    NetworkManager::ConnectionSettings::Ptr settings(new NetworkManager::ConnectionSettings(NetworkManager::ConnectionSettings::Wireless));
    settings->setId(params.ssid);
    settings->setUuid(NetworkManager::ConnectionSettings::createNewUuid());
    settings->setAutoconnect(true);

    auto wifi = settings->setting(NetworkManager::Setting::Wireless).dynamicCast<NetworkManager::WirelessSetting>();
    auto security = settings->setting(NetworkManager::Setting::WirelessSecurity).dynamicCast<NetworkManager::WirelessSecuritySetting>();
    if (!wifi || !security) {
        return {};
    }

    // Secret ownership: system-owned (stored by NM, no permissions) or
    // owned by the user's secret agent (plasma-nm's kded + KWallet).
    const NetworkManager::Setting::SecretFlags flags =
        params.systemWide ? NetworkManager::Setting::SecretFlags(NetworkManager::Setting::None) : NetworkManager::Setting::AgentOwned;
    if (!params.systemWide && !params.loginName.isEmpty()) {
        settings->addToPermissions(params.loginName, QString());
    }
    security->setPskFlags(flags);
    security->setWepKeyFlags(flags);
    security->setLeapPasswordFlags(flags);

    // Without setInitialized(true) NM silently drops the group.
    wifi->setInitialized(true);
    wifi->setSsid(params.ssid.toUtf8());
    if (params.adhoc) {
        wifi->setMode(NetworkManager::WirelessSetting::Adhoc);
    }

    if (type != NetworkManager::NoneSecurity) {
        security->setInitialized(true);
        wifi->setSecurity(u"802-11-wireless-security"_s);

        switch (type) {
        case NetworkManager::StaticWep:
            security->setKeyMgmt(NetworkManager::WirelessSecuritySetting::Wep);
            security->setWepKey0(params.password);
            break;
        case NetworkManager::OWE:
            security->setKeyMgmt(NetworkManager::WirelessSecuritySetting::OWE);
            break;
        case NetworkManager::SAE:
            security->setKeyMgmt(params.adhoc ? NetworkManager::WirelessSecuritySetting::WpaNone : NetworkManager::WirelessSecuritySetting::SAE);
            security->setPsk(params.password);
            break;
        default: // WpaPsk, Wpa2Psk
            security->setKeyMgmt(params.adhoc ? NetworkManager::WirelessSecuritySetting::WpaNone : NetworkManager::WirelessSecuritySetting::WpaPsk);
            security->setPsk(params.password);
            break;
        }
    }

    return settings->toMap();
}

} // namespace WifiConnectionBuilder
