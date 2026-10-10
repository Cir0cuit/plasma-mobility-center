// SPDX-License-Identifier: GPL-2.0-or-later
//
// See batterymonitor.h Read-only: nothing here changes
// system state (no Refresh, no PowerDevil refreshStatus).

#include "batterymonitor.h"

#include <QDBusConnection>
#include <QDBusError>
#include <QTimer>

#include <Solid/Battery>
#include <Solid/DeviceNotifier>

#include <cmath>

#include "dbusasync.h"
#include "servicewatch.h"

using namespace Qt::StringLiterals;

namespace
{
const QString kUPowerService = u"org.freedesktop.UPower"_s;
const QString kUPowerPath = u"/org/freedesktop/UPower"_s;
const QString kUPowerIface = u"org.freedesktop.UPower"_s;
const QString kDisplayDevicePath = u"/org/freedesktop/UPower/devices/DisplayDevice"_s;
const QString kDeviceIface = u"org.freedesktop.UPower.Device"_s;

const QString kPdService = u"org.kde.Solid.PowerManagement"_s;
const QString kPdPath = u"/org/kde/Solid/PowerManagement"_s;
const QString kPdIface = u"org.kde.Solid.PowerManagement"_s;

// UPower device kind / state (org.freedesktop.UPower.Device).
constexpr uint kKindBattery = 2;

BatteryMonitor::ChargeState fromSolid(Solid::Battery::ChargeState state)
{
    switch (state) {
    case Solid::Battery::Charging:
        return BatteryMonitor::Charging;
    case Solid::Battery::Discharging:
        return BatteryMonitor::Discharging;
    case Solid::Battery::FullyCharged:
        return BatteryMonitor::FullyCharged;
    case Solid::Battery::NoCharge:
        break;
    }
    return BatteryMonitor::NoCharge;
}

// Solid's own mapping of UPower's State (upowerbattery.cpp).
BatteryMonitor::ChargeState fromUPowerState(uint state)
{
    switch (state) {
    case 1:
        return BatteryMonitor::Charging;
    case 2:
        return BatteryMonitor::Discharging;
    case 4:
        return BatteryMonitor::FullyCharged;
    default:
        return BatteryMonitor::NoCharge;
    }
}

// Not translated: the core module has no KI18n (the tile shows it as-is).
QString technologyName(Solid::Battery::Technology technology)
{
    switch (technology) {
    case Solid::Battery::LithiumIon:
        return u"Lithium ion"_s;
    case Solid::Battery::LithiumPolymer:
        return u"Lithium polymer"_s;
    case Solid::Battery::LithiumIronPhosphate:
        return u"Lithium iron phosphate"_s;
    case Solid::Battery::LeadAcid:
        return u"Lead acid"_s;
    case Solid::Battery::NickelCadmium:
        return u"Nickel cadmium"_s;
    case Solid::Battery::NickelMetalHydride:
        return u"Nickel metal hydride"_s;
    case Solid::Battery::UnknownTechnology:
        break;
    }
    return {};
}

int capacityOf(int reported, double energyFull, double energyFullDesign)
{
    if (reported > 0) {
        return qBound(0, reported, 100);
    }
    if (energyFull > 0 && energyFullDesign > 0) {
        return qBound(0, int(std::lround(energyFull / energyFullDesign * 100.0)), 100);
    }
    return -1;
}

double clampedFull(double energyFull, double energyFullDesign)
{
    return energyFullDesign > 0 ? qMin(energyFull, energyFullDesign) : energyFull;
}

bool isMonitoredBattery(const Solid::Device &device)
{
    // UPower's aggregate is not a battery of its own (Solid hides it today;
    // be explicit in case a backend ever lists it).
    if (device.udi().endsWith(u"/DisplayDevice"_s)) {
        return false;
    }
    const auto *battery = device.as<Solid::Battery>();
    return battery && battery->type() == Solid::Battery::PrimaryBattery && battery->isPowerSupply();
}
} // namespace

