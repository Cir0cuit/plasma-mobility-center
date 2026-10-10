// SPDX-License-Identifier: GPL-2.0-or-later
//
// PlasmaNmBridge - the two plasma-nm settings we share,
// both in ~/.config/plasma-nm [General] (KF6::ConfigCore, KConfigWatcher):
//   * AirplaneModeEnabled - written by us with KConfig::Notify so the tray's
//     airplane icon follows. Live only on Plasma >= 6.5 (the watcher arrived
//     in 6.4.90); on 6.3/6.4 the tray stays stale. While it is true the tray
//     hides its own Wi-Fi toggles, so whenever OUR airplane mode ends - by
//     any route - write false.
//   * SystemConnectionsByDefault - default true from plasma-nm 6.6, false
//     before; decides who owns new connections' secrets.
// This is a file contract, not a public API.

#pragma once

#include <KConfigWatcher>
#include <KSharedConfig>

#include <QObject>
#include <QQmlEngine>

class PlasmaNmBridge : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    /// The plasma-nm config can be read (always true once loaded).
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    /// Last error worth showing (e.g. the file is immutable); empty if none.
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)
    /// Current AirplaneModeEnabled value (watched).
    Q_PROPERTY(bool airplaneFlag READ airplaneFlag NOTIFY airplaneFlagChanged)
    /// SystemConnectionsByDefault, with plasma-nm's default (true) when unset.
    Q_PROPERTY(bool systemConnectionsByDefault READ systemConnectionsByDefault NOTIFY systemConnectionsByDefaultChanged)

public:
    explicit PlasmaNmBridge(QObject *parent = nullptr);
    ~PlasmaNmBridge() override;

    bool available() const;
    QString errorText() const;
    bool airplaneFlag() const;
    bool systemConnectionsByDefault() const;

    /// Writes AirplaneModeEnabled = @p on with KConfig::Notify.
    /// STATE-CHANGING: only call from airplane mode on/off.
    Q_INVOKABLE void setAirplaneFlag(bool on);

    /// Re-reads both keys. Read-only.
    Q_INVOKABLE void refresh();

Q_SIGNALS:
    void availableChanged();
    void errorTextChanged();
    void airplaneFlagChanged();
    void systemConnectionsByDefaultChanged();

private:
    void read();
    void setErrorText(const QString &text);

    KSharedConfigPtr m_config;
    KConfigWatcher::Ptr m_watcher;
    bool m_airplaneFlag = false;
    bool m_systemConnectionsByDefault = true;
    QString m_errorText;
};
