// SPDX-License-Identifier: GPL-2.0-or-later
//
// See screenbrightness.h

#include "screenbrightness.h"

#include <QDBusConnection>
#include <QDBusError>

#include "dbusasync.h"

using namespace Qt::StringLiterals;

namespace
{
const QString kService = u"org.kde.ScreenBrightness"_s;
const QString kRootPath = u"/org/kde/ScreenBrightness"_s;
const QString kRootIface = u"org.kde.ScreenBrightness"_s;
const QString kDisplayIface = u"org.kde.ScreenBrightness.Display"_s;

const QString kPmService = u"org.kde.Solid.PowerManagement"_s;
const QString kPmPath = u"/org/kde/Solid/PowerManagement"_s;
const QString kPmIface = u"org.kde.Solid.PowerManagement"_s;

constexpr int kThrottleMs = 50;
constexpr int kScreenOffDelayMs = 400;
constexpr uint kSuppressIndicator = 0x1;

QString displayPath(const QString &name)
{
    return kRootPath + u'/' + name;
}

int nextInstance()
{
    static int counter = 0;
    return ++counter;
}
} // namespace

ScreenBrightness::ScreenBrightness(QObject *parent)
    : QAbstractListModel(parent)
    , m_context(u"MobilityCenter-%1"_s.arg(nextInstance()))
    , m_throttle(new QTimer(this))
{
    m_throttle->setSingleShot(true);
    m_throttle->setInterval(kThrottleMs);
    connect(m_throttle, &QTimer::timeout, this, [this] {
        // Trailing edge: send what accumulated during the window, and keep
        // the window open while values keep coming.
        if (m_pendingWrites.isEmpty()) {
            return;
        }
        const QStringList names = m_pendingWrites.keys();
        for (const QString &name : names) {
            sendBrightness(name);
        }
        m_throttle->start();
    });

    const QDBusConnection bus = QDBusConnection::sessionBus();
    m_brightnessWatch = std::make_unique<MC::ServiceWatch>(bus, QStringList{kService}, this, [this](const QString &, bool registered) {
        registered ? onBrightnessServiceUp() : onBrightnessServiceDown();
    });
    m_pmWatch = std::make_unique<MC::ServiceWatch>(bus, QStringList{kPmService}, this, [this](const QString &, bool registered) {
        registered ? onPowerManagementUp() : onPowerManagementDown();
    });
}

ScreenBrightness::~ScreenBrightness() = default;

int ScreenBrightness::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_displays.size();
}

QVariant ScreenBrightness::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid)) {
        return {};
    }
    const Display &d = m_displays.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case LabelRole:
        return d.label;
    case NameRole:
        return d.name;
    case IsInternalRole:
        return d.isInternal;
    case BrightnessRole:
        return d.brightness;
    case MaxBrightnessRole:
        return d.maxBrightness;
    case PercentRole:
        return percentOf(d);
    }
    return {};
}

QHash<int, QByteArray> ScreenBrightness::roleNames() const
{
    return {
        {NameRole, "name"},
        {LabelRole, "label"},
        {IsInternalRole, "isInternal"},
        {BrightnessRole, "brightness"},
        {MaxBrightnessRole, "maxBrightness"},
        {PercentRole, "percent"},
    };
}

bool ScreenBrightness::available() const
{
    return m_available;
}

QString ScreenBrightness::errorText() const
{
    return m_errorText;
}

int ScreenBrightness::count() const
{
    return rowCount();
}

int ScreenBrightness::primaryRow() const
{
    return m_primaryRow;
}

QString ScreenBrightness::primaryName() const
{
    return m_primaryRow >= 0 ? m_displays.at(m_primaryRow).name : QString();
}

int ScreenBrightness::primaryPercent() const
{
    return m_primaryPercent;
}

bool ScreenBrightness::screenOffAvailable() const
{
    return m_screenOffAvailable;
}

