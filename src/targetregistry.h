#pragma once

#include "targetdefinition.h"

#include <QList>
#include <QString>
#include <QStringList>

class TargetRegistry
{
public:
    struct LoadResult {
        QList<TargetDefinition> targets;
        QStringList errors;
    };

    explicit TargetRegistry(QString systemPath = {}, QString userPath = {});

    LoadResult loadTargets() const;
    QString systemTargetsPath() const;
    QString userTargetsPath() const;

private:
    QString m_systemPath;
    QString m_userPath;
};
