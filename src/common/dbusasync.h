// SPDX-License-Identifier: GPL-2.0-or-later
//
// Asynchronous D-Bus helpers shared by every backend.
//
// The plugins run inside plasmashell, so nothing here ever blocks: every call
// is asyncCall() + QDBusPendingCallWatcher, and every callback is bound to a
// `context` QObject - it runs in that object's thread and is dropped silently
// if the object is destroyed before the reply arrives.
//
//   MC::DBus::call(bus, msg, this, [this](const QDBusMessage &reply) { ... });
//   MC::DBus::getAll(bus, service, path, iface, this, [this](const QVariantMap &props, const QDBusError &err) { ... });
//   MC::DBus::get(bus, service, path, iface, u"Prop"_s, this, [this](const QVariant &value, const QDBusError &err) { ... });
//   MC::DBus::set(bus, service, path, iface, u"Prop"_s, value, this, [this](const QDBusError &err) { ... });
//   MC::DBus::onSignal(bus, service, path, iface, u"Signal"_s, this, [this](const QDBusMessage &msg) { ... });
//   MC::DBus::onPropertiesChanged(bus, service, path, iface, this, [this](const QVariantMap &changed, const QStringList &invalidated) { ... });
//
// Property values are passed through MC::DBus::demarshall(), so nested
// containers (aa{sv}, a(ss), ...) arrive as QVariantList / QVariantMap
// instead of raw QDBusArgument objects.

#pragma once

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusVariant>
#include <QObject>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

#include <functional>

namespace MC::DBus
{

inline QVariant demarshall(const QVariant &value);

/// Decodes a QDBusArgument (read from its current position) into plain
/// QVariant containers. Maps are keyed by the key's string form; structs
/// become QVariantList. Note that a QDBusArgument can only be read once.
inline QVariant demarshallArgument(const QDBusArgument &arg)
{
    switch (arg.currentType()) {
    case QDBusArgument::BasicType:
    case QDBusArgument::VariantType:
        return demarshall(arg.asVariant());
    case QDBusArgument::ArrayType: {
        QVariantList list;
        arg.beginArray();
        while (!arg.atEnd()) {
            list.append(demarshall(arg.asVariant()));
        }
        arg.endArray();
        return list;
    }
    case QDBusArgument::StructureType: {
        QVariantList fields;
        arg.beginStructure();
        while (!arg.atEnd()) {
            fields.append(demarshall(arg.asVariant()));
        }
        arg.endStructure();
        return fields;
    }
    case QDBusArgument::MapType: {
        QVariantMap map;
        arg.beginMap();
        while (!arg.atEnd()) {
            arg.beginMapEntry();
            const QVariant key = demarshall(arg.asVariant());
            const QVariant val = demarshall(arg.asVariant());
            arg.endMapEntry();
            map.insert(key.toString(), val);
        }
        arg.endMap();
        return map;
    }
    case QDBusArgument::MapEntryType:
    case QDBusArgument::UnknownType:
        break;
    }
    return {};
}

/// Unwraps QDBusVariant and QDBusArgument recursively; anything else is
/// returned unchanged (QtDBus already decodes basic types and "as").
inline QVariant demarshall(const QVariant &value)
{
    if (value.metaType() == QMetaType::fromType<QDBusVariant>()) {
        return demarshall(qvariant_cast<QDBusVariant>(value).variant());
    }
    if (value.metaType() == QMetaType::fromType<QDBusArgument>()) {
        return demarshallArgument(qvariant_cast<QDBusArgument>(value));
    }
    return value;
}

/// demarshall() applied to every value of an a{sv}.
inline QVariantMap demarshallMap(const QVariantMap &map)
{
    QVariantMap out;
    for (auto it = map.cbegin(); it != map.cend(); ++it) {
        out.insert(it.key(), demarshall(it.value()));
    }
    return out;
}

/// Sends @p msg asynchronously; @p done receives the reply message, whose
/// type() is QDBusMessage::ErrorMessage on failure (errorName()/errorMessage()).
inline void call(const QDBusConnection &bus, const QDBusMessage &msg, QObject *context, std::function<void(const QDBusMessage &reply)> done)
{
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(msg), context);
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, context, [done = std::move(done)](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        if (done) {
            done(w->reply());
        }
    });
}

/// Convenience: build a method call message.
inline QDBusMessage methodCall(const QString &service, const QString &path, const QString &interface, const QString &method, const QVariantList &args = {})
{
    QDBusMessage msg = QDBusMessage::createMethodCall(service, path, interface, method);
    if (!args.isEmpty()) {
        msg.setArguments(args);
    }
    return msg;
}

