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

bool validateStringMap(const QString &targetId,
                       const QJsonValue &value,
                       const QString &path,
                       const QString &jsonPath,
                       QList<TargetDiagnostic> *diagnostics)
{
    if (!value.isUndefined() && !value.isObject()) {
        return appendDiagnostic(diagnostics,
                                jsonPath,
                                QStringLiteral("%1.type").arg(path),
                                QStringLiteral("Target '%1' %2 must be an object").arg(targetId, path));
    }

    bool ok = true;
    const QJsonObject map = value.toObject();
    for (auto it = map.begin(); it != map.end(); ++it) {
        if (it.key().isEmpty()) {
            ok = appendDiagnostic(diagnostics,
                                  jsonPath,
                                  QStringLiteral("%1.key.empty").arg(path),
                                  QStringLiteral("Target '%1' %2 keys must be non-empty strings").arg(targetId, path));
        }
        if (!it.value().isString()) {
            ok = appendDiagnostic(diagnostics,
                                  QStringLiteral("%1/%2").arg(jsonPath, it.key()),
                                  QStringLiteral("%1.value.type").arg(path),
                                  QStringLiteral("Target '%1' %2 values must be strings").arg(targetId, path));
        }
    }
    return ok;
}

bool validateJsonValuePlaceholders(const QString &targetId,
                                   const QJsonValue &value,
                                   const QString &path,
                                   const QString &jsonPath,
                                   QList<TargetDiagnostic> *diagnostics)
{
    if (value.isString()) {
        bool ok = true;
        const auto matches = kPlaceholderPattern.globalMatch(value.toString());
        auto it = matches;
        while (it.hasNext()) {
            const QString placeholder = it.next().captured(1);
            if (placeholder != QLatin1StringView("FILENAME")) {
                ok = appendDiagnostic(diagnostics,
                                      jsonPath,
                                      QStringLiteral("%1.placeholder.unsupported").arg(path),
                                      QStringLiteral("Target '%1' %2 contains unsupported placeholder ${%3}")
                                          .arg(targetId, path, placeholder));
            }
        }
        return ok;
    }

    if (value.isArray()) {
        bool ok = true;
        const QJsonArray array = value.toArray();
        for (int i = 0; i < array.size(); ++i) {
            ok = validateJsonValuePlaceholders(targetId,
                                               array.at(i),
                                               QStringLiteral("%1[%2]").arg(path).arg(i),
                                               QStringLiteral("%1/%2").arg(jsonPath).arg(i),
                                               diagnostics)
                && ok;
        }
        return ok;
    }

    if (value.isObject()) {
        bool ok = true;
        const QJsonObject object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it) {
            ok = validateJsonValuePlaceholders(targetId,
                                               it.value(),
                                               QStringLiteral("%1.%2").arg(path, it.key()),
                                               QStringLiteral("%1/%2").arg(jsonPath, it.key()),
                                               diagnostics)
                && ok;
        }
        return ok;
    }

    return true;
}

