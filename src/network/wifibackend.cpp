// SPDX-License-Identifier: GPL-2.0-or-later
//
// See wifibackend.h
//
// NetworkManagerQt keeps a process-wide cache of NetworkManager's state (the
// same one plasma-nm's tray uses inside plasmashell); we read that cache and
// rebuild our derived state, coalesced, whenever one of its signals fires.
// Everything we *send* is asynchronous: NMQt's own pending replies for
// activate / add / disconnect / remove / scan, and our own D-Bus calls for
// the radio switches and permissions (NMQt's versions of those block).

#include "wifibackend.h"

#include <NetworkManagerQt/AccessPoint>
#include <NetworkManagerQt/ActiveConnection>
#include <NetworkManagerQt/Connection>
#include <NetworkManagerQt/ConnectionSettings>
#include <NetworkManagerQt/Device>
#include <NetworkManagerQt/Manager>
#include <NetworkManagerQt/Settings>
#include <NetworkManagerQt/Utils>
#include <NetworkManagerQt/WirelessDevice>
#include <NetworkManagerQt/WirelessNetwork>
#include <NetworkManagerQt/WirelessSetting>

#include <KConfigGroup>
#include <KSharedConfig>

#include <QDBusConnection>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QStringDecoder>

#include <pwd.h>
#include <unistd.h>

#include "dbusasync.h"
#include "wificonnectionbuilder.h"
#include "wifinetworkmodel.h"

using namespace Qt::StringLiterals;

namespace
{
const QString kNmService = u"org.freedesktop.NetworkManager"_s;
const QString kNmPath = u"/org/freedesktop/NetworkManager"_s;
const QString kNmIface = u"org.freedesktop.NetworkManager"_s;
const QString kModifySystem = u"org.freedesktop.NetworkManager.settings.modify.system"_s;

constexpr int kScanIntervalMs = 10000; // plasma-nm's NM_REQUESTSCAN_LIMIT_RATE
constexpr int kScanningTimeoutMs = 15000;
constexpr int kRebuildDelayMs = 50;

/// Printable SSID: the UTF-8 text, or "\xNN" escapes for invalid bytes.
QString printableSsid(const QByteArray &raw, const QString &fallback)
{
    QStringDecoder decoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    const QString text = decoder.decode(raw);
    if (!decoder.hasError() && !raw.isEmpty()) {
        return text;
    }
    if (raw.isEmpty()) {
        return fallback;
    }
    QString out;
    for (const char c : raw) {
        const uchar b = uchar(c);
        if (b >= 0x20 && b < 0x7f) {
            out += QLatin1Char(c);
        } else {
            out += u"\\x%1"_s.arg(b, 2, 16, u'0');
        }
    }
    return out;
}

QString loginName()
{
    if (const passwd *pw = getpwuid(getuid())) {
        return QString::fromLocal8Bit(pw->pw_name);
    }
    return qEnvironmentVariable("USER");
}

/// Short English text for a device failure reason (C++ carries no i18n;
/// the QML shows it as-is under the network list).
QString reasonText(NetworkManager::Device::StateChangeReason reason)
{
    using D = NetworkManager::Device;
    switch (reason) {
    case D::NoSecretsReason:
    case D::AuthSupplicantDisconnectReason:
        return u"The password is wrong or missing"_s;
    case D::AuthSupplicantTimeoutReason:
    case D::AuthSupplicantFailedReason:
    case D::AuthSupplicantConfigFailedReason:
        return u"The network did not accept the connection"_s;
    case D::SsidNotFound:
        return u"The network is out of range"_s;
    case D::DhcpStartFailedReason:
    case D::DhcpErrorReason:
    case D::DhcpFailedReason:
        return u"No IP address was assigned"_s;
    case D::UserRequestedReason:
        return {};
    default:
        return u"The connection failed"_s;
    }
}

NetworkManager::WirelessDevice::Ptr wirelessDevice(const QString &uni)
{
    return NetworkManager::findNetworkInterface(uni).objectCast<NetworkManager::WirelessDevice>();
}

/// SSID of the Wi-Fi connection behind @p ac ("" if none).
QByteArray activeRawSsid(const NetworkManager::ActiveConnection::Ptr &ac)
{
    if (!ac || !ac->connection()) {
        return {};
    }
    const auto settings = ac->connection()->settings();
    if (!settings || settings->connectionType() != NetworkManager::ConnectionSettings::Wireless) {
        return {};
    }
    const auto wifi = settings->setting(NetworkManager::Setting::Wireless).dynamicCast<NetworkManager::WirelessSetting>();
    return wifi ? wifi->ssid() : QByteArray();
}
} // namespace

