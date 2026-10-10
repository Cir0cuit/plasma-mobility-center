// SPDX-License-Identifier: GPL-2.0-or-later
//
// See displayconfig.h

#include "displayconfig.h"

#include <KScreen/Config>
#include <KScreen/ConfigMonitor>
#include <KScreen/EDID>
#include <KScreen/GetConfigOperation>
#include <KScreen/Mode>
#include <KScreen/Output>
#include <KScreen/SetConfigOperation>

#include <QDBusConnection>
#include <QLoggingCategory>
#include <QTimer>

#include "dbusasync.h"
#include "projectionlogic.h"
#include "refreshratelogic.h"

using namespace Qt::StringLiterals;


static_assert(int(DisplayConfig::Unknown) == int(ProjectionLogic::Unknown));
static_assert(int(DisplayConfig::InternalOnly) == int(ProjectionLogic::InternalOnly));
static_assert(int(DisplayConfig::Mirror) == int(ProjectionLogic::Mirror));
static_assert(int(DisplayConfig::Extend) == int(ProjectionLogic::Extend));
static_assert(int(DisplayConfig::ExternalOnly) == int(ProjectionLogic::ExternalOnly));

namespace
{
Q_LOGGING_CATEGORY(lcDisplay, "mobilitycenter.display", QtInfoMsg)

// kscreen's OSD service. D-Bus activatable: only ever CALLED, on explicit
// user request - never introspected, watched or pinged.
const QString kOsdService = u"org.kde.kscreen.osdService"_s;
const QString kOsdPath = u"/org/kde/kscreen/osdService"_s;
const QString kOsdIface = u"org.kde.kscreen.osdService"_s;

QString outputName(const KScreen::OutputPtr &output)
{
    if (!output) {
        return {};
    }
    QString name;
    if (const KScreen::Edid *edid = output->edid(); edid && edid->isValid()) {
        name = (edid->vendor() + u' ' + edid->name()).trimmed();
    }
    if (name.isEmpty()) {
        name = (output->vendor() + u' ' + output->model()).trimmed();
    }
    return name.isEmpty() ? output->name() : name;
}
} // namespace

DisplayConfig::DisplayConfig(QObject *parent)
    : QObject(parent)
    , m_updateTimer(new QTimer(this))
{
    m_updateTimer->setSingleShot(true);
    m_updateTimer->setInterval(0);
    connect(m_updateTimer, &QTimer::timeout, this, &DisplayConfig::updateState);

    // Fires for every change the backend reports. With a config we only
    // re-derive (ConfigMonitor has already updated it in place); without
    // one (the first fetch failed) this is the retry trigger.
    connect(KScreen::ConfigMonitor::instance(), &KScreen::ConfigMonitor::configurationChanged, this, [this] {
        if (m_config) {
            scheduleUpdate();
        } else if (!m_fetching) {
            refresh();
        }
    });

    refresh();
}

DisplayConfig::~DisplayConfig()
{
    if (m_config) {
        KScreen::ConfigMonitor::instance()->removeConfig(m_config);
    }
}

void DisplayConfig::refresh()
{
    const quint64 generation = ++m_generation;
    m_fetching = true;
    auto *op = new KScreen::GetConfigOperation(KScreen::ConfigOperation::NoOptions, this);
    connect(op, &KScreen::ConfigOperation::finished, this, [this, generation](KScreen::ConfigOperation *finished) {
        onGetConfigFinished(finished, generation);
    });
}

void DisplayConfig::onGetConfigFinished(KScreen::ConfigOperation *op, quint64 generation)
{
    if (generation != m_generation) {
        return; // a newer refresh() superseded this one
    }
    m_fetching = false;
    if (op->hasError() || !op->config()) {
        // Expected without a usable backend (no Wayland/X11 connection, e.g.
        // QT_QPA_PLATFORM=offscreen): the tile simply hides.
        qCDebug(lcDisplay) << "could not read the display configuration:" << op->errorString();
        setConfig({});
        return;
    }
    setConfig(op->config());
}

