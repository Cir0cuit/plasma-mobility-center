// SPDX-License-Identifier: GPL-2.0-or-later
//
// See projectionlogic.h Modelled on kscreen's
// OsdAction::applyAction (GPL-2.0-or-later).

#include "projectionlogic.h"

#include <KScreen/Mode>

#include <QRect>
#include <QSet>

namespace
{
/// Makes sure @p output has a usable current mode before it is enabled
/// (a disabled output may carry none): keep a valid one, else the preferred
/// one, else the largest/fastest mode it offers.
void ensureCurrentMode(const KScreen::OutputPtr &output)
{
    if (output->currentMode()) {
        return;
    }
    QString id = output->preferredModeId();
    if (id.isEmpty() || !output->mode(id)) {
        KScreen::ModePtr best;
        const KScreen::ModeList modes = output->modes();
        for (const KScreen::ModePtr &mode : modes) {
            const qint64 area = qint64(mode->size().width()) * mode->size().height();
            const qint64 bestArea = best ? qint64(best->size().width()) * best->size().height() : -1;
            if (!best || area > bestArea || (area == bestArea && mode->refreshRate() > best->refreshRate())) {
                best = mode;
            }
        }
        id = best ? best->id() : QString();
    }
    if (!id.isEmpty()) {
        output->setCurrentModeId(id);
    }
}

void resetReplication(const ProjectionLogic::Pair &pair)
{
#ifdef MC_KSCREEN_REPLICATION
    pair.internal->setReplicationSource(0);
    pair.external->setReplicationSource(0);
#else
    Q_UNUSED(pair)
#endif
}

/// The highest-rate mode of @p output with @p size; null if none.
KScreen::ModePtr bestModeAtSize(const KScreen::OutputPtr &output, const QSize &size)
{
    KScreen::ModePtr best;
    const KScreen::ModeList modes = output->modes();
    for (const KScreen::ModePtr &mode : modes) {
        if (mode->size() == size && (!best || mode->refreshRate() > best->refreshRate())) {
            best = mode;
        }
    }
    return best;
}

/// The largest (by area) mode size both outputs offer; invalid if none.
QSize largestCommonSize(const KScreen::OutputPtr &a, const KScreen::OutputPtr &b)
{
    QSet<QPair<int, int>> sizesB;
    const KScreen::ModeList modesB = b->modes();
    for (const KScreen::ModePtr &mode : modesB) {
        sizesB.insert({mode->size().width(), mode->size().height()});
    }
    QSize best;
    const KScreen::ModeList modesA = a->modes();
    for (const KScreen::ModePtr &mode : modesA) {
        const QSize s = mode->size();
        if (!s.isValid() || s.isEmpty() || !sizesB.contains({s.width(), s.height()})) {
            continue;
        }
        const qint64 area = qint64(s.width()) * s.height();
        const qint64 bestArea = best.isValid() ? qint64(best.width()) * best.height() : -1;
        if (area > bestArea || (area == bestArea && s.width() > best.width())) {
            best = s;
        }
    }
    return best;
}

/// After an EMULATED mirror both outputs run at a common (often non-native)
/// size: going back to extend or a single screen restores the preferred
/// modes. Native replication leaves the modes alone, so nothing to undo.
void restorePreferredModes(const ProjectionLogic::Pair &pair)
{
    for (const KScreen::OutputPtr &output : {pair.internal, pair.external}) {
        const QString preferred = output->preferredModeId();
        if (!preferred.isEmpty() && output->mode(preferred)) {
            output->setCurrentModeId(preferred);
        }
    }
}

bool isEmulatedMirror(const KScreen::ConfigPtr &config, const ProjectionLogic::Pair &pair)
{
#ifdef MC_KSCREEN_REPLICATION
    if (pair.internal->replicationSource() != 0 || pair.external->replicationSource() != 0) {
        return false;
    }
#else
    Q_UNUSED(pair)
#endif
    return ProjectionLogic::detect(config) == ProjectionLogic::Mirror;
}

/// Puts the single enabled output first in priority (it becomes primary).
void makePrimary(const KScreen::ConfigPtr &config, const KScreen::OutputPtr &output)
{
    config->setOutputPriority(output, 1);
    config->adjustPriorities(output);
}
} // namespace

bool ProjectionLogic::isVirtual(const KScreen::OutputPtr &output)
{
    return output && output->name().startsWith(QLatin1String("Virtual-"));
}

KScreen::OutputList ProjectionLogic::candidates(const KScreen::ConfigPtr &config)
{
    KScreen::OutputList result;
    if (!config) {
        return result;
    }
    const KScreen::OutputList outputs = config->outputs();
    for (auto it = outputs.cbegin(); it != outputs.cend(); ++it) {
        const KScreen::OutputPtr &output = it.value();
        if (output && output->isConnected() && !isVirtual(output)) {
            result.insert(it.key(), output);
        }
    }
    return result;
}

