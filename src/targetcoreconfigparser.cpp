#include "targetcoreconfigparser.h"

#include <QJsonArray>
#include <QRegularExpression>

namespace {
const QRegularExpression kIdPattern(QStringLiteral("^[a-z0-9][a-z0-9_-]*$"));
const QRegularExpression kMimePattern(QStringLiteral("^[^/\\s]+/[^/\\s]+$"));
const QRegularExpression kMimeWildcardPattern(QStringLiteral("^[^/\\s]+/\\*$"));

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

QStringList stringListValue(const QJsonObject &object, const char *key)
{
    QStringList values;
    const QJsonValue value = object.value(QLatin1StringView(key));
    if (!value.isArray()) {
        return values;
    }

    for (const QJsonValue &entry : value.toArray()) {
        const QString text = entry.toString();
        if (!text.isEmpty()) {
            values.append(text);
        }
    }
    return values;
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

bool parsePluginTypes(const QString &targetId,
                      const QJsonValue &value,
                      QStringList *pluginTypes,
                      QList<TargetDiagnostic> *diagnostics)
{
    if (!value.isUndefined() && !value.isArray()) {
        return appendDiagnostic(diagnostics,
                                QStringLiteral("/pluginTypes"),
                                QStringLiteral("pluginTypes.type"),
                                QStringLiteral("Target '%1' pluginTypes must be a list").arg(targetId));
    }

    if (pluginTypes) {
        *pluginTypes = value.isArray() ? stringListValue(QJsonObject{{QStringLiteral("pluginTypes"), value}}, "pluginTypes") : QStringList{};
    }
    return true;
}

bool parseConstraints(const QString &targetId,
                      const QJsonValue &value,
                      QStringList *constraints,
                      QList<TargetDiagnostic> *diagnostics)
{
    if (!value.isUndefined() && !value.isArray()) {
        return appendDiagnostic(diagnostics,
                                QStringLiteral("/constraints"),
                                QStringLiteral("constraints.type"),
                                QStringLiteral("Target '%1' constraints must be a list").arg(targetId));
    }

    bool ok = true;
    QStringList parsedValues;
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
        parsedValues.append(text);
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

    if (constraints) {
        *constraints = parsedValues;
    }
    return ok;
}

bool parseExtensions(const QString &targetId,
                     const QJsonValue &value,
                     QStringList *extensions,
                     QList<TargetDiagnostic> *diagnostics)
{
    if (!value.isUndefined() && !value.isArray()) {
        return appendDiagnostic(diagnostics,
                                QStringLiteral("/extensions"),
                                QStringLiteral("extensions.type"),
                                QStringLiteral("Target '%1' extensions must be a list").arg(targetId));
    }

    bool ok = true;
    QStringList parsedValues;
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

        parsedValues.append(text);
        const QString normalized = text.startsWith(QLatin1Char('.')) ? text.mid(1) : text;
        if (normalized.isEmpty() || normalized.contains(QLatin1Char('/')) || normalized.contains(QLatin1Char('*'))
            || normalized.contains(QLatin1Char(' '))) {
            ok = appendDiagnostic(diagnostics,
                                  QStringLiteral("/extensions/%1").arg(i),
                                  QStringLiteral("extensions.invalid"),
                                  QStringLiteral("Target '%1' extensions must contain plain file suffixes such as png or .png").arg(targetId));
        }
    }

    if (extensions) {
        *extensions = parsedValues;
    }
    return ok;
}
}

bool TargetCoreConfigParser::parse(const QJsonObject &target, ParsedTargetCoreConfig *parsed, QList<TargetDiagnostic> *diagnostics)
{
    const QString targetId = target.value(QStringLiteral("id")).toString();
    if (targetId.isEmpty() || !kIdPattern.match(targetId).hasMatch()) {
        return appendDiagnostic(diagnostics,
                                QStringLiteral("/id"),
                                QStringLiteral("target.id.invalid"),
                                QStringLiteral("Invalid target id: '%1'").arg(targetId));
    }

    ParsedTargetCoreConfig local;
    local.id = targetId;
    local.displayName = target.value(QStringLiteral("displayName")).toString();
    if (local.displayName.isEmpty()) {
        local.displayName = local.id;
    }
    local.description = target.value(QStringLiteral("description")).toString();
    local.icon = target.value(QStringLiteral("icon")).toString();
    if (local.icon.isEmpty()) {
        local.icon = QStringLiteral("image-x-generic");
    }

    bool ok = true;
    ok = parsePluginTypes(targetId, target.value(QStringLiteral("pluginTypes")), &local.pluginTypes, diagnostics) && ok;
    ok = parseConstraints(targetId, target.value(QStringLiteral("constraints")), &local.constraints, diagnostics) && ok;
    ok = parseExtensions(targetId, target.value(QStringLiteral("extensions")), &local.extensions, diagnostics) && ok;

    if (parsed) {
        *parsed = local;
    }
    return ok;
}