bool validateResponseExtractor(const QString &targetId,
                               const QJsonObject &response,
                               const QString &path,
                               const QString &jsonPath,
                               QList<TargetDiagnostic> *diagnostics)
{
    if (response.isEmpty()) {
        return appendDiagnostic(diagnostics,
                                jsonPath,
                                QStringLiteral("%1.type").arg(path),
                                QStringLiteral("Target '%1' %2 must be an object").arg(targetId, path));
    }

    const QString responseType = stringValue(response, "type");
    bool ok = true;
    if (responseType != QLatin1StringView("text_url")
        && responseType != QLatin1StringView("regex")
        && responseType != QLatin1StringView("json_pointer")
        && responseType != QLatin1StringView("header")
        && responseType != QLatin1StringView("redirect_url")
        && responseType != QLatin1StringView("xml_xpath")) {
        return appendDiagnostic(diagnostics,
                                QStringLiteral("%1/type").arg(jsonPath),
                                QStringLiteral("%1.type.invalid").arg(path),
                                QStringLiteral("Target '%1' %2.type must be text_url, regex, json_pointer, header, redirect_url, or xml_xpath")
                                    .arg(targetId, path));
    }

    if (responseType == QLatin1StringView("regex")) {
        if (stringValue(response, "pattern").isEmpty()) {
            ok = appendDiagnostic(diagnostics,
                                  QStringLiteral("%1/pattern").arg(jsonPath),
                                  QStringLiteral("%1.pattern.empty").arg(path),
                                  QStringLiteral("Target '%1' %2.pattern must be a non-empty string").arg(targetId, path));
        }
        if (!response.value(QStringLiteral("group")).isUndefined() && response.value(QStringLiteral("group")).toInt(-1) < 0) {
            ok = appendDiagnostic(diagnostics,
                                  QStringLiteral("%1/group").arg(jsonPath),
                                  QStringLiteral("%1.group.invalid").arg(path),
                                  QStringLiteral("Target '%1' %2.group must be a non-negative integer").arg(targetId, path));
        }
    }

    if (responseType == QLatin1StringView("json_pointer")) {
        const QString pointer = stringValue(response, "pointer");
        if (pointer.isEmpty()) {
            ok = appendDiagnostic(diagnostics,
                                  QStringLiteral("%1/pointer").arg(jsonPath),
                                  QStringLiteral("%1.pointer.empty").arg(path),
                                  QStringLiteral("Target '%1' %2.pointer must be a non-empty string").arg(targetId, path));
        }
        if (!pointer.isEmpty() && !pointer.startsWith(QLatin1Char('/'))) {
            ok = appendDiagnostic(diagnostics,
                                  QStringLiteral("%1/pointer").arg(jsonPath),
                                  QStringLiteral("%1.pointer.invalid").arg(path),
                                  QStringLiteral("Target '%1' %2.pointer must start with '/'").arg(targetId, path));
        }
    }

    if (responseType == QLatin1StringView("header") && stringValue(response, "name").isEmpty()) {
        ok = appendDiagnostic(diagnostics,
                              QStringLiteral("%1/name").arg(jsonPath),
                              QStringLiteral("%1.name.empty").arg(path),
                              QStringLiteral("Target '%1' %2.name must be a non-empty string").arg(targetId, path));
    }

    if (responseType == QLatin1StringView("xml_xpath")) {
        const QString xpath = stringValue(response, "xpath");
        if (xpath.isEmpty()) {
            ok = appendDiagnostic(diagnostics,
                                  QStringLiteral("%1/xpath").arg(jsonPath),
                                  QStringLiteral("%1.xpath.empty").arg(path),
                                  QStringLiteral("Target '%1' %2.xpath must be a non-empty string").arg(targetId, path));
        }
        if (!xpath.isEmpty() && !xpath.startsWith(QLatin1Char('/'))) {
            ok = appendDiagnostic(diagnostics,
                                  QStringLiteral("%1/xpath").arg(jsonPath),
                                  QStringLiteral("%1.xpath.invalid").arg(path),
                                  QStringLiteral("Target '%1' %2.xpath must start with '/'").arg(targetId, path));
        }
    }

    return ok;
}

