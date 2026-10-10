// SPDX-License-Identifier: GPL-2.0-or-later
//
// Helpers for the tst_api_* tests: check a class's static meta-object for the
// properties, invokables and signals the QML package relies on - without
// creating an instance (so no backend ever starts, no D-Bus traffic).

#pragma once

#include <QMetaMethod>
#include <QMetaObject>
#include <QMetaProperty>
#include <QStringList>
#include <QTest>

namespace ApiCheck
{

/// Returns "" when OK, else a description of what is wrong.
inline QString checkProperty(const QMetaObject &mo, const QString &spec)
{
    // spec: "name" (read-only, NOTIFY or CONSTANT) or "name rw" (also writable).
    const QStringList parts = spec.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    const QByteArray name = parts.value(0).toLatin1();
    const bool writable = parts.contains(QLatin1String("rw"));
    const int idx = mo.indexOfProperty(name.constData());
    if (idx < 0) {
        return QStringLiteral("%1: missing property %2").arg(QLatin1String(mo.className()), QString::fromLatin1(name));
    }
    const QMetaProperty prop = mo.property(idx);
    if (!prop.isConstant() && !prop.hasNotifySignal()) {
        return QStringLiteral("%1: property %2 has neither NOTIFY nor CONSTANT").arg(QLatin1String(mo.className()), QString::fromLatin1(name));
    }
    if (writable && !prop.isWritable()) {
        return QStringLiteral("%1: property %2 must be writable").arg(QLatin1String(mo.className()), QString::fromLatin1(name));
    }
    return {};
}

inline QString checkMethod(const QMetaObject &mo, const char *signature)
{
    const QByteArray normalized = QMetaObject::normalizedSignature(signature);
    if (mo.indexOfMethod(normalized.constData()) < 0) {
        return QStringLiteral("%1: missing method/signal %2").arg(QLatin1String(mo.className()), QString::fromLatin1(normalized));
    }
    return {};
}

} // namespace ApiCheck

/// Verifies every property spec in @p specs (QStringList) on meta-object @p mo.
#define MC_REQUIRE_PROPERTIES(mo, specs)                                                                                                                       \
    do {                                                                                                                                                       \
        for (const QString &spec_ : (specs)) {                                                                                                                 \
            const QString err_ = ApiCheck::checkProperty((mo), spec_);                                                                                         \
            QVERIFY2(err_.isEmpty(), qPrintable(err_));                                                                                                        \
        }                                                                                                                                                      \
    } while (false)

/// Verifies every method signature in @p sigs (list of const char*).
#define MC_REQUIRE_METHODS(mo, ...)                                                                                                                            \
    do {                                                                                                                                                       \
        for (const char *sig_ : {__VA_ARGS__}) {                                                                                                               \
            const QString err_ = ApiCheck::checkMethod((mo), sig_);                                                                                            \
            QVERIFY2(err_.isEmpty(), qPrintable(err_));                                                                                                        \
        }                                                                                                                                                      \
    } while (false)

/// The common backend conventions.
#define MC_REQUIRE_CONVENTIONS(mo)                                                                                                                             \
    do {                                                                                                                                                       \
        MC_REQUIRE_PROPERTIES((mo), QStringList({QStringLiteral("available"), QStringLiteral("errorText")}));                                                  \
        MC_REQUIRE_METHODS((mo), "refresh()");                                                                                                                 \
    } while (false)