bool ScreenBrightness::showOsd() const
{
    return m_showOsd;
}

void ScreenBrightness::setShowOsd(bool show)
{
    if (m_showOsd == show) {
        return;
    }
    m_showOsd = show;
    Q_EMIT showOsdChanged();
}

void ScreenBrightness::setPercent(const QString &name, int percent)
{
    const int row = rowOf(name);
    if (row < 0) {
        return;
    }
    Display &d = m_displays[row];
    if (d.maxBrightness <= 0) {
        return;
    }
    const int value = qRound(qBound(0, percent, 100) * d.maxBrightness / 100.0);

    // Optimistic update; BrightnessChanged from other clients corrects it.
    if (d.brightness != value) {
        d.brightness = value;
        const QModelIndex idx = index(row, 0);
        Q_EMIT dataChanged(idx, idx, {BrightnessRole, PercentRole});
        updateDerived();
    }

    m_pendingWrites.insert(name, value);
    if (!m_throttle->isActive()) {
        // Leading edge: send right away, then hold further writes for a window.
        sendBrightness(name);
        m_throttle->start();
    }
}

void ScreenBrightness::sendBrightness(const QString &name)
{
    const auto it = m_pendingWrites.constFind(name);
    if (it == m_pendingWrites.cend()) {
        return;
    }
    const int value = it.value();
    m_pendingWrites.erase(it);

    const uint flags = m_showOsd ? 0 : kSuppressIndicator;
    const QDBusMessage msg =
        MC::DBus::methodCall(kService, displayPath(name), kDisplayIface, u"SetBrightnessWithContext"_s, {value, flags, m_context});
    MC::DBus::call(QDBusConnection::sessionBus(), msg, this, [this](const QDBusMessage &reply) {
        if (reply.type() == QDBusMessage::ErrorMessage) {
            setErrorText(reply.errorMessage());
        } else {
            setErrorText({});
        }
    });
}

void ScreenBrightness::turnScreenOff()
{
    if (!m_screenOffAvailable) {
        return;
    }
    // Any input event wakes the screen again, so give the click time to end.
    QTimer::singleShot(kScreenOffDelayMs, this, [this] {
        const QDBusMessage msg = MC::DBus::methodCall(u"org.kde.kglobalaccel"_s,
                                                      u"/component/org_kde_powerdevil"_s,
                                                      u"org.kde.kglobalaccel.Component"_s,
                                                      u"invokeShortcut"_s,
                                                      {u"Turn Off Screen"_s});
        MC::DBus::call(QDBusConnection::sessionBus(), msg, this, [this](const QDBusMessage &reply) {
            if (reply.type() == QDBusMessage::ErrorMessage) {
                setErrorText(reply.errorMessage());
            }
        });
    });
}

void ScreenBrightness::refresh()
{
    if (m_brightnessWatch->isRegistered(kService)) {
        fetchDisplayNames();
        for (const Display &d : std::as_const(m_displays)) {
            fetchDisplay(d.name);
        }
    }
}

QVariantMap ScreenBrightness::get(int row) const
{
    QVariantMap map;
    if (row < 0 || row >= rowCount()) {
        return map;
    }
    const QModelIndex idx = index(row, 0);
    const auto roles = roleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it) {
        map.insert(QString::fromUtf8(it.value()), data(idx, it.key()));
    }
    return map;
}