WifiBackend::WifiBackend(QObject *parent)
    : QObject(parent)
    , m_networks(new WifiNetworkModel(this))
{
    m_rebuildTimer.setSingleShot(true);
    m_rebuildTimer.setInterval(kRebuildDelayMs);
    connect(&m_rebuildTimer, &QTimer::timeout, this, &WifiBackend::rebuild);

    m_scanTimer.setInterval(kScanIntervalMs);
    connect(&m_scanTimer, &QTimer::timeout, this, &WifiBackend::requestScan);

    m_scanningTimeout.setSingleShot(true);
    m_scanningTimeout.setInterval(kScanningTimeoutMs);
    connect(&m_scanningTimeout, &QTimer::timeout, this, [this] {
        setScanning(false);
    });

    auto *nm = NetworkManager::notifier();
    connect(nm, &NetworkManager::Notifier::serviceAppeared, this, &WifiBackend::onServiceAppeared);
    connect(nm, &NetworkManager::Notifier::serviceDisappeared, this, &WifiBackend::onServiceDisappeared);
    connect(nm, &NetworkManager::Notifier::statusChanged, this, &WifiBackend::updateAvailable);
    connect(nm, &NetworkManager::Notifier::deviceAdded, this, &WifiBackend::reloadDevices);
    connect(nm, &NetworkManager::Notifier::deviceRemoved, this, &WifiBackend::reloadDevices);
    connect(nm, &NetworkManager::Notifier::wirelessEnabledChanged, this, [this] {
        Q_EMIT wirelessEnabledChanged();
        updateScanTimer();
        scheduleRebuild();
    });
    connect(nm, &NetworkManager::Notifier::wirelessHardwareEnabledChanged, this, &WifiBackend::wirelessEnabledChanged);
    connect(nm, &NetworkManager::Notifier::wwanEnabledChanged, this, &WifiBackend::wwanChanged);
    connect(nm, &NetworkManager::Notifier::wwanHardwareEnabledChanged, this, &WifiBackend::wwanChanged);
    connect(nm, &NetworkManager::Notifier::activeConnectionsChanged, this, &WifiBackend::scheduleRebuild);

    auto *settings = NetworkManager::settingsNotifier();
    connect(settings, &NetworkManager::SettingsNotifier::connectionAdded, this, &WifiBackend::scheduleRebuild);
    connect(settings, &NetworkManager::SettingsNotifier::connectionRemoved, this, [this](const QString &path) {
        const auto it = m_profileConnections.find(path);
        if (it != m_profileConnections.end()) {
            disconnect(it.value());
            m_profileConnections.erase(it);
        }
        scheduleRebuild();
    });

    // NM announces permission changes (polkit policy, session changes).
    MC::DBus::onSignal(QDBusConnection::systemBus(), kNmService, kNmPath, kNmIface, u"CheckPermissions"_s, this, [this](const QDBusMessage &) {
        fetchPermissions();
    });

    if (NetworkManager::status() != NetworkManager::Unknown) {
        onServiceAppeared();
    } else {
        updateAvailable();
    }
}

WifiBackend::~WifiBackend()
{
    for (const auto &c : std::as_const(m_deviceConnections)) {
        disconnect(c);
    }
    for (const auto &c : std::as_const(m_dynamicConnections)) {
        disconnect(c);
    }
    for (const auto &c : std::as_const(m_profileConnections)) {
        disconnect(c);
    }
}

// ---------------------------------------------------------------------------
// State

bool WifiBackend::available() const
{
    return m_available;
}

QString WifiBackend::errorText() const
{
    return m_errorText;
}

