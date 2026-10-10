// SPDX-License-Identifier: GPL-2.0-or-later
//
// DisplayConfig - refresh-rate toggle and projection mode.
//
// Library: KF6::Screen (libkscreen). GetConfigOperation ->
// ConfigMonitor::instance()->addConfig(cfg); watch outputAdded/Removed and,
// per output, isConnectedChanged, isEnabledChanged, currentModeIdChanged,
// modesChanged, replicationSourceChanged, posChanged. If the operation fails,
// available = false and retry on configurationChanged.
//
// Writes: on cfg->clone(), check Config::canBeApplied(c,
// RequireAtLeastOneEnabledScreen), then new SetConfigOperation(c), handle
// finished -> hasError(). KWin persists the result (kwinoutputconfig.json);
// on X11 kscreen's kded does.
//
// The pure parts live in ProjectionLogic and RefreshRateLogic.
//
// NEVER introspect org.kde.kscreen.osdService or org.kde.KScreen: both are
// D-Bus activatable and introspection would start them.
// showKdeSwitcher() calls showActionSelector only on explicit user request.

#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QVariantList>

#include <KScreen/Types>

class QTimer;
namespace KScreen
{
class ConfigOperation;
}

class DisplayConfig : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    /// A libkscreen config was obtained and at least one output is connected.
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    /// Last error worth showing (SetConfigOperation failed, ...); empty if none.
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)
    /// A SetConfigOperation is running.
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    /// Connected outputs, excluding "Virtual-*" (screencast) outputs.
    Q_PROPERTY(int outputCount READ outputCount NOTIFY outputsChanged)

    /// The built-in panel (connected, enabled Output::Panel) offers >= 2
    /// refresh-rate groups at its current size. Hidden otherwise (e.g. a
    /// panel whose only native mode is 120 Hz, with lower rates offered only
    /// at other resolutions).
    Q_PROPERTY(bool refreshToggleAvailable READ refreshToggleAvailable NOTIFY refreshRatesChanged)
    /// Panel refresh-rate groups at the current size, qRound(Hz), descending.
    Q_PROPERTY(QList<int> refreshRates READ refreshRates NOTIFY refreshRatesChanged)
    /// Current panel refresh rate, qRound(Hz); 0 unknown.
    Q_PROPERTY(int currentRefreshRate READ currentRefreshRate NOTIFY refreshRatesChanged)
    /// What toggleRefreshRate() switches to: the highest group if the
    /// current one is not the highest, else the next lower one; 0 if none.
    Q_PROPERTY(int alternateRefreshRate READ alternateRefreshRate NOTIFY refreshRatesChanged)

    /// Exactly two connected non-virtual outputs: the projection chips show.
    Q_PROPERTY(bool projectionAvailable READ projectionAvailable NOTIFY outputsChanged)
    /// Current mode derived from enabled flags, replicationSource or
    /// overlapping geometry.
    Q_PROPERTY(ProjectionMode projectionMode READ projectionMode NOTIFY projectionModeChanged)
    /// Mirror is emulated (same position, largest common size, best rate,
    /// copied scale) because native replication is unavailable (libkscreen <
    /// 6.3.90 at build time, or no PerOutputScaling at run time, e.g. Plasma
    /// 6.3 / X11).
    Q_PROPERTY(bool mirrorEmulated READ mirrorEmulated NOTIFY outputsChanged)
    /// Human readable names (EDID vendor/model, else connector) of the
    /// internal and the external output; "" when !projectionAvailable.
    /// An enabled output can do HDR (internal panel preferred). The HDR
    /// switch shows when true.
    Q_PROPERTY(bool hdrAvailable READ hdrAvailable NOTIFY hdrChanged)
    /// HDR is on for that output.
    Q_PROPERTY(bool hdrEnabled READ hdrEnabled NOTIFY hdrChanged)
    /// Name of the output the HDR switch acts on; empty when none.
    Q_PROPERTY(QString hdrOutputName READ hdrOutputName NOTIFY hdrChanged)
    /// An enabled output supports automatic (ambient light) brightness -
    /// needs an ambient light sensor and Plasma >= 6.6 at build and run time.
    Q_PROPERTY(bool autoBrightnessAvailable READ autoBrightnessAvailable NOTIFY autoBrightnessChanged)
    /// Automatic brightness is on for that output.
    Q_PROPERTY(bool autoBrightnessEnabled READ autoBrightnessEnabled NOTIFY autoBrightnessChanged)
    Q_PROPERTY(QString internalName READ internalName NOTIFY outputsChanged)
    Q_PROPERTY(QString externalName READ externalName NOTIFY outputsChanged)

