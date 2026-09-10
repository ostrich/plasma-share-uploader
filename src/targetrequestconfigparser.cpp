#include "targetrequestconfigparser.h"
#include "targetformat.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>

namespace {
const QRegularExpression kPlaceholderPattern(QStringLiteral(R"(\$\{([A-Z_]+)\})"));

bool appendDiagnostic(
    QList<TargetDiagnostic>* diagnostics, const QString& jsonPath, const QString& code, const QString& message)
{
    if (diagnostics) {
        diagnostics->append(TargetDiagnostic { TargetDiagnosticSeverity::Error, { }, jsonPath, code, message });
    }
    return false;
}

bool validateWalletReferences(const QJsonValue& value, const QString& path, QList<TargetDiagnostic>* diagnostics)
{
    bool ok = true;
    if (value.isString()) {
        const auto text = value.toString();
        const QRegularExpression keyPattern(QStringLiteral("\\A[A-Za-z0-9_.-]+\\z"));
        qsizetype offset = 0;
        while ((offset = text.indexOf(QStringLiteral("${WALLET:"), offset)) >= 0) {
            const auto end = text.indexOf(QLatin1Char('}'), offset);
            if (end < 0 || !keyPattern.match(text.mid(offset + 9, end - offset - 9)).hasMatch()) {
                return appendDiagnostic(diagnostics, path, QStringLiteral("request.wallet.invalid"),
                    QStringLiteral(
                        "Wallet references must use ${WALLET:name} with letters, digits, dot, dash, or underscore."));
            }
            offset = end + 1;
        }
    } else if (value.isObject()) {
        const auto object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it)
            ok = validateWalletReferences(it.value(), path + QLatin1Char('/') + it.key(), diagnostics) && ok;
    } else if (value.isArray()) {
        const auto array = value.toArray();
        for (int i = 0; i < array.size(); ++i)
            ok = validateWalletReferences(array.at(i), path + QLatin1Char('/') + QString::number(i), diagnostics) && ok;
    }
    return ok;
}

