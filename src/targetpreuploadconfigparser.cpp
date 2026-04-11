#include "targetpreuploadconfigparser.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>

namespace {
const QRegularExpression kPlaceholderPattern(QStringLiteral(R"(\$\{([A-Z_]+)\})"));
const QRegularExpression kMimePattern(QStringLiteral("^[^/\\s]+/[^/\\s]+$"));
const QRegularExpression kMimeWildcardPattern(QStringLiteral("^[^/\\s]+/\\*$"));
constexpr int kDefaultPreUploadTimeoutMs = 30000;

bool appendDiagnostic(QList<TargetDiagnostic> *diagnostics,
                      const QString &jsonPath,
                      const QString &code,
                      const QString &message)
{
    if (diagnostics) {
        diagnostics->append(TargetDiagnostic{TargetDiagnosticSeverity::Error, {}, jsonPath, code, message});
    }
    return false;
}

QJsonArray arrayValue(const QJsonObject &parent, const char *key)
{
    const QJsonValue value = parent.value(QLatin1StringView(key));
    return value.isArray() ? value.toArray() : QJsonArray();
}

QString stringValue(const QJsonObject &parent, const char *key)
{
    return parent.value(QLatin1StringView(key)).toString();
}

bool validateMimePattern(const QString &messagePath,
                         const QString &jsonPath,
                         const QString &pattern,
                         const QString &codePrefix,
                         QList<TargetDiagnostic> *diagnostics)
{
    if (pattern.isEmpty()) {
        return appendDiagnostic(diagnostics,
                                jsonPath,
                                QStringLiteral("%1.empty").arg(codePrefix),
                                QStringLiteral("%1 must contain non-empty strings").arg(messagePath));
    }
    if (pattern != QLatin1StringView("*/*")
        && !kMimePattern.match(pattern).hasMatch()
        && !kMimeWildcardPattern.match(pattern).hasMatch()) {
        return appendDiagnostic(diagnostics,
                                jsonPath,
                                QStringLiteral("%1.invalid").arg(codePrefix),
                                QStringLiteral("%1 entries must be exact MIME types, type/*, or */*").arg(messagePath));
    }
    if (pattern.startsWith(QLatin1StringView("*/")) && pattern != QLatin1StringView("*/*")) {
        return appendDiagnostic(diagnostics,
                                jsonPath,
                                QStringLiteral("%1.invalid").arg(codePrefix),
                                QStringLiteral("%1 entries must be exact MIME types, type/*, or */*").arg(messagePath));
    }
    return true;
}
}

