// SPDX-License-Identifier: GPL-2.0-or-later
//
// KeyboardBacklight - keyboard backlight level.
//
// Primary path: session bus org.kde.Solid.PowerManagement,
//   /org/kde/Solid/PowerManagement/Actions/KeyboardBrightnessControl.
// Fallback (PowerDevil absent, action unsupported, or 6.3.0's start-up race):
//   system bus org.freedesktop.UPower /org/freedesktop/UPower/KbdBacklight.
// Both speak RAW LEVELS 0..max (not percent, despite PowerDevil's parameter
// name). Never talk to UPower's per-LED objects.
//
// Rules the implementation must keep:
//   * subscribe (supportedActionsChanged, value/max signals) BEFORE querying;
//   * available = supported && max > 0 (PowerDevil 6.3.0-6.7.5 with UPower
//     >= 1.91.4 reports "supported" with max 0 on machines without a backlight);
//   * keyboardBrightnessMaxChanged is emitted on every change - idempotent;
//   * UPower DaemonVersion == "1.91.4" never signals Fn-key changes, so
//     rereadOnShow is true and the tile calls refresh() whenever it becomes
//     visible, is hovered or is expanded.

#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QString>

#include <memory>

#include "servicewatch.h"

class KeyboardBacklight : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    /// A controllable keyboard backlight exists (max > 0) on either path.
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    /// Last error worth showing; empty if none.
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)
    /// Current raw level, 0..maxBrightness (0 = off).
    Q_PROPERTY(int brightness READ brightness NOTIFY brightnessChanged)
    /// Highest raw level, > 0 when available (e.g. 3 on most laptops).
    Q_PROPERTY(int maxBrightness READ maxBrightness NOTIFY maxBrightnessChanged)
    /// Number of discrete non-off levels; equals maxBrightness. The tile uses
    /// a segmented control when steps <= 4, else a stepped slider.
    Q_PROPERTY(int steps READ steps NOTIFY maxBrightnessChanged)
    /// round(brightness / maxBrightness * 100), 0..100; 0 when unavailable.
    Q_PROPERTY(int percent READ percent NOTIFY brightnessChanged)
    /// Values come from the UPower fallback rather than PowerDevil.
    Q_PROPERTY(bool viaUPower READ viaUPower NOTIFY viaUPowerChanged)
    /// Changes made with Fn keys are not signalled (UPower 1.91.4): the tile
    /// must call refresh() when it is shown, hovered or expanded.
    Q_PROPERTY(bool rereadOnShow READ rereadOnShow NOTIFY rereadOnShowChanged)

public:
    explicit KeyboardBacklight(QObject *parent = nullptr);
    ~KeyboardBacklight() override;

    bool available() const;
    QString errorText() const;
    int brightness() const;
    int maxBrightness() const;
    int steps() const;
    int percent() const;
    bool viaUPower() const;
    bool rereadOnShow() const;

    /// Sets the raw @p level (clamped 0..maxBrightness). @p silent = true uses
    /// setKeyboardBrightnessSilent (no OSD) - use it while dragging; the UPower
    /// path never shows an OSD. STATE-CHANGING: only call on user action.
    Q_INVOKABLE void setLevel(int level, bool silent = true);

    /// Re-reads level and max (asynchronously). Read-only; cheap.
    Q_INVOKABLE void refresh();

Q_SIGNALS:
    void availableChanged();
    void errorTextChanged();
    void brightnessChanged();
    void maxBrightnessChanged();
    void viaUPowerChanged();
    void rereadOnShowChanged();

private:
    enum class Path {
        None,
        PowerDevil,
        UPower,
    };

    // What each source last told us; -1 = not read (yet) or unreadable.
    struct PowerDevilState {
        bool registered = false;
        bool supported = false;
        int max = -1;
        int brightness = -1;
    };
    struct UPowerState {
        bool registered = false;
        int max = -1;
        int brightness = -1;
        QString daemonVersion;
    };

    void onPowerDevilUp();
    void onPowerDevilDown();
    void checkPowerDevilSupported();
    void readPowerDevil(bool readBrightness);
    void onUPowerUp();
    void onUPowerDown();
    void readUPower(bool readVersion);
    void update();
    void setErrorText(const QString &text);

    Path m_path = Path::None;
    int m_brightness = 0;
    int m_max = 0;
    bool m_rereadOnShow = false;
    QString m_errorText;

    PowerDevilState m_pd;
    UPowerState m_up;
    // Bumped on every (re)registration and loss of the service, so replies
    // to requests sent to an earlier instance are dropped.
    quint64 m_pdGeneration = 0;
    quint64 m_upGeneration = 0;
    std::unique_ptr<MC::ServiceWatch> m_pdWatch;
    std::unique_ptr<MC::ServiceWatch> m_upWatch;
    QList<QObject *> m_pdRelays;
    QList<QObject *> m_upRelays;
};
