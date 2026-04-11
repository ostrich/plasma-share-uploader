#pragma once

#include "targetdiagnostic.h"

#include <QJsonObject>
#include <QList>
#include <QString>

enum class PreUploadFileHandling
{
    InplaceCopy,
    OutputFile
};

struct ParsedPreUploadCommand
{
    QStringList argv;
};

struct ParsedPreUploadRule
{
    QStringList mimePatterns;
    PreUploadFileHandling fileHandling = PreUploadFileHandling::InplaceCopy;
    QList<ParsedPreUploadCommand> commands;
    int timeoutMs = 30000;
};

struct ParsedPreUploadConfig
{
    QList<ParsedPreUploadRule> rules;
};

namespace TargetPreUploadConfigParser {
bool parse(const QJsonObject &target, ParsedPreUploadConfig *parsed, QList<TargetDiagnostic> *diagnostics = nullptr);
}