BatteryMonitor::BatteryMonitor(QObject *parent)
    : QObject(parent)
    , m_rebuildTimer(new QTimer(this))
{
    // Solid emits one signal per changed property; publish once per batch.
    m_rebuildTimer->setSingleShot(true);
    m_rebuildTimer->setInterval(0);
    connect(m_rebuildTimer, &QTimer::timeout, this, &BatteryMonitor::rebuild);

    auto *notifier = Solid::DeviceNotifier::instance();
    connect(notifier, &Solid::DeviceNotifier::deviceAdded, this, [this](const QString &udi) {
        const Solid::Device device(udi);
        if (device.is<Solid::Battery>()) {
            relistBatteries();
        }
    });
    connect(notifier, &Solid::DeviceNotifier::deviceRemoved, this, [this](const QString &udi) {
        for (const Solid::Device &device : std::as_const(m_devices)) {
            if (device.udi() == udi) {
                relistBatteries();
                return;
            }
        }
    });

    m_upowerWatch = std::make_unique<MC::ServiceWatch>(QDBusConnection::systemBus(), QStringList{kUPowerService}, this, [this](const QString &, bool registered) {
        registered ? onUPowerUp() : onUPowerDown();
    });
    m_powerDevilWatch = std::make_unique<MC::ServiceWatch>(QDBusConnection::sessionBus(), QStringList{kPdService}, this, [this](const QString &, bool registered) {
        registered ? onPowerDevilUp() : onPowerDevilDown();
    });

    relistBatteries();
}

BatteryMonitor::~BatteryMonitor() = default;

bool BatteryMonitor::available() const
{
    return m_state.available;
}

QString BatteryMonitor::errorText() const
{
    return m_errorText;
}

int BatteryMonitor::batteryCount() const
{
    return m_state.batteries.size();
}

int BatteryMonitor::percent() const
{
    return m_state.percent;
}

BatteryMonitor::ChargeState BatteryMonitor::chargeState() const
{
    return m_state.chargeState;
}

bool BatteryMonitor::pluggedIn() const
{
    return m_pluggedIn;
}

qint64 BatteryMonitor::remainingMsec() const
{
    return m_remainingMsec;
}

int BatteryMonitor::chargeStopThreshold() const
{
    return m_chargeStopThreshold;
}

void BatteryMonitor::setChargeStopThreshold(int threshold)
{
    if (threshold < 0 || threshold > 100) {
        threshold = -1;
    }
    if (m_chargeStopThreshold == threshold) {
        return;
    }
    m_chargeStopThreshold = threshold;
    Q_EMIT chargeStopThresholdChanged();
    updateTimes();
}

bool BatteryMonitor::limitReached() const
{
    return m_limitReached;
}

qint64 BatteryMonitor::timeToLimitMsec() const
{
    return m_timeToLimitMsec;
}

int BatteryMonitor::capacity() const
{
    return m_state.capacity;
}

double BatteryMonitor::energy() const
{
    return m_state.energy;
}

double BatteryMonitor::energyFull() const
{
    return m_state.energyFull;
}

double BatteryMonitor::energyFullDesign() const
{
    return m_state.energyFullDesign;
}

double BatteryMonitor::energyRate() const
{
    return m_state.energyRate;
}

int BatteryMonitor::cycleCount() const
{
    return m_state.cycleCount;
}

QString BatteryMonitor::technology() const
{
    return m_state.technology;
}

QString BatteryMonitor::productName() const
{
    return m_state.productName;
}

QVariantList BatteryMonitor::batteries() const
{
    return m_state.batteries;
}

void BatteryMonitor::refresh()
{
    relistBatteries();
    if (m_upowerWatch->isRegistered(kUPowerService)) {
        fetchUPower();
        fetchDisplayDevice();
    }
    if (m_powerDevilWatch->isRegistered(kPdService)) {
        fetchRemaining();
    }
}

void BatteryMonitor::relistBatteries()
{
    const QList<Solid::Device> all = Solid::Device::listFromType(Solid::DeviceInterface::Battery);
    QList<Solid::Device> devices;
    for (const Solid::Device &device : all) {
        if (isMonitoredBattery(device)) {
            devices.append(device);
        }
    }

    QStringList known;
    for (const Solid::Device &device : std::as_const(m_devices)) {
        known.append(device.udi());
    }
    for (const Solid::Device &device : std::as_const(devices)) {
        if (known.contains(device.udi())) {
            continue;
        }
        // New battery: follow every property we publish. The connections end
        // with the Solid::Battery object.
        const auto *battery = device.as<Solid::Battery>();
        const auto changed = [this] {
            scheduleRebuild();
        };
        connect(battery, &Solid::Battery::presentStateChanged, this, changed);
        connect(battery, &Solid::Battery::chargePercentChanged, this, changed);
        connect(battery, &Solid::Battery::capacityChanged, this, changed);
        connect(battery, &Solid::Battery::cycleCountChanged, this, changed);
        connect(battery, &Solid::Battery::chargeStateChanged, this, changed);
        connect(battery, &Solid::Battery::timeToEmptyChanged, this, changed);
        connect(battery, &Solid::Battery::timeToFullChanged, this, changed);
        connect(battery, &Solid::Battery::energyChanged, this, changed);
        connect(battery, &Solid::Battery::energyFullChanged, this, changed);
        connect(battery, &Solid::Battery::energyFullDesignChanged, this, changed);
        connect(battery, &Solid::Battery::energyRateChanged, this, changed);
    }
    // Disconnect batteries that went away but whose objects Solid keeps.
    for (const Solid::Device &device : std::as_const(m_devices)) {
        bool stillThere = false;
        for (const Solid::Device &d : std::as_const(devices)) {
            stillThere = stillThere || d.udi() == device.udi();
        }
        if (!stillThere) {
            if (const auto *battery = device.as<Solid::Battery>()) {
                disconnect(battery, nullptr, this, nullptr);
            }
        }
    }

    m_devices = devices;
    scheduleRebuild();
}

