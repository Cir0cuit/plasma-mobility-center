// SPDX-License-Identifier: GPL-2.0-or-later
//
// See refreshratelogic.h

#include "refreshratelogic.h"

#include <KScreen/Mode>

#include <algorithm>

namespace RefreshRateLogic
{

namespace
{
/// Modes of @p output with the size of its current mode; empty without one.
QList<KScreen::ModePtr> modesAtCurrentSize(const KScreen::OutputPtr &output)
{
    QList<KScreen::ModePtr> result;
    if (!output) {
        return result;
    }
    const KScreen::ModePtr current = output->currentMode();
    if (!current) {
        return result;
    }
    const QSize size = current->size();
    const KScreen::ModeList modes = output->modes();
    for (const KScreen::ModePtr &mode : modes) {
        if (mode && mode->size() == size && mode->refreshRate() > 0) {
            result.append(mode);
        }
    }
    return result;
}
} // namespace

KScreen::OutputPtr panel(const KScreen::ConfigPtr &config)
{
    if (!config) {
        return {};
    }
    // OutputList is a QMap keyed by id: the first match is the lowest id.
    const KScreen::OutputList outputs = config->outputs();
    for (const KScreen::OutputPtr &output : outputs) {
        if (output && output->isConnected() && output->isEnabled() && output->type() == KScreen::Output::Panel) {
            return output;
        }
    }
    return {};
}

QList<int> groups(const KScreen::OutputPtr &output)
{
    QList<int> result;
    const QList<KScreen::ModePtr> modes = modesAtCurrentSize(output);
    for (const KScreen::ModePtr &mode : modes) {
        const int hz = qRound(mode->refreshRate());
        if (hz > 0 && !result.contains(hz)) {
            result.append(hz);
        }
    }
    std::sort(result.begin(), result.end(), std::greater<int>());
    return result;
}

QString bestModeId(const KScreen::OutputPtr &output, int hz)
{
    if (hz <= 0) {
        return {};
    }
    const QStringList preferred = output ? output->preferredModes() : QStringList();
    KScreen::ModePtr best;
    const QList<KScreen::ModePtr> modes = modesAtCurrentSize(output);
    for (const KScreen::ModePtr &mode : modes) {
        if (qRound(mode->refreshRate()) != hz) {
            continue;
        }
        if (!best || mode->refreshRate() > best->refreshRate()
            || (qFuzzyCompare(mode->refreshRate(), best->refreshRate()) && preferred.contains(mode->id()) && !preferred.contains(best->id()))) {
            best = mode;
        }
    }
    return best ? best->id() : QString();
}

int alternate(const QList<int> &groups, int current)
{
    if (groups.size() < 2) {
        return 0;
    }
    const int highest = groups.constFirst();
    if (current != highest) {
        return highest;
    }
    return groups.at(1);
}

} // namespace RefreshRateLogic
