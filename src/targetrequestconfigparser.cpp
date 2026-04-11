#include "targetrequestconfigparser.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>

namespace {
const QRegularExpression kPlaceholderPattern(QStringLiteral(R"(\$\{([A-Z_]+)\})"));

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

QJsonObject objectValue(const QJsonObject &parent, const char *key)
{
    const QJsonValue value = parent.value(QLatin1StringView(key));
    return value.isObject() ? value.toObject() : QJsonObject();
}

QString stringValue(const QJsonObject &parent, const char *key)
{
    return parent.value(QLatin1StringView(key)).toString();
}

bool parseStringMap(const QString &targetId,
                    const QJsonValue &value,
                    const QString &path,
                    const QString &jsonPath,
                    QMap<QString, QString> *out,
                    QList<TargetDiagnostic> *diagnostics)
{
    if (!value.isUndefined() && !value.isObject()) {
        return appendDiagnostic(diagnostics,
                                jsonPath,
                                QStringLiteral("%1.type").arg(path),
                                QStringLiteral("Target '%1' %2 must be an object").arg(targetId, path));
    }

    bool ok = true;
    QMap<QString, QString> parsed;
    const QJsonObject object = value.toObject();
    for (auto it = object.begin(); it != object.end(); ++it) {
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
            continue;
        }
        parsed.insert(it.key(), it.value().toString());
    }

    if (out) {
        *out = parsed;
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
}

bool TargetRequestConfigParser::parse(const QJsonObject &target, ParsedRequestConfig *parsed, QList<TargetDiagnostic> *diagnostics)
{
    const QString targetId = target.value(QStringLiteral("id")).toString();
    const QJsonObject request = objectValue(target, "request");
    if (request.isEmpty()) {
        return appendDiagnostic(diagnostics,
                                QStringLiteral("/request"),
                                QStringLiteral("request.missing"),
                                QStringLiteral("Target '%1' missing request object").arg(targetId));
    }

    ParsedRequestConfig local;
    local.url = stringValue(request, "url");
    local.method = stringValue(request, "method").toUpper();
    const QString requestType = stringValue(request, "type");
    if (requestType.isEmpty() || requestType == QLatin1StringView("multipart")) {
        local.type = RequestBodyType::Multipart;
    } else if (requestType == QLatin1StringView("raw")) {
        local.type = RequestBodyType::Raw;
    } else if (requestType == QLatin1StringView("form_urlencoded")) {
        local.type = RequestBodyType::FormUrlencoded;
    } else if (requestType == QLatin1StringView("json")) {
        local.type = RequestBodyType::Json;
    }

    bool ok = true;
    if (local.url.isEmpty()) {
        ok = appendDiagnostic(diagnostics,
                              QStringLiteral("/request/url"),
                              QStringLiteral("request.url.empty"),
                              QStringLiteral("Target '%1' request.url must be a non-empty string").arg(targetId));
    }
    if (local.method.isEmpty()) {
        ok = appendDiagnostic(diagnostics,
                              QStringLiteral("/request/method"),
                              QStringLiteral("request.method.empty"),
                              QStringLiteral("Target '%1' request.method must be a non-empty string").arg(targetId));
    }

    const bool requestTypeValid = requestType.isEmpty()
        || requestType == QLatin1StringView("multipart")
        || requestType == QLatin1StringView("raw")
        || requestType == QLatin1StringView("form_urlencoded")
        || requestType == QLatin1StringView("json");
    if (!requestTypeValid) {
        ok = appendDiagnostic(diagnostics,
                              QStringLiteral("/request/type"),
                              QStringLiteral("request.type.invalid"),
                              QStringLiteral("Target '%1' request.type must be multipart, raw, form_urlencoded, or json").arg(targetId));
    }

    ok = parseStringMap(targetId,
                        request.value(QStringLiteral("headers")),
                        QStringLiteral("request.headers"),
                        QStringLiteral("/request/headers"),
                        &local.headers,
                        diagnostics)
        && ok;
    ok = parseStringMap(targetId,
                        request.value(QStringLiteral("query")),
                        QStringLiteral("request.query"),
                        QStringLiteral("/request/query"),
                        &local.query,
                        diagnostics)
        && ok;

    if (!requestTypeValid) {
        if (parsed) {
            *parsed = local;
        }
        return ok;
    }

    switch (local.type) {
    case RequestBodyType::Multipart: {
        if (local.method != QLatin1StringView("POST")) {
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
            break;
        }

        local.fileField = stringValue(multipart, "fileField");
        if (local.fileField.isEmpty()) {
            ok = appendDiagnostic(diagnostics,
                                  QStringLiteral("/request/multipart/fileField"),
                                  QStringLiteral("request.multipart.fileField.empty"),
                                  QStringLiteral("Target '%1' request.multipart.fileField must be a non-empty string").arg(targetId));
        }
        ok = parseStringMap(targetId,
                            multipart.value(QStringLiteral("fields")),
                            QStringLiteral("request.multipart.fields"),
                            QStringLiteral("/request/multipart/fields"),
                            &local.multipartFields,
                            diagnostics)
            && ok;
        break;
    }
    case RequestBodyType::Raw:
        local.contentType = stringValue(request, "contentType");
        if (local.method != QLatin1StringView("POST") && local.method != QLatin1StringView("PUT")) {
            ok = appendDiagnostic(diagnostics,
                                  QStringLiteral("/request/method"),
                                  QStringLiteral("request.method.raw"),
                                  QStringLiteral("Target '%1' request.method must be POST or PUT for raw").arg(targetId));
        }
        break;
    case RequestBodyType::FormUrlencoded: {
        if (local.method != QLatin1StringView("POST") && local.method != QLatin1StringView("PUT")) {
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
            break;
        }
        ok = parseStringMap(targetId,
                            form.value(QStringLiteral("fields")),
                            QStringLiteral("request.formUrlencoded.fields"),
                            QStringLiteral("/request/formUrlencoded/fields"),
                            &local.formFields,
                            diagnostics)
            && ok;
        break;
    }
    case RequestBodyType::Json: {
        if (local.method != QLatin1StringView("POST") && local.method != QLatin1StringView("PUT")) {
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
            break;
        }
        if (!json.contains(QStringLiteral("fields"))) {
            ok = appendDiagnostic(diagnostics,
                                  QStringLiteral("/request/json/fields"),
                                  QStringLiteral("request.json.fields.missing"),
                                  QStringLiteral("Target '%1' request.json.fields must be present").arg(targetId));
        } else {
            local.jsonFields = json.value(QStringLiteral("fields"));
            ok = validateJsonValuePlaceholders(targetId,
                                               local.jsonFields,
                                               QStringLiteral("request.json.fields"),
                                               QStringLiteral("/request/json/fields"),
                                               diagnostics)
                && ok;
        }
        break;
    }
    }

    if (parsed) {
        *parsed = local;
    }
    return ok;
}
