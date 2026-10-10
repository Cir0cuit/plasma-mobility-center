// SPDX-License-Identifier: GPL-2.0-or-later
//
// DeviceFilterModel - pure logic, no D-Bus, no sound server.
//
// AudioBackend is never created here: PulseAudioQt::Context is a process
// singleton bound to the real server. Instead a small list model exposes
// the same role names PulseAudioQt's SinkModel/SourceModel publish (checked
// against the Sink/Source meta-objects in roleContract()), and fake ports
// carry an `availability` property like PulseAudioQt::Port.

#include <QAbstractListModel>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QTest>

#include <PulseAudioQt/Context>
#include <PulseAudioQt/Port>
#include <PulseAudioQt/Sink>
#include <PulseAudioQt/Source>

#include "devicefiltermodel.h"

using namespace Qt::StringLiterals;

namespace
{
// PulseAudioQt::AbstractModel numbering: PulseObject = UserRole + 1, then
// the SortByDefault role, then one role per (capitalised) property.
enum FakeRole {
    PulseObjectRole = Qt::UserRole + 1,
    SortByDefaultRole,
    NameRole,
    DescriptionRole,
    VolumeRole,
    MutedRole,
    DefaultRole,
    PortsRole,
    VirtualDeviceRole,
};

constexpr qint64 kNormal = 65536; // PA_VOLUME_NORM
} // namespace

class FakePort : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int availability READ availability NOTIFY availabilityChanged)
public:
    explicit FakePort(int availability, QObject *parent = nullptr)
        : QObject(parent)
        , m_availability(availability)
    {
    }
    int availability() const
    {
        return m_availability;
    }
Q_SIGNALS:
    void availabilityChanged();

private:
    int m_availability;
};

/// Stand-in for PulseAudioQt::SinkModel: fixed role names, and one
/// dataChanged(row, row, {role}) per changed value, as AbstractModel emits.
class FakeSourceModel : public QAbstractListModel
{
    Q_OBJECT
public:
    FakeSourceModel(const QHash<int, QByteArray> &roles, QObject *parent)
        : QAbstractListModel(parent)
        , m_roles(roles)
    {
    }
    QHash<int, QByteArray> roleNames() const override
    {
        return m_roles;
    }
    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : int(m_rows.size());
    }
    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!checkIndex(index, CheckIndexOption::IndexIsValid)) {
            return {};
        }
        return m_rows.at(index.row()).value(role);
    }
    int appendRow(const QHash<int, QVariant> &values)
    {
        const int row = int(m_rows.size());
        beginInsertRows({}, row, row);
        m_rows << values;
        endInsertRows();
        return row;
    }
    void removeRow(int row)
    {
        beginRemoveRows({}, row, row);
        m_rows.removeAt(row);
        endRemoveRows();
    }
    void set(int row, int role, const QVariant &value)
    {
        m_rows[row].insert(role, value);
        Q_EMIT dataChanged(index(row), index(row), {role});
    }

private:
    QHash<int, QByteArray> m_roles;
    QList<QHash<int, QVariant>> m_rows;
};

class FakeDevice : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
};

class TestDeviceFilterModel : public QObject
{
    Q_OBJECT

    QList<QObject *> m_owned;

    FakeSourceModel *makeSource()
    {
        auto *model = new FakeSourceModel(
            {
            {Qt::DisplayRole, "display"},
            {PulseObjectRole, "PulseObject"},
            {SortByDefaultRole, "SortByDefault"},
            {NameRole, "Name"},
            {DescriptionRole, "Description"},
            {VolumeRole, "Volume"},
            {MutedRole, "Muted"},
            {DefaultRole, "Default"},
            {PortsRole, "Ports"},
            {VirtualDeviceRole, "VirtualDevice"},
            },
            this);
        return model;
    }

    /// Ports as PulseAudioQt hands them out: QList<Port*> (here QList<FakePort*>).
    QVariant ports(const QList<int> &availabilities)
    {
        QList<FakePort *> list;
        for (int a : availabilities) {
            auto *port = new FakePort(a, this);
            m_owned << port;
            list << port;
        }
        return QVariant::fromValue(list);
    }

    int addDevice(FakeSourceModel *model,
                             const QString &name,
                             const QString &description = {},
                             const QVariant &portList = {},
                             bool isVirtual = false,
                             qint64 volume = kNormal)
    {
        auto *device = new FakeDevice(this);
        m_owned << device;
        return model->appendRow({
            {Qt::DisplayRole, name},
            {PulseObjectRole, QVariant::fromValue<QObject *>(device)},
            {NameRole, name},
            {DescriptionRole, description},
            {VolumeRole, volume},
            {MutedRole, false},
            {DefaultRole, false},
            {PortsRole, portList.isValid() ? portList : ports({})},
            {VirtualDeviceRole, isVirtual},
        });
    }

