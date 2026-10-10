// SPDX-License-Identifier: GPL-2.0-or-later
//
// See audiobackend.h
//
// Read-only on construction: Context::instance() only connects to the sound
// server (inside plasmashell it is plasma-pa's existing connection), the
// models and the default-device bindings only read, and plasmaparc is only
// read. Nothing is written until an invokable is called on user action.

#include "audiobackend.h"

#include <algorithm>

#include <KConfigGroup>
#include <KSharedConfig>

#include <PulseAudioQt/Context>
#include <PulseAudioQt/Models>
#include <PulseAudioQt/Server>
#include <PulseAudioQt/Sink>
#include <PulseAudioQt/Source>

#include "devicefiltermodel.h"
#include "volumefeedback.h"

using namespace Qt::StringLiterals;

namespace
{
const QString kPlasmaPaConfig = u"plasmaparc"_s;
const QString kGeneralGroup = u"General"_s;

constexpr int kDefaultVolumeStep = 5;
constexpr int kNormalMaxPercent = 100;
constexpr int kRaisedMaxPercent = 150; // plasma-pa: 1.5 * normalVolume
constexpr int kAlreadyRaisedPercent = 101; // above this the slider goes to 150

int percentOf(const PulseAudioQt::VolumeObject *object)
{
    if (!object) {
        return 0;
    }
    return qRound(object->volume() * 100.0 / PulseAudioQt::normalVolume());
}

void ensureCppOwnership(QObject *object)
{
    if (object) {
        QQmlEngine::setObjectOwnership(object, QQmlEngine::CppOwnership);
    }
}
} // namespace

AudioBackend::AudioBackend(QObject *parent)
    : QObject(parent)
    , m_context(PulseAudioQt::Context::instance())
    , m_outputs(new DeviceFilterModel(DeviceFilterModel::Outputs, this))
    , m_inputs(new DeviceFilterModel(DeviceFilterModel::Inputs, this))
{
    // Never setApplicationId() and never delete the context: inside
    // plasmashell it is plasma-pa's.
    m_sinkModel = new PulseAudioQt::SinkModel(this);
    m_sourceModel = new PulseAudioQt::SourceModel(this);
    m_outputs->setSourceModel(m_sinkModel);
    m_inputs->setSourceModel(m_sourceModel);

    connect(m_context, &PulseAudioQt::Context::stateChanged, this, &AudioBackend::updateState);
    connect(m_context, &PulseAudioQt::Context::autoConnectingChanged, this, &AudioBackend::updateState);

    if (PulseAudioQt::Server *server = m_context->server()) {
        connect(server, &PulseAudioQt::Server::defaultSinkChanged, this, &AudioBackend::bindDefaultSink);
        connect(server, &PulseAudioQt::Server::defaultSourceChanged, this, &AudioBackend::bindDefaultSource);
    }

    connect(m_outputs, &DeviceFilterModel::countChanged, this, &AudioBackend::updateAvailable);
    connect(m_inputs, &DeviceFilterModel::countChanged, this, [this] {
        const bool nonEmpty = m_inputs->count() > 0;
        if (nonEmpty != m_inputsNonEmpty) {
            m_inputsNonEmpty = nonEmpty;
            Q_EMIT defaultSourceChanged();
        }
    });

    m_configWatcher = KConfigWatcher::create(KSharedConfig::openConfig(kPlasmaPaConfig));
    connect(m_configWatcher.data(), &KConfigWatcher::configChanged, this, [this](const KConfigGroup &group) {
        if (group.name() == kGeneralGroup) {
            readSettings();
        }
    });
    readSettings();

    m_inputsNonEmpty = m_inputs->count() > 0;
    refresh();
}

AudioBackend::~AudioBackend()
{
    for (const auto &c : std::as_const(m_sinkConnections)) {
        disconnect(c);
    }
    for (const auto &c : std::as_const(m_sourceConnections)) {
        disconnect(c);
    }
}

bool AudioBackend::available() const
{
    return m_available;
}

QString AudioBackend::errorText() const
{
    return m_errorText;
}

bool AudioBackend::connecting() const
{
    switch (m_context->state()) {
    case PulseAudioQt::Context::State::Connecting:
    case PulseAudioQt::Context::State::Authorizing:
    case PulseAudioQt::Context::State::SettingName:
        return true;
    default:
        return autoConnecting();
    }
}

bool AudioBackend::failed() const
{
    const auto state = m_context->state();
    return state == PulseAudioQt::Context::State::Failed || state == PulseAudioQt::Context::State::Terminated;
}