bool validateRequest(const QString &targetId, const QJsonObject &request, QList<TargetDiagnostic> *diagnostics)
{
    if (request.isEmpty()) {
        return appendDiagnostic(diagnostics,
                                QStringLiteral("/request"),
                                QStringLiteral("request.missing"),
                                QStringLiteral("Target '%1' missing request object").arg(targetId));
    }

    const QString url = stringValue(request, "url");
    const QString method = stringValue(request, "method");
    const QString requestType = stringValue(request, "type").isEmpty()
        ? QStringLiteral("multipart")
        : stringValue(request, "type");

    bool ok = true;
    if (url.isEmpty()) {
        ok = appendDiagnostic(diagnostics,
                              QStringLiteral("/request/url"),
                              QStringLiteral("request.url.empty"),
                              QStringLiteral("Target '%1' request.url must be a non-empty string").arg(targetId));
    }
    if (method.isEmpty()) {
        ok = appendDiagnostic(diagnostics,
                              QStringLiteral("/request/method"),
                              QStringLiteral("request.method.empty"),
                              QStringLiteral("Target '%1' request.method must be a non-empty string").arg(targetId));
    }

    const bool requestTypeValid = requestType == QLatin1StringView("multipart")
        || requestType == QLatin1StringView("raw")
        || requestType == QLatin1StringView("form_urlencoded")
        || requestType == QLatin1StringView("json");
    if (!requestTypeValid) {
        ok = appendDiagnostic(diagnostics,
                              QStringLiteral("/request/type"),
                              QStringLiteral("request.type.invalid"),
                              QStringLiteral("Target '%1' request.type must be multipart, raw, form_urlencoded, or json").arg(targetId));
    }

    ok = validateStringMap(targetId,
                           request.value(QStringLiteral("headers")),
                           QStringLiteral("request.headers"),
                           QStringLiteral("/request/headers"),
                           diagnostics)
        && ok;
    ok = validateStringMap(targetId,
                           request.value(QStringLiteral("query")),
                           QStringLiteral("request.query"),
                           QStringLiteral("/request/query"),
                           diagnostics)
        && ok;

    if (!requestTypeValid) {
        return ok;
    }

    if (requestType == QLatin1StringView("multipart")) {
        if (method.toUpper() != QLatin1StringView("POST")) {
            ok = appendDiagnostic(diagnostics,
                                  QStringLiteral("/request/method"),
                                  QStringLiteral("request.method.multipart"),
                                  QStringLiteral("Target '%1' request.method must be POST for multipart").arg(targetId));
        }

        const QJsonObject multipart = objectValue(request, "multipart");
        if (multipart.isEmpty()) {
            ok = appendDiagnostic(diagnostics,
                                  QStringLiteral("/request/multipart"),
                                  QStringLiteral("request.multipart.missing"),
                                  QStringLiteral("Target '%1' request.multipart must be an object").arg(targetId));
            return ok;
        }
        if (stringValue(multipart, "fileField").isEmpty()) {
            ok = appendDiagnostic(diagnostics,
                                  QStringLiteral("/request/multipart/fileField"),
                                  QStringLiteral("request.multipart.fileField.empty"),
                                  QStringLiteral("Target '%1' request.multipart.fileField must be a non-empty string").arg(targetId));
        }
        ok = validateStringMap(targetId,
                               multipart.value(QStringLiteral("fields")),
                               QStringLiteral("request.multipart.fields"),
                               QStringLiteral("/request/multipart/fields"),
                               diagnostics)
            && ok;
    } else if (requestType == QLatin1StringView("raw")) {
        if (method.toUpper() != QLatin1StringView("POST") && method.toUpper() != QLatin1StringView("PUT")) {
            ok = appendDiagnostic(diagnostics,
                                  QStringLiteral("/request/method"),
                                  QStringLiteral("request.method.raw"),
                                  QStringLiteral("Target '%1' request.method must be POST or PUT for raw").arg(targetId));
        }
    } else if (requestType == QLatin1StringView("form_urlencoded")) {
        if (method.toUpper() != QLatin1StringView("POST") && method.toUpper() != QLatin1StringView("PUT")) {
            ok = appendDiagnostic(diagnostics,
                                  QStringLiteral("/request/method"),
                                  QStringLiteral("request.method.form_urlencoded"),
                                  QStringLiteral("Target '%1' request.method must be POST or PUT for form_urlencoded").arg(targetId));
        }

        const QJsonObject form = objectValue(request, "formUrlencoded");
        if (form.isEmpty()) {
            ok = appendDiagnostic(diagnostics,
                                  QStringLiteral("/request/formUrlencoded"),
                                  QStringLiteral("request.formUrlencoded.missing"),
                                  QStringLiteral("Target '%1' request.formUrlencoded must be an object").arg(targetId));
            return ok;
        }
        ok = validateStringMap(targetId,
                               form.value(QStringLiteral("fields")),
                               QStringLiteral("request.formUrlencoded.fields"),
                               QStringLiteral("/request/formUrlencoded/fields"),
                               diagnostics)
            && ok;
    } else if (requestType == QLatin1StringView("json")) {
        if (method.toUpper() != QLatin1StringView("POST") && method.toUpper() != QLatin1StringView("PUT")) {
            ok = appendDiagnostic(diagnostics,
                                  QStringLiteral("/request/method"),
                                  QStringLiteral("request.method.json"),
                                  QStringLiteral("Target '%1' request.method must be POST or PUT for json").arg(targetId));
        }

        const QJsonObject json = objectValue(request, "json");
        if (json.isEmpty()) {
            ok = appendDiagnostic(diagnostics,
                                  QStringLiteral("/request/json"),
                                  QStringLiteral("request.json.missing"),
                                  QStringLiteral("Target '%1' request.json must be an object").arg(targetId));
            return ok;
        }
        if (!json.contains(QStringLiteral("fields"))) {
            ok = appendDiagnostic(diagnostics,
                                  QStringLiteral("/request/json/fields"),
                                  QStringLiteral("request.json.fields.missing"),
                                  QStringLiteral("Target '%1' request.json.fields must be present").arg(targetId));
        } else {
            ok = validateJsonValuePlaceholders(targetId,
                                               json.value(QStringLiteral("fields")),
                                               QStringLiteral("request.json.fields"),
                                               QStringLiteral("/request/json/fields"),
                                               diagnostics)
                && ok;
        }
    }

    return ok;
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

bool validateExtensions(const QString &targetId, const QJsonValue &value, QList<TargetDiagnostic> *diagnostics)
{
    if (!value.isUndefined() && !value.isArray()) {
        return appendDiagnostic(diagnostics,
                                QStringLiteral("/extensions"),
                                QStringLiteral("extensions.type"),
                                QStringLiteral("Target '%1' extensions must be a list").arg(targetId));
    }

    bool ok = true;
    const QJsonArray entries = value.toArray();
    for (int i = 0; i < entries.size(); ++i) {
        const QString text = entries.at(i).toString().trimmed();
        if (text.isEmpty()) {
            ok = appendDiagnostic(diagnostics,
                                  QStringLiteral("/extensions/%1").arg(i),
                                  QStringLiteral("extensions.empty"),
                                  QStringLiteral("Target '%1' extensions must contain non-empty strings").arg(targetId));
            continue;
        }

        const QString normalized = text.startsWith(QLatin1Char('.')) ? text.mid(1) : text;
        if (normalized.isEmpty() || normalized.contains(QLatin1Char('/')) || normalized.contains(QLatin1Char('*'))
            || normalized.contains(QLatin1Char(' '))) {
            ok = appendDiagnostic(diagnostics,
                                  QStringLiteral("/extensions/%1").arg(i),
                                  QStringLiteral("extensions.invalid"),
                                  QStringLiteral("Target '%1' extensions must contain plain file suffixes such as png or .png").arg(targetId));
        }
    }

    return ok;
}

bool validateConstraints(const QString &targetId, const QJsonValue &value, QList<TargetDiagnostic> *diagnostics)
{
    if (!value.isUndefined() && !value.isArray()) {
        return appendDiagnostic(diagnostics,
                                QStringLiteral("/constraints"),
                                QStringLiteral("constraints.type"),
                                QStringLiteral("Target '%1' constraints must be a list").arg(targetId));
    }

    bool ok = true;
    const QJsonArray entries = value.toArray();
    for (int i = 0; i < entries.size(); ++i) {
        const QString text = entries.at(i).toString();
        if (text.isEmpty()) {
            ok = appendDiagnostic(diagnostics,
                                  QStringLiteral("/constraints/%1").arg(i),
                                  QStringLiteral("constraints.empty"),
                                  QStringLiteral("Target '%1' constraints must contain non-empty strings").arg(targetId));
            continue;
        }
        if (!text.startsWith(QLatin1StringView("mimeType:"))) {
            ok = appendDiagnostic(diagnostics,
                                  QStringLiteral("/constraints/%1").arg(i),
                                  QStringLiteral("constraints.unsupported"),
                                  QStringLiteral("Target '%1' constraints currently only support mimeType: patterns").arg(targetId));
            continue;
        }
        ok = validateMimePattern(QStringLiteral("Target '%1' constraint").arg(targetId),
                                 QStringLiteral("/constraints/%1").arg(i),
                                 text.mid(9),
                                 QStringLiteral("constraints.mime"),
                                 diagnostics)
            && ok;
    }

    return ok;
}

bool validatePreUpload(const QString &targetId, const QJsonValue &value, QList<TargetDiagnostic> *diagnostics)
{
    if (!value.isUndefined() && !value.isArray()) {
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
                ok = validateMimePattern(QStringLiteral("%1.mime").arg(rulePath),
                                         QStringLiteral("%1/mime/%2").arg(ruleJsonPath).arg(mimeIndex),
                                         patternValue.toString(),
                                         QStringLiteral("preUpload.mime"),
                                         diagnostics)
                    && ok;
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
        }

        const QJsonArray commands = arrayValue(rule, "commands");
        if (commands.isEmpty()) {
            ok = appendDiagnostic(diagnostics,
                                  QStringLiteral("%1/commands").arg(ruleJsonPath),
                                  QStringLiteral("preUpload.commands.empty"),
                                  QStringLiteral("%1.commands must be a non-empty list").arg(rulePath));
            continue;
        }
        if (validFileHandling && fileHandling == QLatin1StringView("output_file") && commands.size() != 1) {
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
                const QString arg = argv.at(argIndex).toString();
                if (arg.isEmpty()) {
                    ok = appendDiagnostic(diagnostics,
                                          QStringLiteral("%1/argv/%2").arg(commandJsonPath).arg(argIndex),
                                          QStringLiteral("preUpload.argv.entry.empty"),
                                          QStringLiteral("%1.argv must contain non-empty strings").arg(commandPath));
                    continue;
                }

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
        }

        if (validFileHandling && fileHandling == QLatin1StringView("output_file") && !sawOutFile) {
            ok = appendDiagnostic(diagnostics,
                                  QStringLiteral("%1/commands/0/argv").arg(ruleJsonPath),
                                  QStringLiteral("preUpload.argv.outfile.missing"),
                                  QStringLiteral("%1.commands[0].argv must include ${OUT_FILE}").arg(rulePath));
        }

        const QJsonValue timeout = rule.value(QStringLiteral("timeoutMs"));
        if (!timeout.isUndefined() && (!timeout.isDouble() || timeout.toInt() <= 0)) {
            ok = appendDiagnostic(diagnostics,
                                  QStringLiteral("%1/timeoutMs").arg(ruleJsonPath),
                                  QStringLiteral("preUpload.timeout.invalid"),
                                  QStringLiteral("%1.timeoutMs must be a positive integer").arg(rulePath));
        }
    }

    return ok;
}

