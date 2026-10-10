// SPDX-License-Identifier: GPL-2.0-or-later
//
// ChargeLimitArgs: the setthreshold argument rule.

#include <QTest>

#include "chargelimitargs.h"

using namespace Qt::StringLiterals;

class TestChargeLimitArgs : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void build_data()
    {
        QTest::addColumn<int>("newStop");
        QTest::addColumn<int>("currentStart");
        QTest::addColumn<bool>("sendsStart");
        QTest::addColumn<int>("expectedStart");

        QTest::newRow("end-only hardware: stop only") << 80 << -1 << false << -1;
        QTest::newRow("start below new stop: stop only") << 80 << 70 << false << -1;
        QTest::newRow("start equal to new stop: stop only") << 80 << 80 << false << -1;
        QTest::newRow("start above new stop: both, start = stop") << 60 << 75 << true << 60;
        QTest::newRow("raise stop above start: stop only") << 100 << 95 << false << -1;
    }

    void build()
    {
        QFETCH(int, newStop);
        QFETCH(int, currentStart);
        QFETCH(bool, sendsStart);
        QFETCH(int, expectedStart);

        const QVariantMap args = ChargeLimitArgs::build(newStop, currentStart);
        QCOMPARE(args.value(u"chargeStopThreshold"_s).toInt(), newStop);
        QCOMPARE(args.contains(u"chargeStartThreshold"_s), sendsStart);
        if (sendsStart) {
            QCOMPARE(args.value(u"chargeStartThreshold"_s).toInt(), expectedStart);
        }
        QVERIFY(!args.contains(u"chargeType"_s));
        QCOMPARE(args.size(), sendsStart ? 2 : 1);
    }

    void snap_data()
    {
        QTest::addColumn<int>("input");
        QTest::addColumn<int>("expected");

        QTest::newRow("below minimum") << 20 << 50;
        QTest::newRow("minimum") << 50 << 50;
        QTest::newRow("round down") << 62 << 60;
        QTest::newRow("round up") << 63 << 65;
        QTest::newRow("exact step") << 80 << 80;
        QTest::newRow("maximum") << 100 << 100;
        QTest::newRow("above maximum") << 130 << 100;
    }

    void snap()
    {
        QFETCH(int, input);
        QFETCH(int, expected);
        QCOMPARE(ChargeLimitArgs::snap(input), expected);
    }
};

QTEST_GUILESS_MAIN(TestChargeLimitArgs)
#include "tst_chargelimit_args.moc"
