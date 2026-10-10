// SPDX-License-Identifier: GPL-2.0-or-later
//
// Freezes the public API of the .core module, as the QML package uses it.
// Changing a name here means changing package/contents/ui/** too, and
// bumping MC_API_VERSION.

#include <QTest>

#include "apicheck.h"

using namespace Qt::StringLiterals;

#include "batterymonitor.h"
#include "chargelimit.h"
#include "keyboardbacklight.h"
#include "moduleinfo.h"
#include "nightlight.h"
#include "powerprofiles.h"
#include "presentationmode.h"
#include "screenbrightness.h"

class TestApiCore : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void moduleInfo()
    {
        const QMetaObject &mo = ModuleInfo::staticMetaObject;
        MC_REQUIRE_PROPERTIES(mo, QStringList({u"apiVersion"_s, u"version"_s, u"builtModules"_s, u"feedbackSoundBuilt"_s}));
        ModuleInfo info; // pure, no backend
        QCOMPARE(info.apiVersion(), MC_API_VERSION);
        QVERIFY(info.builtModules().contains(u"core"_s));
    }

    void screenBrightness()
    {
        const QMetaObject &mo = ScreenBrightness::staticMetaObject;
        MC_REQUIRE_CONVENTIONS(mo);
        MC_REQUIRE_PROPERTIES(mo,
                              QStringList({u"count"_s,
                                           u"primaryRow"_s,
                                           u"primaryName"_s,
                                           u"primaryPercent"_s,
                                           u"screenOffAvailable"_s,
                                           u"showOsd rw"_s}));
        MC_REQUIRE_METHODS(mo, "setPercent(QString,int)", "turnScreenOff()", "get(int)");
    }

    void keyboardBacklight()
    {
        const QMetaObject &mo = KeyboardBacklight::staticMetaObject;
        MC_REQUIRE_CONVENTIONS(mo);
        MC_REQUIRE_PROPERTIES(mo,
                              QStringList({u"brightness"_s,
                                           u"maxBrightness"_s,
                                           u"steps"_s,
                                           u"percent"_s,
                                           u"viaUPower"_s,
                                           u"rereadOnShow"_s}));
        MC_REQUIRE_METHODS(mo, "setLevel(int,bool)", "setLevel(int)");
    }

    void batteryMonitor()
    {
        const QMetaObject &mo = BatteryMonitor::staticMetaObject;
        MC_REQUIRE_CONVENTIONS(mo);
        MC_REQUIRE_PROPERTIES(mo,
                              QStringList({u"batteryCount"_s,
                                           u"percent"_s,
                                           u"chargeState"_s,
                                           u"pluggedIn"_s,
                                           u"remainingMsec"_s,
                                           u"chargeStopThreshold rw"_s,
                                           u"limitReached"_s,
                                           u"timeToLimitMsec"_s,
                                           u"capacity"_s,
                                           u"energy"_s,
                                           u"energyFull"_s,
                                           u"energyFullDesign"_s,
                                           u"energyRate"_s,
                                           u"cycleCount"_s,
                                           u"technology"_s,
                                           u"productName"_s,
                                           u"batteries"_s}));
        QVERIFY(mo.indexOfEnumerator("ChargeState") >= 0);
    }

    void powerProfiles()
    {
        const QMetaObject &mo = PowerProfiles::staticMetaObject;
        MC_REQUIRE_CONVENTIONS(mo);
        MC_REQUIRE_PROPERTIES(mo,
                              QStringList({u"backendService"_s,
                                           u"profiles"_s,
                                           u"activeProfile"_s,
                                           u"performanceDegraded"_s,
                                           u"degradedReasons"_s,
                                           u"holds"_s,
                                           u"hasBatteryAware"_s,
                                           u"batteryAware"_s,
                                           u"configuredProfile"_s}));
        MC_REQUIRE_METHODS(mo, "setActiveProfile(QString)");
    }

    void chargeLimit()
    {
        const QMetaObject &mo = ChargeLimit::staticMetaObject;
        MC_REQUIRE_CONVENTIONS(mo);
        MC_REQUIRE_PROPERTIES(mo,
                              QStringList({u"stopThreshold"_s,
                                           u"startThreshold"_s,
                                           u"endOnly"_s,
                                           u"minimum"_s,
                                           u"maximum"_s,
                                           u"step"_s,
                                           u"busy"_s,
                                           u"authRequired"_s,
                                           u"permission"_s,
                                           u"canInstallPermission"_s,
                                           u"installingPermission"_s,
                                           u"needsReplug"_s,
                                           u"polling rw"_s}));
        MC_REQUIRE_METHODS(mo, "setStopThreshold(int,QQuickItem*)", "setStopThreshold(int)", "checkAuthorization()", "installPermission()", "writeFinished(bool)", "permissionInstallFinished(bool)");
    }

    void nightLight()
    {
        const QMetaObject &mo = NightLight::staticMetaObject;
        MC_REQUIRE_CONVENTIONS(mo);
        MC_REQUIRE_PROPERTIES(mo,
                              QStringList({u"enabled"_s,
                                           u"running"_s,
                                           u"inhibited"_s,
                                           u"checked"_s,
                                           u"currentTemperature"_s,
                                           u"targetTemperature"_s,
                                           u"neutral"_s,
                                           u"mode"_s,
                                           u"userInhibited"_s,
                                           u"presentationInhibited rw"_s,
                                           u"enableMode rw"_s,
                                           u"canEnableDirectly"_s}));
        MC_REQUIRE_METHODS(mo, "setChecked(bool)", "kcmRequested(QString)");
    }

    void presentationMode()
    {
        const QMetaObject &mo = PresentationMode::staticMetaObject;
        MC_REQUIRE_CONVENTIONS(mo);
        MC_REQUIRE_PROPERTIES(mo,
                              QStringList({u"active rw"_s,
                                           u"sleepBlocked"_s,
                                           u"lockBlocked"_s,
                                           u"dndAvailable"_s,
                                           u"dndActive"_s,
                                           u"dndRevokedByUser"_s,
                                           u"blockedByUser"_s,
                                           u"nightLightInhibited"_s,
                                           u"nightLight rw"_s,
                                           u"inhibitNightLight rw"_s}));
    }
};

QTEST_GUILESS_MAIN(TestApiCore)
#include "tst_api_core.moc"
