// SPDX-License-Identifier: GPL-2.0-or-later
//
// AudioBackend - output/input volume and device pickers.
//
// Library: KF6::PulseAudioQt, 1.6.0 API subset only (no Server::version,
// Context::loadModule, ...). Context::instance() is shared with plasma-pa
// inside plasmashell: never call setApplicationId(), never delete the
// context, set CppOwnership on every object handed to QML, hold Sink/Source
// in QPointer. PulseAudioQt needs a GLib event dispatcher (plasmashell has
// one; test harnesses must not set QT_NO_GLIB).
//
// plasmaparc [General] (KConfigWatcher): VolumeStep -> volumeStep,
// RaiseMaximumVolume -> raiseMaximum, AudioFeedback gates the feedback sound.
//
// No OSD from us: plasma-pa's kded already shows one for changes to the
// preferred sink (6.3.1+).
//
// QML: `import io.github.cir0cuit.mobilitycenter.audio` then
// `AudioBackend.available` etc. (singleton).

#pragma once

#include <QObject>
#include <QPointer>
#include <QQmlEngine>

#include <KConfigWatcher>

#include <memory>

#include "devicefiltermodel.h"

namespace PulseAudioQt
{
class Context;
class Sink;
class Source;
class Device;
class SinkModel;
class SourceModel;
}

class VolumeFeedback;

class AudioBackend : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// Context state is Ready AND the filtered outputs model is non-empty.
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    /// Last error worth showing; empty if none.
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)
    /// Context is Connecting/Authorizing/SettingName, or auto-reconnecting:
    /// the tile shows a dimmed "Connecting..." card.
    Q_PROPERTY(bool connecting READ connecting NOTIFY stateChanged)
    /// The context failed (Failed/Terminated).
    Q_PROPERTY(bool failed READ failed NOTIFY stateChanged)
    /// PulseAudioQt is retrying by itself; show Retry only when
    /// failed && !autoConnecting.
    Q_PROPERTY(bool autoConnecting READ autoConnecting NOTIFY stateChanged)
    /// Default sink (PulseAudioQt::Sink*), or null. CppOwnership.
    Q_PROPERTY(QObject *defaultSink READ defaultSink NOTIFY defaultSinkChanged)
    /// Default source (PulseAudioQt::Source*), or null. CppOwnership.
    Q_PROPERTY(QObject *defaultSource READ defaultSource NOTIFY defaultSourceChanged)
    /// Output devices (sinks), filtered - see DeviceFilterModel.
    Q_PROPERTY(DeviceFilterModel *outputs READ outputs CONSTANT)
    /// Input devices (sources, monitors already excluded), filtered.
    Q_PROPERTY(DeviceFilterModel *inputs READ inputs CONSTANT)

    /// Default sink volume in percent of normalVolume, 0..maxVolumePercent.
    Q_PROPERTY(int outputVolumePercent READ outputVolumePercent NOTIFY defaultSinkChanged)
    /// Default sink is muted.
    Q_PROPERTY(bool outputMuted READ outputMuted NOTIFY defaultSinkChanged)
    /// Default sink's human readable description; "" when none.
    Q_PROPERTY(QString outputDescription READ outputDescription NOTIFY defaultSinkChanged)
    /// Default sink's PulseAudio name (what setDefaultOutput takes).
    Q_PROPERTY(QString outputName READ outputName NOTIFY defaultSinkChanged)
    /// Freedesktop icon name for the current output volume/mute state
    /// (audio-volume-muted/-low/-medium/-high).
    Q_PROPERTY(QString outputIconName READ outputIconName NOTIFY defaultSinkChanged)
    /// A default source exists and `inputs` is non-empty (mic section shows).
    Q_PROPERTY(bool inputAvailable READ inputAvailable NOTIFY defaultSourceChanged)
    /// Default source volume in percent, 0..maxVolumePercent.
    Q_PROPERTY(int inputVolumePercent READ inputVolumePercent NOTIFY defaultSourceChanged)
    /// Default source is muted.
    Q_PROPERTY(bool inputMuted READ inputMuted NOTIFY defaultSourceChanged)
    /// Default source description; "" when none.
    Q_PROPERTY(QString inputDescription READ inputDescription NOTIFY defaultSourceChanged)
    /// Default source's PulseAudio name (what setDefaultInput takes).
    Q_PROPERTY(QString inputName READ inputName NOTIFY defaultSourceChanged)

    /// plasmaparc VolumeStep, percent per wheel/key step (default 5).
    Q_PROPERTY(int volumeStep READ volumeStep NOTIFY settingsChanged)
    /// plasmaparc RaiseMaximumVolume: the slider may go to 150 %.
    Q_PROPERTY(bool raiseMaximum READ raiseMaximum NOTIFY settingsChanged)
    /// Slider maximum: 150 when raiseMaximum or the volume already exceeds
    /// 101 %, else 100. (150 % = 1.5 * normalVolume, NOT maximumUIVolume().)
    Q_PROPERTY(int maxVolumePercent READ maxVolumePercent NOTIFY maxVolumePercentChanged)
    /// Built with libcanberra (HAVE_CANBERRA).
    Q_PROPERTY(bool feedbackAvailable READ feedbackAvailable CONSTANT)
    /// INPUT from config volumeFeedback (default true). The sound plays only
    /// if feedbackAvailable && feedbackEnabled && plasmaparc AudioFeedback.
    Q_PROPERTY(bool feedbackEnabled READ feedbackEnabled WRITE setFeedbackEnabled NOTIFY feedbackEnabledChanged)