bool AudioBackend::autoConnecting() const
{
    return m_context->isAutoConnecting();
}

QObject *AudioBackend::defaultSink() const
{
    return m_sink.data();
}

QObject *AudioBackend::defaultSource() const
{
    return m_source.data();
}

DeviceFilterModel *AudioBackend::outputs() const
{
    return m_outputs;
}

DeviceFilterModel *AudioBackend::inputs() const
{
    return m_inputs;
}

int AudioBackend::outputVolumePercent() const
{
    return percentOf(m_sink.data());
}

bool AudioBackend::outputMuted() const
{
    return m_sink && m_sink->isMuted();
}

QString AudioBackend::outputDescription() const
{
    return m_sink ? m_sink->description() : QString();
}

QString AudioBackend::outputName() const
{
    return m_sink ? m_sink->name() : QString();
}

QString AudioBackend::outputIconName() const
{
    // plasma-pa's icon thresholds (25 % / 75 %).
    const int percent = outputVolumePercent();
    if (!m_sink || outputMuted() || percent <= 0) {
        return u"audio-volume-muted"_s;
    }
    if (percent <= 25) {
        return u"audio-volume-low"_s;
    }
    if (percent <= 75) {
        return u"audio-volume-medium"_s;
    }
    return u"audio-volume-high"_s;
}

bool AudioBackend::inputAvailable() const
{
    return m_source && m_inputs->count() > 0;
}

int AudioBackend::inputVolumePercent() const
{
    return percentOf(m_source.data());
}

bool AudioBackend::inputMuted() const
{
    return m_source && m_source->isMuted();
}

QString AudioBackend::inputDescription() const
{
    return m_source ? m_source->description() : QString();
}

QString AudioBackend::inputName() const
{
    return m_source ? m_source->name() : QString();
}

int AudioBackend::volumeStep() const
{
    return m_volumeStep;
}

bool AudioBackend::raiseMaximum() const
{
    return m_raiseMaximum;
}

int AudioBackend::maxVolumePercent() const
{
    return m_maxVolumePercent;
}

bool AudioBackend::feedbackAvailable() const
{
    return VolumeFeedback::isSupported();
}

bool AudioBackend::feedbackEnabled() const
{
    return m_feedbackEnabled;
}

void AudioBackend::setFeedbackEnabled(bool enabled)
{
    if (m_feedbackEnabled == enabled) {
        return;
    }
    m_feedbackEnabled = enabled;
    Q_EMIT feedbackEnabledChanged();
}

void AudioBackend::setDefaultOutput(const QString &name)
{
    if (name.isEmpty()) {
        return;
    }
    // Not Sink::setDefault(true): BUG 500968 before PulseAudioQt 1.9.
    m_context->setDefaultSink(name);
}

void AudioBackend::setDefaultInput(const QString &name)
{
    if (name.isEmpty()) {
        return;
    }
    m_context->setDefaultSource(name);
}

void AudioBackend::setVolumePercent(QObject *device, int percent)
{
    auto *dev = qobject_cast<PulseAudioQt::Device *>(device);
    if (!dev || !dev->isVolumeWritable()) {
        return;
    }
    percent = std::clamp(percent, 0, deviceMaxPercent(dev));
    const qint64 volume = qRound64(percent / 100.0 * PulseAudioQt::normalVolume());
    if (dev->volume() != volume) {
        dev->setVolume(volume);
    }
    const bool mute = percent == 0;
    if (dev->isMuted() != mute) {
        dev->setMuted(mute);
    }
}

void AudioBackend::setOutputVolumePercent(int percent)
{
    setVolumePercent(defaultSink(), percent);
}

void AudioBackend::setInputVolumePercent(int percent)
{
    setVolumePercent(defaultSource(), percent);
}

void AudioBackend::toggleMute(QObject *device)
{
    if (auto *dev = qobject_cast<PulseAudioQt::Device *>(device)) {
        dev->setMuted(!dev->isMuted());
    }
}

void AudioBackend::toggleOutputMute()
{
    toggleMute(defaultSink());
}

void AudioBackend::toggleInputMute()
{
    toggleMute(defaultSource());
}

int AudioBackend::volumePercent(QObject *device) const
{
    const auto *dev = qobject_cast<PulseAudioQt::VolumeObject *>(device);
    return dev ? percentOf(dev) : -1;
}

void AudioBackend::playFeedback()
{
    if (!VolumeFeedback::isSupported() || !m_feedbackEnabled || !m_audioFeedback || !m_sink) {
        return;
    }
    if (!m_feedback) {
        m_feedback = std::make_unique<VolumeFeedback>();
    }
    if (m_feedback->isValid()) {
        m_feedback->play(m_sink->index());
    }
}

