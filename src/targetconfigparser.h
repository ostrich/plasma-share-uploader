#pragma once

#include "targetcoreconfigparser.h"
#include "targetdiagnostic.h"
#include "targetpreuploadconfigparser.h"
#include "targetrequestconfigparser.h"
#include "targetresponseconfigparser.h"

#include <QJsonObject>
#include <QList>

struct ParsedTargetConfig
{
    ParsedTargetCoreConfig core;
    ParsedRequestConfig request;
    ParsedResponseConfig response;
    ParsedPreUploadConfig preUpload;
    bool valid = false;
};

namespace TargetConfigParser {
bool parse(const QJsonObject &target, ParsedTargetConfig *parsed, QList<TargetDiagnostic> *diagnostics = nullptr);
}