void DisplayConfig::setConfig(const KScreen::ConfigPtr &config)
{
    if (m_config == config) {
        scheduleUpdate();
        return;
    }
    if (m_config) {
        KScreen::ConfigMonitor::instance()->removeConfig(m_config);
        disconnect(m_config.data(), nullptr, this, nullptr);
        const KScreen::OutputList old = m_config->outputs();
        for (const KScreen::OutputPtr &output : old) {
            disconnect(output.data(), nullptr, this, nullptr);
        }
    }
    m_config = config;
    if (m_config) {
        KScreen::ConfigMonitor::instance()->addConfig(m_config);
        connect(m_config.data(), &KScreen::Config::outputAdded, this, [this](const KScreen::OutputPtr &output) {
            watchOutput(output);
            scheduleUpdate();
        });
        connect(m_config.data(), &KScreen::Config::outputRemoved, this, &DisplayConfig::scheduleUpdate);
        connect(m_config.data(), &KScreen::Config::prioritiesChanged, this, &DisplayConfig::scheduleUpdate);
        const KScreen::OutputList outputs = m_config->outputs();
        for (const KScreen::OutputPtr &output : outputs) {
            watchOutput(output);
        }
    }
    updateState();
}

void DisplayConfig::watchOutput(const KScreen::OutputPtr &output)
{
    if (!output) {
        return;
    }
    KScreen::Output *o = output.data();
    connect(o, &KScreen::Output::isConnectedChanged, this, &DisplayConfig::scheduleUpdate, Qt::UniqueConnection);
    connect(o, &KScreen::Output::isEnabledChanged, this, &DisplayConfig::scheduleUpdate, Qt::UniqueConnection);
    connect(o, &KScreen::Output::currentModeIdChanged, this, &DisplayConfig::scheduleUpdate, Qt::UniqueConnection);
    connect(o, &KScreen::Output::modesChanged, this, &DisplayConfig::scheduleUpdate, Qt::UniqueConnection);
    connect(o, &KScreen::Output::replicationSourceChanged, this, &DisplayConfig::scheduleUpdate, Qt::UniqueConnection);
    connect(o, &KScreen::Output::posChanged, this, &DisplayConfig::scheduleUpdate, Qt::UniqueConnection);
    connect(o, &KScreen::Output::scaleChanged, this, &DisplayConfig::scheduleUpdate, Qt::UniqueConnection);
    connect(o, &KScreen::Output::outputChanged, this, &DisplayConfig::scheduleUpdate, Qt::UniqueConnection);
    connect(o, &KScreen::Output::capabilitiesChanged, this, &DisplayConfig::scheduleUpdate, Qt::UniqueConnection);
    connect(o, &KScreen::Output::hdrEnabledChanged, this, &DisplayConfig::scheduleUpdate, Qt::UniqueConnection);
#ifdef MC_KSCREEN_AUTOBRIGHTNESS
    connect(o, &KScreen::Output::automaticBrightnessChanged, this, &DisplayConfig::scheduleUpdate, Qt::UniqueConnection);
#endif
}

namespace
{
// The output a per-output switch acts on: the internal panel when it has
// @p capability, else the first enabled output that has it.
KScreen::OutputPtr outputWith(const KScreen::ConfigPtr &config, KScreen::Output::Capability capability)
{
    if (!config) {
        return {};
    }
    KScreen::OutputPtr first;
    const KScreen::OutputList outputs = config->outputs();
    for (const KScreen::OutputPtr &output : outputs) {
        if (!output || !output->isConnected() || !output->isEnabled() || !(output->capabilities() & capability)) {
            continue;
        }
        if (output->type() == KScreen::Output::Panel) {
            return output;
        }
        if (!first) {
            first = output;
        }
    }
    return first;
}
} // namespace

void DisplayConfig::scheduleUpdate()
{
    m_updateTimer->start();
}

