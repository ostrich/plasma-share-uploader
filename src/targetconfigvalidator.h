#pragma once

#include <QJsonObject>
#include <QStringList>

namespace TargetConfigValidator {
bool validateTarget(const QJsonObject &target, QStringList *errors = nullptr);
}