    static QStringList names(const DeviceFilterModel &model)
    {
        QStringList out;
        const int nameRole = model.roleNames().key("Name");
        for (int row = 0; row < model.rowCount(); ++row) {
            out << model.data(model.index(row, 0), nameRole).toString();
        }
        return out;
    }

private Q_SLOTS:
    void cleanup()
    {
        qDeleteAll(m_owned);
        m_owned.clear();
    }

    void roleContract()
    {
        // The role names the filter relies on are the capitalised property
        // names of PulseAudioQt's Sink/Source (AbstractModel::initRoleNames).
        for (const QMetaObject *mo : {&PulseAudioQt::Sink::staticMetaObject, &PulseAudioQt::Source::staticMetaObject}) {
            for (const char *property : {"name", "description", "volume", "ports", "virtualDevice", "default", "muted"}) {
                QVERIFY2(mo->indexOfProperty(property) >= 0, property);
            }
        }
        // Port availability is read by property name; Unavailable is what the
        // filter compares against.
        QVERIFY(PulseAudioQt::Port::staticMetaObject.indexOfProperty("availability") >= 0);
        QCOMPARE(int(PulseAudioQt::Port::Unavailable), 2);
        QCOMPARE(PulseAudioQt::normalVolume(), kNormal);
    }

    void noSource()
    {
        DeviceFilterModel model(DeviceFilterModel::Outputs);
        QCOMPARE(model.kind(), DeviceFilterModel::Outputs);
        QCOMPARE(model.count(), 0);
        QVERIFY(model.get(0).isEmpty());
        QVERIFY(model.dynamicSortFilter());
        QVERIFY(!model.showVirtual());
    }

    void filterRules()
    {
        FakeSourceModel *source = makeSource();
        addDevice(source, u"alsa_output.speaker"_s, u"Speaker"_s, ports({int(PulseAudioQt::Port::Available)}));
        addDevice(source, u"auto_null"_s, u"Dummy Output"_s);
        addDevice(source, u"alsa_output.hdmi1"_s, u"HDMI 1"_s, ports({int(PulseAudioQt::Port::Unavailable)}));
        addDevice(source, u"alsa_output.headset"_s, u"Headset"_s, ports({int(PulseAudioQt::Port::Unavailable), int(PulseAudioQt::Port::Available)}));
        addDevice(source, u"alsa_output.unknown"_s, u"Unknown port"_s, ports({int(PulseAudioQt::Port::Unknown)}));
        addDevice(source, u"bluez_output.no_ports"_s, u"No ports"_s);
        addDevice(source, u"easyeffects_sink"_s, u"Easy Effects Sink"_s, {}, true);

        DeviceFilterModel model(DeviceFilterModel::Outputs);
        model.setSourceModel(source);

        QCOMPARE(names(model),
                 QStringList({u"alsa_output.speaker"_s,
                              u"alsa_output.headset"_s, // two ports: kept even with one unavailable
                              u"alsa_output.unknown"_s,
                              u"bluez_output.no_ports"_s}));
        QCOMPARE(model.count(), 4);
    }

    void portsAsVariantList()
    {
        // Any list of QObjects with `availability` works, not only QList<T*>.
        FakeSourceModel *source = makeSource();
        auto *port = new FakePort(int(PulseAudioQt::Port::Unavailable), this);
        m_owned << port;
        addDevice(source, u"mic2"_s, u"Mic 2"_s, QVariantList{QVariant::fromValue<QObject *>(port)});
        addDevice(source, u"mic1"_s, u"Digital Microphone"_s);

        DeviceFilterModel model(DeviceFilterModel::Inputs);
        model.setSourceModel(source);
        QCOMPARE(model.kind(), DeviceFilterModel::Inputs);
        QCOMPARE(names(model), QStringList({u"mic1"_s}));
    }

