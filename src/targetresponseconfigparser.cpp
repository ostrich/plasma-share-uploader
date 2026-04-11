#include "targetresponseconfigparser.h"

#include <QJsonObject>

namespace {
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

bool parseExtractor(const QString &targetId,
                    const QJsonValue &value,
                    const QString &path,
                    const QString &jsonPath,
                    bool required,
                    ParsedResponseExtractor *parsed,
                    QList<TargetDiagnostic> *diagnostics)
{
    if (value.isUndefined()) {
        if (required) {
            return appendDiagnostic(diagnostics,
                                    jsonPath,
                                    QStringLiteral("%1.missing").arg(path),
                                    QStringLiteral("Target '%1' %2 must be an object").arg(targetId, path));
        }
        if (parsed) {
            *parsed = {};
        }
        return true;
    }

    if (!value.isObject()) {
        return appendDiagnostic(diagnostics,
                                jsonPath,
                                QStringLiteral("%1.type").arg(path),
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
    } else if (responseType == QLatin1StringView("xml_xpath")) {
        local.type = ResponseExtractorType::XmlXpath;
    } else {
        return appendDiagnostic(diagnostics,
                                QStringLiteral("%1/type").arg(jsonPath),
                                QStringLiteral("%1.type.invalid").arg(path),
                                QStringLiteral("Target '%1' %2.type must be text_url, regex, json_pointer, header, redirect_url, or xml_xpath")
                                    .arg(targetId, path));
    }

    bool ok = true;
    local.valid = true;
    if (local.type == ResponseExtractorType::Regex) {
        local.pattern = stringValue(object, "pattern");
        local.group = object.value(QStringLiteral("group")).toInt(1);
        if (local.pattern.isEmpty()) {
            ok = appendDiagnostic(diagnostics,
                                  QStringLiteral("%1/pattern").arg(jsonPath),
                                  QStringLiteral("%1.pattern.empty").arg(path),
                                  QStringLiteral("Target '%1' %2.pattern must be a non-empty string").arg(targetId, path));
        }
        if (!object.value(QStringLiteral("group")).isUndefined() && object.value(QStringLiteral("group")).toInt(-1) < 0) {
            ok = appendDiagnostic(diagnostics,
                                  QStringLiteral("%1/group").arg(jsonPath),
                                  QStringLiteral("%1.group.invalid").arg(path),
                                  QStringLiteral("Target '%1' %2.group must be a non-negative integer").arg(targetId, path));
        }
    }

    if (local.type == ResponseExtractorType::JsonPointer) {
        local.pointer = stringValue(object, "pointer");
        if (local.pointer.isEmpty()) {
            ok = appendDiagnostic(diagnostics,
                                  QStringLiteral("%1/pointer").arg(jsonPath),
                                  QStringLiteral("%1.pointer.empty").arg(path),
                                  QStringLiteral("Target '%1' %2.pointer must be a non-empty string").arg(targetId, path));
        }
        if (!local.pointer.isEmpty() && !local.pointer.startsWith(QLatin1Char('/'))) {
            ok = appendDiagnostic(diagnostics,
                                  QStringLiteral("%1/pointer").arg(jsonPath),
                                  QStringLiteral("%1.pointer.invalid").arg(path),
                                  QStringLiteral("Target '%1' %2.pointer must start with '/'").arg(targetId, path));
        }
    }

    if (local.type == ResponseExtractorType::Header) {
        local.name = stringValue(object, "name");
        if (local.name.isEmpty()) {
            ok = appendDiagnostic(diagnostics,
                                  QStringLiteral("%1/name").arg(jsonPath),
                                  QStringLiteral("%1.name.empty").arg(path),
                                  QStringLiteral("Target '%1' %2.name must be a non-empty string").arg(targetId, path));
        }
    }

    if (local.type == ResponseExtractorType::XmlXpath) {
        local.xpath = stringValue(object, "xpath");
        if (local.xpath.isEmpty()) {
            ok = appendDiagnostic(diagnostics,
                                  QStringLiteral("%1/xpath").arg(jsonPath),
                                  QStringLiteral("%1.xpath.empty").arg(path),
                                  QStringLiteral("Target '%1' %2.xpath must be a non-empty string").arg(targetId, path));
        }
        if (!local.xpath.isEmpty() && !local.xpath.startsWith(QLatin1Char('/'))) {
            ok = appendDiagnostic(diagnostics,
                                  QStringLiteral("%1/xpath").arg(jsonPath),
                                  QStringLiteral("%1.xpath.invalid").arg(path),
                                  QStringLiteral("Target '%1' %2.xpath must start with '/'").arg(targetId, path));
        }
    }

    if (parsed) {
        *parsed = local;
    }
    return ok;
}
}

bool TargetResponseConfigParser::parse(const QJsonObject &target, ParsedResponseConfig *parsed, QList<TargetDiagnostic> *diagnostics)
{
    const QString targetId = target.value(QStringLiteral("id")).toString();
    const QJsonObject response = objectValue(target, "response");

    ParsedResponseConfig local;
    bool ok = true;
    ok = parseExtractor(targetId, response, QStringLiteral("response"), QStringLiteral("/response"), true, &local.success, diagnostics) && ok;
    ok = parseExtractor(targetId, response.value(QStringLiteral("error")), QStringLiteral("response.error"), QStringLiteral("/response/error"), false, &local.error, diagnostics) && ok;
    ok = parseExtractor(targetId, response.value(QStringLiteral("thumbnail")), QStringLiteral("response.thumbnail"), QStringLiteral("/response/thumbnail"), false, &local.thumbnail, diagnostics) && ok;
    ok = parseExtractor(targetId, response.value(QStringLiteral("deletion")), QStringLiteral("response.deletion"), QStringLiteral("/response/deletion"), false, &local.deletion, diagnostics) && ok;

    if (parsed) {
        *parsed = local;
    }
    return ok;
}
