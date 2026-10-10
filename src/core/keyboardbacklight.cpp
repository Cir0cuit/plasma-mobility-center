// SPDX-License-Identifier: GPL-2.0-or-later
//
// See keyboardbacklight.h
//
// Both sources are watched all the time (both are cheap, read-only until the
// user moves the control): PowerDevil is used whenever it offers a usable
// backlight, UPower's composite KbdBacklight object otherwise. UPower's
// DaemonVersion is read in either case, because UPower 1.91.4 swallows Fn-key
// changes for PowerDevil as well.

#include "keyboardbacklight.h"

#include <QDBusConnection>
#include <QDBusError>

#include "dbusasync.h"

using namespace Qt::StringLiterals;

namespace
{
const QString kPdService = u"org.kde.Solid.PowerManagement"_s;
const QString kPdPath = u"/org/kde/Solid/PowerManagement"_s;
const QString kPdIface = u"org.kde.Solid.PowerManagement"_s;
const QString kPdKbdPath = u"/org/kde/Solid/PowerManagement/Actions/KeyboardBrightnessControl"_s;
const QString kPdKbdIface = u"org.kde.Solid.PowerManagement.Actions.KeyboardBrightnessControl"_s;
const QString kPdAction = u"KeyboardBrightnessControl"_s;

const QString kUpService = u"org.freedesktop.UPower"_s;
const QString kUpPath = u"/org/freedesktop/UPower"_s;
const QString kUpIface = u"org.freedesktop.UPower"_s;
const QString kUpKbdPath = u"/org/freedesktop/UPower/KbdBacklight"_s;
const QString kUpKbdIface = u"org.freedesktop.UPower.KbdBacklight"_s;

// Fn-key changes are never signalled by this exact release.
const QString kSilentUPowerVersion = u"1.91.4"_s;

// The single int a method reply / signal carries, or -1.
int intArg(const QDBusMessage &msg)
{
    if (msg.type() == QDBusMessage::ErrorMessage || msg.arguments().isEmpty()) {
        return -1;
    }
    bool ok = false;
    const int value = msg.arguments().constFirst().toInt(&ok);
    return ok ? value : -1;
}
} // namespace

KeyboardBacklight::KeyboardBacklight(QObject *parent)
    : QObject(parent)
{
    m_pdWatch = std::make_unique<MC::ServiceWatch>(QDBusConnection::sessionBus(), QStringList{kPdService}, this, [this](const QString &, bool registered) {
        registered ? onPowerDevilUp() : onPowerDevilDown();
    });
    m_upWatch = std::make_unique<MC::ServiceWatch>(QDBusConnection::systemBus(), QStringList{kUpService}, this, [this](const QString &, bool registered) {
        registered ? onUPowerUp() : onUPowerDown();
    });
}

KeyboardBacklight::~KeyboardBacklight() = default;

bool KeyboardBacklight::available() const
{
    return m_path != Path::None;
}

QString KeyboardBacklight::errorText() const
{
    return m_errorText;
}

int KeyboardBacklight::brightness() const
{
    return m_brightness;
}

int KeyboardBacklight::maxBrightness() const
{
    return m_max;
}

int KeyboardBacklight::steps() const
{
    return maxBrightness();
}

int KeyboardBacklight::percent() const
{
    const int max = maxBrightness();
    return max > 0 ? qRound(100.0 * brightness() / max) : 0;
}

bool KeyboardBacklight::viaUPower() const
{
    return m_path == Path::UPower;
}

bool KeyboardBacklight::rereadOnShow() const
{
    return m_rereadOnShow;
}

void KeyboardBacklight::setLevel(int level, bool silent)
{
    if (m_path == Path::None) {
        return;
    }
    const int value = qBound(0, level, m_max);

    QDBusMessage msg;
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (m_path == Path::PowerDevil) {
        // Optimistic; keyboardBrightnessChanged confirms or corrects it.
        m_pd.brightness = value;
        msg = MC::DBus::methodCall(kPdService, kPdKbdPath, kPdKbdIface, silent ? u"setKeyboardBrightnessSilent"_s : u"setKeyboardBrightness"_s, {value});
    } else {
        // UPower never shows an OSD; `silent` does not apply.
        m_up.brightness = value;
        bus = QDBusConnection::systemBus();
        msg = MC::DBus::methodCall(kUpService, kUpKbdPath, kUpKbdIface, u"SetBrightness"_s, {value});
    }
    update();

    MC::DBus::call(bus, msg, this, [this](const QDBusMessage &reply) {
        if (reply.type() == QDBusMessage::ErrorMessage) {
            setErrorText(reply.errorMessage());
            refresh(); // undo the optimistic value
        } else {
            setErrorText({});
        }
    });
}

