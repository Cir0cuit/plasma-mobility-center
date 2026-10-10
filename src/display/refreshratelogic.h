// SPDX-License-Identifier: GPL-2.0-or-later
//
// RefreshRateLogic - pure helpers for the refresh-rate toggle,
// unit-testable on hand-built KScreen outputs.
//
//   * panel(): the connected, enabled Output::Panel (else null);
//   * groups(): modes at the output's current size grouped by
//     qRound(refreshRate), descending; the toggle shows only with >= 2;
//   * bestModeId(): within a group, the mode with the highest exact rate
//     (ties: the preferred mode);
//   * alternate(): highest group if current is not the highest, else the
//     next lower group; 0 if fewer than 2 groups.

#pragma once

#include <KScreen/Config>
#include <KScreen/Output>

#include <QList>
#include <QString>

namespace RefreshRateLogic
{

KScreen::OutputPtr panel(const KScreen::ConfigPtr &config);

QList<int> groups(const KScreen::OutputPtr &output);

QString bestModeId(const KScreen::OutputPtr &output, int hz);

int alternate(const QList<int> &groups, int current);

} // namespace RefreshRateLogic