bool WifiBackend::networkManagerRunning() const
{
    return m_nmRunning;
}

bool WifiBackend::wirelessEnabled() const
{
    return m_nmRunning && NetworkManager::isWirelessEnabled();
}

void WifiBackend::setWirelessEnabled(bool enabled)
{
    setRadio(u"WirelessEnabled"_s, enabled);
}

bool WifiBackend::hardwareEnabled() const
{
    return m_nmRunning && NetworkManager::isWirelessHardwareEnabled();
}

bool WifiBackend::wwanEnabled() const
{
    return m_nmRunning && NetworkManager::isWwanEnabled();
}

void WifiBackend::setWwanEnabled(bool enabled)
{
    setRadio(u"WwanEnabled"_s, enabled);
}

bool WifiBackend::wwanAvailable() const
{
    if (!m_nmRunning) {
        return false;
    }
    const auto devices = NetworkManager::networkInterfaces();
    return std::any_of(devices.cbegin(), devices.cend(), [](const NetworkManager::Device::Ptr &d) {
        return d->type() == NetworkManager::Device::Modem;
    });
}

bool WifiBackend::wwanHardwareEnabled() const
{
    return m_nmRunning && NetworkManager::isWwanHardwareEnabled();
}

bool WifiBackend::scanning() const
{
    return m_scanning;
}

bool WifiBackend::scanActive() const
{
    return m_scanActive;
}

void WifiBackend::setScanActive(bool active)
{
    if (m_scanActive == active) {
        return;
    }
    m_scanActive = active;
    Q_EMIT scanActiveChanged();
    updateScanTimer();
}

QString WifiBackend::activeSsid() const
{
    return m_activeSsid;
}

int WifiBackend::activeSignal() const
{
    return m_activeSignal;
}

bool WifiBackend::activating() const
{
    return m_activating;
}

QString WifiBackend::activatingSsid() const
{
    return m_activatingSsid;
}

WifiNetworkModel *WifiBackend::networks() const
{
    return m_networks;
}

QVariantMap WifiBackend::permissions() const
{
    return m_permissions;
}

bool WifiBackend::canToggleWifi() const
{
    return permissions().value(QStringLiteral("org.freedesktop.NetworkManager.enable-disable-wifi")).toString() == QLatin1String("yes");
}

// ---------------------------------------------------------------------------
// Service, devices, permissions

void WifiBackend::onServiceAppeared()
{
    updateAvailable();
    reloadDevices();
    fetchPermissions();
    Q_EMIT wirelessEnabledChanged();
    Q_EMIT wwanChanged();
}

void WifiBackend::onServiceDisappeared()
{
    for (const auto &c : std::as_const(m_profileConnections)) {
        disconnect(c);
    }
    m_profileConnections.clear();
    m_activatingByDevice.clear();
    m_lastScanRequest.clear();
    setScanning(false);
    reloadDevices();
    if (!m_permissions.isEmpty()) {
        m_permissions.clear();
        Q_EMIT permissionsChanged();
    }
    Q_EMIT wirelessEnabledChanged();
    Q_EMIT wwanChanged();
}

