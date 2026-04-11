#pragma once

#include "targetdiagnostic.h"

#include <QJsonObject>
#include <QList>
#include <QString>

enum class ResponseExtractorType
{
    TextUrl,
    Regex,
    JsonPointer,
    Header,
    RedirectUrl,
    XmlXpath
};

struct ParsedResponseExtractor
{
    ResponseExtractorType type = ResponseExtractorType::TextUrl;
    QString pattern;
    int group = 1;
    QString pointer;
    QString name;
    QString xpath;
    bool valid = false;
};

struct ParsedResponseConfig
{
    ParsedResponseExtractor success;
    ParsedResponseExtractor error;
    ParsedResponseExtractor thumbnail;
    ParsedResponseExtractor deletion;
};

namespace TargetResponseConfigParser {
bool parse(const QJsonObject &target, ParsedResponseConfig *parsed, QList<TargetDiagnostic> *diagnostics = nullptr);
}
