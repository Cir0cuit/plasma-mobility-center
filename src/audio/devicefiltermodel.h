// SPDX-License-Identifier: GPL-2.0-or-later
//
// DeviceFilterModel - the output or input device list of the Volume tile
//. A QSortFilterProxyModel (dynamicSortFilter) over
// PulseAudioQt::SinkModel / SourceModel, created by AudioBackend.
//
// Hidden rows:
//   * name == "auto_null";
//   * devices with exactly one port whose availability is Unavailable;
//   * virtual devices (EasyEffects etc.) unless showVirtual (decision:
//     hidden by default).
// Monitor sources are already excluded by PulseAudioQt. Port changes arrive
// as portsChanged -> dataChanged, which re-filters by itself.
//
// Roles: every PulseAudioQt role passes through unchanged - the capitalised
// property names of the device, notably "Name" (QString), "Description"
// (QString), "Default" (bool), "Volume" (qint64), "Muted" (bool),
// "IconName" (QString) and "PulseObject" (the Sink/Source QObject*, for
// AudioBackend.setVolumePercent / toggleMute). Capitalised role names cannot
// be `required property` in delegates - read them through `model.Name`.
// Added on top (lower case): see ExtraRoles below.

#pragma once

#include <QQmlEngine>
#include <QSortFilterProxyModel>

class DeviceFilterModel : public QSortFilterProxyModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by AudioBackend.outputs / AudioBackend.inputs")

    /// Number of visible rows.
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    /// Which side this model lists.
    Q_PROPERTY(Kind kind READ kind CONSTANT)
    /// Also list virtual devices. Default false.
    Q_PROPERTY(bool showVirtual READ showVirtual WRITE setShowVirtual NOTIFY showVirtualChanged)

public:
    enum Kind {
        Outputs,
        Inputs,
    };
    Q_ENUM(Kind)

    enum ExtraRoles {
        PercentRole = Qt::UserRole + 1000, ///< "percent": int, volume in percent of normalVolume
        LabelRole, ///< "label": QString, Description, falling back to Name
    };
    Q_ENUM(ExtraRoles)

    explicit DeviceFilterModel(Kind kind, QObject *parent = nullptr);
    ~DeviceFilterModel() override;

    int count() const;
    Kind kind() const;
    bool showVirtual() const;
    void setShowVirtual(bool show);

    QHash<int, QByteArray> roleNames() const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    /// Row @p row as a map keyed by role name; {} if out of range.
    Q_INVOKABLE QVariantMap get(int row) const;

Q_SIGNALS:
    void countChanged();
    void showVirtualChanged();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    /// Source role id for @p name ("Name", "Volume", ...), -1 if absent.
    int sourceRole(const QByteArray &name) const;
    void onDataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight, const QList<int> &roles);

    Kind m_kind;
    bool m_showVirtual = false;
    // Role-name cache, rebuilt lazily whenever the source model differs.
    mutable QHash<QByteArray, int> m_sourceRoles;
    mutable const QAbstractItemModel *m_rolesSource = nullptr;
};