void BatteryMonitor::scheduleRebuild()
{
    if (!m_rebuildTimer->isActive()) {
        m_rebuildTimer->start();
    }
}

void BatteryMonitor::rebuild()
{
    Snapshot s;
    QList<const Solid::Battery *> present;

    double sumEnergy = 0;
    double sumFull = 0;
    double sumDesign = 0;
    double sumRate = 0;
    for (const Solid::Device &device : std::as_const(m_devices)) {
        const auto *b = device.as<Solid::Battery>();
        if (!b) {
            continue;
        }
        const double design = qMax(0.0, b->energyFullDesign());
        const double full = qMax(0.0, b->energyFull());
        QVariantMap map;
        map.insert(u"udi"_s, device.udi());
        map.insert(u"vendor"_s, device.vendor());
        map.insert(u"product"_s, device.product());
        map.insert(u"percent"_s, qBound(0, b->chargePercent(), 100));
        map.insert(u"chargeState"_s, int(fromSolid(b->chargeState())));
        map.insert(u"capacity"_s, capacityOf(b->capacity(), full, design));
        map.insert(u"energy"_s, qMax(0.0, b->energy()));
        map.insert(u"energyFull"_s, clampedFull(full, design));
        map.insert(u"energyFullDesign"_s, design);
        map.insert(u"energyRate"_s, std::abs(b->energyRate()));
        map.insert(u"cycleCount"_s, b->cycleCount() >= 0 ? b->cycleCount() : -1);
        map.insert(u"technology"_s, technologyName(b->technology()));
        map.insert(u"present"_s, b->isPresent());
        s.batteries.append(map);

        if (b->isPresent()) {
            present.append(b);
            sumEnergy += qMax(0.0, b->energy());
            sumFull += full;
            sumDesign += design;
            sumRate += std::abs(b->energyRate());
        }
    }

    if (!m_devices.isEmpty()) {
        const Solid::Device &first = m_devices.constFirst();
        s.productName = u"%1 %2"_s.arg(first.vendor(), first.product()).simplified();
        s.technology = s.batteries.constFirst().toMap().value(u"technology"_s).toString();
    }

    s.available = !present.isEmpty();
    if (present.size() == 1) {
        const Solid::Battery *b = present.constFirst();
        s.percent = qBound(0, b->chargePercent(), 100);
        s.chargeState = fromSolid(b->chargeState());
        s.energy = sumEnergy;
        s.energyFullRaw = sumFull;
        s.energyFullDesign = sumDesign;
        s.energyRate = sumRate;
        s.capacity = capacityOf(b->capacity(), sumFull, sumDesign);
        s.cycleCount = b->cycleCount() >= 0 ? b->cycleCount() : -1;
        s.timeToEmpty = qMax<qint64>(0, b->timeToEmpty());
        s.timeToFull = qMax<qint64>(0, b->timeToFull());
    } else if (present.size() > 1) {
        // Several batteries: UPower's DisplayDevice is the aggregate the rest
        // of Plasma shows; until it is known, sum up like UPower does.
        const bool display = m_displayDevice.value(u"IsPresent"_s).toBool() && m_displayDevice.value(u"Type"_s).toUInt() == kKindBattery;
        if (display) {
            s.percent = qBound(0, int(std::lround(m_displayDevice.value(u"Percentage"_s).toDouble())), 100);
            s.chargeState = fromUPowerState(m_displayDevice.value(u"State"_s).toUInt());
            s.energy = qMax(0.0, m_displayDevice.value(u"Energy"_s).toDouble());
            s.energyFullRaw = qMax(0.0, m_displayDevice.value(u"EnergyFull"_s).toDouble());
            s.energyRate = std::abs(m_displayDevice.value(u"EnergyRate"_s).toDouble());
            s.timeToEmpty = qMax<qint64>(0, m_displayDevice.value(u"TimeToEmpty"_s).toLongLong());
            s.timeToFull = qMax<qint64>(0, m_displayDevice.value(u"TimeToFull"_s).toLongLong());
        } else {
            s.percent = sumFull > 0 ? qBound(0, int(std::lround(sumEnergy / sumFull * 100.0)), 100) : 0;
            s.energy = sumEnergy;
            s.energyFullRaw = sumFull;
            s.energyRate = sumRate;
            // Any battery charging means the system charges.
            bool charging = false;
            bool discharging = false;
            bool full = true;
            for (const Solid::Battery *b : std::as_const(present)) {
                charging = charging || b->chargeState() == Solid::Battery::Charging;
                discharging = discharging || b->chargeState() == Solid::Battery::Discharging;
                full = full && b->chargeState() == Solid::Battery::FullyCharged;
            }
            s.chargeState = charging ? Charging : discharging ? Discharging : full ? FullyCharged : NoCharge;
        }
        s.energyFullDesign = sumDesign;
        s.capacity = capacityOf(0, sumFull, sumDesign);
        s.cycleCount = -1; // no meaningful total
    }
    s.energyFull = clampedFull(s.energyFullRaw, s.energyFullDesign);

    const Snapshot old = m_state;
    m_state = s;

    if (old.available != s.available) {
        Q_EMIT availableChanged();
    }
    if (old.batteries != s.batteries || old.technology != s.technology || old.productName != s.productName) {
        Q_EMIT batteriesChanged();
    }
    if (old.percent != s.percent) {
        Q_EMIT percentChanged();
    }
    if (old.chargeState != s.chargeState) {
        Q_EMIT chargeStateChanged();
    }
    if (old.capacity != s.capacity || old.energy != s.energy || old.energyFull != s.energyFull || old.energyFullDesign != s.energyFullDesign
        || old.energyRate != s.energyRate || old.cycleCount != s.cycleCount) {
        Q_EMIT detailsChanged();
    }
    updateTimes();
}