public:
    explicit AudioBackend(QObject *parent = nullptr);
    ~AudioBackend() override;

    bool available() const;
    QString errorText() const;
    bool connecting() const;
    bool failed() const;
    bool autoConnecting() const;
    QObject *defaultSink() const;
    QObject *defaultSource() const;
    DeviceFilterModel *outputs() const;
    DeviceFilterModel *inputs() const;
    int outputVolumePercent() const;
    bool outputMuted() const;
    QString outputDescription() const;
    QString outputName() const;
    QString outputIconName() const;
    bool inputAvailable() const;
    int inputVolumePercent() const;
    bool inputMuted() const;
    QString inputDescription() const;
    QString inputName() const;
    int volumeStep() const;
    bool raiseMaximum() const;
    int maxVolumePercent() const;
    bool feedbackAvailable() const;
    bool feedbackEnabled() const;
    void setFeedbackEnabled(bool enabled);

    // All invokables below except volumePercent() and refresh() are
    // STATE-CHANGING: only call them on user action.

    /// Context::setDefaultSink(@p name). (Never Sink::setDefault(true):
    /// BUG 500968 before PulseAudioQt 1.9.)
    Q_INVOKABLE void setDefaultOutput(const QString &name);
    /// Context::setDefaultSource(@p name).
    Q_INVOKABLE void setDefaultInput(const QString &name);
    /// Sets @p device (a Sink/Source from the models' PulseObject role, or
    /// defaultSink/defaultSource) to @p percent of normalVolume, clamped to
    /// 0..maxVolumePercent; 0 also mutes, > 0 unmutes.
    Q_INVOKABLE void setVolumePercent(QObject *device, int percent);
    /// Shorthands for the default sink / source.
    Q_INVOKABLE void setOutputVolumePercent(int percent);
    Q_INVOKABLE void setInputVolumePercent(int percent);
    /// Toggles mute of @p device / the default sink / the default source.
    Q_INVOKABLE void toggleMute(QObject *device);
    Q_INVOKABLE void toggleOutputMute();
    Q_INVOKABLE void toggleInputMute();
    /// Volume of @p device in percent of normalVolume; -1 for a bad object.
    Q_INVOKABLE int volumePercent(QObject *device) const;
    /// Plays the volume-change feedback sound on the default sink (call on
    /// slider release) if enabled - see feedbackEnabled.
    Q_INVOKABLE void playFeedback();
    /// Context::reconnectDaemon() - the Retry button.
    Q_INVOKABLE void reconnect();
    /// Re-reads plasmaparc settings and re-evaluates state. Read-only.
    Q_INVOKABLE void refresh();

Q_SIGNALS:
    void availableChanged();
    void errorTextChanged();
    void stateChanged();
    /// The default sink changed, OR its volume, mute state, description or
    /// icon changed (every output* property notifies through this signal).
    void defaultSinkChanged();
    /// Same for the default source and the input* properties (also emitted
    /// when `inputs` becomes empty or non-empty).
    void defaultSourceChanged();
    void settingsChanged();
    void maxVolumePercentChanged();
    void feedbackEnabledChanged();

private:
    void readSettings();
    void updateState();
    void updateAvailable();
    void updateMaxVolume();
    void setErrorText(const QString &text);
    void bindDefaultSink(PulseAudioQt::Sink *sink);
    void bindDefaultSource(PulseAudioQt::Source *source);
    int deviceMaxPercent(const PulseAudioQt::Device *device) const;

    PulseAudioQt::Context *m_context = nullptr;
    PulseAudioQt::SinkModel *m_sinkModel = nullptr;
    PulseAudioQt::SourceModel *m_sourceModel = nullptr;
    DeviceFilterModel *m_outputs = nullptr;
    DeviceFilterModel *m_inputs = nullptr;
    QPointer<PulseAudioQt::Sink> m_sink;
    QPointer<PulseAudioQt::Source> m_source;
    QList<QMetaObject::Connection> m_sinkConnections;
    QList<QMetaObject::Connection> m_sourceConnections;
    KConfigWatcher::Ptr m_configWatcher;
    std::unique_ptr<VolumeFeedback> m_feedback;
    QString m_errorText;
    bool m_available = false;
    bool m_inputsNonEmpty = false;
    int m_maxVolumePercent = 100;
    int m_volumeStep = 5;
    bool m_raiseMaximum = false;
    bool m_audioFeedback = true;
    bool m_feedbackEnabled = true;
};
