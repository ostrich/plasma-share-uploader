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

bool validateJsonValuePlaceholders(const QString &targetId, const QJsonValue &value, const QString &path, QStringList *errors)
{
    if (value.isString()) {
        const auto matches = kPlaceholderPattern.globalMatch(value.toString());
        auto it = matches;
        while (it.hasNext()) {
            const QString placeholder = it.next().captured(1);
            if (placeholder != QLatin1StringView("FILENAME")) {
                return appendError(errors, QStringLiteral("Target '%1' %2 contains unsupported placeholder ${%3}")
                                               .arg(targetId, path, placeholder));
            }
        }
        return true;
    }

    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        for (int i = 0; i < array.size(); ++i) {
            if (!validateJsonValuePlaceholders(targetId, array.at(i), QStringLiteral("%1[%2]").arg(path).arg(i), errors)) {
                return false;
            }
        }
        return true;
    }

    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it) {
            if (!validateJsonValuePlaceholders(targetId, it.value(), QStringLiteral("%1.%2").arg(path, it.key()), errors)) {
                return false;
            }
        }
    }
    return true;
}

bool validateResponseExtractor(const QString &targetId, const QJsonObject &response, const QString &path, QStringList *errors)
{
    if (response.isEmpty()) {
        return appendError(errors, QStringLiteral("Target '%1' %2 must be an object").arg(targetId, path));
    }

    const QString responseType = stringValue(response, "type");
    if (responseType != QLatin1StringView("text_url")
        && responseType != QLatin1StringView("regex")
        && responseType != QLatin1StringView("json_pointer")
        && responseType != QLatin1StringView("header")
        && responseType != QLatin1StringView("redirect_url")
        && responseType != QLatin1StringView("xml_xpath")) {
        return appendError(errors,
                           QStringLiteral("Target '%1' %2.type must be text_url, regex, json_pointer, header, redirect_url, or xml_xpath")
                               .arg(targetId, path));
    }

    if (responseType == QLatin1StringView("regex")) {
        if (stringValue(response, "pattern").isEmpty()) {
            return appendError(errors, QStringLiteral("Target '%1' %2.pattern must be a non-empty string").arg(targetId, path));
        }
        if (!response.value(QStringLiteral("group")).isUndefined() && response.value(QStringLiteral("group")).toInt(-1) < 0) {
            return appendError(errors, QStringLiteral("Target '%1' %2.group must be a non-negative integer").arg(targetId, path));
        }
    }

    if (responseType == QLatin1StringView("json_pointer")) {
        const QString pointer = stringValue(response, "pointer");
        if (pointer.isEmpty()) {
            return appendError(errors, QStringLiteral("Target '%1' %2.pointer must be a non-empty string").arg(targetId, path));
        }
        if (!pointer.startsWith(QLatin1Char('/'))) {
            return appendError(errors, QStringLiteral("Target '%1' %2.pointer must start with '/'").arg(targetId, path));
        }
    }

    if (responseType == QLatin1StringView("header") && stringValue(response, "name").isEmpty()) {
        return appendError(errors, QStringLiteral("Target '%1' %2.name must be a non-empty string").arg(targetId, path));
    }

    if (responseType == QLatin1StringView("xml_xpath")) {
        const QString xpath = stringValue(response, "xpath");
        if (xpath.isEmpty()) {
            return appendError(errors, QStringLiteral("Target '%1' %2.xpath must be a non-empty string").arg(targetId, path));
        }
        if (!xpath.startsWith(QLatin1Char('/'))) {
            return appendError(errors, QStringLiteral("Target '%1' %2.xpath must start with '/'").arg(targetId, path));
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
    if (requestType != QLatin1StringView("multipart")
        && requestType != QLatin1StringView("raw")
        && requestType != QLatin1StringView("form_urlencoded")
        && requestType != QLatin1StringView("json")) {
        return appendError(errors, QStringLiteral("Target '%1' request.type must be multipart, raw, form_urlencoded, or json").arg(targetId));
    }
    if (method.isEmpty()) {
        return appendError(errors, QStringLiteral("Target '%1' request.method must be a non-empty string").arg(targetId));
    }
    if (!validateStringMap(targetId, request.value(QStringLiteral("headers")), QStringLiteral("request.headers"), errors)) {
        return false;
    }
    if (!validateStringMap(targetId, request.value(QStringLiteral("query")), QStringLiteral("request.query"), errors)) {
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
    } else if (requestType == QLatin1StringView("raw")) {
        if (method.toUpper() != QLatin1StringView("POST") && method.toUpper() != QLatin1StringView("PUT")) {
            return appendError(errors, QStringLiteral("Target '%1' request.method must be POST or PUT for raw").arg(targetId));
        }
    } else if (requestType == QLatin1StringView("form_urlencoded")) {
        if (method.toUpper() != QLatin1StringView("POST") && method.toUpper() != QLatin1StringView("PUT")) {
            return appendError(errors, QStringLiteral("Target '%1' request.method must be POST or PUT for form_urlencoded").arg(targetId));
        }
        const QJsonObject form = objectValue(request, "formUrlencoded");
        if (form.isEmpty()) {
            return appendError(errors, QStringLiteral("Target '%1' request.formUrlencoded must be an object").arg(targetId));
        }
        if (!validateStringMap(targetId,
                               form.value(QStringLiteral("fields")),
                               QStringLiteral("request.formUrlencoded.fields"),
                               errors)) {
            return false;
        }
    } else if (requestType == QLatin1StringView("json")) {
        if (method.toUpper() != QLatin1StringView("POST") && method.toUpper() != QLatin1StringView("PUT")) {
            return appendError(errors, QStringLiteral("Target '%1' request.method must be POST or PUT for json").arg(targetId));
        }
        const QJsonObject json = objectValue(request, "json");
        if (json.isEmpty()) {
            return appendError(errors, QStringLiteral("Target '%1' request.json must be an object").arg(targetId));
        }
        if (!json.contains(QStringLiteral("fields"))) {
            return appendError(errors, QStringLiteral("Target '%1' request.json.fields must be present").arg(targetId));
        }
        if (!validateJsonValuePlaceholders(targetId, json.value(QStringLiteral("fields")), QStringLiteral("request.json.fields"), errors)) {
            return false;
        }
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

bool validateExtensions(const QString &targetId, const QJsonValue &value, QStringList *errors)
{
    if (!value.isUndefined() && !value.isArray()) {
        return appendError(errors, QStringLiteral("Target '%1' extensions must be a list").arg(targetId));
    }

    for (const QJsonValue &entry : value.toArray()) {
        const QString text = entry.toString().trimmed();
        if (text.isEmpty()) {
            return appendError(errors, QStringLiteral("Target '%1' extensions must contain non-empty strings").arg(targetId));
        }
        const QString normalized = text.startsWith(QLatin1Char('.')) ? text.mid(1) : text;
        if (normalized.isEmpty() || normalized.contains(QLatin1Char('/')) || normalized.contains(QLatin1Char('*'))
            || normalized.contains(QLatin1Char(' '))) {
            return appendError(errors,
                               QStringLiteral("Target '%1' extensions must contain plain file suffixes such as png or .png").arg(targetId));
        }
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
    if (!validateResponseExtractor(targetId, response, QStringLiteral("response"), errors)) {
        return false;
    }
    const QJsonValue errorValue = response.value(QStringLiteral("error"));
    if (!errorValue.isUndefined() && !errorValue.isObject()) {
        return appendError(errors, QStringLiteral("Target '%1' response.error must be an object").arg(targetId));
    }
    if (errorValue.isObject()
        && !validateResponseExtractor(targetId, errorValue.toObject(), QStringLiteral("response.error"), errors)) {
        return false;
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
    if (!validateExtensions(targetId, target.value(QStringLiteral("extensions")), errors)) {
        return false;
    }
    return true;
}