    void showVirtual()
    {
        FakeSourceModel *source = makeSource();
        addDevice(source, u"speaker"_s);
        addDevice(source, u"easyeffects_sink"_s, {}, {}, true);

        DeviceFilterModel model(DeviceFilterModel::Outputs);
        model.setSourceModel(source);
        QCOMPARE(model.count(), 1);

        QSignalSpy virtualSpy(&model, &DeviceFilterModel::showVirtualChanged);
        QSignalSpy countSpy(&model, &DeviceFilterModel::countChanged);
        model.setShowVirtual(true);
        QCOMPARE(virtualSpy.count(), 1);
        QVERIFY(countSpy.count() >= 1);
        QCOMPARE(model.count(), 2);
        QCOMPARE(names(model), QStringList({u"speaker"_s, u"easyeffects_sink"_s}));

        model.setShowVirtual(true); // no change, no signal
        QCOMPARE(virtualSpy.count(), 1);

        model.setShowVirtual(false);
        QCOMPARE(virtualSpy.count(), 2);
        QCOMPARE(model.count(), 1);
    }

    void dynamicRefilter()
    {
        // PulseAudioQt emits dataChanged(row, {role}) per changed property
        // (portsChanged on every device update, virtualDeviceChanged, ...).
        FakeSourceModel *source = makeSource();
        addDevice(source, u"speaker"_s);
        const int hdmi = addDevice(source, u"hdmi"_s, {}, ports({int(PulseAudioQt::Port::Available)}));

        DeviceFilterModel model(DeviceFilterModel::Outputs);
        model.setSourceModel(source);
        QCOMPARE(model.count(), 2);

        QSignalSpy countSpy(&model, &DeviceFilterModel::countChanged);
        source->set(hdmi, PortsRole, ports({int(PulseAudioQt::Port::Unavailable)})); // cable unplugged
        QCOMPARE(model.count(), 1);
        QCOMPARE(countSpy.count(), 1);

        source->set(hdmi, PortsRole, ports({int(PulseAudioQt::Port::Available)})); // plugged back in
        QCOMPARE(model.count(), 2);
        QCOMPARE(countSpy.count(), 2);

        source->set(hdmi, VirtualDeviceRole, true);
        QCOMPARE(names(model), QStringList({u"speaker"_s}));

        // Rows appearing later are filtered as well.
        addDevice(source, u"auto_null"_s);
        addDevice(source, u"usb"_s);
        QCOMPARE(names(model), QStringList({u"speaker"_s, u"usb"_s}));

        source->removeRow(0);
        QCOMPARE(names(model), QStringList({u"usb"_s}));
    }

    void percentAndLabelRoles()
    {
        FakeSourceModel *source = makeSource();
        addDevice(source, u"speaker"_s, u"Speaker"_s, {}, false, kNormal);
        addDevice(source, u"half"_s, QString(), {}, false, kNormal / 2);
        addDevice(source, u"raised"_s, u"Raised"_s, {}, false, kNormal * 3 / 2);
        addDevice(source, u"silent"_s, u"Silent"_s, {}, false, 0);

        DeviceFilterModel model(DeviceFilterModel::Outputs);
        model.setSourceModel(source);
        const auto roles = model.roleNames();
        QCOMPARE(roles.value(DeviceFilterModel::PercentRole), "percent"_ba);
        QCOMPARE(roles.value(DeviceFilterModel::LabelRole), "label"_ba);
        // Every PulseAudioQt role passes through.
        for (const QByteArray &name : {"Name"_ba, "Description"_ba, "Default"_ba, "Volume"_ba, "Muted"_ba, "PulseObject"_ba}) {
            QVERIFY2(roles.values().contains(name), name.constData());
        }

        auto at = [&](int row, int role) {
            return model.data(model.index(row, 0), role);
        };
        QCOMPARE(at(0, DeviceFilterModel::PercentRole).toInt(), 100);
        QCOMPARE(at(1, DeviceFilterModel::PercentRole).toInt(), 50);
        QCOMPARE(at(2, DeviceFilterModel::PercentRole).toInt(), 150);
        QCOMPARE(at(3, DeviceFilterModel::PercentRole).toInt(), 0);

        QCOMPARE(at(0, DeviceFilterModel::LabelRole).toString(), u"Speaker"_s);
        QCOMPARE(at(1, DeviceFilterModel::LabelRole).toString(), u"half"_s); // no description: name
        QVERIFY(!model.data(QModelIndex(), DeviceFilterModel::PercentRole).isValid());

        // Rounding: 1 % of normal is 655.36.
        source->set(1, VolumeRole, qint64(655));
        QCOMPARE(at(1, DeviceFilterModel::PercentRole).toInt(), 1);
    }

