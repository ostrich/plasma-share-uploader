#include "constraintmatcher.h"

#include <QFileInfo>
#include <QMimeDatabase>

namespace {
bool mimeMatchesPattern(const QString& mimeType, const QString& pattern)
{
    if (pattern == QLatin1StringView("*/*")) {
        return true;
    }
    if (pattern.endsWith(QLatin1StringView("/*"))) {
        const QStringView prefix = QStringView { pattern }.left(pattern.size() - 1);
        return mimeType.startsWith(prefix);
    }
    return mimeType == pattern;
}

QString normalizeExtension(const QString& extension)
{
    QString normalized = extension.trimmed().toLower();
    if (normalized.startsWith(QLatin1Char('.'))) {
        normalized.remove(0, 1);
    }
    return normalized;
}
}

bool ConstraintMatcher::targetMatchesFiles(const TargetDefinition& target, const QStringList& filePaths)
{
    const QStringList mimeTypes = target.mimeTypes();
    const QStringList extensionFilters = target.extensions();
    const bool hasMimeConstraints = !mimeTypes.isEmpty();
    const bool hasExtensionFilters = !extensionFilters.isEmpty();
    if (!hasMimeConstraints && !hasExtensionFilters) {
        return true;
    }

    QMimeDatabase mimeDb;
    QStringList normalizedExtensions;
    for (const QString& extension : extensionFilters) {
        normalizedExtensions.append(normalizeExtension(extension));
    }

    for (const QString& filePath : filePaths) {
        if (hasMimeConstraints) {
            const auto mimeType = mimeDb.mimeTypeForFile(filePath, QMimeDatabase::MatchContent).name();
            bool matched = false;
            for (const auto& pattern : mimeTypes) {
                if (mimeMatchesPattern(mimeType, pattern)) {
                    matched = true;
                    break;
                }
            }
            if (!matched)
                return false;
        }

        if (hasExtensionFilters) {
            const QString suffix = normalizeExtension(QFileInfo(filePath).suffix());
            if (suffix.isEmpty() || !normalizedExtensions.contains(suffix)) {
                return false;
            }
        }
    }

    return true;
}

QList<TargetDefinition> ConstraintMatcher::filterTargets(
    const QList<TargetDefinition>& targets, const QStringList& filePaths)
{
    QList<TargetDefinition> matches;
    for (const TargetDefinition& target : targets) {
        if (targetMatchesFiles(target, filePaths)) {
            matches.append(target);
        }
    }
    return matches;
}
