#pragma once

#include "targetconfigparser.h"

#include <QString>
#include <QStringList>

struct TargetDefinition {
    enum class Source {
        System,
        User,
    };

    ParsedTargetConfig target;
    Source source = Source::System;

    QString id() const;
    QString displayName() const;
    QString description() const;
    QString icon() const;
    QStringList constraints() const;
    QStringList extensions() const;
    bool isBundled() const;
};