    void derivedRolesNotify()
    {
        FakeSourceModel *source = makeSource();
        const int speaker = addDevice(source, u"speaker"_s, u"Speaker"_s);

        DeviceFilterModel model(DeviceFilterModel::Outputs);
        model.setSourceModel(source);
        QCOMPARE(model.count(), 1);

        auto rolesSeen = [](const QSignalSpy &spy) {
            QList<int> all;
            for (const auto &args : spy) {
                all += args.at(2).value<QList<int>>();
            }
            return all;
        };

        QSignalSpy spy(&model, &QAbstractItemModel::dataChanged);
        source->set(speaker, VolumeRole, kNormal / 4);
        QVERIFY(rolesSeen(spy).contains(int(VolumeRole)));
        QVERIFY(rolesSeen(spy).contains(int(DeviceFilterModel::PercentRole)));
        QVERIFY(!rolesSeen(spy).contains(int(DeviceFilterModel::LabelRole)));
        QCOMPARE(model.data(model.index(0, 0), DeviceFilterModel::PercentRole).toInt(), 25);

        spy.clear();
        source->set(speaker, DescriptionRole, u"Laptop speaker"_s);
        QVERIFY(rolesSeen(spy).contains(int(DeviceFilterModel::LabelRole)));
        QVERIFY(!rolesSeen(spy).contains(int(DeviceFilterModel::PercentRole)));
        QCOMPARE(model.data(model.index(0, 0), DeviceFilterModel::LabelRole).toString(), u"Laptop speaker"_s);

        spy.clear();
        source->set(speaker, MutedRole, true); // unrelated role: no derived roles
        QVERIFY(!rolesSeen(spy).contains(int(DeviceFilterModel::LabelRole)));
        QVERIFY(!rolesSeen(spy).contains(int(DeviceFilterModel::PercentRole)));
    }

    void getRow()
    {
        FakeSourceModel *source = makeSource();
        addDevice(source, u"auto_null"_s);
        addDevice(source, u"speaker"_s, u"Speaker"_s, {}, false, kNormal * 2 / 5);

        DeviceFilterModel model(DeviceFilterModel::Outputs);
        model.setSourceModel(source);
        const QVariantMap row = model.get(0);
        QCOMPARE(row.value(u"Name"_s).toString(), u"speaker"_s);
        QCOMPARE(row.value(u"label"_s).toString(), u"Speaker"_s);
        QCOMPARE(row.value(u"percent"_s).toInt(), 40);
        QVERIFY(row.value(u"PulseObject"_s).value<QObject *>());
        QVERIFY(model.get(1).isEmpty());
        QVERIFY(model.get(-1).isEmpty());
    }

    void pulseObjectKeepsCppOwnership()
    {
        FakeSourceModel *source = makeSource();
        addDevice(source, u"speaker"_s);

        DeviceFilterModel model(DeviceFilterModel::Outputs);
        model.setSourceModel(source);
        auto *object = model.data(model.index(0, 0), PulseObjectRole).value<QObject *>();
        QVERIFY(object);
        QCOMPARE(QQmlEngine::objectOwnership(object), QQmlEngine::CppOwnership);
    }

    void replaceSourceModel()
    {
        FakeSourceModel *first = makeSource();
        addDevice(first, u"a"_s);
        addDevice(first, u"auto_null"_s);

        // A second model with different role numbering.
        auto *second = new FakeSourceModel({{Qt::UserRole + 50, "Name"}, {Qt::UserRole + 51, "VirtualDevice"}}, this);
        for (const auto &[name, isVirtual] : {std::pair{u"b"_s, false}, std::pair{u"auto_null"_s, false}, std::pair{u"v"_s, true}}) {
            second->appendRow({{Qt::UserRole + 50, name}, {Qt::UserRole + 51, isVirtual}});
        }

        DeviceFilterModel model(DeviceFilterModel::Outputs);
        model.setSourceModel(first);
        QCOMPARE(names(model), QStringList({u"a"_s}));

        QSignalSpy countSpy(&model, &DeviceFilterModel::countChanged);
        model.setSourceModel(second);
        QVERIFY(countSpy.count() >= 1);
        QCOMPARE(names(model), QStringList({u"b"_s}));
        // No Volume/Description role in this source: derived roles degrade.
        QVERIFY(!model.data(model.index(0, 0), DeviceFilterModel::PercentRole).isValid());
        QCOMPARE(model.data(model.index(0, 0), DeviceFilterModel::LabelRole).toString(), u"b"_s);
    }
};

QTEST_GUILESS_MAIN(TestDeviceFilterModel)
#include "tst_devicefiltermodel.moc"