void BatteryMonitor::onUPowerUp()
{
    ++m_upowerGeneration;
    qDeleteAll(m_upowerRelays);
    m_upowerRelays.clear();

    const QDBusConnection bus = QDBusConnection::systemBus();
    m_upowerRelays << MC::DBus::onPropertiesChanged(bus, kUPowerService, kUPowerPath, kUPowerIface, this, [this](const QVariantMap &changed, const QStringList &invalidated) {
        if (changed.contains(u"OnBattery"_s)) {
            setOnBattery(true, changed.value(u"OnBattery"_s).toBool());
        } else if (invalidated.contains(u"OnBattery"_s)) {
            fetchUPower();
        }
    });
    m_upowerRelays << MC::DBus::onPropertiesChanged(bus, kUPowerService, kDisplayDevicePath, kDeviceIface, this, [this](const QVariantMap &changed, const QStringList &invalidated) {
        if (!invalidated.isEmpty()) {
            fetchDisplayDevice();
            return;
        }
        for (auto it = changed.cbegin(); it != changed.cend(); ++it) {
            m_displayDevice.insert(it.key(), it.value());
        }
        if (m_state.batteries.size() > 1) {
            scheduleRebuild();
        }
    });

    fetchUPower();
    fetchDisplayDevice();
    // A UPower restart may have re-created the devices behind Solid's back.
    relistBatteries();
}

void BatteryMonitor::onUPowerDown()
{
    ++m_upowerGeneration;
    qDeleteAll(m_upowerRelays);
    m_upowerRelays.clear();
    m_displayDevice.clear();
    setOnBattery(false, false);
    scheduleRebuild();
}

void BatteryMonitor::fetchUPower()
{
    const quint64 generation = m_upowerGeneration;
    MC::DBus::get(QDBusConnection::systemBus(),
                  kUPowerService,
                  kUPowerPath,
                  kUPowerIface,
                  u"OnBattery"_s,
                  this,
                  [this, generation](const QVariant &value, const QDBusError &error) {
                      if (generation != m_upowerGeneration) {
                          return;
                      }
                      if (error.isValid()) {
                          setErrorText(error.message());
                          setOnBattery(false, false);
                          return;
                      }
                      setErrorText({});
                      setOnBattery(true, value.toBool());
                  });
}

