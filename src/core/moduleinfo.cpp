// SPDX-License-Identifier: GPL-2.0-or-later

#include "moduleinfo.h"

ModuleInfo::ModuleInfo(QObject *parent)
    : QObject(parent)
{
}

int ModuleInfo::apiVersion() const
{
    return MC_API_VERSION;
}

QString ModuleInfo::version() const
{
    return QStringLiteral(MC_VERSION_STRING);
}

QStringList ModuleInfo::builtModules() const
{
    return QStringLiteral(MC_BUILT_MODULES).split(QLatin1Char(','), Qt::SkipEmptyParts);
}

bool ModuleInfo::feedbackSoundBuilt() const
{
#ifdef MC_BUILT_WITH_CANBERRA
    return true;
#else
    return false;
#endif
}