DisplayConfig::State DisplayConfig::computeState() const
{
    State s;
    if (!m_config) {
        return s;
    }
    s.outputCount = ProjectionLogic::candidates(m_config).size();
    s.available = s.outputCount > 0;

    if (const KScreen::OutputPtr panel = RefreshRateLogic::panel(m_config)) {
        s.refreshRates = RefreshRateLogic::groups(panel);
        if (const KScreen::ModePtr mode = panel->currentMode()) {
            s.currentRefreshRate = qRound(mode->refreshRate());
        }
    }

    if (const KScreen::OutputPtr hdr = outputWith(m_config, KScreen::Output::Capability::HighDynamicRange)) {
        s.hdrAvailable = true;
        s.hdrEnabled = hdr->isHdrEnabled();
        s.hdrOutputName = outputName(hdr);
    }
#ifdef MC_KSCREEN_AUTOBRIGHTNESS
    if (const KScreen::OutputPtr ab = outputWith(m_config, KScreen::Output::Capability::AutomaticBrightness)) {
        s.autoBrightnessAvailable = true;
        s.autoBrightnessEnabled = ab->automaticBrightness();
    }
#endif

    const ProjectionLogic::Pair pair = ProjectionLogic::pickPair(m_config);
    s.projectionAvailable = pair.isValid();
    if (pair.isValid()) {
        s.projectionMode = ProjectionLogic::detect(m_config);
        s.internalName = outputName(pair.internal);
        s.externalName = outputName(pair.external);
    }
    s.mirrorEmulated = !ProjectionLogic::supportsNativeReplication(m_config);
    return s;
}

void DisplayConfig::updateState()
{
    m_updateTimer->stop();
    const State old = m_state;
    m_state = computeState();

    if (old.available != m_state.available) {
        Q_EMIT availableChanged();
    }
    if (old.outputCount != m_state.outputCount || old.projectionAvailable != m_state.projectionAvailable
        || old.mirrorEmulated != m_state.mirrorEmulated || old.internalName != m_state.internalName || old.externalName != m_state.externalName) {
        Q_EMIT outputsChanged();
    }
    if (old.refreshRates != m_state.refreshRates || old.currentRefreshRate != m_state.currentRefreshRate) {
        Q_EMIT refreshRatesChanged();
    }
    if (old.projectionMode != m_state.projectionMode) {
        Q_EMIT projectionModeChanged();
    }
    if (old.hdrAvailable != m_state.hdrAvailable || old.hdrEnabled != m_state.hdrEnabled || old.hdrOutputName != m_state.hdrOutputName) {
        Q_EMIT hdrChanged();
    }
    if (old.autoBrightnessAvailable != m_state.autoBrightnessAvailable || old.autoBrightnessEnabled != m_state.autoBrightnessEnabled) {
        Q_EMIT autoBrightnessChanged();
    }
}

bool DisplayConfig::available() const
{
    return m_state.available;
}

QString DisplayConfig::errorText() const
{
    return m_errorText;
}

bool DisplayConfig::busy() const
{
    return m_busy;
}

int DisplayConfig::outputCount() const
{
    return m_state.outputCount;
}

bool DisplayConfig::refreshToggleAvailable() const
{
    return refreshRates().size() >= 2;
}

QList<int> DisplayConfig::refreshRates() const
{
    return m_state.refreshRates;
}

int DisplayConfig::currentRefreshRate() const
{
    return m_state.currentRefreshRate;
}

int DisplayConfig::alternateRefreshRate() const
{
    return RefreshRateLogic::alternate(refreshRates(), currentRefreshRate());
}

bool DisplayConfig::projectionAvailable() const
{
    return m_state.projectionAvailable;
}

DisplayConfig::ProjectionMode DisplayConfig::projectionMode() const
{
    return ProjectionMode(m_state.projectionMode);
}

bool DisplayConfig::mirrorEmulated() const
{
    return m_state.mirrorEmulated;
}

bool DisplayConfig::hdrAvailable() const
{
    return m_state.hdrAvailable;
}

bool DisplayConfig::hdrEnabled() const
{
    return m_state.hdrEnabled;
}

QString DisplayConfig::hdrOutputName() const
{
    return m_state.hdrOutputName;
}

bool DisplayConfig::autoBrightnessAvailable() const
{
    return m_state.autoBrightnessAvailable;
}

bool DisplayConfig::autoBrightnessEnabled() const
{
    return m_state.autoBrightnessEnabled;
}

QString DisplayConfig::internalName() const
{
    return m_state.internalName;
}

QString DisplayConfig::externalName() const
{
    return m_state.externalName;
}

void DisplayConfig::setBusy(bool busy)
{
    if (m_busy != busy) {
        m_busy = busy;
        Q_EMIT busyChanged();
    }
}

