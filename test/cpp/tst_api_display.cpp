// SPDX-License-Identifier: GPL-2.0-or-later
//
// Freezes the public API of the .display module, as the QML package uses it.

#include <QTest>

#include "apicheck.h"

using namespace Qt::StringLiterals;

#include "displayconfig.h"

class TestApiDisplay : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void displayConfig()
    {
        const QMetaObject &mo = DisplayConfig::staticMetaObject;
        MC_REQUIRE_CONVENTIONS(mo);
        MC_REQUIRE_PROPERTIES(mo,
                              QStringList({u"busy"_s,
                                           u"outputCount"_s,
                                           u"refreshToggleAvailable"_s,
                                           u"refreshRates"_s,
                                           u"currentRefreshRate"_s,
                                           u"alternateRefreshRate"_s,
                                           u"projectionAvailable"_s,
                                           u"projectionMode"_s,
                                           u"mirrorEmulated"_s,
                                           u"internalName"_s,
                                           u"externalName"_s}));
        MC_REQUIRE_METHODS(mo, "setRefreshRate(int)", "toggleRefreshRate()", "setProjectionMode(int)", "showKdeSwitcher()", "setHdrEnabled(bool)", "setAutoBrightnessEnabled(bool)");
        QVERIFY(mo.indexOfEnumerator("ProjectionMode") >= 0);
    }
};

QTEST_GUILESS_MAIN(TestApiDisplay)
#include "tst_api_display.moc"