void BatteryMonitor::fetchDisplayDevice()
{
    const quint64 generation = m_upowerGeneration;
    MC::DBus::getAll(QDBusConnection::systemBus(),
                     kUPowerService,
                     kDisplayDevicePath,
                     kDeviceIface,
                     this,
                     [this, generation](const QVariantMap &props, const QDBusError &error) {
                         if (generation != m_upowerGeneration) {
                             return;
                         }
                         // No DisplayDevice (very old UPower): the Solid sums stay in use.
                         m_displayDevice = error.isValid() ? QVariantMap() : props;
                         scheduleRebuild();
                     });
}

void BatteryMonitor::setOnBattery(bool known, bool onBattery)
{
    const bool plugged = known && !onBattery;
    if (plugged != m_pluggedIn) {
        m_pluggedIn = plugged;
        Q_EMIT pluggedInChanged();
    }
    updateTimes();
}

void BatteryMonitor::onPowerDevilUp()
{
    const quint64 generation = ++m_powerDevilGeneration;
    delete m_powerDevilRelay;
    m_powerDevilRelay = MC::DBus::onSignal(QDBusConnection::sessionBus(),
                                           kPdService,
                                           kPdPath,
                                           kPdIface,
                                           u"smoothedBatteryRemainingTimeChanged"_s,
                                           this,
                                           [this, generation](const QDBusMessage &msg) {
                                               if (generation != m_powerDevilGeneration || msg.arguments().isEmpty()) {
                                                   return;
                                               }
                                               m_powerDevilKnown = true;
                                               m_powerDevilRemaining = qint64(msg.arguments().constFirst().toULongLong());
                                               updateTimes();
                                           });
    fetchRemaining();
}

void BatteryMonitor::onPowerDevilDown()
{
    ++m_powerDevilGeneration;
    delete m_powerDevilRelay;
    m_powerDevilRelay = nullptr;
    m_powerDevilKnown = false;
    m_powerDevilRemaining = 0;
    updateTimes();
}

void BatteryMonitor::fetchRemaining()
{
    const quint64 generation = m_powerDevilGeneration;
    const QDBusMessage msg = MC::DBus::methodCall(kPdService, kPdPath, kPdIface, u"smoothedBatteryRemainingTime"_s);
    MC::DBus::call(QDBusConnection::sessionBus(), msg, this, [this, generation](const QDBusMessage &reply) {
        if (generation != m_powerDevilGeneration) {
            return;
        }
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
            // Not offered (or failed): fall back to UPower's raw estimate.
            m_powerDevilKnown = false;
            m_powerDevilRemaining = 0;
        } else {
            m_powerDevilKnown = true;
            m_powerDevilRemaining = qint64(reply.arguments().constFirst().toULongLong());
        }
        updateTimes();
    });
}

void BatteryMonitor::updateTimes()
{
    const Snapshot &s = m_state;
    const bool moving = s.chargeState == Charging || s.chargeState == Discharging;

    qint64 remaining = 0;
    if (moving) {
        if (m_powerDevilKnown) {
            remaining = m_powerDevilRemaining;
        } else {
            remaining = 1000 * (s.chargeState == Charging ? s.timeToFull : s.timeToEmpty);
        }
    }

    const int stop = m_chargeStopThreshold;
    const bool hasLimit = stop > 0 && stop < 100;

    qint64 toLimit = 0;
    if (hasLimit && s.chargeState == Charging && s.energyRate > 0 && s.energyFullRaw > 0) {
        const double wh = s.energyFullRaw * stop / 100.0 - s.energy;
        if (wh > 0) {
            toLimit = qint64(std::llround(wh / s.energyRate * 3600.0 * 1000.0));
        }
    }

    // The battery applet's rule: the kernel may stop within a point of the limit.
    const bool reached = s.available && m_pluggedIn && hasLimit && qAbs(s.percent - stop) <= 1;

    if (remaining != m_remainingMsec) {
        m_remainingMsec = remaining;
        Q_EMIT remainingMsecChanged();
    }
    if (toLimit != m_timeToLimitMsec) {
        m_timeToLimitMsec = toLimit;
        Q_EMIT timeToLimitMsecChanged();
    }
    if (reached != m_limitReached) {
        m_limitReached = reached;
        Q_EMIT limitReachedChanged();
    }
}

void BatteryMonitor::setErrorText(const QString &text)
{
    if (m_errorText == text) {
        return;
    }
    m_errorText = text;
    Q_EMIT errorTextChanged();
}
