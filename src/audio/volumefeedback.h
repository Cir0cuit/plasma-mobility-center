// SPDX-License-Identifier: GPL-2.0-or-later
//
// VolumeFeedback - the "audio-volume-change" click after a volume change,
// copied from plasma-pa's volumefeedback.cpp: libcanberra,
// event id "audio-volume-change", role "alert", sound theme from kdeglobals
// [Sounds] Theme, played on the given sink. Compiled in every build; it is a
// no-op unless HAVE_CANBERRA is defined (CMake option MC_WITH_CANBERRA and
// libcanberra found). Private helper of AudioBackend (not a QML type).

#pragma once

#include <QtGlobal>

class VolumeFeedback
{
public:
    VolumeFeedback();
    ~VolumeFeedback();

    VolumeFeedback(const VolumeFeedback &) = delete;
    VolumeFeedback &operator=(const VolumeFeedback &) = delete;

    /// Built with libcanberra.
    static bool isSupported();

    /// A canberra context could be created (always false without canberra).
    bool isValid() const;

    /// Plays the feedback sound on sink index @p sinkIndex. No-op when !isValid().
    void play(quint32 sinkIndex);

private:
    struct Private;
    Private *d = nullptr;
};
