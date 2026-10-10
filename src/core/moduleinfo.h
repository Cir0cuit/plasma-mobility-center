// SPDX-License-Identifier: GPL-2.0-or-later
//
// ModuleInfo - build facts of the installed C++ modules, for the QML package.
//
// The package compares apiVersion with the value it was written against
// (CenterView.qml) and shows a "please reinstall / restart plasmashell"
// placeholder on a mismatch, instead of tiles that half work against a stale
// system-wide module.

#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QStringList>

class ModuleInfo : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// Version of the C++ <-> QML contract (MC_API_VERSION in CMakeLists.txt).
    /// Bumped on every incompatible change to any module's public API.
    Q_PROPERTY(int apiVersion READ apiVersion CONSTANT)
    /// Project version string, e.g. "0.1.0".
    Q_PROPERTY(QString version READ version CONSTANT)
    /// Modules that were built alongside this one: a subset of
    /// ["core", "audio", "network", "display"]. A tile whose module is listed
    /// here but fails to import means a broken install, not missing hardware.
    Q_PROPERTY(QStringList builtModules READ builtModules CONSTANT)
    /// True when .audio was built with libcanberra (volume feedback sound).
    Q_PROPERTY(bool feedbackSoundBuilt READ feedbackSoundBuilt CONSTANT)

public:
    explicit ModuleInfo(QObject *parent = nullptr);

    int apiVersion() const;
    QString version() const;
    QStringList builtModules() const;
    bool feedbackSoundBuilt() const;
};
