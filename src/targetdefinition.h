#pragma once

#include "targetconfigparser.h"

#include <QString>
#include <QStringList>

struct TargetDefinition {
    ParsedTargetConfig target;

    QString id() const;
    QString displayName() const;
    QString description() const;
    QString icon() const;
    QStringList mimeTypes() const;
    QStringList extensions() const;
};
