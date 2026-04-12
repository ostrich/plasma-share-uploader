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

    explicit TargetRegistry(QString systemPath = {}, QString userPath = {}, QString statePath = {});

    LoadResult loadTargets() const;
    QString systemTargetsPath() const;
    QString userTargetsPath() const;
    QString stateFilePath() const;

private:
    QString m_systemPath;
    QString m_userPath;
    QString m_statePath;
};
