#include "targetregistry.h"

#include "targetconfigparser.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSet>
#include <QStandardPaths>

namespace {
QString defaultDevTargetsPath()
{
    return QStringLiteral(PLASMA_SHARE_UPLOADER_DEV_TARGETS_PATH);
}

QString defaultSystemTargetsPath()
{
    return QStringLiteral(PLASMA_SHARE_UPLOADER_SYSTEM_TARGETS_PATH);
}

QString defaultUserTargetsPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/plasma-share-uploader/targets");
}

QString defaultStateFilePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/plasma-share-uploader/state.json");
}

void appendRegistryDiagnostic(QList<TargetDiagnostic> &diagnostics,
                              const QString &filePath,
                              const QString &jsonPath,
                              const QString &code,
                              const QString &message)
{
    diagnostics.append(TargetDiagnostic{TargetDiagnosticSeverity::Error, filePath, jsonPath, code, message});
}

void loadTargetFile(const QString &path,
                    TargetDefinition::Source source,
                    QMap<QString, TargetDefinition> &targets,
                    QList<TargetDiagnostic> &diagnostics)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        appendRegistryDiagnostic(diagnostics, path, {}, QStringLiteral("file.open_failed"), QStringLiteral("Failed to open target file"));
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject() || doc.object().isEmpty()) {
        appendRegistryDiagnostic(diagnostics, path, {}, QStringLiteral("file.invalid_json_object"), QStringLiteral("Target file is not a JSON object"));
        return;
    }

    const QJsonObject targetObject = doc.object();
    TargetDefinition definition;
    QList<TargetDiagnostic> fileDiagnostics;
    if (!TargetConfigParser::parse(targetObject, &definition.target, &fileDiagnostics)) {
        for (TargetDiagnostic &diagnostic : fileDiagnostics) {
            diagnostic.filePath = path;
            diagnostics.append(diagnostic);
        }
        return;
    }
    definition.source = source;
    targets.insert(definition.id(), definition);
}

void loadTargetsFromDirectory(const QString &path,
                              bool required,
                              TargetDefinition::Source source,
                              QMap<QString, TargetDefinition> &targets,
                              QList<TargetDiagnostic> &diagnostics)
{
    const QDir dir(path);
    if (!dir.exists()) {
        if (required) {
            appendRegistryDiagnostic(diagnostics, path, {}, QStringLiteral("directory.missing"), QStringLiteral("Missing targets directory"));
        }
        return;
    }

    const QStringList fileNames = dir.entryList(QStringList{QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    for (const QString &fileName : fileNames) {
        loadTargetFile(dir.filePath(fileName), source, targets, diagnostics);
    }
}

QSet<QString> loadDisabledBundledTargetIds(const QString &path, QList<TargetDiagnostic> &diagnostics)
{
    QSet<QString> disabledIds;
    QFile file(path);
    if (!file.exists()) {
        return disabledIds;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        appendRegistryDiagnostic(diagnostics, path, QStringLiteral("/disabledBundledTargets"),
                                 QStringLiteral("state.open_failed"),
                                 QStringLiteral("Failed to open state file"));
        return disabledIds;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        appendRegistryDiagnostic(diagnostics, path, QStringLiteral("/disabledBundledTargets"),
                                 QStringLiteral("state.invalid_json_object"),
                                 QStringLiteral("State file is not a JSON object"));
        return disabledIds;
    }

    const QJsonValue disabledValue = doc.object().value(QStringLiteral("disabledBundledTargets"));
    if (disabledValue.isUndefined()) {
        return disabledIds;
    }
    if (!disabledValue.isArray()) {
        appendRegistryDiagnostic(diagnostics, path, QStringLiteral("/disabledBundledTargets"),
                                 QStringLiteral("state.disabledBundledTargets.not_array"),
                                 QStringLiteral("disabledBundledTargets must be an array"));
        return disabledIds;
    }

    const QJsonArray disabledArray = disabledValue.toArray();
    for (int i = 0; i < disabledArray.size(); ++i) {
        const QJsonValue item = disabledArray.at(i);
        if (!item.isString() || item.toString().isEmpty()) {
            appendRegistryDiagnostic(diagnostics, path,
                                     QStringLiteral("/disabledBundledTargets/%1").arg(i),
                                     QStringLiteral("state.disabledBundledTargets.item.invalid"),
                                     QStringLiteral("disabledBundledTargets entries must be non-empty strings"));
            continue;
        }
        disabledIds.insert(item.toString());
    }

    return disabledIds;
}
}

TargetRegistry::TargetRegistry(QString systemPath, QString userPath, QString statePath)
    : m_systemPath(std::move(systemPath))
    , m_userPath(std::move(userPath))
    , m_statePath(std::move(statePath))
{
}

TargetRegistry::LoadResult TargetRegistry::loadTargets() const
{
    LoadResult result;
    QMap<QString, TargetDefinition> mergedTargets;
    const QSet<QString> disabledBundledTargetIds = loadDisabledBundledTargetIds(stateFilePath(), result.diagnostics);

    loadTargetsFromDirectory(systemTargetsPath(), true, TargetDefinition::Source::System, mergedTargets, result.diagnostics);
    loadTargetsFromDirectory(userTargetsPath(), false, TargetDefinition::Source::User, mergedTargets, result.diagnostics);

    for (auto it = mergedTargets.begin(); it != mergedTargets.end();) {
        if (it->isBundled() && disabledBundledTargetIds.contains(it.key())) {
            it = mergedTargets.erase(it);
            continue;
        }
        ++it;
    }

    result.targets = mergedTargets.values();
    return result;
}

QString TargetRegistry::systemTargetsPath() const
{
    if (!m_systemPath.isEmpty()) {
        return m_systemPath;
    }

    const QString devPath = defaultDevTargetsPath();
    if (QDir(devPath).exists()) {
        return devPath;
    }

    return defaultSystemTargetsPath();
}

QString TargetRegistry::userTargetsPath() const
{
    return m_userPath.isEmpty() ? defaultUserTargetsPath() : m_userPath;
}

QString TargetRegistry::stateFilePath() const
{
    return m_statePath.isEmpty() ? defaultStateFilePath() : m_statePath;
}
