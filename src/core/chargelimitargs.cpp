// SPDX-License-Identifier: GPL-2.0-or-later
//
// The rule for the arguments of PowerDevil's setthreshold action; pure.

#include "chargelimitargs.h"

#include <QtGlobal>

namespace ChargeLimitArgs
{

int snap(int percent)
{
    const int snapped = qRound(double(percent) / Step) * Step;
    return qBound(Minimum, snapped, Maximum);
}

QVariantMap build(int newStop, int currentStart)
{
    QVariantMap args{{QStringLiteral("chargeStopThreshold"), newStop}};
    if (currentStart != -1 && currentStart > newStop) {
        args.insert(QStringLiteral("chargeStartThreshold"), newStop);
    }
    return args;
}

} // namespace ChargeLimitArgs
