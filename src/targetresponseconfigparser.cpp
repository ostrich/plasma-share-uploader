#include "targetresponseconfigparser.h"
#include "targetformat.h"
#include "targetuploader_utils.h"

#include <QJsonObject>
#include <QRegularExpression>

namespace {
bool appendDiagnostic(
    QList<TargetDiagnostic>* diagnostics, const QString& jsonPath, const QString& code, const QString& message)
{
    if (diagnostics) {
        diagnostics->append(TargetDiagnostic { TargetDiagnosticSeverity::Error, { }, jsonPath, code, message });
    }
    return false;
}

QJsonObject objectValue(const QJsonObject& parent, const char* key)
{
    const QJsonValue value = parent.value(QLatin1StringView(key));
    return value.isObject() ? value.toObject() : QJsonObject();
}

QString stringValue(const QJsonObject& parent, const char* key)
{
    return parent.value(QLatin1StringView(key)).toString();
}

bool parseExtractor(const QString& targetId, const QJsonValue& value, const QString& path, const QString& jsonPath,
    bool required, ParsedResponseExtractor* parsed, QList<TargetDiagnostic>* diagnostics)
{
    if (value.isUndefined()) {
        if (required) {
            return appendDiagnostic(diagnostics, jsonPath, QStringLiteral("%1.missing").arg(path),
                QStringLiteral("Target '%1' %2 must be an object").arg(targetId, path));
        }
        if (parsed) {
            *parsed = { };
        }
        return true;
    }

    if (!value.isObject()) {
        return appendDiagnostic(diagnostics, jsonPath, QStringLiteral("%1.type").arg(path),
            QStringLiteral("Target '%1' %2 must be an object").arg(targetId, path));
    }

    const QJsonObject object = value.toObject();
    const QString responseType = stringValue(object, "type");
    ParsedResponseExtractor local;

    if (responseType == QLatin1StringView("text_url")) {
        local.type = ResponseExtractorType::TextUrl;
    } else if (responseType == QLatin1StringView("regex")) {
        local.type = ResponseExtractorType::Regex;
    } else if (responseType == QLatin1StringView("json_pointer")) {
        local.type = ResponseExtractorType::JsonPointer;
    } else if (responseType == QLatin1StringView("header")) {
        local.type = ResponseExtractorType::Header;
    } else if (responseType == QLatin1StringView("redirect_url")) {
        local.type = ResponseExtractorType::RedirectUrl;
    } else if (responseType == QLatin1StringView("xml_path")) {
        local.type = ResponseExtractorType::XmlPath;
    } else {
        return appendDiagnostic(diagnostics, QStringLiteral("%1/type").arg(jsonPath),
            QStringLiteral("%1.type.invalid").arg(path),
            QStringLiteral(
                "Target '%1' %2.type must be text_url, regex, json_pointer, header, redirect_url, or xml_path")
                .arg(targetId, path));
    }

    bool ok = TargetFormat::removed(object, { "xpath" }, jsonPath, diagnostics);
    for (const auto key : { "pattern", "pointer", "name", "path" })
        ok = TargetFormat::string(object, QString::fromLatin1(key), jsonPath, diagnostics) && ok;
    const auto group = object.value(QStringLiteral("group"));
    if (!group.isUndefined()
        && (!group.isDouble() || group.toDouble() < 0 || group.toDouble() > 2147483647.0
            || group.toDouble() != group.toInt(-1)))
        ok = TargetFormat::problem(diagnostics, jsonPath + QStringLiteral("/group"),
            QStringLiteral("Must be a non-negative integer up to 2147483647"));
    local.valid = true;
    if (local.type == ResponseExtractorType::Regex) {
        local.pattern = stringValue(object, "pattern");
        local.group = object.value(QStringLiteral("group")).toInt(1);
        if (local.pattern.isEmpty()) {
            ok = appendDiagnostic(diagnostics, QStringLiteral("%1/pattern").arg(jsonPath),
                QStringLiteral("%1.pattern.empty").arg(path),
                QStringLiteral("Target '%1' %2.pattern must be a non-empty string").arg(targetId, path));
        }
        if (!local.pattern.isEmpty()) {
            const QRegularExpression regex(local.pattern);
            if (!regex.isValid()) {
                ok = appendDiagnostic(diagnostics, QStringLiteral("%1/pattern").arg(jsonPath),
                    QStringLiteral("%1.pattern.invalid").arg(path),
                    QStringLiteral("Target '%1' %2.pattern is invalid: %3").arg(targetId, path, regex.errorString()));
            } else if (local.group > regex.captureCount()) {
                ok = appendDiagnostic(diagnostics, QStringLiteral("%1/group").arg(jsonPath),
                    QStringLiteral("%1.group.out_of_range").arg(path),
                    QStringLiteral("Target '%1' %2.group exceeds the pattern's capture count").arg(targetId, path));
            }
        }
    }

    if (local.type == ResponseExtractorType::JsonPointer) {
        local.pointer = stringValue(object, "pointer");
        if (!object.value(QStringLiteral("pointer")).isString()
            || !TargetUploaderUtils::isValidJsonPointer(local.pointer))
            ok = TargetFormat::problem(diagnostics, jsonPath + QStringLiteral("/pointer"),
                QStringLiteral("Must be an RFC 6901 JSON Pointer; use an empty string for the whole document"));
    }

    if (local.type == ResponseExtractorType::Header) {
        local.name = stringValue(object, "name");
        if (local.name.isEmpty()) {
            ok = appendDiagnostic(diagnostics, QStringLiteral("%1/name").arg(jsonPath),
                QStringLiteral("%1.name.empty").arg(path),
                QStringLiteral("Target '%1' %2.name must be a non-empty string").arg(targetId, path));
        }
    }

    if (local.type == ResponseExtractorType::XmlPath) {
        local.path = stringValue(object, "path");
        if (!TargetUploaderUtils::isValidXmlPath(local.path))
            ok = TargetFormat::problem(diagnostics, jsonPath + QStringLiteral("/path"),
                QStringLiteral(
                    "Use an absolute element path such as /files/file[2]/url; XPath expressions are not supported"));
    }

    local.valid = ok;
    if (parsed) {
        *parsed = local;
    }
    return ok;
}
}

