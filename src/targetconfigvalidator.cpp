#include "targetconfigvalidator.h"

#include "targetconfigparser.h"

bool TargetConfigValidator::validateTarget(const QJsonObject &target, QList<TargetDiagnostic> *diagnostics)
{
    ParsedTargetConfig parsed;
    return TargetConfigParser::parse(target, &parsed, diagnostics);
}
