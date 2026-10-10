// SPDX-License-Identifier: GPL-2.0-or-later
//
// DisplayConfig against libkscreen's in-process Fake backend. CTest sets
// KSCREEN_BACKEND=Fake and KSCREEN_BACKEND_INPROCESS=1 for every test, so a
// SetConfigOperation here can never reach KWin; keep it that way (assert the
// variables in initTestCase before creating a DisplayConfig).
//
// The Fake backend reads its outputs from TEST_DATA (test/mocks/display_*.json)
// (BackendManager::setBackendArgs). Its D-Bus adaptor is not registered in
// process, so the hotplug case changes the backend with its own
// Get/SetConfigOperation and checks that DisplayConfig follows through
// ConfigMonitor.

#include <QSignalSpy>
#include <QTest>

#include <KScreen/Config>
#include <KScreen/GetConfigOperation>
#include <KScreen/SetConfigOperation>
#include <KScreen/Output>
#include <kscreen/backendmanager_p.h>

#include "displayconfig.h"
#include "mockbus.h"

using namespace Qt::StringLiterals;

class TestDisplayConfig : public QObject
{
    Q_OBJECT

    /// Points the Fake backend at @p file; the next GetConfigOperation
    /// loads it.
    static void useFakeData(const QString &file, KScreen::Config::Features features = {})
    {
        KScreen::BackendManager::instance()->shutdownBackend();
        QVariantMap args{{u"TEST_DATA"_s, MockBus::mockFile(file)}};
        if (features) {
            args.insert(u"SUPPORTED_FEATURES"_s, int(features));
        }
        KScreen::BackendManager::instance()->setBackendArgs(args);
    }

    /// Changes the Fake backend's config behind DisplayConfig's back, as a
    /// hotplug or another client (System Settings) would.
    static bool changeBackend(const std::function<void(const KScreen::ConfigPtr &)> &change)
    {
        auto *get = new KScreen::GetConfigOperation;
        if (!get->exec()) {
            return false;
        }
        const KScreen::ConfigPtr cfg = get->config()->clone();
        change(cfg);
        auto *set = new KScreen::SetConfigOperation(cfg);
        return set->exec();
    }

private Q_SLOTS:
    void initTestCase()
    {
        // The safety net this file relies on (test/CMakeLists.txt).
        QCOMPARE(qEnvironmentVariable("KSCREEN_BACKEND"), QStringLiteral("Fake"));
        QCOMPARE(qEnvironmentVariable("KSCREEN_BACKEND_INPROCESS"), QStringLiteral("1"));
        MC_REQUIRE_PRIVATE_SESSION_BUS();
        QCOMPARE(KScreen::BackendManager::instance()->method(), KScreen::BackendManager::InProcess);
    }

    void singleNativeModeHidesToggle()
    {
        useFakeData(u"display_singlemode.json"_s);
        DisplayConfig dc;
        QSignalSpy busySpy(&dc, &DisplayConfig::busyChanged);
        QVERIFY(!dc.available());
        QTRY_VERIFY(dc.available());
        QCOMPARE(dc.outputCount(), 1);
        QCOMPARE(dc.refreshRates(), QList<int>({120}));
        QCOMPARE(dc.currentRefreshRate(), 120);
        QVERIFY(!dc.refreshToggleAvailable());
        QCOMPARE(dc.alternateRefreshRate(), 0);
        QVERIFY(!dc.projectionAvailable());
        QCOMPARE(dc.projectionMode(), DisplayConfig::Unknown);
        QCOMPARE(dc.internalName(), QString());

        // Nothing to toggle to: no SetConfigOperation at all.
        dc.toggleRefreshRate();
        dc.setRefreshRate(60);
        QTest::qWait(100);
        QCOMPARE(busySpy.count(), 0);
        QCOMPARE(dc.currentRefreshRate(), 120);
    }

    void refreshToggle()
    {
        useFakeData(u"display_dualrate.json"_s);
        DisplayConfig dc;
        QSignalSpy ratesSpy(&dc, &DisplayConfig::refreshRatesChanged);
        QTRY_VERIFY(dc.available());
        QCOMPARE(dc.refreshRates(), QList<int>({120, 60}));
        QVERIFY(dc.refreshToggleAvailable());
        QCOMPARE(dc.currentRefreshRate(), 120);
        QCOMPARE(dc.alternateRefreshRate(), 60);
        QVERIFY(ratesSpy.count() >= 1);

        QSignalSpy busySpy(&dc, &DisplayConfig::busyChanged);
        dc.toggleRefreshRate();
        QVERIFY(dc.busy());
        QTRY_VERIFY(!dc.busy());
        QCOMPARE(busySpy.count(), 2);
        QCOMPARE(dc.errorText(), QString());
        QTRY_COMPARE(dc.currentRefreshRate(), 60);
        QCOMPARE(dc.alternateRefreshRate(), 120);
        QCOMPARE(dc.refreshRates(), QList<int>({120, 60}));

        // A second instance reads back what the backend now has: the best
        // 60 Hz mode (60.00, not 59.95) was applied.
        {
            DisplayConfig other;
            QTRY_VERIFY(other.available());
            QCOMPARE(other.currentRefreshRate(), 60);
        }

        dc.toggleRefreshRate();
        QTRY_VERIFY(!dc.busy());
        QTRY_COMPARE(dc.currentRefreshRate(), 120);
    }

