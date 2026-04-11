#include "targetconfigvalidator.h"

#include "targetcoreconfigparser.h"
#include "targetpreuploadconfigparser.h"
#include "targetrequestconfigparser.h"
#include "targetresponseconfigparser.h"

#include <QJsonArray>
#include <QRegularExpression>

namespace {
const QRegularExpression kPlaceholderPattern(QStringLiteral(R"(\$\{([A-Z_]+)\})"));
const QRegularExpression kMimePattern(QStringLiteral("^[^/\\s]+/[^/\\s]+$"));
const QRegularExpression kMimeWildcardPattern(QStringLiteral("^[^/\\s]+/\\*$"));

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

}

bool TargetConfigValidator::validateTarget(const QJsonObject &target, QList<TargetDiagnostic> *diagnostics)
{
    ParsedTargetCoreConfig core;
    const bool coreOk = TargetCoreConfigParser::parse(target, &core, diagnostics);
    if (core.id.isEmpty()) {
        return false;
    }

    bool ok = coreOk;
    ParsedRequestConfig request;
    ParsedResponseConfig response;
    ParsedPreUploadConfig preUpload;
    ok = TargetRequestConfigParser::parse(target, &request, diagnostics) && ok;
    ok = TargetResponseConfigParser::parse(target, &response, diagnostics) && ok;
    ok = TargetPreUploadConfigParser::parse(target, &preUpload, diagnostics) && ok;
    return ok;
}
