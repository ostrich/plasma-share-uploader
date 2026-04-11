#include "constraintmatcher.h"

#include <QFileInfo>
#include <QMimeDatabase>

namespace {
bool mimeMatchesPattern(const QString &mimeType, const QString &pattern)
{
    if (pattern == QLatin1StringView("*/*")) {
        return true;
    }
    if (pattern.endsWith(QLatin1StringView("/*"))) {
        const QStringView prefix = QStringView{pattern}.left(pattern.size() - 1);
        return mimeType.startsWith(prefix);
    }
    return mimeType == pattern;
}

QString normalizeExtension(const QString &extension)
{
    QString normalized = extension.trimmed().toLower();
    if (normalized.startsWith(QLatin1Char('.'))) {
        normalized.remove(0, 1);
    }
    return normalized;
}
}

bool ConstraintMatcher::targetMatchesFiles(const TargetDefinition &target, const QStringList &filePaths)
{
    const QStringList constraints = target.constraints();
    const QStringList extensionFilters = target.extensions();
    const bool hasMimeConstraints = !constraints.isEmpty();
    const bool hasExtensionFilters = !extensionFilters.isEmpty();
    if (!hasMimeConstraints && !hasExtensionFilters) {
        return true;
    }

    QMimeDatabase mimeDb;
    QStringList normalizedExtensions;
    for (const QString &extension : extensionFilters) {
        normalizedExtensions.append(normalizeExtension(extension));
    }

    for (const QString &filePath : filePaths) {
        if (hasMimeConstraints) {
            for (const QString &constraint : constraints) {
                if (!constraint.startsWith(QLatin1StringView("mimeType:"))) {
                    return false;
                }

                const QString pattern = constraint.mid(9);
                const QString mimeType = mimeDb.mimeTypeForFile(filePath, QMimeDatabase::MatchContent).name();
                if (!mimeMatchesPattern(mimeType, pattern)) {
                    return false;
                }
            }
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

QList<TargetDefinition> ConstraintMatcher::filterTargets(const QList<TargetDefinition> &targets, const QStringList &filePaths)
{
    QList<TargetDefinition> matches;
    for (const TargetDefinition &target : targets) {
        if (targetMatchesFiles(target, filePaths)) {
            matches.append(target);
        }
    }
    return matches;
}
