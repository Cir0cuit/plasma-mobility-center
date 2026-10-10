// SPDX-License-Identifier: GPL-2.0-or-later
//
// ChargeLimitArgs - the argument map for PowerDevil's KAuth helper
// org.kde.powerdevil.chargethresholdhelper.setthreshold. Pure functions,
// unit-tested in test/cpp/tst_chargelimit_args.cpp.
//
// The rule (checked against daemon/chargethresholdhelper_linux.cpp at v6.3.0
// and v6.7.0; a missing key is read as -1 = "do not touch", and the
// start > stop check only runs when both keys are present):
//   * always send chargeStopThreshold;
//   * send chargeStartThreshold only if the current start != -1 AND
//     start > new stop; then send start = new stop;
//   * never send chargeType.

#pragma once

#include <QVariantMap>

namespace ChargeLimitArgs
{

/// Slider range and step used by the UI.
inline constexpr int Minimum = 50;
inline constexpr int Maximum = 100;
inline constexpr int Step = 5;

/// Snaps @p percent to the nearest multiple of Step and clamps it to
/// Minimum..Maximum.
int snap(int percent);

/// Builds the setthreshold arguments for a new stop threshold @p newStop
/// (already snapped), given the current start threshold @p currentStart
/// (-1 = end-only hardware or unknown).
QVariantMap build(int newStop, int currentStart);

} // namespace ChargeLimitArgs
