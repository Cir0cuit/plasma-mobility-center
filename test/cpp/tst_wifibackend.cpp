// SPDX-License-Identifier: GPL-2.0-or-later
//
// WifiBackend / WifiNetworkModel / PlasmaNmBridge against python-dbusmock's
// networkmanager template (+ test/mocks/network_nm.py) on a PRIVATE system
// bus (mockbus.h). Never against the live NetworkManager: radio switches,
// connects and forgets are real changes. KConfig goes to a QTemporaryDir.

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <KConfig>
#include <KConfigGroup>

#include "dbusasync.h"
#include "mockbus.h"
#include "plasmanmbridge.h"
#include "wifibackend.h"
#include "wifinetworkmodel.h"

using namespace Qt::StringLiterals;

namespace
{
const QString kService = u"org.freedesktop.NetworkManager"_s;
const QString kMain = u"/org/freedesktop"_s;
const QString kManager = u"/org/freedesktop/NetworkManager"_s;
const QString kManagerIface = u"org.freedesktop.NetworkManager"_s;
const QString kMockIface = u"org.freedesktop.DBus.Mock"_s;
const QString kDev = u"/org/freedesktop/NetworkManager/Devices/wlan0"_s;

// AP security flags (NM80211ApSecurityFlags / NM80211ApFlags).
constexpr uint kPrivacy = 0x1;
constexpr uint kRsnPsk = 0x188; // CCMP pair+group, PSK
constexpr uint kRsnSae = 1160; // CCMP pair+group, SAE (seen live)
constexpr uint kRsnSaeTransition = 1416; // + PSK (seen live)
constexpr uint kRsnEap = 0x288; // CCMP pair+group, 802.1X
constexpr uint kRsnOwe = 0x888; // CCMP pair+group, OWE

constexpr uint kStateDisconnected = 30;
constexpr uint kStateActivated = 100;

QDBusMessage callOn(const QString &path, const QString &iface, const QString &method, const QVariantList &args = {})
{
    QDBusMessage msg = QDBusMessage::createMethodCall(kService, path, iface, method);
    msg.setArguments(args);
    return QDBusConnection::systemBus().call(msg);
}

QDBusMessage callMock(const QString &method, const QVariantList &args = {})
{
    return callOn(kMain, kMockIface, method, args);
}

bool mockOk(const QDBusMessage &reply)
{
    if (reply.type() == QDBusMessage::ErrorMessage) {
        qWarning() << reply.errorName() << reply.errorMessage();
        return false;
    }
    return true;
}

QString mockString(const QDBusMessage &reply)
{
    return mockOk(reply) && !reply.arguments().isEmpty() ? reply.arguments().constFirst().toString() : QString();
}

// The argument lists of every call of @p method on @p path, oldest first.
QList<QVariantList> methodCalls(const QString &path, const QString &method)
{
    QList<QVariantList> out;
    const QDBusMessage reply = callOn(path, kMockIface, u"GetMethodCalls"_s, {method});
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        return out;
    }
    const QVariantList calls = MC::DBus::demarshall(reply.arguments().constFirst()).toList();
    for (const QVariant &call : calls) {
        const QVariantList fields = call.toList(); // (timestamp, args)
        if (fields.size() == 2) {
            out.append(fields.at(1).toList());
        }
    }
    return out;
}

QVariant managerProperty(const QString &name)
{
    const QDBusMessage reply = callOn(kManager, u"org.freedesktop.DBus.Properties"_s, u"Get"_s, {kManagerIface, name});
    return reply.arguments().isEmpty() ? QVariant() : MC::DBus::demarshall(reply.arguments().constFirst());
}

QStringList managerSettingsConnections()
{
    const QDBusMessage reply =
        callOn(u"/org/freedesktop/NetworkManager/Settings"_s, u"org.freedesktop.NetworkManager.Settings"_s, u"ListConnections"_s);
    QStringList out;
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
        const auto paths = qdbus_cast<QList<QDBusObjectPath>>(reply.arguments().constFirst());
        for (const QDBusObjectPath &p : paths) {
            out << p.path();
        }
    }
    return out;
}

