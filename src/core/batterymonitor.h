// SPDX-License-Identifier: GPL-2.0-or-later
//
// BatteryMonitor - battery level, AC state, remaining time and health
//.
//
// Sources:
//   * Solid::Battery for every PrimaryBattery that isPowerSupply() (watch
//     DeviceNotifier added/removed and presentStateChanged);
//   * system bus UPower OnBattery for the AC state (Solid has no AC adapter
//     with the UPower backend) - service watcher + PropertiesChanged;
//   * PowerDevil smoothedBatteryRemainingTime()/...Changed(t) for the time;
//   * UPower DisplayDevice for the aggregate of several batteries.
// Read-only: this class never changes system state.
//
// Aggregate properties describe the DisplayDevice (or the only battery);
// `batteries` lists each battery separately for the details view.

#pragma once

#include <QList>
#include <QObject>
#include <QQmlEngine>
#include <QVariantList>
#include <QVariantMap>

#include <Solid/Device>

#include <memory>

class QTimer;

namespace MC
{
class ServiceWatch;
}

class BatteryMonitor : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    /// At least one primary, power-supplying battery is present.
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    /// Last error worth showing; empty if none.
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)
    /// Number of primary batteries.
    Q_PROPERTY(int batteryCount READ batteryCount NOTIFY batteriesChanged)
    /// Aggregate charge, 0..100.
    Q_PROPERTY(int percent READ percent NOTIFY percentChanged)
    /// Aggregate ChargeState.
    Q_PROPERTY(ChargeState chargeState READ chargeState NOTIFY chargeStateChanged)
    /// On AC power (UPower OnBattery == false). false while UPower is unknown.
    Q_PROPERTY(bool pluggedIn READ pluggedIn NOTIFY pluggedInChanged)
    /// PowerDevil's smoothed remaining time in ms: to empty while discharging,
    /// to 100 % while charging. 0 = unknown / not applicable.
    Q_PROPERTY(qint64 remainingMsec READ remainingMsec NOTIFY remainingMsecChanged)
    /// INPUT from QML: ChargeLimit.stopThreshold when ChargeLimit is
    /// available, else -1 (no limit). Drives limitReached and timeToLimitMsec.
    Q_PROPERTY(int chargeStopThreshold READ chargeStopThreshold WRITE setChargeStopThreshold NOTIFY chargeStopThresholdChanged)
    /// Plugged in and percent within chargeStopThreshold +- 1: the UI says
    /// "Limited to N %" instead of a time (the battery applet's rule).
    Q_PROPERTY(bool limitReached READ limitReached NOTIFY limitReachedChanged)
    /// While charging towards a stop threshold:
    /// (energyFull * stop / 100 - energy) / energyRate, in ms. 0 = unknown or
    /// no threshold (then use remainingMsec).
    Q_PROPERTY(qint64 timeToLimitMsec READ timeToLimitMsec NOTIFY timeToLimitMsecChanged)
    /// Health: energyFull / energyFullDesign in percent, 0..100 (UPower caps
    /// it at 100). -1 = unknown.
    Q_PROPERTY(int capacity READ capacity NOTIFY detailsChanged)
    /// Current energy in Wh. 0 = unknown.
    Q_PROPERTY(double energy READ energy NOTIFY detailsChanged)
    /// Energy when full, Wh (clamped to energyFullDesign). 0 = unknown.
    Q_PROPERTY(double energyFull READ energyFull NOTIFY detailsChanged)
    /// Design energy when full, Wh. 0 = unknown.
    Q_PROPERTY(double energyFullDesign READ energyFullDesign NOTIFY detailsChanged)
    /// Charge/discharge rate in W, always >= 0 (direction: chargeState). 0 = idle/unknown.
    Q_PROPERTY(double energyRate READ energyRate NOTIFY detailsChanged)
    /// Charge cycles; -1 = unknown (Solid::Battery::cycleCount, KF >= 6.9).
    Q_PROPERTY(int cycleCount READ cycleCount NOTIFY detailsChanged)
    /// Battery technology, e.g. "Lithium ion"; empty if unknown (constant per battery).
    Q_PROPERTY(QString technology READ technology NOTIFY batteriesChanged)
    /// Vendor + model of the first battery, for the details view; may be empty.
    Q_PROPERTY(QString productName READ productName NOTIFY batteriesChanged)
    /// One map per battery, keys: udi (QString), vendor (QString),
    /// product (QString), percent (int 0..100), chargeState (int ChargeState),
    /// capacity (int %, -1 unknown), energy / energyFull / energyFullDesign
    /// (double Wh), energyRate (double W), cycleCount (int, -1 unknown),
    /// technology (QString), present (bool).
    Q_PROPERTY(QVariantList batteries READ batteries NOTIFY batteriesChanged)

