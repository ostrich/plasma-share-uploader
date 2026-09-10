#pragma once

#include "targetdiagnostic.h"
#include <QJsonObject>
#include <QRegularExpression>

namespace TargetFormat {
inline bool problem(QList<TargetDiagnostic>* diagnostics, const QString& path, const QString& message,
    const QString& code = QStringLiteral("format.invalid"))
{
    if (diagnostics)
        diagnostics->append({ TargetDiagnosticSeverity::Error, { }, path, code, message });
    return false;
}
inline bool string(const QJsonObject& object, const QString& key, const QString& path,
    QList<TargetDiagnostic>* diagnostics, bool required = false, bool nonempty = false)
{
    const auto value = object.value(key);
    if (value.isUndefined() && !required)
        return true;
    if (!value.isString() || (nonempty && value.toString().isEmpty()))
        return problem(diagnostics, path + QLatin1Char('/') + key,
            nonempty ? QStringLiteral("Must be a non-empty string") : QStringLiteral("Must be a string"));
    return true;
}
inline bool removed(const QJsonObject& object, std::initializer_list<const char*> keys, const QString& path,
    QList<TargetDiagnostic>* diagnostics)
{
    bool ok = true;
    for (const auto key : keys) {
        const auto name = QString::fromLatin1(key);
        if (object.contains(name))
            ok = problem(diagnostics, path + QLatin1Char('/') + name,
                     QStringLiteral(
                         "This field is not supported in schemaVersion 1; see the target format documentation"),
                     QStringLiteral("format.removed"))
                && ok;
    }
    return ok;
}
inline bool mimePattern(const QString& value)
{
    static const QRegularExpression pattern(
        QStringLiteral(R"(\A(?:\*/\*|[A-Za-z0-9!#$%&'+.^_`|~-]+/(?:\*|[A-Za-z0-9!#$%&'+.^_`|~-]+))\z)"));
    return pattern.match(value).hasMatch();
}
inline bool extension(const QString& value)
{
    static const QRegularExpression pattern(QStringLiteral(R"(\A\.?[^.\s/*\\]+\z)"));
    return pattern.match(value).hasMatch();
}
}
