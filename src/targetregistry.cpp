#include "targetregistry.h"

#include "targetconfigparser.h"

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

void appendRegistryDiagnostic(QList<TargetDiagnostic> &diagnostics,
                              const QString &filePath,
                              const QString &jsonPath,
                              const QString &code,
                              const QString &message)
{
    diagnostics.append(TargetDiagnostic{TargetDiagnosticSeverity::Error, filePath, jsonPath, code, message});
}

void loadTargetFile(const QString &path, QMap<QString, TargetDefinition> &targets, QList<TargetDiagnostic> &diagnostics)
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
    targets.insert(definition.id(), definition);
}

void loadTargetsFromDirectory(const QString &path, bool required, QMap<QString, TargetDefinition> &targets, QList<TargetDiagnostic> &diagnostics)
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
        loadTargetFile(dir.filePath(fileName), targets, diagnostics);
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

    loadTargetsFromDirectory(systemTargetsPath(), true, mergedTargets, result.diagnostics);
    loadTargetsFromDirectory(userTargetsPath(), false, mergedTargets, result.diagnostics);

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
