#include "targetregistry.h"

#include "targetconfigparser.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QStandardPaths>
#include <QTemporaryDir>

namespace {
void appendRegistryDiagnostic(QList<TargetDiagnostic> &diagnostics,
                              const QString &filePath,
                              const QString &jsonPath,
                              const QString &code,
                              const QString &message)
{
    diagnostics.append(TargetDiagnostic{TargetDiagnosticSeverity::Error, filePath, jsonPath, code, message});
}

bool initializeActiveTargets(const QString &bundledPath, const QString &activePath,
                             QList<TargetDiagnostic> &diagnostics)
{
    const QFileInfo activeInfo(activePath);
    // An existing directory, even an empty one, is the user's complete selection.
    // Also leave invalid paths untouched so the loader can diagnose them.
    if (activeInfo.exists() || activeInfo.isSymLink()) {
        return true;
    }

    const QDir bundledDir(bundledPath);
    if (!bundledDir.exists() || !bundledDir.isReadable()) {
        appendRegistryDiagnostic(diagnostics, bundledPath, {}, QStringLiteral("directory.unavailable"),
                                 QStringLiteral("Cannot read bundled presets to initialize upload targets"));
        return false;
    }

    const QString parentPath = activeInfo.absolutePath();
    if (!QDir().mkpath(parentPath)) {
        appendRegistryDiagnostic(diagnostics, parentPath, {}, QStringLiteral("directory.create_failed"),
                                 QStringLiteral("Failed to create upload configuration directory"));
        return false;
    }

    // Publish all default links together; failed initialization can be retried.
    QTemporaryDir pending(QDir(parentPath).filePath(QStringLiteral(".targets-XXXXXX")));
    if (!pending.isValid()) {
        appendRegistryDiagnostic(diagnostics, activePath, {}, QStringLiteral("directory.create_failed"),
                                 QStringLiteral("Failed to prepare upload targets directory"));
        return false;
    }
    const auto presets = bundledDir.entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    for (const QFileInfo &preset : presets) {
        if (!QFile::link(preset.absoluteFilePath(), pending.filePath(preset.fileName()))) {
            appendRegistryDiagnostic(diagnostics, preset.absoluteFilePath(), {}, QStringLiteral("preset.link_failed"),
                                     QStringLiteral("Failed to enable bundled preset"));
            return false;
        }
    }

    if (!QDir().rename(pending.path(), activeInfo.absoluteFilePath())) {
        // Another host application may have initialized the directory first.
        if (QFileInfo(activePath).isDir()) {
            return true;
        }
        appendRegistryDiagnostic(diagnostics, activePath, {}, QStringLiteral("directory.create_failed"),
                                 QStringLiteral("Failed to create upload targets directory"));
        return false;
    }
    pending.setAutoRemove(false);
    return true;
}

void loadTargetFile(const QString &path, QMap<QString, TargetDefinition> &targets,
                    QMap<QString, QString> &targetPaths, QList<TargetDiagnostic> &diagnostics)
{
    const QFileInfo info(path);
    if (!info.isFile()) {
        appendRegistryDiagnostic(diagnostics, path, {}, QStringLiteral("file.unavailable"),
                                 info.isSymLink() ? QStringLiteral("Target link does not point to an existing file")
                                                  : QStringLiteral("Target configuration is not a regular file"));
        return;
    }
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

    TargetDefinition definition;
    QList<TargetDiagnostic> fileDiagnostics;
    if (!TargetConfigParser::parse(doc.object(), &definition.target, &fileDiagnostics)) {
        for (TargetDiagnostic &diagnostic : fileDiagnostics) {
            diagnostic.filePath = path;
            diagnostics.append(diagnostic);
        }
        return;
    }
    if (targetPaths.contains(definition.id())) {
        appendRegistryDiagnostic(diagnostics, path, QStringLiteral("/id"), QStringLiteral("target.duplicate_id"),
                                 QStringLiteral("Target '%1' is already defined by %2").arg(definition.id(), targetPaths.value(definition.id())));
        return;
    }
    targetPaths.insert(definition.id(), path);
    targets.insert(definition.id(), definition);
}
}

TargetRegistry::TargetRegistry(QString bundledPath, QString activePath)
    : m_bundledPath(std::move(bundledPath))
    , m_activePath(std::move(activePath))
{
}

TargetRegistry::LoadResult TargetRegistry::loadTargets() const
{
    LoadResult result;
    const QString activePath = activeTargetsPath();
    if (!initializeActiveTargets(bundledTargetsPath(), activePath, result.diagnostics)) {
        return result;
    }
    const QDir activeDir(activePath);
    if (!activeDir.exists() || !activeDir.isReadable()) {
        appendRegistryDiagnostic(result.diagnostics, activePath, {}, QStringLiteral("directory.unavailable"),
                                 QStringLiteral("Cannot read active upload targets directory"));
        return result;
    }

    QMap<QString, TargetDefinition> targets;
    QMap<QString, QString> targetPaths;
    // System includes broken symlinks so they produce a diagnostic instead of disappearing.
    const auto entries = activeDir.entryInfoList({QStringLiteral("*.json")}, QDir::Files | QDir::System, QDir::Name);
    for (const QFileInfo &entry : entries) {
        if (!entry.isDir()) {
            loadTargetFile(entry.absoluteFilePath(), targets, targetPaths, result.diagnostics);
        }
    }
    result.targets = targets.values();
    return result;
}

QString TargetRegistry::bundledTargetsPath() const
{
    if (!m_bundledPath.isEmpty()) {
        return m_bundledPath;
    }

#ifdef PLASMA_SHARE_UPLOADER_DEV_TARGETS_PATH
    const QString devPath = QStringLiteral(PLASMA_SHARE_UPLOADER_DEV_TARGETS_PATH);
    if (QDir(devPath).exists()) {
        return devPath;
    }
#endif

    return QStringLiteral(PLASMA_SHARE_UPLOADER_SYSTEM_TARGETS_PATH);
}

QString TargetRegistry::activeTargetsPath() const
{
    return m_activePath.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/plasma-share-uploader/targets")
        : m_activePath;
}
