// SPDX-License-Identifier: GPL-2.0-or-later
//
// ProjectionLogic - the four projection modes as pure functions on a
// KScreen::Config, reimplementing kscreen's OsdAction::applyAction
// (GPL-2.0-or-later). No backend, no D-Bus: unit-tested on hand-built
// configs in test/cpp/tst_projectionlogic.cpp.
//
// Rules:
//   * candidates = connected outputs whose name does not start with
//     "Virtual-" (screencasts publish those - a heuristic);
//   * projection needs exactly 2 candidates; internal = the Output::Panel,
//     else the first candidate;
//   * Mirror: with nativeReplication (build MC_KSCREEN_REPLICATION AND
//     runtime supportedFeatures() & Feature::PerOutputScaling - NOT
//     Feature::OutputReplication) reset replicationSource(0) on both, then
//     external->setReplicationSource(internal->id()); otherwise emulate:
//     both at the same position, the largest common size, the highest
//     refresh rate at that size, and the internal scale copied;
//   * Extend: both enabled, internal at (0,0), external x =
//     internal->logicalSizeForOutputInt(*internal).width(), y = 0;
//   * InternalOnly / ExternalOnly: enable one, disable the other;
//   * detect(): derive the mode from enabled flags, replicationSource or
//     overlapping geometry.

#pragma once

#include <KScreen/Config>
#include <KScreen/Output>

class ProjectionLogic
{
public:
    /// Same values as DisplayConfig::ProjectionMode.
    enum Mode : int {
        Unknown = 0,
        InternalOnly,
        Mirror,
        Extend,
        ExternalOnly,
    };

    struct Pair {
        KScreen::OutputPtr internal;
        KScreen::OutputPtr external;
        bool isValid() const
        {
            return internal && external;
        }
    };

    ProjectionLogic() = delete;

    /// Output name starts with "Virtual-".
    static bool isVirtual(const KScreen::OutputPtr &output);

    /// Connected, non-virtual outputs, ordered by output id.
    static KScreen::OutputList candidates(const KScreen::ConfigPtr &config);

    /// The internal/external pair; invalid unless there are exactly 2 candidates.
    static Pair pickPair(const KScreen::ConfigPtr &config);

    /// Native replication usable: compiled with MC_KSCREEN_REPLICATION and
    /// config->supportedFeatures() has PerOutputScaling.
    static bool supportsNativeReplication(const KScreen::ConfigPtr &config);

    /// Applies @p mode to @p config IN PLACE (pass a clone). Returns false and
    /// leaves the config untouched when the mode cannot be applied (no valid
    /// pair, Unknown, no common mode for an emulated mirror).
    static bool apply(const KScreen::ConfigPtr &config, Mode mode, bool nativeReplication);

    /// The mode @p config is in; Unknown without a valid pair.
    static Mode detect(const KScreen::ConfigPtr &config);
};
