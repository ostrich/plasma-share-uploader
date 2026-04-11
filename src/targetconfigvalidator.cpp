#include "targetconfigvalidator.h"

#include "targetcoreconfigparser.h"
#include "targetpreuploadconfigparser.h"
#include "targetrequestconfigparser.h"
#include "targetresponseconfigparser.h"

bool TargetConfigValidator::validateTarget(const QJsonObject &target, QList<TargetDiagnostic> *diagnostics)
{
    ParsedTargetCoreConfig core;
    const bool coreOk = TargetCoreConfigParser::parse(target, &core, diagnostics);
    if (core.id.isEmpty()) {
        return false;
    }

    bool ok = coreOk;

    ParsedRequestConfig request;
    ok = TargetRequestConfigParser::parse(target, &request, diagnostics) && ok;

    ParsedResponseConfig response;
    ok = TargetResponseConfigParser::parse(target, &response, diagnostics) && ok;

    ParsedPreUploadConfig preUpload;
    ok = TargetPreUploadConfigParser::parse(target, &preUpload, diagnostics) && ok;

    return ok;
}
