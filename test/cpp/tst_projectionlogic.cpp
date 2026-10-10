// SPDX-License-Identifier: GPL-2.0-or-later
//
// ProjectionLogic on hand-built KScreen configs - no backend involved.
//
// Cases: pickPair validity with 1/2/3 outputs and a Virtual-* screencast
// output; internal = Panel else first; Extend / InternalOnly / ExternalOnly /
// Mirror (native and emulated); detect() round trip.

#include <QTest>

#include <KScreen/Config>
#include <KScreen/Mode>
#include <KScreen/Output>
#include <KScreen/Screen>

#include "projectionlogic.h"

using namespace Qt::StringLiterals;

namespace
{
KScreen::OutputPtr makeOutput(int id, const QString &name, KScreen::Output::Type type, bool connected = true)
{
    KScreen::OutputPtr output(new KScreen::Output);
    output->setId(id);
    output->setName(name);
    output->setType(type);
    output->setConnected(connected);
    output->setEnabled(connected);
    return output;
}

void addMode(const KScreen::OutputPtr &output, const QString &id, const QSize &size, float hz, bool current = false)
{
    KScreen::ModeList modes = output->modes();
    KScreen::ModePtr mode(new KScreen::Mode);
    mode->setId(id);
    mode->setSize(size);
    mode->setRefreshRate(hz);
    modes.insert(id, mode);
    output->setModes(modes);
    if (current) {
        output->setCurrentModeId(id);
    }
}

/// A laptop panel (2880x1800@120 + 1920x1200@60, scale 2) and an HDMI
/// monitor (2560x1440@144, 1920x1200@60 and @75, 1920x1080@60) to its right.
struct Rig {
    KScreen::ConfigPtr config;
    KScreen::OutputPtr panel;
    KScreen::OutputPtr hdmi;
};

Rig makeRig(KScreen::Config::Features features = KScreen::Config::Feature::Writable | KScreen::Config::Feature::PerOutputScaling)
{
    Rig rig;
    rig.config.reset(new KScreen::Config);
    rig.config->setSupportedFeatures(features);
    rig.config->setScreen(KScreen::ScreenPtr(new KScreen::Screen)); // clone() needs one
    // The HDMI output gets the lower id, so "internal = Panel" is tested
    // against "first candidate".
    rig.hdmi = makeOutput(1, u"HDMI-A-1"_s, KScreen::Output::HDMI);
    addMode(rig.hdmi, u"h1"_s, QSize(2560, 1440), 144.0f, true);
    addMode(rig.hdmi, u"h2"_s, QSize(1920, 1200), 59.95f);
    addMode(rig.hdmi, u"h3"_s, QSize(1920, 1200), 74.98f);
    addMode(rig.hdmi, u"h4"_s, QSize(1920, 1080), 60.0f);
    rig.hdmi->setPreferredModes({u"h1"_s});
    rig.panel = makeOutput(2, u"eDP-1"_s, KScreen::Output::Panel);
    addMode(rig.panel, u"p1"_s, QSize(2880, 1800), 120.0f, true);
    addMode(rig.panel, u"p2"_s, QSize(1920, 1200), 60.0f);
    rig.panel->setPreferredModes({u"p1"_s});
    rig.panel->setScale(2.0);
    rig.panel->setPos(QPoint(0, 0));
    rig.config->setOutputs({{1, rig.hdmi}, {2, rig.panel}});
    // Extended to the right of the panel's logical width.
    rig.hdmi->setPos(QPoint(rig.config->logicalSizeForOutputInt(*rig.panel).width(), 0));
    return rig;
}
}