void DisplayConfig::setErrorText(const QString &text)
{
    if (m_errorText != text) {
        m_errorText = text;
        Q_EMIT errorTextChanged();
    }
}

// STATE-CHANGING: only reached from setRefreshRate()/setProjectionMode(),
// i.e. on user action.
void DisplayConfig::applyConfig(const KScreen::ConfigPtr &config)
{
    if (!KScreen::Config::canBeApplied(config, KScreen::Config::ValidityFlag::RequireAtLeastOneEnabledScreen)) {
        setErrorText(u"This display configuration cannot be applied."_s);
        return;
    }
    setErrorText({});
    setBusy(true);
    auto *op = new KScreen::SetConfigOperation(config, this);
    connect(op, &KScreen::ConfigOperation::finished, this, [this](KScreen::ConfigOperation *finished) {
        setBusy(false);
        if (finished->hasError()) {
            const QString detail = finished->errorString();
            setErrorText(detail.isEmpty() ? u"Changing the display configuration failed."_s
                                          : u"Changing the display configuration failed: %1"_s.arg(detail));
        }
        // Read back what the backend actually did. ConfigMonitor usually
        // updates m_config in place as well; a fresh read also covers
        // backends that do not report the change.
        refresh();
    });
}

void DisplayConfig::setRefreshRate(int hz)
{
    if (!m_config || m_busy) {
        return;
    }
    const KScreen::ConfigPtr clone = m_config->clone();
    const KScreen::OutputPtr panel = RefreshRateLogic::panel(clone);
    if (!panel) {
        return;
    }
    const QString modeId = RefreshRateLogic::bestModeId(panel, hz);
    if (modeId.isEmpty() || modeId == panel->currentModeId()) {
        return;
    }
    panel->setCurrentModeId(modeId);
    applyConfig(clone);
}

void DisplayConfig::toggleRefreshRate()
{
    const int target = alternateRefreshRate();
    if (target > 0) {
        setRefreshRate(target);
    }
}

void DisplayConfig::setProjectionMode(int mode)
{
    if (!m_config || m_busy || mode <= int(Unknown) || mode > int(ExternalOnly)) {
        return;
    }
    const KScreen::ConfigPtr clone = m_config->clone();
    const bool native = ProjectionLogic::supportsNativeReplication(clone);
    if (!ProjectionLogic::apply(clone, ProjectionLogic::Mode(mode), native)) {
        setErrorText(mode == int(Mirror) ? u"The screens have no resolution in common to mirror."_s : u"This projection mode is not available."_s);
        return;
    }
    applyConfig(clone);
}

void DisplayConfig::setHdrEnabled(bool enabled)
{
    if (!m_config || m_busy) {
        return;
    }
    const KScreen::ConfigPtr clone = m_config->clone();
    const KScreen::OutputPtr output = outputWith(clone, KScreen::Output::Capability::HighDynamicRange);
    if (!output || output->isHdrEnabled() == enabled) {
        return;
    }
    output->setHdrEnabled(enabled);
    applyConfig(clone);
}

void DisplayConfig::setAutoBrightnessEnabled(bool enabled)
{
#ifdef MC_KSCREEN_AUTOBRIGHTNESS
    if (!m_config || m_busy) {
        return;
    }
    const KScreen::ConfigPtr clone = m_config->clone();
    const KScreen::OutputPtr output = outputWith(clone, KScreen::Output::Capability::AutomaticBrightness);
    if (!output || output->automaticBrightness() == enabled) {
        return;
    }
    output->setAutomaticBrightness(enabled);
    applyConfig(clone);
#else
    Q_UNUSED(enabled)
#endif
}

void DisplayConfig::showKdeSwitcher()
{
    // Explicit user request only: this D-Bus-activates kscreen's OSD service.
    MC::DBus::call(QDBusConnection::sessionBus(),
                   MC::DBus::methodCall(kOsdService, kOsdPath, kOsdIface, u"showActionSelector"_s),
                   this,
                   [](const QDBusMessage &reply) {
                       if (reply.type() == QDBusMessage::ErrorMessage) {
                           qCWarning(lcDisplay) << "showActionSelector failed:" << reply.errorMessage();
                       }
                   });
}