void AudioBackend::reconnect()
{
    m_context->reconnectDaemon();
}

void AudioBackend::refresh()
{
    m_configWatcher->config()->reparseConfiguration();
    readSettings();
    const PulseAudioQt::Server *server = m_context->server();
    bindDefaultSink(server ? server->defaultSink() : nullptr);
    bindDefaultSource(server ? server->defaultSource() : nullptr);
    updateState();
}

void AudioBackend::readSettings()
{
    const KConfigGroup group(m_configWatcher->config(), kGeneralGroup);
    const int step = std::clamp(group.readEntry("VolumeStep", kDefaultVolumeStep), 1, 100);
    const bool raise = group.readEntry("RaiseMaximumVolume", false);
    m_audioFeedback = group.readEntry("AudioFeedback", true);
    if (step != m_volumeStep || raise != m_raiseMaximum) {
        m_volumeStep = step;
        m_raiseMaximum = raise;
        Q_EMIT settingsChanged();
    }
    updateMaxVolume();
}

void AudioBackend::updateState()
{
    Q_EMIT stateChanged();
    if (failed() && !autoConnecting()) {
        setErrorText(u"Could not connect to the sound server"_s);
    } else {
        setErrorText({});
    }
    updateAvailable();
}

void AudioBackend::updateAvailable()
{
    const bool available = m_context->state() == PulseAudioQt::Context::State::Ready && m_outputs->count() > 0;
    if (available != m_available) {
        m_available = available;
        Q_EMIT availableChanged();
    }
}

void AudioBackend::updateMaxVolume()
{
    const int max = m_raiseMaximum || outputVolumePercent() > kAlreadyRaisedPercent ? kRaisedMaxPercent : kNormalMaxPercent;
    if (max != m_maxVolumePercent) {
        m_maxVolumePercent = max;
        Q_EMIT maxVolumePercentChanged();
    }
}

void AudioBackend::setErrorText(const QString &text)
{
    if (m_errorText == text) {
        return;
    }
    m_errorText = text;
    Q_EMIT errorTextChanged();
}

void AudioBackend::bindDefaultSink(PulseAudioQt::Sink *sink)
{
    if (sink == m_sink && !m_sinkConnections.isEmpty()) {
        return;
    }
    for (const auto &c : std::as_const(m_sinkConnections)) {
        disconnect(c);
    }
    m_sinkConnections.clear();
    m_sink = sink;
    if (sink) {
        ensureCppOwnership(sink);
        const auto changed = [this] {
            updateMaxVolume();
            Q_EMIT defaultSinkChanged();
        };
        m_sinkConnections << connect(sink, &PulseAudioQt::VolumeObject::volumeChanged, this, changed)
                          << connect(sink, &PulseAudioQt::VolumeObject::mutedChanged, this, changed)
                          << connect(sink, &PulseAudioQt::Device::descriptionChanged, this, changed)
                          << connect(sink, &PulseAudioQt::PulseObject::nameChanged, this, changed)
                          << connect(sink, &QObject::destroyed, this, [this] {
                                 bindDefaultSink(nullptr);
                             });
    }
    updateMaxVolume();
    Q_EMIT defaultSinkChanged();
}

void AudioBackend::bindDefaultSource(PulseAudioQt::Source *source)
{
    if (source == m_source && !m_sourceConnections.isEmpty()) {
        return;
    }
    for (const auto &c : std::as_const(m_sourceConnections)) {
        disconnect(c);
    }
    m_sourceConnections.clear();
    m_source = source;
    if (source) {
        ensureCppOwnership(source);
        const auto changed = [this] {
            Q_EMIT defaultSourceChanged();
        };
        m_sourceConnections << connect(source, &PulseAudioQt::VolumeObject::volumeChanged, this, changed)
                            << connect(source, &PulseAudioQt::VolumeObject::mutedChanged, this, changed)
                            << connect(source, &PulseAudioQt::Device::descriptionChanged, this, changed)
                            << connect(source, &PulseAudioQt::PulseObject::nameChanged, this, changed)
                            << connect(source, &QObject::destroyed, this, [this] {
                                   bindDefaultSource(nullptr);
                               });
    }
    Q_EMIT defaultSourceChanged();
}

int AudioBackend::deviceMaxPercent(const PulseAudioQt::Device *device) const
{
    if (m_raiseMaximum || percentOf(device) > kAlreadyRaisedPercent) {
        return kRaisedMaxPercent;
    }
    return std::max(m_maxVolumePercent, kNormalMaxPercent);
}
