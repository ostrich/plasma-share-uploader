#include "constraintmatcher.h"

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
}

bool ConstraintMatcher::targetMatchesFiles(const TargetDefinition &target, const QStringList &filePaths)
{
    const QStringList constraints = target.constraints();
    if (constraints.isEmpty()) {
        return true;
    }

    QMimeDatabase mimeDb;
    for (const QString &constraint : constraints) {
        if (!constraint.startsWith(QLatin1StringView("mimeType:"))) {
            return false;
        }

        const QString pattern = constraint.mid(9);
        for (const QString &filePath : filePaths) {
            const QString mimeType = mimeDb.mimeTypeForFile(filePath, QMimeDatabase::MatchContent).name();
            if (!mimeMatchesPattern(mimeType, pattern)) {
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
