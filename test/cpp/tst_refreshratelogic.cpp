// SPDX-License-Identifier: GPL-2.0-or-later
//
// RefreshRateLogic - pure refresh-rate grouping.
//
// groups()/bestModeId()/panel() on hand-built outputs - no backend.

#include <QTest>

#include <KScreen/Config>
#include <KScreen/Mode>
#include <KScreen/Output>

#include "refreshratelogic.h"

using namespace Qt::StringLiterals;

namespace
{
struct ModeSpec {
    QString id;
    QSize size;
    float hz;
};

KScreen::OutputPtr makeOutput(int id, const QString &name, KScreen::Output::Type type, const QList<ModeSpec> &specs, const QString &current,
                              bool connected = true, bool enabled = true)
{
    KScreen::OutputPtr output(new KScreen::Output);
    output->setId(id);
    output->setName(name);
    output->setType(type);
    output->setConnected(connected);
    output->setEnabled(enabled);
    KScreen::ModeList modes;
    for (const ModeSpec &spec : specs) {
        KScreen::ModePtr mode(new KScreen::Mode);
        mode->setId(spec.id);
        mode->setSize(spec.size);
        mode->setRefreshRate(spec.hz);
        modes.insert(spec.id, mode);
    }
    output->setModes(modes);
    output->setCurrentModeId(current);
    return output;
}

const QSize k2880(2880, 1800);
const QSize k1920(1920, 1200);
}

class TestRefreshRateLogic : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void alternate_data()
    {
        QTest::addColumn<QList<int>>("groups");
        QTest::addColumn<int>("current");
        QTest::addColumn<int>("expected");

        QTest::newRow("no modes") << QList<int>{} << 0 << 0;
        QTest::newRow("single native mode") << QList<int>{120} << 120 << 0;
        QTest::newRow("at highest -> next lower") << QList<int>{120, 60} << 120 << 60;
        QTest::newRow("at lower -> highest") << QList<int>{120, 60} << 60 << 120;
        QTest::newRow("three groups, at middle -> highest") << QList<int>{165, 120, 60} << 120 << 165;
        QTest::newRow("unknown current -> highest") << QList<int>{144, 60} << 0 << 144;
    }

    void alternate()
    {
        QFETCH(QList<int>, groups);
        QFETCH(int, current);
        QFETCH(int, expected);
        QCOMPARE(RefreshRateLogic::alternate(groups, current), expected);
    }

    void singleNativeMode()
    {
        // The Galaxy Book4 case: one native 120 Hz mode, ~60 Hz common modes
        // only at other sizes -> one group -> no toggle.
        const auto out = makeOutput(1, u"eDP-1"_s, KScreen::Output::Panel,
                                    {{u"native"_s, k2880, 120.0f}, {u"c60"_s, k1920, 60.0f}, {u"c59"_s, QSize(1280, 800), 59.94f}}, u"native"_s);
        QCOMPARE(RefreshRateLogic::groups(out), QList<int>({120}));
        QCOMPARE(RefreshRateLogic::bestModeId(out, 120), u"native"_s);
        QCOMPARE(RefreshRateLogic::bestModeId(out, 60), QString());
        QCOMPARE(RefreshRateLogic::alternate(RefreshRateLogic::groups(out), 120), 0);
    }

    void groupedRates()
    {
        const auto out = makeOutput(1, u"eDP-1"_s, KScreen::Output::Panel,
                                    {{u"a"_s, k2880, 59.95f},
                                     {u"b"_s, k2880, 60.00f},
                                     {u"c"_s, k2880, 119.98f},
                                     {u"d"_s, k2880, 120.00f},
                                     {u"e"_s, k1920, 144.0f}, // other size: ignored
                                     {u"f"_s, k1920, 48.0f}},
                                    u"a"_s);
        QCOMPARE(RefreshRateLogic::groups(out), QList<int>({120, 60}));
        QCOMPARE(RefreshRateLogic::bestModeId(out, 120), u"d"_s);
        QCOMPARE(RefreshRateLogic::bestModeId(out, 60), u"b"_s);
        QCOMPARE(RefreshRateLogic::bestModeId(out, 144), QString());
        QCOMPARE(RefreshRateLogic::bestModeId(out, 0), QString());
        QCOMPARE(RefreshRateLogic::alternate(RefreshRateLogic::groups(out), 60), 120);
    }

    void groupsFollowCurrentSize()
    {
        const auto out = makeOutput(1, u"eDP-1"_s, KScreen::Output::Panel,
                                    {{u"a"_s, k2880, 120.0f}, {u"b"_s, k1920, 60.0f}, {u"c"_s, k1920, 144.0f}}, u"b"_s);
        QCOMPARE(RefreshRateLogic::groups(out), QList<int>({144, 60}));
    }

    void tieBreaksOnPreferredMode()
    {
        const auto out = makeOutput(1, u"eDP-1"_s, KScreen::Output::Panel, {{u"a"_s, k2880, 60.0f}, {u"b"_s, k2880, 60.0f}, {u"c"_s, k2880, 90.0f}}, u"c"_s);
        out->setPreferredModes({u"b"_s});
        QCOMPARE(RefreshRateLogic::bestModeId(out, 60), u"b"_s);
        out->setPreferredModes({u"a"_s});
        QCOMPARE(RefreshRateLogic::bestModeId(out, 60), u"a"_s);
    }

    void noCurrentMode()
    {
        const auto out = makeOutput(1, u"eDP-1"_s, KScreen::Output::Panel, {{u"a"_s, k2880, 60.0f}, {u"b"_s, k2880, 120.0f}}, QString());
        QCOMPARE(RefreshRateLogic::groups(out), QList<int>());
        QCOMPARE(RefreshRateLogic::bestModeId(out, 60), QString());
        QCOMPARE(RefreshRateLogic::groups(KScreen::OutputPtr()), QList<int>());
    }

    void panel()
    {
        QVERIFY(!RefreshRateLogic::panel(KScreen::ConfigPtr()));

        KScreen::ConfigPtr config(new KScreen::Config);
        const auto hdmi = makeOutput(1, u"HDMI-A-1"_s, KScreen::Output::HDMI, {{u"a"_s, k1920, 60.0f}}, u"a"_s);
        const auto edp = makeOutput(2, u"eDP-1"_s, KScreen::Output::Panel, {{u"a"_s, k2880, 120.0f}}, u"a"_s);
        config->setOutputs({{1, hdmi}, {2, edp}});
        QCOMPARE(RefreshRateLogic::panel(config), edp);

        edp->setEnabled(false); // lid closed / external only
        QVERIFY(!RefreshRateLogic::panel(config));
        edp->setEnabled(true);
        edp->setConnected(false);
        QVERIFY(!RefreshRateLogic::panel(config));

        // No panel at all (desktop): no toggle.
        KScreen::ConfigPtr desktop(new KScreen::Config);
        desktop->setOutputs({{1, makeOutput(1, u"DP-1"_s, KScreen::Output::DisplayPort, {{u"a"_s, k1920, 144.0f}, {u"b"_s, k1920, 60.0f}}, u"a"_s)}});
        QVERIFY(!RefreshRateLogic::panel(desktop));
    }
};

QTEST_GUILESS_MAIN(TestRefreshRateLogic)
#include "tst_refreshratelogic.moc"