ProjectionLogic::Pair ProjectionLogic::pickPair(const KScreen::ConfigPtr &config)
{
    const KScreen::OutputList list = candidates(config);
    if (list.size() != 2) {
        return {};
    }
    const KScreen::OutputPtr first = list.first();
    const KScreen::OutputPtr second = list.last();
    if (second->type() == KScreen::Output::Panel && first->type() != KScreen::Output::Panel) {
        return {second, first};
    }
    return {first, second};
}

bool ProjectionLogic::supportsNativeReplication(const KScreen::ConfigPtr &config)
{
#ifdef MC_KSCREEN_REPLICATION
    // Feature::OutputReplication is not a usable capability test;
    // PerOutputScaling identifies the KWayland backend, which honours
    // replicationSource.
    return config && config->supportedFeatures().testFlag(KScreen::Config::Feature::PerOutputScaling);
#else
    Q_UNUSED(config)
    return false;
#endif
}

bool ProjectionLogic::apply(const KScreen::ConfigPtr &config, Mode mode, bool nativeReplication)
{
    const Pair pair = pickPair(config);
    if (!pair.isValid()) {
        return false;
    }
#ifndef MC_KSCREEN_REPLICATION
    nativeReplication = false;
#endif
    const KScreen::OutputPtr &internal = pair.internal;
    const KScreen::OutputPtr &external = pair.external;

    if (mode != Mirror && mode != Unknown && isEmulatedMirror(config, pair)) {
        restorePreferredModes(pair);
    }

    switch (mode) {
    case InternalOnly:
        resetReplication(pair);
        ensureCurrentMode(internal);
        internal->setEnabled(true);
        internal->setPos(QPoint(0, 0));
        external->setEnabled(false);
        makePrimary(config, internal);
        return true;

    case ExternalOnly:
        resetReplication(pair);
        ensureCurrentMode(external);
        external->setEnabled(true);
        external->setPos(QPoint(0, 0));
        internal->setEnabled(false);
        makePrimary(config, external);
        return true;

    case Extend: {
        resetReplication(pair);
        ensureCurrentMode(internal);
        ensureCurrentMode(external);
        internal->setEnabled(true);
        external->setEnabled(true);
        internal->setPos(QPoint(0, 0));
        external->setPos(QPoint(config->logicalSizeForOutputInt(*internal).width(), 0));
        config->adjustPriorities();
        return true;
    }

    case Mirror: {
        if (nativeReplication) {
#ifdef MC_KSCREEN_REPLICATION
            ensureCurrentMode(internal);
            ensureCurrentMode(external);
            resetReplication(pair);
            internal->setEnabled(true);
            external->setEnabled(true);
            external->setReplicationSource(internal->id());
            config->adjustPriorities();
            return true;
#endif
        }
        // Emulated: same position, largest common size, best rate there,
        // internal scale copied - check feasibility before touching anything.
        const QSize size = largestCommonSize(internal, external);
        if (!size.isValid()) {
            return false;
        }
        const KScreen::ModePtr internalMode = bestModeAtSize(internal, size);
        const KScreen::ModePtr externalMode = bestModeAtSize(external, size);
        if (!internalMode || !externalMode) {
            return false;
        }
        resetReplication(pair);
        internal->setEnabled(true);
        external->setEnabled(true);
        internal->setCurrentModeId(internalMode->id());
        external->setCurrentModeId(externalMode->id());
        internal->setPos(QPoint(0, 0));
        external->setPos(QPoint(0, 0));
        external->setScale(internal->scale());
        config->adjustPriorities();
        return true;
    }

    case Unknown:
        break;
    }
    return false;
}

ProjectionLogic::Mode ProjectionLogic::detect(const KScreen::ConfigPtr &config)
{
    const Pair pair = pickPair(config);
    if (!pair.isValid()) {
        return Unknown;
    }
    const bool internalOn = pair.internal->isEnabled();
    const bool externalOn = pair.external->isEnabled();
    if (internalOn && !externalOn) {
        return InternalOnly;
    }
    if (!internalOn && externalOn) {
        return ExternalOnly;
    }
    if (!internalOn && !externalOn) {
        return Unknown;
    }
#ifdef MC_KSCREEN_REPLICATION
    if (pair.external->replicationSource() == pair.internal->id() || pair.internal->replicationSource() == pair.external->id()) {
        return Mirror;
    }
#endif
    const QRect a = config->outputGeometryForOutput(*pair.internal);
    const QRect b = config->outputGeometryForOutput(*pair.external);
    if (a.isValid() && b.isValid()) {
        return a.intersects(b) ? Mirror : Extend;
    }
    return pair.internal->pos() == pair.external->pos() ? Mirror : Extend;
}
