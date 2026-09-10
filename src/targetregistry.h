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

    explicit TargetRegistry(QString bundledPath = {}, QString activePath = {});

    LoadResult loadTargets() const;
    QString bundledTargetsPath() const;
    QString activeTargetsPath() const;

private:
    QString m_bundledPath;
    QString m_activePath;
};
