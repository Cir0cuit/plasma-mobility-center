// SPDX-License-Identifier: GPL-2.0-or-later
//
// See devicefiltermodel.h
//
// Everything is read through the source model's role names (as PulseAudioQt
// publishes them), never through the Sink/Source objects, so the rules work
// unchanged on a plain fake model in the unit test.

#include "devicefiltermodel.h"

#include <QObject>
#include <QQmlEngine>

#include <PulseAudioQt/Context>
#include <PulseAudioQt/Port>

using namespace Qt::StringLiterals;

namespace
{
const QByteArray kNameRole = "Name"_ba;
const QByteArray kDescriptionRole = "Description"_ba;
const QByteArray kVolumeRole = "Volume"_ba;
const QByteArray kPortsRole = "Ports"_ba;
const QByteArray kVirtualRole = "VirtualDevice"_ba;
const QByteArray kPulseObjectRole = "PulseObject"_ba;

const QString kDummyOutputName = u"auto_null"_s;

/// True when @p ports (the "Ports" role: QList<Port*>, or any list of
/// QObjects with an `availability` property) holds exactly one port and that
/// port is Unavailable - plasma-pa's "inactive device" rule.
bool isSingleUnavailablePort(const QVariant &ports)
{
    if (!ports.isValid() || !ports.canConvert<QVariantList>()) {
        return false;
    }
    const QVariantList list = ports.toList();
    if (list.size() != 1) {
        return false;
    }
    const QObject *port = list.constFirst().value<QObject *>();
    if (!port) {
        return false;
    }
    const QVariant availability = port->property("availability");
    return availability.isValid() && availability.toInt() == PulseAudioQt::Port::Unavailable;
}
} // namespace

DeviceFilterModel::DeviceFilterModel(Kind kind, QObject *parent)
    : QSortFilterProxyModel(parent)
    , m_kind(kind)
{
    setDynamicSortFilter(true);
    connect(this, &QAbstractItemModel::rowsInserted, this, &DeviceFilterModel::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &DeviceFilterModel::countChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &DeviceFilterModel::countChanged);
    connect(this, &QAbstractItemModel::layoutChanged, this, &DeviceFilterModel::countChanged);
    connect(this, &QAbstractItemModel::dataChanged, this, &DeviceFilterModel::onDataChanged);
}

DeviceFilterModel::~DeviceFilterModel() = default;

int DeviceFilterModel::count() const
{
    return rowCount();
}

DeviceFilterModel::Kind DeviceFilterModel::kind() const
{
    return m_kind;
}

bool DeviceFilterModel::showVirtual() const
{
    return m_showVirtual;
}

void DeviceFilterModel::setShowVirtual(bool show)
{
    if (m_showVirtual == show) {
        return;
    }
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
    m_showVirtual = show;
    endFilterChange(Direction::Rows);
#else
    m_showVirtual = show;
    invalidateFilter();
#endif
    Q_EMIT showVirtualChanged();
}

QHash<int, QByteArray> DeviceFilterModel::roleNames() const
{
    QHash<int, QByteArray> roles = QSortFilterProxyModel::roleNames();
    roles.insert(PercentRole, "percent");
    roles.insert(LabelRole, "label");
    return roles;
}

QVariant DeviceFilterModel::data(const QModelIndex &index, int role) const
{
    switch (role) {
    case PercentRole: {
        const int volumeRole = sourceRole(kVolumeRole);
        if (volumeRole < 0 || !index.isValid()) {
            return {};
        }
        const QVariant volume = QSortFilterProxyModel::data(index, volumeRole);
        if (!volume.isValid()) {
            return {};
        }
        return qRound(volume.toLongLong() * 100.0 / PulseAudioQt::normalVolume());
    }
    case LabelRole: {
        if (!index.isValid()) {
            return {};
        }
        const int descriptionRole = sourceRole(kDescriptionRole);
        if (descriptionRole >= 0) {
            const QString description = QSortFilterProxyModel::data(index, descriptionRole).toString();
            if (!description.isEmpty()) {
                return description;
            }
        }
        const int nameRole = sourceRole(kNameRole);
        return nameRole >= 0 ? QSortFilterProxyModel::data(index, nameRole).toString() : QString();
    }
    default: {
        QVariant value = QSortFilterProxyModel::data(index, role);
        if (role == sourceRole(kPulseObjectRole)) {
            // The Sink/Source belongs to PulseAudioQt (shared with plasma-pa):
            // QML must never garbage-collect it.
            if (QObject *object = value.value<QObject *>()) {
                QQmlEngine::setObjectOwnership(object, QQmlEngine::CppOwnership);
            }
        }
        return value;
    }
    }
}

QVariantMap DeviceFilterModel::get(int row) const
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

bool DeviceFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    const QAbstractItemModel *source = sourceModel();
    if (!source) {
        return false;
    }
    const QModelIndex idx = source->index(sourceRow, 0, sourceParent);

    if (const int role = sourceRole(kNameRole); role >= 0 && source->data(idx, role).toString() == kDummyOutputName) {
        return false;
    }
    if (const int role = sourceRole(kPortsRole); role >= 0 && isSingleUnavailablePort(source->data(idx, role))) {
        return false;
    }
    if (!m_showVirtual) {
        if (const int role = sourceRole(kVirtualRole); role >= 0 && source->data(idx, role).toBool()) {
            return false;
        }
    }
    return true;
}

int DeviceFilterModel::sourceRole(const QByteArray &name) const
{
    const QAbstractItemModel *source = sourceModel();
    if (m_rolesSource != source) {
        // PulseAudioQt fixes its role names at construction, so one lookup
        // per source model is enough.
        m_rolesSource = source;
        m_sourceRoles.clear();
        if (source) {
            const auto roles = source->roleNames();
            for (auto it = roles.cbegin(); it != roles.cend(); ++it) {
                m_sourceRoles.insert(it.value(), it.key());
            }
        }
    }
    return m_sourceRoles.value(name, -1);
}

void DeviceFilterModel::onDataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight, const QList<int> &roles)
{
    // The extra roles derive from source roles: tell views about them too.
    // (Our own re-emission carries only extra roles, so it does not recurse.)
    if (roles.isEmpty()) {
        return;
    }
    QList<int> extra;
    if (roles.contains(sourceRole(kVolumeRole))) {
        extra << PercentRole;
    }
    if (roles.contains(sourceRole(kDescriptionRole)) || roles.contains(sourceRole(kNameRole))) {
        extra << LabelRole;
    }
    if (!extra.isEmpty()) {
        Q_EMIT dataChanged(topLeft, bottomRight, extra);
    }
}
