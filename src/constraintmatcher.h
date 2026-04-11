#pragma once

#include "targetdefinition.h"

#include <QStringList>

namespace ConstraintMatcher {
bool targetMatchesFiles(const TargetDefinition &target, const QStringList &filePaths);
QList<TargetDefinition> filterTargets(const QList<TargetDefinition> &targets, const QStringList &filePaths);
}