bool parseStringMap(const QString& targetId, const QJsonValue& value, const QString& path, const QString& jsonPath,
    QMap<QString, QString>* out, QList<TargetDiagnostic>* diagnostics)
{
    if (!value.isUndefined() && !value.isObject()) {
        return appendDiagnostic(diagnostics, jsonPath, QStringLiteral("%1.type").arg(path),
            QStringLiteral("Target '%1' %2 must be an object").arg(targetId, path));
    }

    bool ok = true;
    QMap<QString, QString> parsed;
    const QJsonObject object = value.toObject();
    for (auto it = object.begin(); it != object.end(); ++it) {
        if (it.key().isEmpty()) {
            ok = appendDiagnostic(diagnostics, jsonPath, QStringLiteral("%1.key.empty").arg(path),
                QStringLiteral("Target '%1' %2 keys must be non-empty strings").arg(targetId, path));
        }
        if (!it.value().isString()) {
            ok = appendDiagnostic(diagnostics, QStringLiteral("%1/%2").arg(jsonPath, it.key()),
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

bool validateJsonValuePlaceholders(const QString& targetId, const QJsonValue& value, const QString& path,
    const QString& jsonPath, QList<TargetDiagnostic>* diagnostics)
{
    if (value.isString()) {
        bool ok = true;
        const auto matches = kPlaceholderPattern.globalMatch(value.toString());
        auto it = matches;
        while (it.hasNext()) {
            const QString placeholder = it.next().captured(1);
            if (placeholder != QLatin1StringView("FILENAME")) {
                ok = appendDiagnostic(diagnostics, jsonPath, QStringLiteral("%1.placeholder.unsupported").arg(path),
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
            ok = validateJsonValuePlaceholders(targetId, array.at(i), QStringLiteral("%1[%2]").arg(path).arg(i),
                     QStringLiteral("%1/%2").arg(jsonPath).arg(i), diagnostics)
                && ok;
        }
        return ok;
    }

    if (value.isObject()) {
        bool ok = true;
        const QJsonObject object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it) {
            ok = validateJsonValuePlaceholders(targetId, it.value(), QStringLiteral("%1.%2").arg(path, it.key()),
                     QStringLiteral("%1/%2").arg(jsonPath, it.key()), diagnostics)
                && ok;
        }
        return ok;
    }

    return true;
}
}

bool TargetRequestConfigParser::parse(
    const QJsonObject& target, ParsedRequestConfig* parsed, QList<TargetDiagnostic>* diagnostics)
{
    const auto requestValue = target.value(QStringLiteral("request"));
    if (!requestValue.isObject())
        return TargetFormat::problem(diagnostics, QStringLiteral("/request"), QStringLiteral("Must be an object"));
    const auto request = requestValue.toObject();
    const auto targetId = target.value(QStringLiteral("id")).toString();
    const auto requestPath = QStringLiteral("/request");
    const auto bodyPath = QStringLiteral("/request/body");
    ParsedRequestConfig local;
    local.url = request.value(QStringLiteral("url")).toString();
    local.method = request.value(QStringLiteral("method")).toString();
    bool ok = TargetFormat::string(request, QStringLiteral("url"), requestPath, diagnostics, true, true);
    if (local.method != QLatin1StringView("POST") && local.method != QLatin1StringView("PUT"))
        ok = TargetFormat::problem(
            diagnostics, requestPath + QStringLiteral("/method"), QStringLiteral("Must be POST or PUT"));
    ok = TargetFormat::removed(
             request, { "type", "multipart", "formUrlencoded", "json", "contentType" }, requestPath, diagnostics)
        && ok;
    for (const auto key : { "url", "headers", "query" })
        ok = validateWalletReferences(request.value(QLatin1StringView(key)),
                 requestPath + QLatin1Char('/') + QString::fromLatin1(key), diagnostics)
            && ok;
    ok = parseStringMap(targetId, request.value(QStringLiteral("headers")), QStringLiteral("request.headers"),
             requestPath + QStringLiteral("/headers"), &local.headers, diagnostics)
        && ok;
    ok = parseStringMap(targetId, request.value(QStringLiteral("query")), QStringLiteral("request.query"),
             requestPath + QStringLiteral("/query"), &local.query, diagnostics)
        && ok;
    const auto bodyValue = request.value(QStringLiteral("body"));
    if (!bodyValue.isObject()) {
        ok = TargetFormat::problem(diagnostics, bodyPath, QStringLiteral("Must be an object"));
    } else {
        const auto body = bodyValue.toObject();
        const auto type = body.value(QStringLiteral("type")).toString();
        // Retain inactive body settings when the editor switches formats, but
        // validate their types too so mistyped known fields cannot be ignored.
        ok = TargetFormat::string(body, QStringLiteral("fileField"), bodyPath, diagnostics,
                 type == QLatin1StringView("multipart"), type == QLatin1StringView("multipart"))
            && ok;
        ok = TargetFormat::string(body, QStringLiteral("contentType"), bodyPath, diagnostics) && ok;
        QMap<QString, QString> fields;
        ok = parseStringMap(targetId, body.value(QStringLiteral("fields")), QStringLiteral("request.body.fields"),
                 bodyPath + QStringLiteral("/fields"), &fields, diagnostics)
            && ok;
        if (type == QLatin1StringView("multipart")) {
            local.type = RequestBodyType::Multipart;
            local.fileField = body.value(QStringLiteral("fileField")).toString();
            local.multipartFields = fields;

            if (local.method != QLatin1StringView("POST"))
                ok = TargetFormat::problem(diagnostics, requestPath + QStringLiteral("/method"),
                    QStringLiteral("Multipart uploads require POST"));
        } else if (type == QLatin1StringView("raw")) {
            local.type = RequestBodyType::Raw;
            local.contentType = body.value(QStringLiteral("contentType")).toString();
        } else if (type == QLatin1StringView("form_urlencoded")) {
            local.type = RequestBodyType::FormUrlencoded;
            local.formFields = fields;
            if (!body.contains(QStringLiteral("fields")))
                ok = TargetFormat::problem(diagnostics, bodyPath + QStringLiteral("/fields"),
                    QStringLiteral("Must be a string map (an empty object is allowed)"));
        } else if (type == QLatin1StringView("json")) {
            local.type = RequestBodyType::Json;
            if (!body.contains(QStringLiteral("value")))
                ok = TargetFormat::problem(diagnostics, bodyPath + QStringLiteral("/value"),
                    QStringLiteral("Must be present; any JSON value, including null, is allowed"));
            else {
                local.jsonValue = body.value(QStringLiteral("value"));
                ok = validateJsonValuePlaceholders(targetId, local.jsonValue, QStringLiteral("request.body.value"),
                         bodyPath + QStringLiteral("/value"), diagnostics)
                    && ok;
            }
        } else {
            ok = TargetFormat::problem(diagnostics, bodyPath + QStringLiteral("/type"),
                QStringLiteral("Must be multipart, raw, form_urlencoded, or json"));
        }
    }
    if (local.type == RequestBodyType::Json)
        ok = validateWalletReferences(local.jsonValue, bodyPath + QStringLiteral("/value"), diagnostics) && ok;
    else if (local.type == RequestBodyType::Multipart || local.type == RequestBodyType::FormUrlencoded)
        ok = validateWalletReferences(bodyValue.toObject().value(QStringLiteral("fields")),
                 bodyPath + QStringLiteral("/fields"), diagnostics)
            && ok;
    if (parsed)
        *parsed = local;
    return ok;
}
