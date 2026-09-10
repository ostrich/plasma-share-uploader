#include "targetcoreconfigparser.h"
#include "targetformat.h"
#include <QJsonArray>

namespace {
bool parseList(const QJsonObject& accept, const QString& key, QStringList* output, QList<TargetDiagnostic>* diagnostics,
    bool (*valid)(const QString&))
{
    const auto value = accept.value(key);
    const auto path = QStringLiteral("/accept/") + key;
    if (value.isUndefined())
        return true;
    if (!value.isArray())
        return TargetFormat::problem(diagnostics, path, QStringLiteral("Must be an array of strings"));
    bool ok = true;
    const auto array = value.toArray();
    for (qsizetype i = 0; i < array.size(); ++i) {
        if (!array[i].isString() || !valid(array[i].toString())) {
            ok = TargetFormat::problem(diagnostics, path + QLatin1Char('/') + QString::number(i),
                key == QLatin1StringView("mimeTypes") ? QStringLiteral("Use an exact MIME type, type/*, or */*")
                                                      : QStringLiteral("Use a plain file suffix such as png or .jpg"));
        } else
            output->append(array[i].toString().toLower());
    }
    return ok;
}
}

bool TargetCoreConfigParser::parse(
    const QJsonObject& target, ParsedTargetCoreConfig* parsed, QList<TargetDiagnostic>* diagnostics)
{
    ParsedTargetCoreConfig local;
    local.id = target.value(QStringLiteral("id")).toString();
    static const QRegularExpression idPattern(QStringLiteral("\\A[a-z0-9][a-z0-9_-]*\\z"));
    bool ok = true;
    if (!idPattern.match(local.id).hasMatch())
        ok = TargetFormat::problem(diagnostics, QStringLiteral("/id"),
            QStringLiteral("Invalid target id: '%1'").arg(local.id), QStringLiteral("target.id.invalid"));
    for (const auto key : { "displayName", "description", "icon" })
        ok = TargetFormat::string(target, QString::fromLatin1(key), { }, diagnostics) && ok;
    local.displayName = target.value(QStringLiteral("displayName")).toString();
    if (local.displayName.isEmpty())
        local.displayName = local.id;
    local.description = target.value(QStringLiteral("description")).toString();
    local.icon = target.value(QStringLiteral("icon")).toString();
    if (local.icon.isEmpty())
        local.icon = QStringLiteral("image-x-generic");
    ok = TargetFormat::removed(target, { "pluginTypes", "constraints", "extensions" }, { }, diagnostics) && ok;
    const auto accept = target.value(QStringLiteral("accept"));
    if (!accept.isUndefined() && !accept.isObject())
        ok = TargetFormat::problem(diagnostics, QStringLiteral("/accept"), QStringLiteral("Must be an object"));
    else {
        ok = parseList(accept.toObject(), QStringLiteral("mimeTypes"), &local.mimeTypes, diagnostics,
                 TargetFormat::mimePattern)
            && ok;
        ok = parseList(accept.toObject(), QStringLiteral("extensions"), &local.extensions, diagnostics,
                 TargetFormat::extension)
            && ok;
    }
    if (parsed)
        *parsed = local;
    return ok;
}