void ScreenBrightness::onBrightnessServiceUp()
{
    ++m_generation;
    qDeleteAll(m_relays);
    m_relays.clear();

    const QDBusConnection bus = QDBusConnection::sessionBus();
    m_relays << MC::DBus::onPropertiesChanged(bus, kService, kRootPath, kRootIface, this, [this](const QVariantMap &changed, const QStringList &invalidated) {
        if (changed.contains(u"DisplaysDBusNames"_s)) {
            syncDisplays(changed.value(u"DisplaysDBusNames"_s).toStringList());
        } else if (invalidated.contains(u"DisplaysDBusNames"_s)) {
            fetchDisplayNames();
        }
    });
    // DisplaysDBusNames emits a change, but a re-read on Added/Removed costs
    // nothing and covers a PowerDevil that announces only the signal.
    m_relays << MC::DBus::onSignal(bus, kService, kRootPath, kRootIface, u"DisplayAdded"_s, this, [this](const QDBusMessage &) {
        fetchDisplayNames();
    });
    m_relays << MC::DBus::onSignal(bus, kService, kRootPath, kRootIface, u"DisplayRemoved"_s, this, [this](const QDBusMessage &) {
        fetchDisplayNames();
    });
    m_relays << MC::DBus::onSignal(bus, kService, kRootPath, kRootIface, u"BrightnessChanged"_s, this, [this](const QDBusMessage &msg) {
        onBrightnessChanged(msg);
    });

    fetchDisplayNames();
}

void ScreenBrightness::onBrightnessServiceDown()
{
    ++m_generation;
    qDeleteAll(m_relays);
    m_relays.clear();
    m_pendingWrites.clear();
    m_throttle->stop();

    if (!m_displays.isEmpty()) {
        beginResetModel();
        m_displays.clear();
        endResetModel();
        Q_EMIT countChanged();
    }
    updateDerived();
    setAvailable(false);
}

void ScreenBrightness::onPowerManagementUp()
{
    const quint64 generation = ++m_pmGeneration;
    delete m_pmRelay;
    const QDBusConnection bus = QDBusConnection::sessionBus();

    auto check = [this, generation] {
        const QDBusMessage msg = MC::DBus::methodCall(kPmService, kPmPath, kPmIface, u"isActionSupported"_s, {u"DPMSControl"_s});
        MC::DBus::call(QDBusConnection::sessionBus(), msg, this, [this, generation](const QDBusMessage &reply) {
            if (generation != m_pmGeneration) {
                return;
            }
            setScreenOffAvailable(reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()
                                  && reply.arguments().constFirst().toBool());
        });
    };
    m_pmRelay = MC::DBus::onSignal(bus, kPmService, kPmPath, kPmIface, u"supportedActionsChanged"_s, this, [check](const QDBusMessage &) {
        check();
    });
    check();
}

void ScreenBrightness::onPowerManagementDown()
{
    ++m_pmGeneration;
    delete m_pmRelay;
    m_pmRelay = nullptr;
    setScreenOffAvailable(false);
}

void ScreenBrightness::fetchDisplayNames()
{
    const quint64 generation = m_generation;
    MC::DBus::get(QDBusConnection::sessionBus(),
                  kService,
                  kRootPath,
                  kRootIface,
                  u"DisplaysDBusNames"_s,
                  this,
                  [this, generation](const QVariant &value, const QDBusError &error) {
                      if (generation != m_generation) {
                          return;
                      }
                      if (error.isValid()) {
                          setErrorText(error.message());
                          return;
                      }
                      syncDisplays(value.toStringList());
                      setAvailable(true);
                  });
}

void ScreenBrightness::syncDisplays(const QStringList &names)
{
    const int oldCount = m_displays.size();

    for (int row = m_displays.size() - 1; row >= 0; --row) {
        if (!names.contains(m_displays.at(row).name)) {
            beginRemoveRows({}, row, row);
            m_displays.removeAt(row);
            endRemoveRows();
        }
    }
    for (const QString &name : names) {
        if (rowOf(name) >= 0) {
            continue;
        }
        // Rows appear once their properties are known (fetchDisplay), so the
        // tile never shows a display with an unknown maximum.
        fetchDisplay(name);
    }

    if (m_displays.size() != oldCount) {
        Q_EMIT countChanged();
    }
    updateDerived();
}