void WifiBackend::reloadDevices()
{
    for (const auto &c : std::as_const(m_deviceConnections)) {
        disconnect(c);
    }
    m_deviceConnections.clear();
    m_wifiDevices.clear();

    if (NetworkManager::status() != NetworkManager::Unknown) {
        const auto devices = NetworkManager::networkInterfaces();
        for (const NetworkManager::Device::Ptr &device : devices) {
            // NM also lists Bluetooth (and modems): Wi-Fi only.
            if (device->type() != NetworkManager::Device::Wifi) {
                continue;
            }
            auto wifi = device.objectCast<NetworkManager::WirelessDevice>();
            if (!wifi) {
                continue;
            }
            const QString uni = wifi->uni();
            m_wifiDevices.append(uni);
            auto *d = wifi.data();
            m_deviceConnections << connect(d, &NetworkManager::WirelessDevice::networkAppeared, this, &WifiBackend::scheduleRebuild)
                                << connect(d, &NetworkManager::WirelessDevice::networkDisappeared, this, &WifiBackend::scheduleRebuild)
                                << connect(d, &NetworkManager::WirelessDevice::activeAccessPointChanged, this, &WifiBackend::scheduleRebuild)
                                << connect(d, &NetworkManager::Device::activeConnectionChanged, this, &WifiBackend::scheduleRebuild)
                                << connect(d, &NetworkManager::WirelessDevice::lastScanChanged, this, [this] {
                                       setScanning(false);
                                   })
                                << connect(d,
                                           &NetworkManager::Device::stateChanged,
                                           this,
                                           [this, uni](NetworkManager::Device::State newState,
                                                       NetworkManager::Device::State,
                                                       NetworkManager::Device::StateChangeReason reason) {
                                               if (newState == NetworkManager::Device::Failed) {
                                                   const QString ssid = m_activatingByDevice.value(uni);
                                                   const QString text = reasonText(reason);
                                                   if (!ssid.isEmpty() && !text.isEmpty()) {
                                                       reportFailure(ssid, text);
                                                   }
                                               } else if (newState == NetworkManager::Device::Activated) {
                                                   setErrorText({});
                                               }
                                               scheduleRebuild();
                                           });
        }
    }

    updateAvailable();
    Q_EMIT wwanChanged();
    updateScanTimer();
    rebuild();
}

void WifiBackend::updateAvailable()
{
    const bool running = NetworkManager::status() != NetworkManager::Unknown;
    const bool available = running && !m_wifiDevices.isEmpty();
    if (running != m_nmRunning || available != m_available) {
        m_nmRunning = running;
        m_available = available;
        Q_EMIT availableChanged();
        Q_EMIT wirelessEnabledChanged();
        Q_EMIT wwanChanged();
    }
}

void WifiBackend::fetchPermissions()
{
    const QDBusMessage msg = MC::DBus::methodCall(kNmService, kNmPath, kNmIface, u"GetPermissions"_s);
    MC::DBus::call(QDBusConnection::systemBus(), msg, this, [this](const QDBusMessage &reply) {
        QVariantMap perms;
        if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
            perms = MC::DBus::demarshall(reply.arguments().constFirst()).toMap();
        }
        if (perms != m_permissions) {
            m_permissions = perms;
            Q_EMIT permissionsChanged();
        }
    });
}

void WifiBackend::setRadio(const QString &property, bool enabled)
{
    if (!m_nmRunning) {
        return;
    }
    QDBusMessage msg = MC::DBus::methodCall(kNmService,
                                            kNmPath,
                                            u"org.freedesktop.DBus.Properties"_s,
                                            u"Set"_s,
                                            {kNmIface, property, QVariant::fromValue(QDBusVariant(enabled))});
    // NM may ask polkit ("auth"): let the session's agent prompt.
    msg.setInteractiveAuthorizationAllowed(true);
    MC::DBus::call(QDBusConnection::systemBus(), msg, this, [this](const QDBusMessage &reply) {
        if (reply.type() == QDBusMessage::ErrorMessage) {
            setErrorText(reply.errorMessage());
        }
    });
}

// ---------------------------------------------------------------------------
// Network list

void WifiBackend::scheduleRebuild()
{
    if (!m_rebuildTimer.isActive()) {
        m_rebuildTimer.start();
    }
}

void WifiBackend::reloadSaved()
{
    m_saved.clear();
    if (!m_nmRunning) {
        return;
    }
    const auto connections = NetworkManager::listConnections();
    for (const NetworkManager::Connection::Ptr &connection : connections) {
        const QString path = connection->path();
        if (!m_profileConnections.contains(path)) {
            m_profileConnections.insert(path, connect(connection.data(), &NetworkManager::Connection::updated, this, &WifiBackend::scheduleRebuild));
        }
        const auto settings = connection->settings();
        if (!settings || settings->connectionType() != NetworkManager::ConnectionSettings::Wireless) {
            continue;
        }
        const auto wifi = settings->setting(NetworkManager::Setting::Wireless).dynamicCast<NetworkManager::WirelessSetting>();
        if (!wifi || wifi->ssid().isEmpty() || wifi->mode() == NetworkManager::WirelessSetting::Ap) {
            continue; // hotspots are not "saved networks"
        }
        SavedProfile profile;
        profile.uuid = settings->uuid();
        profile.path = path;
        profile.timestamp = settings->timestamp().isValid() ? settings->timestamp().toSecsSinceEpoch() : 0;
        profile.securityType = NetworkManager::securityTypeFromConnectionSetting(settings);
        QList<SavedProfile> &list = m_saved[wifi->ssid()];
        list.append(profile);
        std::stable_sort(list.begin(), list.end(), [](const SavedProfile &a, const SavedProfile &b) {
            return a.timestamp > b.timestamp;
        });
    }
}