void KeyboardBacklight::refresh()
{
    if (m_pd.registered) {
        // With UPower 1.91.4, PowerDevil's cached level is as stale as ours:
        // take the level from UPower below instead.
        const bool upowerKnowsBetter = m_rereadOnShow && m_up.registered && m_up.max > 0;
        if (m_pd.supported) {
            readPowerDevil(!upowerKnowsBetter);
        } else {
            checkPowerDevilSupported();
        }
    }
    if (m_up.registered) {
        readUPower(false);
    }
}

// ---- PowerDevil (session bus) -----------------------------------------------

void KeyboardBacklight::onPowerDevilUp()
{
    ++m_pdGeneration;
    qDeleteAll(m_pdRelays);
    m_pdRelays.clear();
    m_pd = PowerDevilState{};
    m_pd.registered = true;

    // Subscribe first, then query.
    const QDBusConnection bus = QDBusConnection::sessionBus();
    m_pdRelays << MC::DBus::onSignal(bus, kPdService, kPdPath, kPdIface, u"supportedActionsChanged"_s, this, [this](const QDBusMessage &) {
        checkPowerDevilSupported();
    });
    m_pdRelays << MC::DBus::onSignal(bus, kPdService, kPdKbdPath, kPdKbdIface, u"keyboardBrightnessChanged"_s, this, [this](const QDBusMessage &msg) {
        const int value = intArg(msg);
        if (value >= 0) {
            m_pd.brightness = value;
            update();
        }
    });
    // Emitted on every brightness change, not only when the range changes:
    // compare before acting.
    m_pdRelays << MC::DBus::onSignal(bus, kPdService, kPdKbdPath, kPdKbdIface, u"keyboardBrightnessMaxChanged"_s, this, [this](const QDBusMessage &msg) {
        const int value = intArg(msg);
        if (value < 0 || value == m_pd.max) {
            return;
        }
        m_pd.max = value;
        if (m_pd.supported && value > 0) {
            readPowerDevil(true);
        }
        update();
    });

    checkPowerDevilSupported();
}

void KeyboardBacklight::onPowerDevilDown()
{
    ++m_pdGeneration;
    qDeleteAll(m_pdRelays);
    m_pdRelays.clear();
    m_pd = PowerDevilState{};
    update();
}

void KeyboardBacklight::checkPowerDevilSupported()
{
    const quint64 generation = m_pdGeneration;
    const QDBusMessage msg = MC::DBus::methodCall(kPdService, kPdPath, kPdIface, u"isActionSupported"_s, {kPdAction});
    MC::DBus::call(QDBusConnection::sessionBus(), msg, this, [this, generation](const QDBusMessage &reply) {
        if (generation != m_pdGeneration) {
            return;
        }
        const bool supported = reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty() && reply.arguments().constFirst().toBool();
        m_pd.supported = supported;
        if (supported) {
            readPowerDevil(true);
        } else {
            m_pd.max = -1;
            m_pd.brightness = -1;
        }
        update();
    });
}

void KeyboardBacklight::readPowerDevil(bool readBrightness)
{
    const quint64 generation = m_pdGeneration;
    const QDBusConnection bus = QDBusConnection::sessionBus();
    MC::DBus::call(bus, MC::DBus::methodCall(kPdService, kPdKbdPath, kPdKbdIface, u"keyboardBrightnessMax"_s), this, [this, generation](const QDBusMessage &reply) {
        if (generation != m_pdGeneration) {
            return;
        }
        m_pd.max = intArg(reply);
        update();
    });
    if (!readBrightness) {
        return;
    }
    MC::DBus::call(bus, MC::DBus::methodCall(kPdService, kPdKbdPath, kPdKbdIface, u"keyboardBrightness"_s), this, [this, generation](const QDBusMessage &reply) {
        if (generation != m_pdGeneration) {
            return;
        }
        m_pd.brightness = intArg(reply);
        update();
    });
}

// ---- UPower (system bus) ----------------------------------------------------