    void projection()
    {
        useFakeData(u"display_projection.json"_s);
        DisplayConfig dc;
        QTRY_VERIFY(dc.available());
        // eDP-1 + HDMI-A-1; Virtual-1 (a screencast) is ignored.
        QCOMPARE(dc.outputCount(), 2);
        QVERIFY(dc.projectionAvailable());
        QCOMPARE(dc.projectionMode(), DisplayConfig::Extend);
        QCOMPARE(dc.internalName(), u"eDP-1"_s);
        QCOMPARE(dc.externalName(), u"HDMI-A-1"_s);
        QVERIFY(!dc.refreshToggleAvailable());

        QSignalSpy modeSpy(&dc, &DisplayConfig::projectionModeChanged);
        const QList<DisplayConfig::ProjectionMode> modes{DisplayConfig::ExternalOnly, DisplayConfig::Mirror, DisplayConfig::InternalOnly, DisplayConfig::Extend};
        for (DisplayConfig::ProjectionMode mode : modes) {
            dc.setProjectionMode(mode);
            QVERIFY2(dc.busy() || !dc.errorText().isEmpty(), qPrintable(dc.errorText()));
            QTRY_VERIFY(!dc.busy());
            QCOMPARE(dc.errorText(), QString());
            QTRY_COMPARE(dc.projectionMode(), mode);
            QVERIFY(dc.projectionAvailable()); // still two connected outputs
        }
        QVERIFY(modeSpy.count() >= modes.size());
        // The Fake backend has no PerOutputScaling: mirroring is emulated.
        QVERIFY(dc.mirrorEmulated());
    }

    void projectionNativeMirror()
    {
#ifndef MC_KSCREEN_REPLICATION
        QSKIP("built against libkscreen < 6.3.90: no native replication");
#else
        useFakeData(u"display_projection.json"_s, KScreen::Config::Feature::Writable | KScreen::Config::Feature::PerOutputScaling);
        DisplayConfig dc;
        QTRY_VERIFY(dc.available());
        QVERIFY(!dc.mirrorEmulated());
        // 2880 px at scale 2 = 1440 logical px; HDMI at x=2880: extended.
        QCOMPARE(dc.projectionMode(), DisplayConfig::Extend);

        dc.setProjectionMode(DisplayConfig::Mirror);
        QTRY_VERIFY(!dc.busy());
        QCOMPARE(dc.errorText(), QString());
        QTRY_COMPARE(dc.projectionMode(), DisplayConfig::Mirror);

        auto *get = new KScreen::GetConfigOperation;
        QVERIFY(get->exec());
        const KScreen::ConfigPtr cfg = get->config();
        QCOMPARE(cfg->output(2)->replicationSource(), 1);
        QCOMPARE(cfg->output(1)->replicationSource(), 0);
        QCOMPARE(cfg->output(2)->currentModeId(), u"11"_s); // modes untouched

        dc.setProjectionMode(DisplayConfig::Extend);
        QTRY_VERIFY(!dc.busy());
        QTRY_COMPARE(dc.projectionMode(), DisplayConfig::Extend);
#endif
    }

    void invalidProjectionMode()
    {
        useFakeData(u"display_projection.json"_s);
        DisplayConfig dc;
        QTRY_VERIFY(dc.available());
        QSignalSpy busySpy(&dc, &DisplayConfig::busyChanged);
        dc.setProjectionMode(DisplayConfig::Unknown);
        dc.setProjectionMode(42);
        dc.setProjectionMode(-1);
        QCOMPARE(busySpy.count(), 0);
        QCOMPARE(dc.projectionMode(), DisplayConfig::Extend);
    }

    void projectionNeedsTwoOutputs()
    {
        useFakeData(u"display_singlemode.json"_s);
        DisplayConfig dc;
        QTRY_VERIFY(dc.available());
        QSignalSpy busySpy(&dc, &DisplayConfig::busyChanged);
        dc.setProjectionMode(DisplayConfig::Mirror);
        QCOMPARE(busySpy.count(), 0);
        QVERIFY(!dc.errorText().isEmpty());
    }

    void hotplug()
    {
        useFakeData(u"display_projection.json"_s);
        DisplayConfig dc;
        QTRY_VERIFY(dc.available());
        QVERIFY(dc.projectionAvailable());
        QSignalSpy outputsSpy(&dc, &DisplayConfig::outputsChanged);

        // The monitor is unplugged: the projection chips go away.
        QVERIFY(changeBackend([](const KScreen::ConfigPtr &cfg) {
            cfg->output(2)->setConnected(false);
            cfg->output(2)->setEnabled(false);
        }));
        QTRY_VERIFY(!dc.projectionAvailable());
        QCOMPARE(dc.outputCount(), 1);
        QCOMPARE(dc.externalName(), QString());
        QVERIFY(outputsSpy.count() >= 1);

        QVERIFY(changeBackend([](const KScreen::ConfigPtr &cfg) {
            cfg->output(2)->setConnected(true);
            cfg->output(2)->setEnabled(true);
        }));
        QTRY_VERIFY(dc.projectionAvailable());
        QCOMPARE(dc.outputCount(), 2);
    }

    void constructionIsReadOnly()
    {
        // Creating instances and reading must never apply anything: the
        // Fake backend's config stays as loaded.
        useFakeData(u"display_projection.json"_s);
        {
            DisplayConfig a;
            DisplayConfig b;
            QTRY_VERIFY(a.available() && b.available());
            b.refresh();
            QTest::qWait(50);
            QVERIFY(!a.busy() && !b.busy());
        }
        const KScreen::ConfigPtr cfg = KScreen::BackendManager::instance()->config();
        if (cfg) {
            QCOMPARE(cfg->output(2)->pos(), QPoint(2880, 0));
            QCOMPARE(cfg->output(1)->currentModeId(), u"1"_s);
        }
    }
};

QTEST_GUILESS_MAIN(TestDisplayConfig)
#include "tst_displayconfig.moc"