bool TargetResponseConfigParser::parse(
    const QJsonObject& target, ParsedResponseConfig* parsed, QList<TargetDiagnostic>* diagnostics)
{
    const QString targetId = target.value(QStringLiteral("id")).toString();
    const QJsonObject response = objectValue(target, "response");

    ParsedResponseConfig local;
    bool ok = true;
    if (!target.value(QStringLiteral("response")).isObject())
        ok = TargetFormat::problem(diagnostics, QStringLiteral("/response"), QStringLiteral("Must be an object"));
    ok = TargetFormat::removed(response, { "type", "pointer", "pattern", "group", "name", "xpath", "path" },
             QStringLiteral("/response"), diagnostics)
        && ok;
    ok = parseExtractor(targetId, response.value(QStringLiteral("url")), QStringLiteral("response.url"),
             QStringLiteral("/response/url"), true, &local.success, diagnostics)
        && ok;
    ok = parseExtractor(targetId, response.value(QStringLiteral("error")), QStringLiteral("response.error"),
             QStringLiteral("/response/error"), false, &local.error, diagnostics)
        && ok;
    ok = parseExtractor(targetId, response.value(QStringLiteral("thumbnail")), QStringLiteral("response.thumbnail"),
             QStringLiteral("/response/thumbnail"), false, &local.thumbnail, diagnostics)
        && ok;
    ok = parseExtractor(targetId, response.value(QStringLiteral("deletion")), QStringLiteral("response.deletion"),
             QStringLiteral("/response/deletion"), false, &local.deletion, diagnostics)
        && ok;

    if (parsed) {
        *parsed = local;
    }
    return ok;
}