QString pathArg(const QVariant &v)
{
    if (v.metaType() == QMetaType::fromType<QDBusObjectPath>()) {
        return v.value<QDBusObjectPath>().path();
    }
    return v.toString();
}

int rowOf(const WifiNetworkModel *model, const QString &ssid)
{
    for (int i = 0; i < model->count(); ++i) {
        if (model->get(i).value(u"ssid"_s).toString() == ssid) {
            return i;
        }
    }
    return -1;
}

QVariantMap rowData(const WifiNetworkModel *model, const QString &ssid)
{
    const int row = rowOf(model, ssid);
    return row >= 0 ? model->get(row) : QVariantMap();
}

QString addAp(const QString &name, const QByteArray &ssid, uchar strength, uint flags, uint rsn, uint frequency = 5180, uint mode = 2)
{
    return mockString(callMock(u"AddAccessPointEx"_s, {kDev, name, ssid, mode, frequency, QVariant::fromValue(strength), flags, 0u, rsn}));
}

QString addSaved(const QString &name, const QByteArray &ssid, const QString &uuid, qulonglong timestamp)
{
    return mockString(callMock(u"AddSavedConnection"_s, {name, ssid, uuid, timestamp}));
}
} // namespace

class TestWifiBackend : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        MC_REQUIRE_PRIVATE_SESSION_BUS();
        MC_REQUIRE_DBUSMOCK();
        qDBusRegisterMetaType<QMap<QString, QString>>();
        QVERIFY(m_configHome.isValid());
        qputenv("XDG_CONFIG_HOME", m_configHome.path().toLocal8Bit());
        // Before ANY NetworkManagerQt use: it connects to the system bus once.
        QVERIFY2(m_system.start(), qPrintable(m_system.error()));
        MC_REQUIRE_PRIVATE_SYSTEM_BUS();
    }

    void init()
    {
        QVERIFY2(m_nm.start(MockBus::System, {u"--template"_s, MockBus::mockFile(u"network_nm.py"_s)}, kService), qPrintable(m_nm.error()));
    }

    void cleanup()
    {
        m_nm.stop();
    }

    void availableFollowsWifiDevice()
    {
        WifiBackend wifi;
        QTRY_VERIFY(wifi.networkManagerRunning());
        QVERIFY(!wifi.available());

        // NM lists the Bluetooth adapter as a device too: not Wi-Fi.
        QVERIFY(!mockString(callMock(u"AddTypedDevice"_s, {u"bt0"_s, u"hci0"_s, 5})).isEmpty());
        QTest::qWait(200);
        QVERIFY(!wifi.available());
        QVERIFY(!wifi.wwanAvailable());

        QVERIFY(!mockString(callMock(u"AddTypedDevice"_s, {u"modem0"_s, u"wwan0"_s, 8})).isEmpty());
        QTRY_VERIFY(wifi.wwanAvailable());
        QVERIFY(!wifi.available());

        QSignalSpy spy(&wifi, &WifiBackend::availableChanged);
        QCOMPARE(mockString(callMock(u"AddWiFiDevice"_s, {u"wlan0"_s, u"wlan0"_s, kStateDisconnected})), kDev);
        QTRY_VERIFY(wifi.available());
        QVERIFY(spy.count() >= 1);
        QVERIFY(wifi.wirelessEnabled());
        QVERIFY(wifi.hardwareEnabled());
    }

    void listsNetworksWithSecurity()
    {
        addWifiDevice();
        QVERIFY(!addAp(u"ap_open"_s, "Cafe", 40, 0, 0, 2412).isEmpty());
        QVERIFY(!addAp(u"ap_home"_s, "Home", 70, kPrivacy, kRsnPsk).isEmpty());
        QVERIFY(!addAp(u"ap_sae"_s, "Wpa3Only", 60, kPrivacy, kRsnSae).isEmpty());
        QVERIFY(!addAp(u"ap_tm"_s, "Transition", 50, kPrivacy, kRsnSaeTransition).isEmpty());
        QVERIFY(!addAp(u"ap_corp"_s, "Corp", 30, kPrivacy, kRsnEap).isEmpty());
        QVERIFY(!addAp(u"ap_owe"_s, "Airport", 20, kPrivacy, kRsnOwe).isEmpty());
        QVERIFY(!addAp(u"ap_hidden"_s, QByteArray(), 90, kPrivacy, kRsnPsk).isEmpty());

        WifiBackend wifi;
        const WifiNetworkModel *model = wifi.networks();
        QTRY_COMPARE(model->count(), 6); // the hidden network is not listed

        QCOMPARE(rowData(model, u"Cafe"_s).value(u"securityType"_s).toInt(), int(WifiNetworkModel::NoSecurity));
        QCOMPARE(rowData(model, u"Cafe"_s).value(u"secured"_s).toBool(), false);
        QCOMPARE(rowData(model, u"Cafe"_s).value(u"frequency"_s).toInt(), 2412);
        QCOMPARE(rowData(model, u"Home"_s).value(u"securityType"_s).toInt(), int(WifiNetworkModel::Wpa2Psk));
        QCOMPARE(rowData(model, u"Home"_s).value(u"secured"_s).toBool(), true);
        QCOMPARE(rowData(model, u"Home"_s).value(u"enterprise"_s).toBool(), false);
        QCOMPARE(rowData(model, u"Home"_s).value(u"signal"_s).toInt(), 70);
        QCOMPARE(rowData(model, u"Home"_s).value(u"deviceUni"_s).toString(), kDev);
        QCOMPARE(rowData(model, u"Wpa3Only"_s).value(u"securityType"_s).toInt(), int(WifiNetworkModel::SAE));
        QCOMPARE(rowData(model, u"Transition"_s).value(u"securityType"_s).toInt(), int(WifiNetworkModel::SAE));
        QCOMPARE(rowData(model, u"Corp"_s).value(u"enterprise"_s).toBool(), true);
        QVERIFY(wifi.needsEnterprise(u"Corp"_s));
        QVERIFY(!wifi.needsEnterprise(u"Home"_s));
        QCOMPARE(rowData(model, u"Airport"_s).value(u"securityType"_s).toInt(), int(WifiNetworkModel::OWE));
        QCOMPARE(rowData(model, u"Airport"_s).value(u"secured"_s).toBool(), false);

        // Nothing saved: plain signal order.
        QCOMPARE(model->get(0).value(u"ssid"_s).toString(), u"Home"_s);
        QCOMPARE(model->get(5).value(u"ssid"_s).toString(), u"Airport"_s);

        // A new AP appears, another goes away.
        QVERIFY(!addAp(u"ap_new"_s, "Neighbour", 10, 0, 0).isEmpty());
        QTRY_COMPARE(model->count(), 7);
        QVERIFY(mockOk(callMock(u"RemoveAccessPoint"_s, {kDev, u"/org/freedesktop/NetworkManager/AccessPoint/ap_corp"_s})));
        QTRY_COMPARE(rowOf(model, u"Corp"_s), -1);
        QCOMPARE(model->count(), 6);
    }

    void nonUtf8SsidIsPrintable()
    {
        addWifiDevice();
        QVERIFY(!addAp(u"ap_raw"_s, QByteArray("Caf\xe9", 4), 50, 0, 0).isEmpty());
        WifiBackend wifi;
        QTRY_COMPARE(wifi.networks()->count(), 1);
        QCOMPARE(wifi.networks()->get(0).value(u"displaySsid"_s).toString(), u"Caf\\xe9"_s);
    }

    void savedProfilesNewestWinsAndOutOfRangeHidden()
    {
        addWifiDevice();
        QVERIFY(!addAp(u"ap_home"_s, "Home", 40, kPrivacy, kRsnPsk).isEmpty());
        QVERIFY(!addAp(u"ap_cafe"_s, "Cafe", 80, 0, 0).isEmpty());
        QVERIFY(!addSaved(u"home_old_psk"_s, "Home", u"11111111-1111-1111-1111-111111111111"_s, 100).isEmpty());
        QVERIFY(!addSaved(u"home_new_psk"_s, "Home", u"22222222-2222-2222-2222-222222222222"_s, 200).isEmpty());
        QVERIFY(!addSaved(u"faraway"_s, "FarAway", u"33333333-3333-3333-3333-333333333333"_s, 300).isEmpty());

        WifiBackend wifi;
        const WifiNetworkModel *model = wifi.networks();
        QTRY_COMPARE(model->count(), 2);
        QTRY_VERIFY(rowData(model, u"Home"_s).value(u"saved"_s).toBool());
        QCOMPARE(rowData(model, u"Home"_s).value(u"connectionUuid"_s).toString(), u"22222222-2222-2222-2222-222222222222"_s);
        QCOMPARE(wifi.savedConnectionUuid(u"Home"_s), u"22222222-2222-2222-2222-222222222222"_s);
        QCOMPARE(rowOf(model, u"FarAway"_s), -1); // saved but out of range
        QCOMPARE(wifi.savedConnectionUuid(u"FarAway"_s), u"33333333-3333-3333-3333-333333333333"_s);
        QCOMPARE(rowData(model, u"Cafe"_s).value(u"saved"_s).toBool(), false);
        // Saved before stronger unsaved networks.
        QCOMPARE(model->get(0).value(u"ssid"_s).toString(), u"Home"_s);

        // Forget removes the newest profile; the older one then matches.
        wifi.forget(u"Home"_s);
        QTRY_VERIFY(!managerSettingsConnections().contains(u"/org/freedesktop/NetworkManager/Settings/home_new_psk"_s));
        QVERIFY(managerSettingsConnections().contains(u"/org/freedesktop/NetworkManager/Settings/home_old_psk"_s));
        QTRY_COMPARE(rowData(model, u"Home"_s).value(u"connectionUuid"_s).toString(), u"11111111-1111-1111-1111-111111111111"_s);
    }

    void activeAndActivating()
    {
        addWifiDevice();
        const QString ap = addAp(u"ap_home"_s, "Home", 66, kPrivacy, kRsnPsk);
        QVERIFY(!addAp(u"ap_cafe"_s, "Cafe", 90, 0, 0).isEmpty());
        const QString saved = addSaved(u"home_psk"_s, "Home", u"44444444-4444-4444-4444-444444444444"_s, 100);
        QVERIFY(!saved.isEmpty());

        WifiBackend wifi;
        const WifiNetworkModel *model = wifi.networks();
        QTRY_COMPARE(model->count(), 2);
        QVERIFY(wifi.activeSsid().isEmpty());

        // Activating first...
        QVERIFY(!mockString(callMock(u"AddActiveConnection"_s, {QStringList{kDev}, saved, ap, u"0"_s, 1u})).isEmpty());
        QTRY_VERIFY(wifi.activating());
        QCOMPARE(wifi.activatingSsid(), u"Home"_s);
        QVERIFY(rowData(model, u"Home"_s).value(u"activating"_s).toBool());
        QVERIFY(wifi.activeSsid().isEmpty());

        // ...then activated.
        const QString acPath = u"/org/freedesktop/NetworkManager/ActiveConnection/0"_s;
        QVERIFY(mockOk(callOn(acPath, u"org.freedesktop.DBus.Properties"_s, u"Set"_s,
                              {u"org.freedesktop.NetworkManager.Connection.Active"_s, u"State"_s, QVariant::fromValue(QDBusVariant(2u))})));
        QTRY_COMPARE(wifi.activeSsid(), u"Home"_s);
        QVERIFY(!wifi.activating());
        QCOMPARE(wifi.activeSignal(), 66);
        QVERIFY(rowData(model, u"Home"_s).value(u"active"_s).toBool());
        QCOMPARE(model->get(0).value(u"ssid"_s).toString(), u"Home"_s); // active first

        // Disconnect = Device.Disconnect.
        wifi.disconnectNetwork();
        QTRY_COMPARE(methodCalls(kDev, u"Disconnect"_s).size(), 1);
        QTRY_VERIFY(wifi.activeSsid().isEmpty());
        QTRY_COMPARE(model->get(0).value(u"ssid"_s).toString(), u"Home"_s); // still saved first
        QVERIFY(!rowData(model, u"Home"_s).value(u"active"_s).toBool());
    }

    void radioSwitchesReachNetworkManager()
    {
        addWifiDevice();
        QVERIFY(!addAp(u"ap_cafe"_s, "Cafe", 90, 0, 0).isEmpty());
        WifiBackend wifi;
        QTRY_COMPARE(wifi.networks()->count(), 1);
        QVERIFY(wifi.wirelessEnabled());

        QSignalSpy spy(&wifi, &WifiBackend::wirelessEnabledChanged);
        wifi.setWirelessEnabled(false);
        QTRY_COMPARE(managerProperty(u"WirelessEnabled"_s).toBool(), false);
        QTRY_VERIFY(!wifi.wirelessEnabled());
        QVERIFY(spy.count() >= 1);
        QTRY_COMPARE(wifi.networks()->count(), 0); // radio off: no list
        QVERIFY(wifi.hardwareEnabled()); // a soft-block is not "hardware off"

        wifi.setWirelessEnabled(true);
        QTRY_VERIFY(wifi.wirelessEnabled());
        QTRY_COMPARE(wifi.networks()->count(), 1);

        QVERIFY(!wifi.wwanEnabled());
        wifi.setWwanEnabled(true);
        QTRY_COMPARE(managerProperty(u"WwanEnabled"_s).toBool(), true);
        QTRY_VERIFY(wifi.wwanEnabled());
    }

    void scanIsThrottled()
    {
        addWifiDevice();
        WifiBackend wifi;
        QTRY_VERIFY(wifi.available());

        wifi.requestScan();
        wifi.requestScan();
        QTRY_COMPARE(methodCalls(kDev, u"RequestScan"_s).size(), 1);
        QTRY_VERIFY(wifi.scanning());
        QTest::qWait(200);
        QCOMPARE(methodCalls(kDev, u"RequestScan"_s).size(), 1);

        // scanActive asks too, but the 10 s client throttle still holds.
        wifi.setScanActive(true);
        QTest::qWait(200);
        QCOMPARE(methodCalls(kDev, u"RequestScan"_s).size(), 1);
        wifi.setScanActive(false);
    }

    void scanActiveScansAndRadioOffDoesNot()
    {
        addWifiDevice();
        WifiBackend wifi;
        QTRY_VERIFY(wifi.available());
        wifi.setWirelessEnabled(false);
        QTRY_VERIFY(!wifi.wirelessEnabled());
        wifi.setScanActive(true);
        QTest::qWait(200);
        QCOMPARE(methodCalls(kDev, u"RequestScan"_s).size(), 0);

        wifi.setWirelessEnabled(true);
        QTRY_VERIFY(wifi.wirelessEnabled());
        QTRY_COMPARE(methodCalls(kDev, u"RequestScan"_s).size(), 1);
    }

    void connectSavedActivatesProfile()
    {
        addWifiDevice();
        const QString ap = addAp(u"ap_home"_s, "Home", 66, kPrivacy, kRsnPsk);
        const QString saved = addSaved(u"home_psk"_s, "Home", u"55555555-5555-5555-5555-555555555555"_s, 100);
        WifiBackend wifi;
        QTRY_VERIFY(rowData(wifi.networks(), u"Home"_s).value(u"saved"_s).toBool());

        wifi.connectSaved(u"Home"_s);
        QTRY_COMPARE(methodCalls(kManager, u"ActivateConnection"_s).size(), 1);
        const QVariantList args = methodCalls(kManager, u"ActivateConnection"_s).constFirst();
        QCOMPARE(pathArg(args.value(0)), saved);
        QCOMPARE(pathArg(args.value(1)), kDev);
        QCOMPARE(pathArg(args.value(2)), ap);
        QTRY_COMPARE(wifi.activeSsid(), u"Home"_s);
    }

    void connectNewSystemWide()
    {
        addWifiDevice();
        const QString ap = addAp(u"ap_sae"_s, "Wpa3Only", 60, kPrivacy, kRsnSae);
        QVERIFY(mockOk(callMock(u"SetPermissions"_s,
                                {QVariant::fromValue(QMap<QString, QString>{{u"org.freedesktop.NetworkManager.settings.modify.system"_s, u"yes"_s},
                                                                            {u"org.freedesktop.NetworkManager.enable-disable-wifi"_s, u"yes"_s}})})));
        WifiBackend wifi;
        QTRY_COMPARE(wifi.networks()->count(), 1);
        QTRY_VERIFY(wifi.canToggleWifi());

        wifi.connectNew(u"Wpa3Only"_s, u"correct horse"_s);
        QTRY_COMPARE(methodCalls(kManager, u"AddAndActivateConnection"_s).size(), 1);
        const QVariantList args = methodCalls(kManager, u"AddAndActivateConnection"_s).constFirst();
        const QVariantMap map = args.value(0).toMap();
        QCOMPARE(map.value(u"802-11-wireless-security"_s).toMap().value(u"key-mgmt"_s).toString(), u"sae"_s);
        QCOMPARE(map.value(u"802-11-wireless"_s).toMap().value(u"ssid"_s).toByteArray(), QByteArray("Wpa3Only"));
        QVERIFY(!map.value(u"connection"_s).toMap().contains(u"permissions"_s));
        QCOMPARE(pathArg(args.value(1)), kDev);
        QCOMPARE(pathArg(args.value(2)), ap);
        QTRY_COMPARE(wifi.activeSsid(), u"Wpa3Only"_s);
    }

    void connectNewPerUserWithoutSystemPermission()
    {
        addWifiDevice();
        QVERIFY(!addAp(u"ap_home"_s, "Home", 66, kPrivacy, kRsnPsk).isEmpty());
        QVERIFY(mockOk(callMock(u"SetPermissions"_s,
                                {QVariant::fromValue(QMap<QString, QString>{{u"org.freedesktop.NetworkManager.settings.modify.system"_s, u"auth"_s}})})));
        WifiBackend wifi;
        QTRY_COMPARE(wifi.networks()->count(), 1);
        QTRY_VERIFY(!wifi.permissions().isEmpty());
        QVERIFY(!wifi.canToggleWifi());

        wifi.connectNew(u"Home"_s, u"hunter22"_s);
        QTRY_COMPARE(methodCalls(kManager, u"AddAndActivateConnection"_s).size(), 1);
        const QVariantMap map = methodCalls(kManager, u"AddAndActivateConnection"_s).constFirst().value(0).toMap();
        const QStringList perms = map.value(u"connection"_s).toMap().value(u"permissions"_s).toStringList();
        QCOMPARE(perms.size(), 1);
        QVERIFY(perms.constFirst().startsWith(u"user:"_s));
        QCOMPARE(map.value(u"802-11-wireless-security"_s).toMap().value(u"psk-flags"_s).toInt(), 1); // AgentOwned
    }

    void enterpriseIsNotConnectedInline()
    {
        addWifiDevice();
        QVERIFY(!addAp(u"ap_corp"_s, "Corp", 66, kPrivacy, kRsnEap).isEmpty());
        WifiBackend wifi;
        QTRY_COMPARE(wifi.networks()->count(), 1);
        wifi.connectNew(u"Corp"_s, u"x"_s);
        QTest::qWait(200);
        QCOMPARE(methodCalls(kManager, u"AddAndActivateConnection"_s).size(), 0);
    }

    void activationFailureIsReported()
    {
        addWifiDevice();
        const QString ap = addAp(u"ap_home"_s, "Home", 66, kPrivacy, kRsnPsk);
        const QString saved = addSaved(u"home_psk"_s, "Home", u"66666666-6666-6666-6666-666666666666"_s, 100);
        WifiBackend wifi;
        QTRY_COMPARE(wifi.networks()->count(), 1);

        QVERIFY(!mockString(callMock(u"AddActiveConnection"_s, {QStringList{kDev}, saved, ap, u"0"_s, 1u})).isEmpty());
        QTRY_VERIFY(wifi.activating());

        QSignalSpy failed(&wifi, &WifiBackend::connectionFailed);
        QVERIFY(mockOk(callMock(u"FailDevice"_s, {kDev, 7u}))); // NM_DEVICE_STATE_REASON_NO_SECRETS
        QTRY_COMPARE(failed.count(), 1);
        QCOMPARE(failed.constFirst().at(0).toString(), u"Home"_s);
        QVERIFY(!failed.constFirst().at(1).toString().isEmpty());
        QVERIFY(!wifi.errorText().isEmpty());
        QTRY_VERIFY(!wifi.activating());
    }

    void networkManagerRestart()
    {
        addWifiDevice();
        QVERIFY(!addAp(u"ap_cafe"_s, "Cafe", 90, 0, 0).isEmpty());
        WifiBackend wifi;
        QTRY_COMPARE(wifi.networks()->count(), 1);

        m_nm.stop();
        QTRY_VERIFY(!wifi.available());
        QVERIFY(!wifi.networkManagerRunning());
        QCOMPARE(wifi.networks()->count(), 0);
        QVERIFY(wifi.activeSsid().isEmpty());

        QVERIFY2(m_nm.start(MockBus::System, {u"--template"_s, MockBus::mockFile(u"network_nm.py"_s)}, kService), qPrintable(m_nm.error()));
        QTRY_VERIFY(wifi.networkManagerRunning());
        addWifiDevice();
        QVERIFY(!addAp(u"ap_home"_s, "Home", 50, kPrivacy, kRsnPsk).isEmpty());
        QTRY_VERIFY(wifi.available());
        QTRY_COMPARE(wifi.networks()->count(), 1);
        QCOMPARE(wifi.networks()->get(0).value(u"ssid"_s).toString(), u"Home"_s);
    }

    void plasmaNmBridge()
    {
        PlasmaNmBridge bridge;
        QVERIFY(bridge.available());
        QCOMPARE(bridge.airplaneFlag(), false);
        QCOMPARE(bridge.systemConnectionsByDefault(), true); // plasma-nm's default

        QSignalSpy spy(&bridge, &PlasmaNmBridge::airplaneFlagChanged);
        bridge.setAirplaneFlag(true);
        QCOMPARE(bridge.airplaneFlag(), true);
        QCOMPARE(spy.count(), 1);
        {
            KConfig file(u"plasma-nm"_s, KConfig::SimpleConfig);
            QCOMPARE(file.group(u"General"_s).readEntry("AirplaneModeEnabled", false), true);
        }
        QVERIFY(QFile::exists(m_configHome.filePath(u"plasma-nm"_s)));

        // Another process (the KCM, the tray) changes the file: we follow.
        {
            KConfig file(u"plasma-nm"_s, KConfig::SimpleConfig);
            KConfigGroup group = file.group(u"General"_s);
            group.writeEntry("SystemConnectionsByDefault", false, KConfig::Notify);
            group.writeEntry("AirplaneModeEnabled", false, KConfig::Notify);
            file.sync();
        }
        QTRY_COMPARE(bridge.systemConnectionsByDefault(), false);
        QTRY_COMPARE(bridge.airplaneFlag(), false);

        bridge.setAirplaneFlag(false); // no change, no signal
        QCOMPARE(spy.count(), 2);
    }

private:
    void addWifiDevice()
    {
        QCOMPARE(mockString(callMock(u"AddWiFiDevice"_s, {u"wlan0"_s, u"wlan0"_s, kStateActivated})), kDev);
    }

    QTemporaryDir m_configHome;
    MockBus::SystemBus m_system; // declared before the mock: destroyed after it
    MockBus::Mock m_nm;
};

QTEST_GUILESS_MAIN(TestWifiBackend)
#include "tst_wifibackend.moc"
