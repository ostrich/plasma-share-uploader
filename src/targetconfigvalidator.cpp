#include "targetconfigvalidator.h"

#include <QJsonArray>
#include <QRegularExpression>

namespace {
const QRegularExpression kIdPattern(QStringLiteral("^[a-z0-9][a-z0-9_-]*$"));
const QRegularExpression kMimePattern(QStringLiteral("^[^/\\s]+/[^/\\s]+$"));
const QRegularExpression kMimeWildcardPattern(QStringLiteral("^[^/\\s]+/\\*$"));
const QRegularExpression kPlaceholderPattern(QStringLiteral(R"(\$\{([A-Z_]+)\})"));

QJsonObject objectValue(const QJsonObject &parent, const char *key)
{
    const QJsonValue value = parent.value(QLatin1StringView(key));
    return value.isObject() ? value.toObject() : QJsonObject();
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

bool appendError(QStringList *errors, const QString &message)
{
    if (errors) {
        errors->append(message);
    }
    return false;
}

bool validateStringMap(const QString &targetId, const QJsonValue &value, const QString &path, QStringList *errors)
{
    if (!value.isUndefined() && !value.isObject()) {
        return appendError(errors, QStringLiteral("Target '%1' %2 must be an object").arg(targetId, path));
    }

    const QJsonObject map = value.toObject();
    for (auto it = map.begin(); it != map.end(); ++it) {
        if (it.key().isEmpty()) {
            return appendError(errors, QStringLiteral("Target '%1' %2 keys must be non-empty strings").arg(targetId, path));
        }
        if (!it.value().isString()) {
            return appendError(errors, QStringLiteral("Target '%1' %2 values must be strings").arg(targetId, path));
        }
    }
    return true;
}

bool validateRequest(const QString &targetId, const QJsonObject &request, QStringList *errors)
{
    if (request.isEmpty()) {
        return appendError(errors, QStringLiteral("Target '%1' missing request object").arg(targetId));
    }

    const QString url = stringValue(request, "url");
    const QString method = stringValue(request, "method");
    const QString requestType = stringValue(request, "type").isEmpty()
        ? QStringLiteral("multipart")
        : stringValue(request, "type");

    if (url.isEmpty()) {
        return appendError(errors, QStringLiteral("Target '%1' request.url must be a non-empty string").arg(targetId));
    }
    if (requestType != QLatin1StringView("multipart") && requestType != QLatin1StringView("raw")) {
        return appendError(errors, QStringLiteral("Target '%1' request.type must be multipart or raw").arg(targetId));
    }
    if (method.isEmpty()) {
        return appendError(errors, QStringLiteral("Target '%1' request.method must be a non-empty string").arg(targetId));
    }
    if (!validateStringMap(targetId, request.value(QStringLiteral("headers")), QStringLiteral("request.headers"), errors)) {
        return false;
    }

    if (requestType == QLatin1StringView("multipart")) {
        if (method.toUpper() != QLatin1StringView("POST")) {
            return appendError(errors, QStringLiteral("Target '%1' request.method must be POST for multipart").arg(targetId));
        }

        const QJsonObject multipart = objectValue(request, "multipart");
        if (multipart.isEmpty()) {
            return appendError(errors, QStringLiteral("Target '%1' request.multipart must be an object").arg(targetId));
        }
        if (stringValue(multipart, "fileField").isEmpty()) {
            return appendError(errors, QStringLiteral("Target '%1' request.multipart.fileField must be a non-empty string").arg(targetId));
        }
        if (!validateStringMap(targetId, multipart.value(QStringLiteral("fields")), QStringLiteral("request.multipart.fields"), errors)) {
            return false;
        }
    } else if (method.toUpper() != QLatin1StringView("POST") && method.toUpper() != QLatin1StringView("PUT")) {
        return appendError(errors, QStringLiteral("Target '%1' request.method must be POST or PUT for raw").arg(targetId));
    }

    return true;
}

bool validateMimePattern(const QString &rulePath, const QString &pattern, QStringList *errors)
{
    if (pattern.isEmpty()) {
        return appendError(errors, QStringLiteral("%1.mime must contain non-empty strings").arg(rulePath));
    }
    if (pattern != QLatin1StringView("*/*")
        && !kMimePattern.match(pattern).hasMatch()
        && !kMimeWildcardPattern.match(pattern).hasMatch()) {
        return appendError(errors, QStringLiteral("%1.mime entries must be exact MIME types, type/*, or */*").arg(rulePath));
    }
    if (pattern.startsWith(QLatin1StringView("*/")) && pattern != QLatin1StringView("*/*")) {
        return appendError(errors, QStringLiteral("%1.mime entries must be exact MIME types, type/*, or */*").arg(rulePath));
    }
    return true;
}

bool validateConstraints(const QString &targetId, const QJsonValue &value, QStringList *errors)
{
    if (!value.isUndefined() && !value.isArray()) {
        return appendError(errors, QStringLiteral("Target '%1' constraints must be a list").arg(targetId));
    }

    for (const QJsonValue &entry : value.toArray()) {
        const QString text = entry.toString();
        if (text.isEmpty()) {
            return appendError(errors, QStringLiteral("Target '%1' constraints must contain non-empty strings").arg(targetId));
        }
        if (!text.startsWith(QLatin1StringView("mimeType:"))) {
            return appendError(errors, QStringLiteral("Target '%1' constraints currently only support mimeType: patterns").arg(targetId));
        }
        if (!validateMimePattern(QStringLiteral("Target '%1' constraint").arg(targetId), text.mid(9), errors)) {
            return false;
        }
    }
    return true;
}

bool validatePreUpload(const QString &targetId, const QJsonValue &value, QStringList *errors)
{
    if (!value.isUndefined() && !value.isArray()) {
        return appendError(errors, QStringLiteral("Target '%1' preUpload must be a list").arg(targetId));
    }

    const QJsonArray rules = value.toArray();
    for (int i = 0; i < rules.size(); ++i) {
        if (!rules.at(i).isObject()) {
            return appendError(errors, QStringLiteral("Target '%1' preUpload[%2] must be an object").arg(targetId).arg(i));
        }
        const QString rulePath = QStringLiteral("Target '%1' preUpload[%2]").arg(targetId).arg(i);
        const QJsonObject rule = rules.at(i).toObject();
        const QJsonArray mimePatterns = arrayValue(rule, "mime");
        if (mimePatterns.isEmpty()) {
            return appendError(errors, QStringLiteral("%1.mime must be a non-empty list").arg(rulePath));
        }
        for (const QJsonValue &patternValue : mimePatterns) {
            if (!patternValue.isString() || !validateMimePattern(rulePath, patternValue.toString(), errors)) {
                return false;
            }
        }

        const QString fileHandling = stringValue(rule, "fileHandling");
        if (fileHandling != QLatin1StringView("inplace_copy") && fileHandling != QLatin1StringView("output_file")) {
            return appendError(errors, QStringLiteral("%1.fileHandling must be inplace_copy or output_file").arg(rulePath));
        }

        const QJsonArray commands = arrayValue(rule, "commands");
        if (commands.isEmpty()) {
            return appendError(errors, QStringLiteral("%1.commands must be a non-empty list").arg(rulePath));
        }
        if (fileHandling == QLatin1StringView("output_file") && commands.size() != 1) {
            return appendError(errors, QStringLiteral("%1.commands must contain exactly one command for output_file").arg(rulePath));
        }

        bool sawOutFile = false;
        for (int commandIndex = 0; commandIndex < commands.size(); ++commandIndex) {
            if (!commands.at(commandIndex).isObject()) {
                return appendError(errors, QStringLiteral("%1.commands[%2] must be an object").arg(rulePath).arg(commandIndex));
            }

            const QString commandPath = QStringLiteral("%1.commands[%2]").arg(rulePath).arg(commandIndex);
            const QJsonArray argv = arrayValue(commands.at(commandIndex).toObject(), "argv");
            if (argv.isEmpty()) {
                return appendError(errors, QStringLiteral("%1.argv must be a non-empty list").arg(commandPath));
            }

            bool hasFile = false;
            bool hasOutFile = false;
            for (const QJsonValue &argValue : argv) {
                const QString arg = argValue.toString();
                if (arg.isEmpty()) {
                    return appendError(errors, QStringLiteral("%1.argv must contain non-empty strings").arg(commandPath));
                }
                hasFile = hasFile || arg.contains(QStringLiteral("${FILE}"));
                hasOutFile = hasOutFile || arg.contains(QStringLiteral("${OUT_FILE}"));
                const auto matches = kPlaceholderPattern.globalMatch(arg);
                auto it = matches;
                while (it.hasNext()) {
                    const QString placeholder = it.next().captured(1);
                    if (placeholder != QLatin1StringView("FILE") && placeholder != QLatin1StringView("OUT_FILE")) {
                        return appendError(errors, QStringLiteral("%1.argv contains unsupported placeholder ${%2}").arg(commandPath, placeholder));
                    }
                }
            }

            if (!hasFile) {
                return appendError(errors, QStringLiteral("%1.argv must include ${FILE}").arg(commandPath));
            }
            if (fileHandling == QLatin1StringView("inplace_copy") && hasOutFile) {
                return appendError(errors, QStringLiteral("%1.argv must not include ${OUT_FILE} for inplace_copy").arg(commandPath));
            }

            sawOutFile = sawOutFile || hasOutFile;
        }

        if (fileHandling == QLatin1StringView("output_file") && !sawOutFile) {
            return appendError(errors, QStringLiteral("%1.commands[0].argv must include ${OUT_FILE}").arg(rulePath));
        }

        const QJsonValue timeout = rule.value(QStringLiteral("timeoutMs"));
        if (!timeout.isUndefined() && (!timeout.isDouble() || timeout.toInt() <= 0)) {
            return appendError(errors, QStringLiteral("%1.timeoutMs must be a positive integer").arg(rulePath));
        }
    }

    return true;
}

bool validateResponse(const QString &targetId, const QJsonObject &response, QStringList *errors)
{
    if (response.isEmpty()) {
        return appendError(errors, QStringLiteral("Target '%1' missing response object").arg(targetId));
    }

    const QString responseType = stringValue(response, "type");
    if (responseType != QLatin1StringView("text_url")
        && responseType != QLatin1StringView("regex")
        && responseType != QLatin1StringView("json_pointer")) {
        return appendError(errors, QStringLiteral("Target '%1' response.type must be text_url, regex, or json_pointer").arg(targetId));
    }

    if (responseType == QLatin1StringView("regex")) {
        if (stringValue(response, "pattern").isEmpty()) {
            return appendError(errors, QStringLiteral("Target '%1' response.pattern must be a non-empty string").arg(targetId));
        }
        if (!response.value(QStringLiteral("group")).isUndefined() && response.value(QStringLiteral("group")).toInt(-1) < 0) {
            return appendError(errors, QStringLiteral("Target '%1' response.group must be a non-negative integer").arg(targetId));
        }
    }

    if (responseType == QLatin1StringView("json_pointer")) {
        const QString pointer = stringValue(response, "pointer");
        if (pointer.isEmpty()) {
            return appendError(errors, QStringLiteral("Target '%1' response.pointer must be a non-empty string").arg(targetId));
        }
        if (!pointer.startsWith(QLatin1Char('/'))) {
            return appendError(errors, QStringLiteral("Target '%1' response.pointer must start with '/'").arg(targetId));
        }
    }
    return true;
}
}

bool TargetConfigValidator::validateTarget(const QJsonObject &target, QStringList *errors)
{
    const QString targetId = target.value(QStringLiteral("id")).toString();
    if (targetId.isEmpty() || !kIdPattern.match(targetId).hasMatch()) {
        return appendError(errors, QStringLiteral("Invalid target id: '%1'").arg(targetId));
    }

    if (!validateRequest(targetId, objectValue(target, "request"), errors)) {
        return false;
    }
    if (!validatePreUpload(targetId, target.value(QStringLiteral("preUpload")), errors)) {
        return false;
    }
    if (!validateResponse(targetId, objectValue(target, "response"), errors)) {
        return false;
    }
    if (!target.value(QStringLiteral("pluginTypes")).isUndefined() && !target.value(QStringLiteral("pluginTypes")).isArray()) {
        return appendError(errors, QStringLiteral("Target '%1' pluginTypes must be a list").arg(targetId));
    }
    if (!validateConstraints(targetId, target.value(QStringLiteral("constraints")), errors)) {
        return false;
    }
    return true;
}
