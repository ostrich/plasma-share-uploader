#pragma once

#include "targetdiagnostic.h"
#include "targetdefinition.h"

#include <QList>
#include <QString>

class TargetRegistry
{
public:
    struct LoadResult {
        QList<TargetDefinition> targets;
        QList<TargetDiagnostic> diagnostics;
    };

    explicit TargetRegistry(QString systemPath = {}, QString userPath = {});

    LoadResult loadTargets() const;
    QString systemTargetsPath() const;
    QString userTargetsPath() const;

private:
    QString m_systemPath;
    QString m_userPath;
};
