#include "targetregistry.h"

#include "targetconfigvalidator.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QStandardPaths>

namespace {
QString defaultSystemTargetsPath()
{
    return QStringLiteral(PLASMA_SHARE_UPLOADER_SYSTEM_TARGETS_PATH);
}

QString defaultUserTargetsPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/plasma-share-uploader/targets.d");
}

void loadTargetFile(const QString &path, QMap<QString, TargetDefinition> &targets, QStringList &errors)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        errors.append(QStringLiteral("Failed to open target file: %1").arg(path));
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject() || doc.object().isEmpty()) {
        errors.append(QStringLiteral("Target file is not a JSON object: %1").arg(path));
        return;
    }

    const QJsonObject targetObject = doc.object();
    QStringList validationErrors;
    if (!TargetConfigValidator::validateTarget(targetObject, &validationErrors)) {
        for (const QString &message : std::as_const(validationErrors)) {
            errors.append(QStringLiteral("%1 (%2)").arg(message, path));
        }
        return;
    }

    TargetDefinition definition;
    definition.config = targetObject;
    targets.insert(definition.id(), definition);
}

void loadTargetsFromDirectory(const QString &path, bool required, QMap<QString, TargetDefinition> &targets, QStringList &errors)
{
    const QDir dir(path);
    if (!dir.exists()) {
        if (required) {
            errors.append(QStringLiteral("Missing targets directory: %1").arg(path));
        }
        return;
    }

    const QStringList fileNames = dir.entryList(QStringList{QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    for (const QString &fileName : fileNames) {
        loadTargetFile(dir.filePath(fileName), targets, errors);
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

    loadTargetsFromDirectory(systemTargetsPath(), true, mergedTargets, result.errors);
    loadTargetsFromDirectory(userTargetsPath(), false, mergedTargets, result.errors);

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