bool TargetPreUploadConfigParser::parse(const QJsonObject &target,
                                        ParsedPreUploadConfig *parsed,
                                        QList<TargetDiagnostic> *diagnostics)
{
    const QString targetId = target.value(QStringLiteral("id")).toString();
    const QJsonValue value = target.value(QStringLiteral("preUpload"));

    ParsedPreUploadConfig local;
    if (value.isUndefined()) {
        if (parsed) {
            *parsed = local;
        }
        return true;
    }

    if (!value.isArray()) {
        return appendDiagnostic(diagnostics,
                                QStringLiteral("/preUpload"),
                                QStringLiteral("preUpload.type"),
                                QStringLiteral("Target '%1' preUpload must be a list").arg(targetId));
    }

    bool ok = true;
    const QJsonArray rules = value.toArray();
    for (int i = 0; i < rules.size(); ++i) {
        if (!rules.at(i).isObject()) {
            ok = appendDiagnostic(diagnostics,
                                  QStringLiteral("/preUpload/%1").arg(i),
                                  QStringLiteral("preUpload.rule.type"),
                                  QStringLiteral("Target '%1' preUpload[%2] must be an object").arg(targetId).arg(i));
            continue;
        }

        ParsedPreUploadRule parsedRule;
        const QString rulePath = QStringLiteral("Target '%1' preUpload[%2]").arg(targetId).arg(i);
        const QString ruleJsonPath = QStringLiteral("/preUpload/%1").arg(i);
        const QJsonObject rule = rules.at(i).toObject();

        const QJsonArray mimePatterns = arrayValue(rule, "mime");
        if (mimePatterns.isEmpty()) {
            ok = appendDiagnostic(diagnostics,
                                  QStringLiteral("%1/mime").arg(ruleJsonPath),
                                  QStringLiteral("preUpload.mime.empty"),
                                  QStringLiteral("%1.mime must be a non-empty list").arg(rulePath));
        } else {
            for (int mimeIndex = 0; mimeIndex < mimePatterns.size(); ++mimeIndex) {
                const QJsonValue patternValue = mimePatterns.at(mimeIndex);
                if (!patternValue.isString()) {
                    ok = appendDiagnostic(diagnostics,
                                          QStringLiteral("%1/mime/%2").arg(ruleJsonPath).arg(mimeIndex),
                                          QStringLiteral("preUpload.mime.type"),
                                          QStringLiteral("%1.mime must contain non-empty strings").arg(rulePath));
                    continue;
                }
                const QString pattern = patternValue.toString();
                ok = validateMimePattern(QStringLiteral("%1.mime").arg(rulePath),
                                         QStringLiteral("%1/mime/%2").arg(ruleJsonPath).arg(mimeIndex),
                                         pattern,
                                         QStringLiteral("preUpload.mime"),
                                         diagnostics)
                    && ok;
                if (!pattern.isEmpty()) {
                    parsedRule.mimePatterns.append(pattern);
                }
            }
        }

        const QString fileHandling = stringValue(rule, "fileHandling");
        const bool validFileHandling = fileHandling == QLatin1StringView("inplace_copy")
            || fileHandling == QLatin1StringView("output_file");
        if (!validFileHandling) {
            ok = appendDiagnostic(diagnostics,
                                  QStringLiteral("%1/fileHandling").arg(ruleJsonPath),
                                  QStringLiteral("preUpload.fileHandling.invalid"),
                                  QStringLiteral("%1.fileHandling must be inplace_copy or output_file").arg(rulePath));
        } else {
            parsedRule.fileHandling = fileHandling == QLatin1StringView("output_file")
                ? PreUploadFileHandling::OutputFile
                : PreUploadFileHandling::InplaceCopy;
        }

        const QJsonArray commands = arrayValue(rule, "commands");
        if (commands.isEmpty()) {
            ok = appendDiagnostic(diagnostics,
                                  QStringLiteral("%1/commands").arg(ruleJsonPath),
                                  QStringLiteral("preUpload.commands.empty"),
                                  QStringLiteral("%1.commands must be a non-empty list").arg(rulePath));
        } else if (validFileHandling && fileHandling == QLatin1StringView("output_file") && commands.size() != 1) {
            ok = appendDiagnostic(diagnostics,
                                  QStringLiteral("%1/commands").arg(ruleJsonPath),
                                  QStringLiteral("preUpload.commands.output_file_count"),
                                  QStringLiteral("%1.commands must contain exactly one command for output_file").arg(rulePath));
        }

        bool sawOutFile = false;
        for (int commandIndex = 0; commandIndex < commands.size(); ++commandIndex) {
            if (!commands.at(commandIndex).isObject()) {
                ok = appendDiagnostic(diagnostics,
                                      QStringLiteral("%1/commands/%2").arg(ruleJsonPath).arg(commandIndex),
                                      QStringLiteral("preUpload.command.type"),
                                      QStringLiteral("%1.commands[%2] must be an object").arg(rulePath).arg(commandIndex));
                continue;
            }

            ParsedPreUploadCommand parsedCommand;
            const QString commandPath = QStringLiteral("%1.commands[%2]").arg(rulePath).arg(commandIndex);
            const QString commandJsonPath = QStringLiteral("%1/commands/%2").arg(ruleJsonPath).arg(commandIndex);
            const QJsonArray argv = arrayValue(commands.at(commandIndex).toObject(), "argv");
            if (argv.isEmpty()) {
                ok = appendDiagnostic(diagnostics,
                                      QStringLiteral("%1/argv").arg(commandJsonPath),
                                      QStringLiteral("preUpload.argv.empty"),
                                      QStringLiteral("%1.argv must be a non-empty list").arg(commandPath));
                continue;
            }

            bool hasFile = false;
            bool hasOutFile = false;
            for (int argIndex = 0; argIndex < argv.size(); ++argIndex) {
                if (!argv.at(argIndex).isString() || argv.at(argIndex).toString().isEmpty()) {
                    ok = appendDiagnostic(diagnostics,
                                          QStringLiteral("%1/argv/%2").arg(commandJsonPath).arg(argIndex),
                                          QStringLiteral("preUpload.argv.entry.empty"),
                                          QStringLiteral("%1.argv must contain non-empty strings").arg(commandPath));
                    continue;
                }

                const QString arg = argv.at(argIndex).toString();
                parsedCommand.argv.append(arg);
                hasFile = hasFile || arg.contains(QStringLiteral("${FILE}"));
                hasOutFile = hasOutFile || arg.contains(QStringLiteral("${OUT_FILE}"));

                const auto matches = kPlaceholderPattern.globalMatch(arg);
                auto it = matches;
                while (it.hasNext()) {
                    const QString placeholder = it.next().captured(1);
                    if (placeholder != QLatin1StringView("FILE") && placeholder != QLatin1StringView("OUT_FILE")) {
                        ok = appendDiagnostic(diagnostics,
                                              QStringLiteral("%1/argv/%2").arg(commandJsonPath).arg(argIndex),
                                              QStringLiteral("preUpload.argv.placeholder.unsupported"),
                                              QStringLiteral("%1.argv contains unsupported placeholder ${%2}").arg(commandPath, placeholder));
                    }
                }
            }

            if (!hasFile) {
                ok = appendDiagnostic(diagnostics,
                                      QStringLiteral("%1/argv").arg(commandJsonPath),
                                      QStringLiteral("preUpload.argv.file.missing"),
                                      QStringLiteral("%1.argv must include ${FILE}").arg(commandPath));
            }
            if (validFileHandling && fileHandling == QLatin1StringView("inplace_copy") && hasOutFile) {
                ok = appendDiagnostic(diagnostics,
                                      QStringLiteral("%1/argv").arg(commandJsonPath),
                                      QStringLiteral("preUpload.argv.outfile.disallowed"),
                                      QStringLiteral("%1.argv must not include ${OUT_FILE} for inplace_copy").arg(commandPath));
            }

            sawOutFile = sawOutFile || hasOutFile;
            parsedRule.commands.append(parsedCommand);
        }

        if (validFileHandling && fileHandling == QLatin1StringView("output_file") && !sawOutFile) {
            ok = appendDiagnostic(diagnostics,
                                  QStringLiteral("%1/commands/0/argv").arg(ruleJsonPath),
                                  QStringLiteral("preUpload.argv.outfile.missing"),
                                  QStringLiteral("%1.commands[0].argv must include ${OUT_FILE}").arg(rulePath));
        }

        const QJsonValue timeout = rule.value(QStringLiteral("timeoutMs"));
        if (!timeout.isUndefined()) {
            if (!timeout.isDouble() || timeout.toInt() <= 0) {
                ok = appendDiagnostic(diagnostics,
                                      QStringLiteral("%1/timeoutMs").arg(ruleJsonPath),
                                      QStringLiteral("preUpload.timeout.invalid"),
                                      QStringLiteral("%1.timeoutMs must be a positive integer").arg(rulePath));
            } else {
                parsedRule.timeoutMs = timeout.toInt(kDefaultPreUploadTimeoutMs);
            }
        }

        local.rules.append(parsedRule);
    }

    if (parsed) {
        *parsed = local;
    }
    return ok;
}