const WifiBackend::SavedProfile *WifiBackend::savedFor(const QByteArray &rawSsid) const
{
    const auto it = m_saved.constFind(rawSsid);
    if (it == m_saved.cend() || it->isEmpty()) {
        return nullptr;
    }
    return &it->constFirst();
}

int WifiBackend::rowFor(const QString &ssid) const
{
    int best = -1;
    const auto &rows = m_networks->m_networks;
    for (int i = 0; i < rows.size(); ++i) {
        if (rows.at(i).ssid == ssid && (best < 0 || rows.at(i).signal > rows.at(best).signal)) {
            best = i;
        }
    }
    return best;
}

void WifiBackend::rebuild()
{
    m_rebuildTimer.stop();
    for (const auto &c : std::as_const(m_dynamicConnections)) {
        disconnect(c);
    }
    m_dynamicConnections.clear();

    reloadSaved();

    QList<WifiNetworkModel::Network> rows;
    QString activeSsid;
    int activeSignal = 0;
    QString activatingSsid;
    QHash<QString, QString> activatingByDevice;

    const bool radioOn = wirelessEnabled();
    for (const QString &uni : std::as_const(m_wifiDevices)) {
        const auto device = wirelessDevice(uni);
        if (!device) {
            continue;
        }

        // What this device is connected / connecting to.
        QByteArray deviceActive;
        QByteArray deviceActivating;
        const auto ac = device->activeConnection();
        if (ac) {
            const QByteArray raw = activeRawSsid(ac);
            if (ac->state() == NetworkManager::ActiveConnection::Activated) {
                deviceActive = raw;
            } else if (ac->state() == NetworkManager::ActiveConnection::Activating) {
                deviceActivating = raw;
            }
            m_dynamicConnections << connect(ac.data(), &NetworkManager::ActiveConnection::stateChanged, this, &WifiBackend::scheduleRebuild);
        }
        if (!deviceActive.isEmpty() && activeSsid.isEmpty()) {
            activeSsid = printableSsid(deviceActive, QString::fromUtf8(deviceActive));
            auto ap = device->activeAccessPoint();
            if (!ap && ac) {
                ap = device->findAccessPoint(ac->specificObject());
            }
            if (ap) {
                activeSignal = ap->signalStrength();
                m_dynamicConnections << connect(ap.data(), &NetworkManager::AccessPoint::signalStrengthChanged, this, &WifiBackend::scheduleRebuild);
            }
        }
        if (!deviceActivating.isEmpty()) {
            const QString ssid = QString::fromUtf8(deviceActivating);
            activatingByDevice.insert(uni, ssid);
            if (activatingSsid.isEmpty()) {
                activatingSsid = printableSsid(deviceActivating, ssid);
            }
        }

        if (!radioOn) {
            continue;
        }
        const auto networks = device->networks();
        for (const NetworkManager::WirelessNetwork::Ptr &network : networks) {
            const auto ap = network->referenceAccessPoint();
            if (!ap || network->ssid().isEmpty()) {
                continue; // hidden networks are not listed (like plasma-nm)
            }
            m_dynamicConnections << connect(network.data(), &NetworkManager::WirelessNetwork::signalStrengthChanged, this, &WifiBackend::scheduleRebuild)
                                 << connect(network.data(), &NetworkManager::WirelessNetwork::referenceAccessPointChanged, this, &WifiBackend::scheduleRebuild);

            WifiNetworkModel::Network row;
            row.rawSsid = ap->rawSsid();
            row.ssid = network->ssid();
            row.displaySsid = printableSsid(row.rawSsid, row.ssid);
            row.signal = network->signalStrength();
            row.adhoc = ap->mode() == NetworkManager::AccessPoint::Adhoc;
            row.securityType = NetworkManager::findBestWirelessSecurity(device->wirelessCapabilities(),
                                                                        true,
                                                                        row.adhoc,
                                                                        ap->capabilities(),
                                                                        ap->wpaFlags(),
                                                                        ap->rsnFlags());
            if (const SavedProfile *saved = savedFor(row.rawSsid)) {
                row.saved = true;
                row.connectionUuid = saved->uuid;
            }
            row.active = !deviceActive.isEmpty() && deviceActive == row.rawSsid;
            row.activating = !deviceActivating.isEmpty() && deviceActivating == row.rawSsid;
            row.frequency = int(ap->frequency());
            row.deviceUni = uni;
            row.apUni = ap->uni();
            rows.append(row);
        }
    }

    std::sort(rows.begin(), rows.end(), &WifiNetworkModel::lessThan);
    m_networks->setNetworks(rows);

    // Keep the last known activating SSID per device until the device
    // settles, so a Failed state can still name the network.
    for (auto it = activatingByDevice.cbegin(); it != activatingByDevice.cend(); ++it) {
        m_activatingByDevice.insert(it.key(), it.value());
    }

    const bool activating = !activatingSsid.isEmpty();
    if (activeSsid != m_activeSsid || activeSignal != m_activeSignal || activating != m_activating || activatingSsid != m_activatingSsid) {
        m_activeSsid = activeSsid;
        m_activeSignal = activeSignal;
        m_activating = activating;
        m_activatingSsid = activatingSsid;
        Q_EMIT activeConnectionChanged();
    }
}

