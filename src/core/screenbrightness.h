// SPDX-License-Identifier: GPL-2.0-or-later
//
// ScreenBrightness - one row per display PowerDevil can dim.
//
// Source: session bus org.kde.ScreenBrightness (PowerDevil, Plasma >= 6.2),
// root object /org/kde/ScreenBrightness plus one object per display. All
// D-Bus traffic is asynchronous; the model empties itself and `available`
// drops to false while PowerDevil is not running, and everything is
// re-queried when it comes back.
//
// Tile rule: show the Brightness tile when `available && count > 0`.
//
// How it behaves:
//   * echo filter: drop BrightnessChanged whose sender is our own
//     baseService() AND whose context is this instance's "MobilityCenter-%1";
//   * BrightnessChanged with an empty source_client => re-GetAll that display;
//   * a value above the cached max => re-GetAll that display;
//   * writes: SetBrightnessWithContext(value, showOsd ? 0 : 0x1, ctx),
//     throttled to ~1 per 50 ms;
//   * never use BrightnessRangeChanged (declared, never emitted).

#pragma once

#include <QAbstractListModel>
#include <QDBusMessage>
#include <QHash>
#include <QQmlEngine>
#include <QTimer>
#include <QVariantMap>

#include <memory>

#include "servicewatch.h"

class ScreenBrightness : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

    /// PowerDevil's org.kde.ScreenBrightness service is registered and its
    /// display list has been read. Rows may still be 0 (VM, desktop without
    /// DDC/CI) - the tile also checks `count`.
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    /// Last error worth showing (failed write, D-Bus error); empty if none.
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)
    /// Number of controllable displays (== rowCount()).
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    /// Row of the first internal display, else 0; -1 when the model is empty.
    Q_PROPERTY(int primaryRow READ primaryRow NOTIFY primaryRowChanged)
    /// D-Bus name ("display0", ...) of primaryRow; empty when the model is empty.
    Q_PROPERTY(QString primaryName READ primaryName NOTIFY primaryRowChanged)
    /// Brightness of primaryRow in percent, 0..100 (0 = lowest *visible*
    /// level, PowerDevil offsets writes by the display's minimum); -1 when empty.
    Q_PROPERTY(int primaryPercent READ primaryPercent NOTIFY primaryPercentChanged)
    /// "Turn screen off" can be offered: PowerDevil
    /// (org.kde.Solid.PowerManagement) is registered AND
    /// isActionSupported("DPMSControl"). Re-evaluated on supportedActionsChanged
    /// and on every re-registration.
    Q_PROPERTY(bool screenOffAvailable READ screenOffAvailable NOTIFY screenOffAvailableChanged)
    /// Let PowerDevil show its brightness OSD for our changes (config key
    /// brightnessShowOsd). false (default) => flag 0x1 SuppressIndicator.
    Q_PROPERTY(bool showOsd READ showOsd WRITE setShowOsd NOTIFY showOsdChanged)

public:
    enum Roles {
        NameRole = Qt::UserRole + 1, ///< "name": QString, D-Bus object name, e.g. "display0" (numbering restarts after a PowerDevil restart)
        LabelRole, ///< "label": QString, human readable display label from PowerDevil
        IsInternalRole, ///< "isInternal": bool, built-in panel
        BrightnessRole, ///< "brightness": int, raw value 0..maxBrightness
        MaxBrightnessRole, ///< "maxBrightness": int, 10000 on KWin/Wayland, sysfs max on X11 - never assume a scale
        PercentRole, ///< "percent": int, round(brightness / maxBrightness * 100), 0..100
    };
    Q_ENUM(Roles)

    explicit ScreenBrightness(QObject *parent = nullptr);
    ~ScreenBrightness() override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool available() const;
    QString errorText() const;
    int count() const;
    int primaryRow() const;
    QString primaryName() const;
    int primaryPercent() const;
    bool screenOffAvailable() const;
    bool showOsd() const;
    void setShowOsd(bool show);

    /// Sets display @p name to @p percent (clamped 0..100). Safe to call on
    /// every slider move: writes are throttled (~1 per 50 ms, last value
    /// wins). The model updates optimistically and is corrected by
    /// BrightnessChanged. STATE-CHANGING: only call on user action.
    Q_INVOKABLE void setPercent(const QString &name, int percent);

    /// Turns the screens off through PowerDevil's "Turn Off Screen" global
    /// shortcut action (honours "lock before turning off"). The call is sent
    /// ~400 ms later, so call it on button *release*: any input event wakes
    /// the screen again. No-op unless screenOffAvailable.
    /// STATE-CHANGING: only call on user action.
    Q_INVOKABLE void turnScreenOff();

    /// Re-reads the display list and every display (GetAll). Read-only.
    Q_INVOKABLE void refresh();

    /// Row @p row as a map with the role names above as keys; {} if out of range.
    Q_INVOKABLE QVariantMap get(int row) const;

Q_SIGNALS:
    void availableChanged();
    void errorTextChanged();
    void countChanged();
    void primaryRowChanged();
    void primaryPercentChanged();
    void screenOffAvailableChanged();
    void showOsdChanged();

private:
    struct Display {
        QString name;
        QString label;
        bool isInternal = false;
        int brightness = 0;
        int maxBrightness = 0;
    };

    void onBrightnessServiceUp();
    void onBrightnessServiceDown();
    void onPowerManagementUp();
    void onPowerManagementDown();
    void fetchDisplayNames();
    void syncDisplays(const QStringList &names);
    void fetchDisplay(const QString &name);
    void onBrightnessChanged(const QDBusMessage &msg);
    void sendBrightness(const QString &name);
    void updateDerived();
    void setAvailable(bool available);
    void setScreenOffAvailable(bool available);
    void setErrorText(const QString &text);
    int rowOf(const QString &name) const;
    static int percentOf(const Display &d);

    QList<Display> m_displays;
    bool m_showOsd = false;
    bool m_available = false;
    bool m_screenOffAvailable = false;
    QString m_errorText;
    int m_primaryRow = -1;
    int m_primaryPercent = -1;
    // Bumped on every service (re)registration and loss, so replies to
    // requests sent to an earlier PowerDevil instance are dropped.
    quint64 m_generation = 0;
    quint64 m_pmGeneration = 0;
    const QString m_context;
    std::unique_ptr<MC::ServiceWatch> m_brightnessWatch;
    std::unique_ptr<MC::ServiceWatch> m_pmWatch;
    QList<QObject *> m_relays;
    QObject *m_pmRelay = nullptr;
    // Write throttle: the value waiting to be sent per display, and the
    // displays written within the current window.
    QHash<QString, int> m_pendingWrites;
    QTimer *m_throttle = nullptr;
};
