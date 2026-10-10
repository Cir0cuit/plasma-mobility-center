// SPDX-License-Identifier: GPL-2.0-or-later
//
// See volumefeedback.h. Follows plasma-pa's volumefeedback.cpp and
// canberracontext.cpp (GPL-2.0-or-later / LGPL): one canberra context, the
// "pulse" driver, the sound theme from kdeglobals [Sounds] Theme, and the
// sound cancelled and restarted when it is still playing.
//
// Nothing here touches the sound server until play(): ca_context_create()
// and ca_context_set_driver() only configure the context; libcanberra
// connects lazily on the first ca_context_play().

#include "volumefeedback.h"

#ifdef HAVE_CANBERRA
#include <canberra.h>

#include <KConfigGroup>
#include <KSharedConfig>

#include <QByteArray>
#include <QString>
#endif

namespace
{
#ifdef HAVE_CANBERRA
constexpr uint32_t kSoundId = 2; // any fixed id; used to cancel our own sound

QByteArray currentSoundTheme()
{
    KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("kdeglobals"));
    config->reparseConfiguration();
    return KConfigGroup(config, QStringLiteral("Sounds")).readEntry("Theme", QStringLiteral("ocean")).toUtf8();
}
#endif
} // namespace

struct VolumeFeedback::Private {
#ifdef HAVE_CANBERRA
    ca_context *context = nullptr;
    QByteArray theme;
#endif
};

VolumeFeedback::VolumeFeedback()
    : d(new Private)
{
#ifdef HAVE_CANBERRA
    if (ca_context_create(&d->context) != CA_SUCCESS) {
        d->context = nullptr;
        return;
    }
    if (ca_context_set_driver(d->context, "pulse") != CA_SUCCESS) {
        ca_context_destroy(d->context);
        d->context = nullptr;
        return;
    }
    ca_context_change_props(d->context,
                            CA_PROP_APPLICATION_NAME,
                            "Mobility Center",
                            CA_PROP_APPLICATION_ID,
                            "io.github.cir0cuit.mobilitycenter",
                            CA_PROP_APPLICATION_ICON_NAME,
                            "audio-volume-high",
                            nullptr);
#endif
}

VolumeFeedback::~VolumeFeedback()
{
#ifdef HAVE_CANBERRA
    if (d->context) {
        ca_context_destroy(d->context);
    }
#endif
    delete d;
}

bool VolumeFeedback::isSupported()
{
#ifdef HAVE_CANBERRA
    return true;
#else
    return false;
#endif
}

bool VolumeFeedback::isValid() const
{
#ifdef HAVE_CANBERRA
    return d->context != nullptr;
#else
    return false;
#endif
}

void VolumeFeedback::play(quint32 sinkIndex)
{
#ifdef HAVE_CANBERRA
    if (!d->context) {
        return;
    }

    const QByteArray theme = currentSoundTheme();
    if (theme != d->theme) {
        d->theme = theme;
        ca_context_change_props(d->context, CA_PROP_CANBERRA_XDG_THEME_NAME, d->theme.constData(), nullptr);
    }

    int playing = 0;
    ca_context_playing(d->context, kSoundId, &playing);
    if (playing) {
        ca_context_cancel(d->context, kSoundId);
    }

    const QByteArray device = QByteArray::number(sinkIndex);
    ca_context_change_device(d->context, device.constData());
    ca_context_play(d->context,
                    kSoundId,
                    CA_PROP_EVENT_DESCRIPTION,
                    "Volume Control Feedback Sound",
                    CA_PROP_EVENT_ID,
                    "audio-volume-change",
                    CA_PROP_MEDIA_ROLE,
                    "alert",
                    CA_PROP_CANBERRA_CACHE_CONTROL,
                    "permanent",
                    CA_PROP_CANBERRA_ENABLE,
                    "1",
                    nullptr);
    ca_context_change_device(d->context, nullptr);
#else
    Q_UNUSED(sinkIndex)
#endif
}