// ---------------------------------------------------------------------------
// Scanning

void WifiBackend::updateScanTimer()
{
    const bool run = m_scanActive && m_available && wirelessEnabled();
    if (run && !m_scanTimer.isActive()) {
        m_scanTimer.start();
        requestScan();
    } else if (!run) {
        m_scanTimer.stop();
    }
}

void WifiBackend::setScanning(bool scanning)
{
    if (scanning) {
        m_scanningTimeout.start();
    } else {
        m_scanningTimeout.stop();
    }
    if (m_scanning == scanning) {
        return;
    }
    m_scanning = scanning;
    Q_EMIT scanningChanged();
}

void WifiBackend::requestScan()
{
    if (!m_available || !wirelessEnabled()) {
        return;
    }
    const QDateTime now = QDateTime::currentDateTime();
    for (const QString &uni : std::as_const(m_wifiDevices)) {
        const auto device = wirelessDevice(uni);
        if (!device || device->state() == NetworkManager::Device::Unavailable) {
            continue;
        }
        // Client throttle: NM refuses scans more often than this anyway.
        const QElapsedTimer &last = m_lastScanRequest[uni];
        if (last.isValid() && last.elapsed() < kScanIntervalMs) {
            continue;
        }
        const QDateTime lastScan = device->lastScan();
        if (lastScan.isValid() && lastScan.msecsTo(now) < kScanIntervalMs) {
            continue;
        }
        m_lastScanRequest[uni].start();

        auto *watcher = new QDBusPendingCallWatcher(device->requestScan(), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
            w->deleteLater();
            // NotAllowed / auth errors are expected (e.g. a scan already
            // running, or no active session): ignore them silently.
            if (!w->isError()) {
                setScanning(true);
            }
        });
    }
}

// ---------------------------------------------------------------------------
// Actions (STATE-CHANGING: only on user action)

void WifiBackend::connectSaved(const QString &ssid)
{
    const int row = rowFor(ssid);
    if (row < 0) {
        return;
    }
    const WifiNetworkModel::Network network = m_networks->m_networks.at(row);
    const SavedProfile *saved = savedFor(network.rawSsid);
    if (!saved) {
        return;
    }
    setErrorText({});
    m_activatingByDevice.insert(network.deviceUni, network.ssid);
    auto *watcher = new QDBusPendingCallWatcher(NetworkManager::activateConnection(saved->path, network.deviceUni, network.apUni), this);
    const QString display = network.ssid;
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, display](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        if (w->isError()) {
            reportFailure(display, w->error().message());
        }
        scheduleRebuild();
    });
}