/// org.freedesktop.DBus.Properties.GetAll. On error @p done gets an empty map
/// and a valid QDBusError.
inline void getAll(const QDBusConnection &bus, const QString &service, const QString &path, const QString &interface, QObject *context,
                   std::function<void(const QVariantMap &properties, const QDBusError &error)> done)
{
    const QDBusMessage msg =
        methodCall(service, path, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("GetAll"), {interface});
    call(bus, msg, context, [done = std::move(done)](const QDBusMessage &reply) {
        if (!done) {
            return;
        }
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
            done({}, QDBusError(reply));
            return;
        }
        const QVariant first = reply.arguments().constFirst();
        QVariantMap props;
        if (first.metaType() == QMetaType::fromType<QDBusArgument>()) {
            props = demarshall(first).toMap();
        } else {
            props = demarshallMap(first.toMap());
        }
        done(props, QDBusError());
    });
}

/// org.freedesktop.DBus.Properties.Get of one property (already demarshalled).
inline void get(const QDBusConnection &bus, const QString &service, const QString &path, const QString &interface, const QString &property,
                QObject *context, std::function<void(const QVariant &value, const QDBusError &error)> done)
{
    const QDBusMessage msg =
        methodCall(service, path, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"), {interface, property});
    call(bus, msg, context, [done = std::move(done)](const QDBusMessage &reply) {
        if (!done) {
            return;
        }
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
            done({}, QDBusError(reply));
            return;
        }
        done(demarshall(reply.arguments().constFirst()), QDBusError());
    });
}

/// org.freedesktop.DBus.Properties.Set. STATE-CHANGING: only call this in
/// response to a user action.
inline void set(const QDBusConnection &bus, const QString &service, const QString &path, const QString &interface, const QString &property,
                const QVariant &value, QObject *context, std::function<void(const QDBusError &error)> done = {})
{
    const QDBusMessage msg = methodCall(service,
                                        path,
                                        QStringLiteral("org.freedesktop.DBus.Properties"),
                                        QStringLiteral("Set"),
                                        {interface, property, QVariant::fromValue(QDBusVariant(value))});
    call(bus, msg, context, [done = std::move(done)](const QDBusMessage &reply) {
        if (done) {
            done(reply.type() == QDBusMessage::ErrorMessage ? QDBusError(reply) : QDBusError());
        }
    });
}

/// Receives one D-Bus signal and forwards it to a std::function. QtDBus can
/// only connect signals to named slots, hence this small relay object. It is
/// a child of `context` and disconnects itself when destroyed; delete it to
/// stop listening.
class SignalRelay : public QObject
{
    Q_OBJECT

public:
    using Handler = std::function<void(const QDBusMessage &message)>;

    SignalRelay(const QDBusConnection &bus,
                const QString &service,
                const QString &path,
                const QString &interface,
                const QString &name,
                QObject *context,
                Handler handler)
        : QObject(context)
        , m_bus(bus)
        , m_service(service)
        , m_path(path)
        , m_interface(interface)
        , m_name(name)
        , m_handler(std::move(handler))
    {
        m_connected = m_bus.connect(m_service, m_path, m_interface, m_name, this, SLOT(relay(QDBusMessage)));
    }

    ~SignalRelay() override
    {
        if (m_connected) {
            m_bus.disconnect(m_service, m_path, m_interface, m_name, this, SLOT(relay(QDBusMessage)));
        }
    }

    bool isConnected() const
    {
        return m_connected;
    }

public Q_SLOTS:
    void relay(const QDBusMessage &message)
    {
        if (m_handler) {
            m_handler(message);
        }
    }

private:
    QDBusConnection m_bus;
    QString m_service;
    QString m_path;
    QString m_interface;
    QString m_name;
    Handler m_handler;
    bool m_connected = false;
};

/// Listens to signal @p name; returns the relay (owned by @p context).
inline SignalRelay *onSignal(const QDBusConnection &bus,
                             const QString &service,
                             const QString &path,
                             const QString &interface,
                             const QString &name,
                             QObject *context,
                             SignalRelay::Handler handler)
{
    return new SignalRelay(bus, service, path, interface, name, context, std::move(handler));
}

/// Listens to org.freedesktop.DBus.Properties.PropertiesChanged for
/// @p interface on @p path; changed values arrive demarshalled.
inline SignalRelay *onPropertiesChanged(const QDBusConnection &bus,
                                        const QString &service,
                                        const QString &path,
                                        const QString &interface,
                                        QObject *context,
                                        std::function<void(const QVariantMap &changed, const QStringList &invalidated)> handler)
{
    return onSignal(bus,
                    service,
                    path,
                    QStringLiteral("org.freedesktop.DBus.Properties"),
                    QStringLiteral("PropertiesChanged"),
                    context,
                    [interface, handler = std::move(handler)](const QDBusMessage &msg) {
                        const QVariantList args = msg.arguments();
                        if (!handler || args.size() < 2 || args.at(0).toString() != interface) {
                            return;
                        }
                        QVariantMap changed;
                        const QVariant raw = args.at(1);
                        if (raw.metaType() == QMetaType::fromType<QDBusArgument>()) {
                            changed = demarshall(raw).toMap();
                        } else {
                            changed = demarshallMap(raw.toMap());
                        }
                        const QStringList invalidated = args.size() > 2 ? demarshall(args.at(2)).toStringList() : QStringList();
                        handler(changed, invalidated);
                    });
}

} // namespace MC::DBus