class TestProjectionLogic : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void isVirtual()
    {
        QVERIFY(ProjectionLogic::isVirtual(makeOutput(1, u"Virtual-1"_s, KScreen::Output::Unknown)));
        QVERIFY(!ProjectionLogic::isVirtual(makeOutput(2, u"eDP-1"_s, KScreen::Output::Panel)));
        QVERIFY(!ProjectionLogic::isVirtual(makeOutput(3, u"HDMI-A-1"_s, KScreen::Output::HDMI)));
        QVERIFY(!ProjectionLogic::isVirtual(KScreen::OutputPtr()));
    }

    void pickPair()
    {
        QVERIFY(!ProjectionLogic::pickPair(KScreen::ConfigPtr()).isValid());

        KScreen::ConfigPtr config(new KScreen::Config);
        const auto panel = makeOutput(5, u"eDP-1"_s, KScreen::Output::Panel);
        config->addOutput(panel);
        QCOMPARE(ProjectionLogic::candidates(config).size(), 1);
        QVERIFY(!ProjectionLogic::pickPair(config).isValid());

        // A disconnected connector does not count.
        config->addOutput(makeOutput(6, u"DP-2"_s, KScreen::Output::DisplayPort, false));
        QVERIFY(!ProjectionLogic::pickPair(config).isValid());

        // A screencast's virtual output does not count either.
        config->addOutput(makeOutput(7, u"Virtual-1"_s, KScreen::Output::Unknown));
        QCOMPARE(ProjectionLogic::candidates(config).size(), 1);
        QVERIFY(!ProjectionLogic::pickPair(config).isValid());

        const auto hdmi = makeOutput(3, u"HDMI-A-1"_s, KScreen::Output::HDMI);
        config->addOutput(hdmi);
        QCOMPARE(ProjectionLogic::candidates(config).keys(), QList<int>({3, 5}));
        ProjectionLogic::Pair pair = ProjectionLogic::pickPair(config);
        QVERIFY(pair.isValid());
        QCOMPARE(pair.internal, panel); // the Panel, although HDMI has the lower id
        QCOMPARE(pair.external, hdmi);

        // Three real outputs: no projection chips.
        config->addOutput(makeOutput(4, u"DP-1"_s, KScreen::Output::DisplayPort));
        QVERIFY(!ProjectionLogic::pickPair(config).isValid());

        // No panel (desktop with two monitors): internal = first candidate.
        KScreen::ConfigPtr desktop(new KScreen::Config);
        const auto dp = makeOutput(2, u"DP-1"_s, KScreen::Output::DisplayPort);
        const auto hdmi2 = makeOutput(1, u"HDMI-A-1"_s, KScreen::Output::HDMI);
        desktop->setOutputs({{2, dp}, {1, hdmi2}});
        pair = ProjectionLogic::pickPair(desktop);
        QVERIFY(pair.isValid());
        QCOMPARE(pair.internal, hdmi2);
        QCOMPARE(pair.external, dp);
    }

    void supportsNativeReplication()
    {
        QVERIFY(!ProjectionLogic::supportsNativeReplication(KScreen::ConfigPtr()));
        // OutputReplication alone is NOT the capability test.
        QVERIFY(!ProjectionLogic::supportsNativeReplication(makeRig(KScreen::Config::Feature::Writable | KScreen::Config::Feature::OutputReplication).config));
#ifdef MC_KSCREEN_REPLICATION
        QVERIFY(ProjectionLogic::supportsNativeReplication(makeRig().config));
#else
        QVERIFY(!ProjectionLogic::supportsNativeReplication(makeRig().config));
#endif
    }

    void applyExtend()
    {
        Rig rig = makeRig();
        rig.panel->setEnabled(false);
        rig.hdmi->setPos(QPoint(0, 0));
        QVERIFY(ProjectionLogic::apply(rig.config, ProjectionLogic::Extend, true));
        QVERIFY(rig.panel->isEnabled());
        QVERIFY(rig.hdmi->isEnabled());
        QCOMPARE(rig.panel->pos(), QPoint(0, 0));
        // 2880 px at scale 2 -> 1440 logical px.
        QCOMPARE(rig.hdmi->pos(), QPoint(1440, 0));
        QCOMPARE(ProjectionLogic::detect(rig.config), ProjectionLogic::Extend);

        // Without per-output scaling the logical width is the mode width.
        Rig x11 = makeRig(KScreen::Config::Feature::Writable | KScreen::Config::Feature::PrimaryDisplay);
        QVERIFY(ProjectionLogic::apply(x11.config, ProjectionLogic::Extend, false));
        QCOMPARE(x11.hdmi->pos(), QPoint(2880, 0));
    }

    void applyExtendEnablesWithoutCurrentMode()
    {
        Rig rig = makeRig();
        rig.hdmi->setEnabled(false);
        rig.hdmi->setCurrentModeId(QString());
        rig.hdmi->setPreferredModes({u"h4"_s});
        QVERIFY(ProjectionLogic::apply(rig.config, ProjectionLogic::Extend, true));
        QVERIFY(rig.hdmi->isEnabled());
        QCOMPARE(rig.hdmi->currentModeId(), u"h4"_s);
    }

    void applySingle()
    {
        Rig rig = makeRig();
        QVERIFY(ProjectionLogic::apply(rig.config, ProjectionLogic::InternalOnly, true));
        QVERIFY(rig.panel->isEnabled());
        QVERIFY(!rig.hdmi->isEnabled());
        QCOMPARE(rig.panel->pos(), QPoint(0, 0));
        QCOMPARE(ProjectionLogic::detect(rig.config), ProjectionLogic::InternalOnly);

        QVERIFY(ProjectionLogic::apply(rig.config, ProjectionLogic::ExternalOnly, true));
        QVERIFY(!rig.panel->isEnabled());
        QVERIFY(rig.hdmi->isEnabled());
        QCOMPARE(rig.hdmi->pos(), QPoint(0, 0));
        QCOMPARE(rig.config->primaryOutput(), rig.hdmi);
        QCOMPARE(ProjectionLogic::detect(rig.config), ProjectionLogic::ExternalOnly);
    }

    void applyMirrorNative()
    {
#ifndef MC_KSCREEN_REPLICATION
        QSKIP("built against libkscreen < 6.3.90: no native replication");
#else
        Rig rig = makeRig();
        rig.panel->setReplicationSource(rig.hdmi->id()); // stale, reversed
        QVERIFY(ProjectionLogic::apply(rig.config, ProjectionLogic::Mirror, true));
        QVERIFY(rig.panel->isEnabled());
        QVERIFY(rig.hdmi->isEnabled());
        QCOMPARE(rig.panel->replicationSource(), 0);
        QCOMPARE(rig.hdmi->replicationSource(), rig.panel->id());
        // Native replication leaves modes alone.
        QCOMPARE(rig.hdmi->currentModeId(), u"h1"_s);
        QCOMPARE(rig.panel->currentModeId(), u"p1"_s);
        QCOMPARE(ProjectionLogic::detect(rig.config), ProjectionLogic::Mirror);

        // Leaving mirror resets the replication source.
        QVERIFY(ProjectionLogic::apply(rig.config, ProjectionLogic::Extend, true));
        QCOMPARE(rig.hdmi->replicationSource(), 0);
        QCOMPARE(ProjectionLogic::detect(rig.config), ProjectionLogic::Extend);
#endif
    }

    void applyMirrorEmulated()
    {
        Rig rig = makeRig(KScreen::Config::Feature::Writable | KScreen::Config::Feature::PrimaryDisplay);
        rig.hdmi->setScale(1.0);
        QVERIFY(ProjectionLogic::apply(rig.config, ProjectionLogic::Mirror, false));
        QVERIFY(rig.panel->isEnabled());
        QVERIFY(rig.hdmi->isEnabled());
        // Largest common size is 1920x1200; the HDMI's best rate there is 74.98.
        QCOMPARE(rig.panel->currentModeId(), u"p2"_s);
        QCOMPARE(rig.hdmi->currentModeId(), u"h3"_s);
        QCOMPARE(rig.panel->pos(), QPoint(0, 0));
        QCOMPARE(rig.hdmi->pos(), QPoint(0, 0));
        QCOMPARE(rig.hdmi->scale(), rig.panel->scale());
#ifdef MC_KSCREEN_REPLICATION
        QCOMPARE(rig.hdmi->replicationSource(), 0);
#endif
        QCOMPARE(ProjectionLogic::detect(rig.config), ProjectionLogic::Mirror);

        // Leaving the emulated mirror restores the native (preferred) modes.
        QVERIFY(ProjectionLogic::apply(rig.config, ProjectionLogic::Extend, false));
        QCOMPARE(rig.panel->currentModeId(), u"p1"_s);
        QCOMPARE(rig.hdmi->currentModeId(), u"h1"_s);
        QCOMPARE(rig.hdmi->pos(), QPoint(2880, 0));
        QCOMPARE(ProjectionLogic::detect(rig.config), ProjectionLogic::Extend);

        QVERIFY(ProjectionLogic::apply(rig.config, ProjectionLogic::Mirror, false));
        QVERIFY(ProjectionLogic::apply(rig.config, ProjectionLogic::ExternalOnly, false));
        QCOMPARE(rig.hdmi->currentModeId(), u"h1"_s);
    }

    void applyKeepsModesOutsideEmulatedMirror()
    {
        // A user-chosen mode survives Extend -> InternalOnly -> Extend.
        Rig rig = makeRig();
        rig.panel->setCurrentModeId(u"p2"_s);
        QVERIFY(ProjectionLogic::apply(rig.config, ProjectionLogic::Extend, true));
        QVERIFY(ProjectionLogic::apply(rig.config, ProjectionLogic::InternalOnly, true));
        QVERIFY(ProjectionLogic::apply(rig.config, ProjectionLogic::Extend, true));
        QCOMPARE(rig.panel->currentModeId(), u"p2"_s);
        QCOMPARE(rig.hdmi->pos(), QPoint(960, 0));
    }

    void applyMirrorNoCommonSize()
    {
        Rig rig = makeRig(KScreen::Config::Feature::Writable);
        // Drop every common size: the HDMI only offers 2560x1440.
        KScreen::ModeList modes;
        modes.insert(u"h1"_s, rig.hdmi->mode(u"h1"_s));
        rig.hdmi->setModes(modes);
        const KScreen::ConfigPtr before = rig.config->clone();
        QVERIFY(!ProjectionLogic::apply(rig.config, ProjectionLogic::Mirror, false));
        // Untouched.
        QCOMPARE(rig.hdmi->pos(), before->output(1)->pos());
        QCOMPARE(rig.panel->currentModeId(), before->output(2)->currentModeId());
        QCOMPARE(ProjectionLogic::detect(rig.config), ProjectionLogic::Extend);
    }

    void applyRejects()
    {
        Rig rig = makeRig();
        QVERIFY(!ProjectionLogic::apply(rig.config, ProjectionLogic::Unknown, true));
        rig.hdmi->setConnected(false);
        QVERIFY(!ProjectionLogic::apply(rig.config, ProjectionLogic::Extend, true));
        QVERIFY(!ProjectionLogic::apply(KScreen::ConfigPtr(), ProjectionLogic::Extend, true));
    }

    void detect_data()
    {
        QTest::addColumn<int>("mode");
        QTest::addColumn<bool>("native");
        QTest::newRow("internal only") << int(ProjectionLogic::InternalOnly) << false;
        QTest::newRow("external only") << int(ProjectionLogic::ExternalOnly) << false;
        QTest::newRow("extend") << int(ProjectionLogic::Extend) << false;
        QTest::newRow("mirror emulated") << int(ProjectionLogic::Mirror) << false;
#ifdef MC_KSCREEN_REPLICATION
        QTest::newRow("mirror native") << int(ProjectionLogic::Mirror) << true;
        QTest::newRow("extend native") << int(ProjectionLogic::Extend) << true;
#endif
    }

    void detect()
    {
        QFETCH(int, mode);
        QFETCH(bool, native);
        // Round trip from every other starting mode.
        const QList<int> starts{ProjectionLogic::InternalOnly, ProjectionLogic::Mirror, ProjectionLogic::Extend, ProjectionLogic::ExternalOnly};
        for (int start : starts) {
            Rig rig = makeRig();
            QVERIFY(ProjectionLogic::apply(rig.config, ProjectionLogic::Mode(start), native));
            QVERIFY(ProjectionLogic::apply(rig.config, ProjectionLogic::Mode(mode), native));
            QCOMPARE(int(ProjectionLogic::detect(rig.config)), mode);
        }
    }

    void detectUnknown()
    {
        QCOMPARE(ProjectionLogic::detect(KScreen::ConfigPtr()), ProjectionLogic::Unknown);
        Rig rig = makeRig();
        rig.config->addOutput(makeOutput(3, u"DP-1"_s, KScreen::Output::DisplayPort));
        QCOMPARE(ProjectionLogic::detect(rig.config), ProjectionLogic::Unknown);
        rig.config->removeOutput(3);
        rig.panel->setEnabled(false);
        rig.hdmi->setEnabled(false);
        QCOMPARE(ProjectionLogic::detect(rig.config), ProjectionLogic::Unknown);
        // A screencast output next to two real ones keeps projection usable.
        rig.panel->setEnabled(true);
        rig.hdmi->setEnabled(true);
        rig.config->addOutput(makeOutput(9, u"Virtual-screencast"_s, KScreen::Output::Unknown));
        QCOMPARE(ProjectionLogic::detect(rig.config), ProjectionLogic::Extend);
    }
};

QTEST_GUILESS_MAIN(TestProjectionLogic)
#include "tst_projectionlogic.moc"
