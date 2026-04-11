#include "targetregistry.h"

#include "targetconfigvalidator.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace {
QString defaultSystemTargetsPath()
{
    return QStringLiteral(PLASMA_SHARE_UPLOADER_SYSTEM_TARGETS_PATH);
}

QString defaultUserTargetsPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + QStringLiteral("/targets.json");
}

void loadTargetsFromFile(const QString &path, bool required, QMap<QString, TargetDefinition> &targets, QStringList &errors)
{
    QFile file(path);
    if (!file.exists()) {
        if (required) {
            errors.append(QStringLiteral("Missing targets file: %1").arg(path));
        }
        return;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        errors.append(QStringLiteral("Failed to open targets file: %1").arg(path));
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        errors.append(QStringLiteral("Targets file is not a JSON object: %1").arg(path));
        return;
    }

    const QJsonValue targetsValue = doc.object().value(QStringLiteral("targets"));
    if (!targetsValue.isArray()) {
        errors.append(QStringLiteral("Targets file does not contain a targets array: %1").arg(path));
        return;
    }

    const QJsonArray targetArray = targetsValue.toArray();
    for (int i = 0; i < targetArray.size(); ++i) {
        if (!targetArray.at(i).isObject()) {
            errors.append(QStringLiteral("Target entry %1 in %2 is not an object").arg(i).arg(path));
            continue;
        }

        const QJsonObject targetObject = targetArray.at(i).toObject();
        QStringList validationErrors;
        if (!TargetConfigValidator::validateTarget(targetObject, &validationErrors)) {
            for (const QString &message : std::as_const(validationErrors)) {
                errors.append(QStringLiteral("%1 (%2)").arg(message, path));
            }
            continue;
        }

        TargetDefinition definition;
        definition.config = targetObject;
        targets.insert(definition.id(), definition);
    }
}
}

TargetRegistry::TargetRegistry(QString systemPath, QString userPath)
    : m_systemPath(std::move(systemPath))
    , m_userPath(std::move(userPath))
{
}

TargetRegistry::LoadResult TargetRegistry::loadTargets() const
{
    LoadResult result;
    QMap<QString, TargetDefinition> mergedTargets;

    loadTargetsFromFile(systemTargetsPath(), true, mergedTargets, result.errors);
    loadTargetsFromFile(userTargetsPath(), false, mergedTargets, result.errors);

    result.targets = mergedTargets.values();
    return result;
}

QString TargetRegistry::systemTargetsPath() const
{
    return m_systemPath.isEmpty() ? defaultSystemTargetsPath() : m_systemPath;
}

QString TargetRegistry::userTargetsPath() const
{
    return m_userPath.isEmpty() ? defaultUserTargetsPath() : m_userPath;
}
