// SPDX-License-Identifier: GPL-2.0-or-later
//
// WifiConnectionBuilder - pure logic, no D-Bus: the settings maps passed to
// NetworkManager's AddAndActivateConnection for every supported security
// type, secret ownership, and the enterprise hand-off (empty map).

#include <QTest>

#include <NetworkManagerQt/Setting>
#include <NetworkManagerQt/Utils>

#include "wificonnectionbuilder.h"
#include "wifinetworkmodel.h"

using namespace Qt::StringLiterals;

namespace
{
WifiConnectionBuilder::Params params(const QString &ssid, int type, const QString &password = {})
{
    WifiConnectionBuilder::Params p;
    p.ssid = ssid;
    p.securityType = type;
    p.password = password;
    p.systemWide = true;
    return p;
}

QString keyMgmt(const NMVariantMapMap &map)
{
    return map.value(u"802-11-wireless-security"_s).value(u"key-mgmt"_s).toString();
}
} // namespace

class TestWifiConnectionBuilder : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void openNetwork()
    {
        const NMVariantMapMap map = WifiConnectionBuilder::build(params(u"Cafe"_s, WifiNetworkModel::NoSecurity));
        QVERIFY(!map.isEmpty());
        const QVariantMap connection = map.value(u"connection"_s);
        QCOMPARE(connection.value(u"id"_s).toString(), u"Cafe"_s);
        QCOMPARE(connection.value(u"type"_s).toString(), u"802-11-wireless"_s);
        QVERIFY(!connection.value(u"uuid"_s).toString().isEmpty());
        // autoconnect defaults to true in NM and is only written when false.
        QVERIFY(!connection.contains(u"autoconnect"_s) || connection.value(u"autoconnect"_s).toBool());
        QVERIFY(!connection.contains(u"permissions"_s));

        const QVariantMap wifi = map.value(u"802-11-wireless"_s);
        QCOMPARE(wifi.value(u"ssid"_s).toByteArray(), QByteArray("Cafe"));
        QVERIFY(!wifi.contains(u"security"_s));
        QVERIFY(!map.contains(u"802-11-wireless-security"_s));
    }

    void uuidIsFresh()
    {
        const auto a = WifiConnectionBuilder::build(params(u"Cafe"_s, WifiNetworkModel::NoSecurity));
        const auto b = WifiConnectionBuilder::build(params(u"Cafe"_s, WifiNetworkModel::NoSecurity));
        QVERIFY(a.value(u"connection"_s).value(u"uuid"_s) != b.value(u"connection"_s).value(u"uuid"_s));
    }

    void personal_data()
    {
        QTest::addColumn<int>("type");
        QTest::addColumn<QString>("expectedKeyMgmt");
        QTest::addColumn<QString>("secretKey");
        QTest::newRow("wpa") << int(WifiNetworkModel::WpaPsk) << u"wpa-psk"_s << u"psk"_s;
        QTest::newRow("wpa2") << int(WifiNetworkModel::Wpa2Psk) << u"wpa-psk"_s << u"psk"_s;
        QTest::newRow("sae") << int(WifiNetworkModel::SAE) << u"sae"_s << u"psk"_s;
        QTest::newRow("wep") << int(WifiNetworkModel::StaticWep) << u"none"_s << u"wep-key0"_s;
    }

    void personal()
    {
        QFETCH(int, type);
        QFETCH(QString, expectedKeyMgmt);
        QFETCH(QString, secretKey);

        const NMVariantMapMap map = WifiConnectionBuilder::build(params(u"Home"_s, type, u"hunter22"_s));
        QVERIFY(!map.isEmpty());
        QCOMPARE(map.value(u"802-11-wireless"_s).value(u"security"_s).toString(), u"802-11-wireless-security"_s);
        QCOMPARE(keyMgmt(map), expectedKeyMgmt);
        QCOMPARE(map.value(u"802-11-wireless-security"_s).value(secretKey).toString(), u"hunter22"_s);
        // System-wide: secrets stored by NM (flags None are not written).
        QVERIFY(!map.value(u"802-11-wireless-security"_s).contains(u"psk-flags"_s));
        QVERIFY(!map.value(u"connection"_s).contains(u"permissions"_s));
    }

    void owe()
    {
        const NMVariantMapMap map = WifiConnectionBuilder::build(params(u"Airport"_s, WifiNetworkModel::OWE));
        QCOMPARE(keyMgmt(map), u"owe"_s);
        QVERIFY(!map.value(u"802-11-wireless-security"_s).contains(u"psk"_s));
        QVERIFY(!WifiConnectionBuilder::needsPassword(WifiNetworkModel::OWE));
        QVERIFY(!WifiConnectionBuilder::needsPassword(WifiNetworkModel::NoSecurity));
        QVERIFY(WifiConnectionBuilder::needsPassword(WifiNetworkModel::SAE));
    }

    void adhoc()
    {
        WifiConnectionBuilder::Params p = params(u"Mesh"_s, WifiNetworkModel::Wpa2Psk, u"secretsecret"_s);
        p.adhoc = true;
        const NMVariantMapMap map = WifiConnectionBuilder::build(p);
        QCOMPARE(map.value(u"802-11-wireless"_s).value(u"mode"_s).toString(), u"adhoc"_s);
        QCOMPARE(keyMgmt(map), u"wpa-none"_s);
    }

    void perUserSecrets()
    {
        WifiConnectionBuilder::Params p = params(u"Home"_s, WifiNetworkModel::Wpa2Psk, u"hunter22"_s);
        p.systemWide = false;
        p.loginName = u"alice"_s;
        const NMVariantMapMap map = WifiConnectionBuilder::build(p);
        const QStringList perms = map.value(u"connection"_s).value(u"permissions"_s).toStringList();
        QCOMPARE(perms, QStringList{u"user:alice:"_s});
        QCOMPARE(map.value(u"802-11-wireless-security"_s).value(u"psk-flags"_s).toInt(), int(NetworkManager::Setting::AgentOwned));
    }

    void enterpriseIsHandedOff_data()
    {
        QTest::addColumn<int>("type");
        QTest::newRow("dynamic-wep") << int(WifiNetworkModel::DynamicWep);
        QTest::newRow("leap") << int(WifiNetworkModel::Leap);
        QTest::newRow("wpa-eap") << int(WifiNetworkModel::WpaEap);
        QTest::newRow("wpa2-eap") << int(WifiNetworkModel::Wpa2Eap);
        QTest::newRow("suite-b") << int(WifiNetworkModel::Wpa3SuiteB192);
        QTest::newRow("unknown") << int(WifiNetworkModel::UnknownSecurity);
    }

    void enterpriseIsHandedOff()
    {
        QFETCH(int, type);
        QVERIFY(WifiConnectionBuilder::isEnterprise(type));
        QVERIFY(WifiConnectionBuilder::build(params(u"Corp"_s, type, u"x"_s)).isEmpty());
    }

    void emptySsid()
    {
        QVERIFY(WifiConnectionBuilder::build(params(QString(), WifiNetworkModel::NoSecurity)).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestWifiConnectionBuilder)
#include "tst_wificonnectionbuilder.moc"