public:
    /// Values equal ProjectionLogic::Mode.
    enum ProjectionMode {
        Unknown = 0,
        InternalOnly,
        Mirror,
        Extend,
        ExternalOnly,
    };
    Q_ENUM(ProjectionMode)

    explicit DisplayConfig(QObject *parent = nullptr);
    ~DisplayConfig() override;

    bool available() const;
    QString errorText() const;
    bool busy() const;
    int outputCount() const;
    bool refreshToggleAvailable() const;
    QList<int> refreshRates() const;
    int currentRefreshRate() const;
    int alternateRefreshRate() const;
    bool projectionAvailable() const;
    ProjectionMode projectionMode() const;
    bool mirrorEmulated() const;
    bool hdrAvailable() const;
    bool hdrEnabled() const;
    QString hdrOutputName() const;
    bool autoBrightnessAvailable() const;
    bool autoBrightnessEnabled() const;
    QString internalName() const;
    QString externalName() const;

    // STATE-CHANGING (display configuration): only call on user action.

    /// Switches the panel to the best mode of group @p hz at the current size.
    Q_INVOKABLE void setRefreshRate(int hz);
    /// setRefreshRate(alternateRefreshRate).
    Q_INVOKABLE void toggleRefreshRate();
    /// Applies @p mode (a ProjectionMode) via ProjectionLogic::apply.
    Q_INVOKABLE void setProjectionMode(int mode);

    /// Switches HDR on/off for hdrOutputName. STATE-CHANGING: user action only.
    Q_INVOKABLE void setHdrEnabled(bool enabled);

    /// Switches automatic brightness on/off. STATE-CHANGING: user action only.
    Q_INVOKABLE void setAutoBrightnessEnabled(bool enabled);
    /// Opens KDE's own display switcher OSD (org.kde.kscreen.osdService
    /// showActionSelector). Only on explicit user request: the call
    /// D-Bus-activates the service.
    Q_INVOKABLE void showKdeSwitcher();

    /// Fetches a fresh config (GetConfigOperation). Read-only.
    Q_INVOKABLE void refresh();

Q_SIGNALS:
    void availableChanged();
    void errorTextChanged();
    void busyChanged();
    void outputsChanged();
    void refreshRatesChanged();
    void projectionModeChanged();
    void hdrChanged();
    void autoBrightnessChanged();

private:
    /// Everything the properties expose, derived from m_config in one go so
    /// that change signals fire only for what actually changed.
    struct State {
        bool available = false;
        int outputCount = 0;
        QList<int> refreshRates;
        int currentRefreshRate = 0;
        bool projectionAvailable = false;
        int projectionMode = 0;
        bool mirrorEmulated = true;
        QString internalName;
        QString externalName;
        bool hdrAvailable = false;
        bool hdrEnabled = false;
        QString hdrOutputName;
        bool autoBrightnessAvailable = false;
        bool autoBrightnessEnabled = false;
    };

    void onGetConfigFinished(KScreen::ConfigOperation *op, quint64 generation);
    void setConfig(const KScreen::ConfigPtr &config);
    void watchOutput(const KScreen::OutputPtr &output);
    void scheduleUpdate();
    void updateState();
    State computeState() const;
    void applyConfig(const KScreen::ConfigPtr &config);
    void setBusy(bool busy);
    void setErrorText(const QString &text);

    KScreen::ConfigPtr m_config;
    State m_state;
    QString m_errorText;
    bool m_busy = false;
    bool m_fetching = false;
    quint64 m_generation = 0;
    QTimer *m_updateTimer = nullptr;
};