void KeyboardBacklight::onUPowerUp()
{
    ++m_upGeneration;
    qDeleteAll(m_upRelays);
    m_upRelays.clear();
    m_up = UPowerState{};
    m_up.registered = true;

    // BrightnessChangedWithSource covers both our writes ("external") and the
    // hardware ("internal"); plain BrightnessChanged is only for UPower
    // releases older than the WithSource signal. Both carry the new level.
    const QDBusConnection bus = QDBusConnection::systemBus();
    auto onChanged = [this](const QDBusMessage &msg) {
        const int value = intArg(msg);
        if (value >= 0 && value != m_up.brightness) {
            m_up.brightness = value;
            update();
        }
    };
    m_upRelays << MC::DBus::onSignal(bus, kUpService, kUpKbdPath, kUpKbdIface, u"BrightnessChangedWithSource"_s, this, onChanged);
    m_upRelays << MC::DBus::onSignal(bus, kUpService, kUpKbdPath, kUpKbdIface, u"BrightnessChanged"_s, this, onChanged);

    readUPower(true);
}

void KeyboardBacklight::onUPowerDown()
{
    ++m_upGeneration;
    qDeleteAll(m_upRelays);
    m_upRelays.clear();
    m_up = UPowerState{};
    update();
}

void KeyboardBacklight::readUPower(bool readVersion)
{
    const quint64 generation = m_upGeneration;
    const QDBusConnection bus = QDBusConnection::systemBus();

    if (readVersion) {
        MC::DBus::get(bus, kUpService, kUpPath, kUpIface, u"DaemonVersion"_s, this, [this, generation](const QVariant &value, const QDBusError &error) {
            if (generation != m_upGeneration) {
                return;
            }
            m_up.daemonVersion = error.isValid() ? QString() : value.toString();
            update();
        });
    }

    // No KbdBacklight object (no backlight, or a UPower built without it)
    // answers with an error: treated as max -1, i.e. unusable.
    MC::DBus::call(bus, MC::DBus::methodCall(kUpService, kUpKbdPath, kUpKbdIface, u"GetMaxBrightness"_s), this, [this, generation](const QDBusMessage &reply) {
        if (generation != m_upGeneration) {
            return;
        }
        m_up.max = intArg(reply);
        update();
    });
    MC::DBus::call(bus, MC::DBus::methodCall(kUpService, kUpKbdPath, kUpKbdIface, u"GetBrightness"_s), this, [this, generation](const QDBusMessage &reply) {
        if (generation != m_upGeneration) {
            return;
        }
        m_up.brightness = intArg(reply);
        // UPower 1.91.4 hides Fn-key changes from PowerDevil too, so a fresh
        // UPower reading beats PowerDevil's cached level - provided both
        // speak the same scale (PowerDevil passes UPower's raw levels).
        if (m_rereadOnShow && m_up.brightness >= 0 && m_pd.supported && m_pd.max > 0 && m_pd.max == m_up.max) {
            m_pd.brightness = m_up.brightness;
        }
        update();
    });
}

// ---- derived state ----------------------------------------------------------

void KeyboardBacklight::update()
{
    Path path = Path::None;
    if (m_pd.registered && m_pd.supported && m_pd.max > 0 && m_pd.brightness >= 0) {
        path = Path::PowerDevil;
    } else if (m_up.registered && m_up.max > 0 && m_up.brightness >= 0) {
        path = Path::UPower;
    }

    int max = 0;
    int brightness = 0;
    if (path == Path::PowerDevil) {
        max = m_pd.max;
        brightness = qBound(0, m_pd.brightness, max);
    } else if (path == Path::UPower) {
        max = m_up.max;
        brightness = qBound(0, m_up.brightness, max);
    }
    const bool reread = m_up.daemonVersion == kSilentUPowerVersion;

    const bool availableChanged_ = (path == Path::None) != (m_path == Path::None);
    const bool viaUPowerChanged_ = (path == Path::UPower) != (m_path == Path::UPower);
    const bool maxChanged = max != m_max;
    const bool brightnessChanged_ = brightness != m_brightness || maxChanged; // percent depends on both
    const bool rereadChanged = reread != m_rereadOnShow;

    m_path = path;
    m_max = max;
    m_brightness = brightness;
    m_rereadOnShow = reread;

    if (maxChanged) {
        Q_EMIT maxBrightnessChanged();
    }
    if (brightnessChanged_) {
        Q_EMIT brightnessChanged();
    }
    if (viaUPowerChanged_) {
        Q_EMIT viaUPowerChanged();
    }
    if (rereadChanged) {
        Q_EMIT rereadOnShowChanged();
    }
    if (availableChanged_) {
        Q_EMIT availableChanged();
    }
}

void KeyboardBacklight::setErrorText(const QString &text)
{
    if (m_errorText == text) {
        return;
    }
    m_errorText = text;
    Q_EMIT errorTextChanged();
}
