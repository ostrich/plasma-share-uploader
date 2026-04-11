#pragma once

#include "targetdiagnostic.h"

#include <QJsonObject>
#include <QList>

namespace TargetConfigValidator {
bool validateTarget(const QJsonObject &target, QList<TargetDiagnostic> *diagnostics = nullptr);
}
