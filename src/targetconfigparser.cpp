#include "targetconfigparser.h"

bool TargetConfigParser::parse(const QJsonObject &target,
                               ParsedTargetConfig *parsed,
                               QList<TargetDiagnostic> *diagnostics)
{
    ParsedTargetConfig local;

    bool ok = TargetCoreConfigParser::parse(target, &local.core, diagnostics);
    if (local.core.id.isEmpty()) {
        if (parsed) {
            *parsed = local;
        }
        return false;
    }

    ok = TargetRequestConfigParser::parse(target, &local.request, diagnostics) && ok;
    ok = TargetResponseConfigParser::parse(target, &local.response, diagnostics) && ok;
    ok = TargetPreUploadConfigParser::parse(target, &local.preUpload, diagnostics) && ok;
    local.valid = ok;

    if (parsed) {
        *parsed = local;
    }
    return ok;
}