bool validateResponse(const QString &targetId, const QJsonObject &response, QList<TargetDiagnostic> *diagnostics)
{
    bool ok = validateResponseExtractor(targetId, response, QStringLiteral("response"), QStringLiteral("/response"), diagnostics);

    const QJsonValue errorValue = response.value(QStringLiteral("error"));
    if (!errorValue.isUndefined() && !errorValue.isObject()) {
        ok = appendDiagnostic(diagnostics,
                              QStringLiteral("/response/error"),
                              QStringLiteral("response.error.type"),
                              QStringLiteral("Target '%1' response.error must be an object").arg(targetId));
    }
    if (errorValue.isObject()) {
        ok = validateResponseExtractor(targetId,
                                       errorValue.toObject(),
                                       QStringLiteral("response.error"),
                                       QStringLiteral("/response/error"),
                                       diagnostics)
            && ok;
    }

    const QJsonValue thumbnailValue = response.value(QStringLiteral("thumbnail"));
    if (!thumbnailValue.isUndefined() && !thumbnailValue.isObject()) {
        ok = appendDiagnostic(diagnostics,
                              QStringLiteral("/response/thumbnail"),
                              QStringLiteral("response.thumbnail.type"),
                              QStringLiteral("Target '%1' response.thumbnail must be an object").arg(targetId));
    }
    if (thumbnailValue.isObject()) {
        ok = validateResponseExtractor(targetId,
                                       thumbnailValue.toObject(),
                                       QStringLiteral("response.thumbnail"),
                                       QStringLiteral("/response/thumbnail"),
                                       diagnostics)
            && ok;
    }

    const QJsonValue deletionValue = response.value(QStringLiteral("deletion"));
    if (!deletionValue.isUndefined() && !deletionValue.isObject()) {
        ok = appendDiagnostic(diagnostics,
                              QStringLiteral("/response/deletion"),
                              QStringLiteral("response.deletion.type"),
                              QStringLiteral("Target '%1' response.deletion must be an object").arg(targetId));
    }
    if (deletionValue.isObject()) {
        ok = validateResponseExtractor(targetId,
                                       deletionValue.toObject(),
                                       QStringLiteral("response.deletion"),
                                       QStringLiteral("/response/deletion"),
                                       diagnostics)
            && ok;
    }

    return ok;
}
}

