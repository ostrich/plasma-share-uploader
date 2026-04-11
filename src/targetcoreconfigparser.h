#pragma once

#include "targetdiagnostic.h"

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

struct ParsedTargetCoreConfig
{
    QString id;
    QString displayName;
    QString description;
    QString icon;
    QStringList pluginTypes;
    QStringList constraints;
    QStringList extensions;
};

namespace TargetCoreConfigParser {
bool parse(const QJsonObject &target, ParsedTargetCoreConfig *parsed, QList<TargetDiagnostic> *diagnostics = nullptr);
}