void WifiBackend::connectNew(const QString &ssid, const QString &password)
{
    const int row = rowFor(ssid);
    if (row < 0) {
        return;
    }
    const WifiNetworkModel::Network network = m_networks->m_networks.at(row);

    // plasma-nm 6.6+ behaviour: system-wide when the user may modify system
    // connections and plasma-nm's SystemConnectionsByDefault (default true)
    // is not switched off; otherwise per user with agent-owned secrets.
    const KConfigGroup plasmaNm(KSharedConfig::openConfig(u"plasma-nm"_s), u"General"_s);
    WifiConnectionBuilder::Params params;
    params.ssid = network.ssid;
    params.password = password;
    params.securityType = network.securityType;
    params.adhoc = network.adhoc;
    params.systemWide =
        plasmaNm.readEntry("SystemConnectionsByDefault", true) && m_permissions.value(kModifySystem).toString() == QLatin1String("yes");
    params.loginName = loginName();
    const NMVariantMapMap map = WifiConnectionBuilder::build(params);
    if (map.isEmpty()) {
        return; // enterprise: the QML hands off to kcm_networkmanagement
    }

    setErrorText({});
    m_activatingByDevice.insert(network.deviceUni, network.ssid);
    auto *watcher = new QDBusPendingCallWatcher(NetworkManager::addAndActivateConnection(map, network.deviceUni, network.apUni), this);
    const QString display = network.ssid;
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, display](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        if (w->isError()) {
            reportFailure(display, w->error().message());
        }
        scheduleRebuild();
    });
}

void WifiBackend::disconnectNetwork()
{
    for (const QString &uni : std::as_const(m_wifiDevices)) {
        const auto device = wirelessDevice(uni);
        if (!device || !device->activeConnection()) {
            continue;
        }
        m_activatingByDevice.remove(uni);
        auto *watcher = new QDBusPendingCallWatcher(device->disconnectInterface(), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
            w->deleteLater();
            if (w->isError()) {
                setErrorText(w->error().message());
            }
        });
    }
}

void WifiBackend::forget(const QString &ssid)
{
    const int row = rowFor(ssid);
    const QByteArray raw = row >= 0 ? m_networks->m_networks.at(row).rawSsid : ssid.toUtf8();
    const SavedProfile *saved = savedFor(raw);
    if (!saved) {
        return;
    }
    const auto connection = NetworkManager::findConnection(saved->path);
    if (!connection) {
        return;
    }
    auto *watcher = new QDBusPendingCallWatcher(connection->remove(), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        if (w->isError()) {
            setErrorText(w->error().message());
        }
        scheduleRebuild();
    });
}

bool WifiBackend::needsEnterprise(const QString &ssid) const
{
    const int row = rowFor(ssid);
    if (row >= 0) {
        return WifiConnectionBuilder::isEnterprise(m_networks->m_networks.at(row).securityType);
    }
    if (const SavedProfile *saved = savedFor(ssid.toUtf8())) {
        return WifiConnectionBuilder::isEnterprise(saved->securityType);
    }
    return false;
}

QString WifiBackend::savedConnectionUuid(const QString &ssid) const
{
    const int row = rowFor(ssid);
    const QByteArray raw = row >= 0 ? m_networks->m_networks.at(row).rawSsid : ssid.toUtf8();
    const SavedProfile *saved = savedFor(raw);
    return saved ? saved->uuid : QString();
}

void WifiBackend::refresh()
{
    updateAvailable();
    reloadDevices();
    fetchPermissions();
}

void WifiBackend::reportFailure(const QString &ssid, const QString &reason)
{
    setErrorText(reason);
    Q_EMIT connectionFailed(ssid, reason);
}

void WifiBackend::setErrorText(const QString &text)
{
    if (m_errorText == text) {
        return;
    }
    m_errorText = text;
    Q_EMIT errorTextChanged();
}