void ScreenBrightness::fetchDisplay(const QString &name)
{
    const quint64 generation = m_generation;
    MC::DBus::getAll(QDBusConnection::sessionBus(),
                     kService,
                     displayPath(name),
                     kDisplayIface,
                     this,
                     [this, generation, name](const QVariantMap &props, const QDBusError &error) {
                         if (generation != m_generation) {
                             return;
                         }
                         if (error.isValid()) {
                             // The display may have gone between the list and this call.
                             return;
                         }
                         Display d;
                         d.name = name;
                         d.label = props.value(u"Label"_s).toString();
                         d.isInternal = props.value(u"IsInternal"_s).toBool();
                         d.maxBrightness = props.value(u"MaxBrightness"_s).toInt();
                         d.brightness = props.value(u"Brightness"_s).toInt();

                         const int row = rowOf(name);
                         if (row >= 0) {
                             m_displays[row] = d;
                             const QModelIndex idx = index(row, 0);
                             Q_EMIT dataChanged(idx, idx);
                         } else {
                             beginInsertRows({}, m_displays.size(), m_displays.size());
                             m_displays.append(d);
                             endInsertRows();
                             Q_EMIT countChanged();
                         }
                         updateDerived();
                     });
}

void ScreenBrightness::onBrightnessChanged(const QDBusMessage &msg)
{
    const QVariantList args = msg.arguments();
    if (args.size() < 4) {
        return;
    }
    const QString name = args.at(0).toString();
    const int value = args.at(1).toInt();
    const QString sourceClient = args.at(2).toString();
    const QString context = args.at(3).toString();

    // Our own write echoing back - the model already holds the value, and
    // applying it late would make a dragged slider jump backwards.
    if (sourceClient == QDBusConnection::sessionBus().baseService() && context == m_context) {
        return;
    }

    const int row = rowOf(name);
    if (row < 0) {
        return;
    }
    Display &d = m_displays[row];
    // No source: the display was replaced or reconfigured (e.g. a new label
    // or range). A value beyond the cached max means the range changed.
    if (sourceClient.isEmpty() || value > d.maxBrightness) {
        fetchDisplay(name);
        return;
    }
    if (d.brightness == value) {
        return;
    }
    d.brightness = value;
    const QModelIndex idx = index(row, 0);
    Q_EMIT dataChanged(idx, idx, {BrightnessRole, PercentRole});
    updateDerived();
}

void ScreenBrightness::updateDerived()
{
    int primary = m_displays.isEmpty() ? -1 : 0;
    for (int row = 0; row < m_displays.size(); ++row) {
        if (m_displays.at(row).isInternal) {
            primary = row;
            break;
        }
    }
    const int percent = primary >= 0 ? percentOf(m_displays.at(primary)) : -1;

    if (primary != m_primaryRow) {
        m_primaryRow = primary;
        Q_EMIT primaryRowChanged();
    }
    if (percent != m_primaryPercent) {
        m_primaryPercent = percent;
        Q_EMIT primaryPercentChanged();
    }
}

void ScreenBrightness::setAvailable(bool available)
{
    if (m_available == available) {
        return;
    }
    m_available = available;
    Q_EMIT availableChanged();
}

void ScreenBrightness::setScreenOffAvailable(bool available)
{
    if (m_screenOffAvailable == available) {
        return;
    }
    m_screenOffAvailable = available;
    Q_EMIT screenOffAvailableChanged();
}

void ScreenBrightness::setErrorText(const QString &text)
{
    if (m_errorText == text) {
        return;
    }
    m_errorText = text;
    Q_EMIT errorTextChanged();
}

int ScreenBrightness::rowOf(const QString &name) const
{
    for (int row = 0; row < m_displays.size(); ++row) {
        if (m_displays.at(row).name == name) {
            return row;
        }
    }
    return -1;
}

int ScreenBrightness::percentOf(const Display &d)
{
    if (d.maxBrightness <= 0) {
        return 0;
    }
    return qBound(0, qRound(d.brightness * 100.0 / d.maxBrightness), 100);
}
