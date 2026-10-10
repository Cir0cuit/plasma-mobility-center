// SPDX-License-Identifier: GPL-2.0-or-later
//
// Freezes the public API of the .network module, as the QML package uses it.

#include <QTest>

#include "apicheck.h"

using namespace Qt::StringLiterals;

#include "plasmanmbridge.h"
#include "wifibackend.h"
#include "wifinetworkmodel.h"

class TestApiNetwork : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void wifiBackend()
    {
        const QMetaObject &mo = WifiBackend::staticMetaObject;
        MC_REQUIRE_CONVENTIONS(mo);
        MC_REQUIRE_PROPERTIES(mo,
                              QStringList({u"networkManagerRunning"_s,
                                           u"wirelessEnabled rw"_s,
                                           u"hardwareEnabled"_s,
                                           u"wwanEnabled rw"_s,
                                           u"wwanAvailable"_s,
                                           u"wwanHardwareEnabled"_s,
                                           u"scanning"_s,
                                           u"scanActive rw"_s,
                                           u"activeSsid"_s,
                                           u"activeSignal"_s,
                                           u"activating"_s,
                                           u"activatingSsid"_s,
                                           u"networks"_s,
                                           u"permissions"_s,
                                           u"canToggleWifi"_s}));
        MC_REQUIRE_METHODS(mo,
                           "requestScan()",
                           "connectSaved(QString)",
                           "connectNew(QString,QString)",
                           "disconnectNetwork()",
                           "forget(QString)",
                           "needsEnterprise(QString)",
                           "savedConnectionUuid(QString)",
                           "connectionFailed(QString,QString)");
    }

    void wifiNetworkModel()
    {
        const QMetaObject &mo = WifiNetworkModel::staticMetaObject;
        MC_REQUIRE_PROPERTIES(mo, QStringList({u"count"_s}));
        MC_REQUIRE_METHODS(mo, "get(int)");
        QVERIFY(mo.indexOfEnumerator("SecurityType") >= 0);

        WifiNetworkModel model; // a plain list model, no NetworkManager access
        const QList<QByteArray> roles = model.roleNames().values();
        for (const char *role : {"ssid",
                                 "displaySsid",
                                 "signal",
                                 "securityType",
                                 "secured",
                                 "enterprise",
                                 "saved",
                                 "connectionUuid",
                                 "active",
                                 "activating",
                                 "frequency",
                                 "deviceUni"}) {
            QVERIFY2(roles.contains(role), role);
        }
    }

    void plasmaNmBridge()
    {
        const QMetaObject &mo = PlasmaNmBridge::staticMetaObject;
        MC_REQUIRE_CONVENTIONS(mo);
        MC_REQUIRE_PROPERTIES(mo, QStringList({u"airplaneFlag"_s, u"systemConnectionsByDefault"_s}));
        MC_REQUIRE_METHODS(mo, "setAirplaneFlag(bool)");
    }
};

QTEST_GUILESS_MAIN(TestApiNetwork)
#include "tst_api_network.moc"