public:
    /// Same values as Solid::Battery::ChargeState.
    enum ChargeState {
        NoCharge = 0, ///< neither charging nor discharging (e.g. held at a charge limit)
        Charging = 1,
        Discharging = 2,
        FullyCharged = 3,
    };
    Q_ENUM(ChargeState)

    explicit BatteryMonitor(QObject *parent = nullptr);
    ~BatteryMonitor() override;

    bool available() const;
    QString errorText() const;
    int batteryCount() const;
    int percent() const;
    ChargeState chargeState() const;
    bool pluggedIn() const;
    qint64 remainingMsec() const;
    int chargeStopThreshold() const;
    void setChargeStopThreshold(int threshold);
    bool limitReached() const;
    qint64 timeToLimitMsec() const;
    int capacity() const;
    double energy() const;
    double energyFull() const;
    double energyFullDesign() const;
    double energyRate() const;
    int cycleCount() const;
    QString technology() const;
    QString productName() const;
    QVariantList batteries() const;

    /// Re-reads everything (Solid, UPower, PowerDevil). Read-only.
    Q_INVOKABLE void refresh();

Q_SIGNALS:
    void availableChanged();
    void errorTextChanged();
    void batteriesChanged();
    void percentChanged();
    void chargeStateChanged();
    void pluggedInChanged();
    void remainingMsecChanged();
    void chargeStopThresholdChanged();
    void limitReachedChanged();
    void timeToLimitMsecChanged();
    void detailsChanged();

private:
    // Aggregate values as published (energyFull already clamped).
    struct Snapshot {
        bool available = false;
        int percent = 0;
        ChargeState chargeState = NoCharge;
        int capacity = -1;
        double energy = 0;
        double energyFull = 0;
        double energyFullRaw = 0; // unclamped, for time-to-limit
        double energyFullDesign = 0;
        double energyRate = 0;
        int cycleCount = -1;
        qint64 timeToEmpty = 0; // s
        qint64 timeToFull = 0; // s
        QString technology;
        QString productName;
        QVariantList batteries;
    };

    void relistBatteries();
    void scheduleRebuild();
    void rebuild();
    void onUPowerUp();
    void onUPowerDown();
    void fetchUPower();
    void fetchDisplayDevice();
    void onPowerDevilUp();
    void onPowerDevilDown();
    void fetchRemaining();
    void setOnBattery(bool known, bool onBattery);
    void updateTimes();
    void setErrorText(const QString &text);

    int m_chargeStopThreshold = -1;
    Snapshot m_state;
    QString m_errorText;
    // Primary, power-supplying batteries from Solid (kept alive here so the
    // Solid::Battery interfaces stay valid).
    QList<Solid::Device> m_devices;
    QTimer *m_rebuildTimer = nullptr;

    // UPower (system bus): OnBattery + DisplayDevice.
    std::unique_ptr<MC::ServiceWatch> m_upowerWatch;
    quint64 m_upowerGeneration = 0;
    QList<QObject *> m_upowerRelays;
    bool m_pluggedIn = false;
    QVariantMap m_displayDevice;

    // PowerDevil (session bus): smoothed remaining time.
    std::unique_ptr<MC::ServiceWatch> m_powerDevilWatch;
    quint64 m_powerDevilGeneration = 0;
    QObject *m_powerDevilRelay = nullptr;
    bool m_powerDevilKnown = false;
    qint64 m_powerDevilRemaining = 0;

    // Derived.
    qint64 m_remainingMsec = 0;
    bool m_limitReached = false;
    qint64 m_timeToLimitMsec = 0;
};