bool TargetConfigValidator::validateTarget(const QJsonObject &target, QList<TargetDiagnostic> *diagnostics)
{
    const QString targetId = target.value(QStringLiteral("id")).toString();
    if (targetId.isEmpty() || !kIdPattern.match(targetId).hasMatch()) {
        return appendDiagnostic(diagnostics,
                                QStringLiteral("/id"),
                                QStringLiteral("target.id.invalid"),
                                QStringLiteral("Invalid target id: '%1'").arg(targetId));
    }

    bool ok = true;
    ok = validateRequest(targetId, objectValue(target, "request"), diagnostics) && ok;
    ok = validatePreUpload(targetId, target.value(QStringLiteral("preUpload")), diagnostics) && ok;
    ok = validateResponse(targetId, objectValue(target, "response"), diagnostics) && ok;

    if (!target.value(QStringLiteral("pluginTypes")).isUndefined() && !target.value(QStringLiteral("pluginTypes")).isArray()) {
        ok = appendDiagnostic(diagnostics,
                              QStringLiteral("/pluginTypes"),
                              QStringLiteral("pluginTypes.type"),
                              QStringLiteral("Target '%1' pluginTypes must be a list").arg(targetId));
    }

    ok = validateConstraints(targetId, target.value(QStringLiteral("constraints")), diagnostics) && ok;
    ok = validateExtensions(targetId, target.value(QStringLiteral("extensions")), diagnostics) && ok;
    return ok;
}
