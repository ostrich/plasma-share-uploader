#pragma once

#include "targetpreuploadconfigparser.h"

#include <QJsonObject>
#include <QString>
#include <QTemporaryDir>
#include <functional>
#include <memory>

class QObject;

namespace PreUploadProcessor {
struct Result {
    bool ok = false;
    QString uploadPath;
    QString tempDirPath;
    QString errorMessage;
    // Keep prepared files alive until the last result owning them is released.
    std::shared_ptr<QTemporaryDir> tempDir;
};

// The context owns the task. Destroying it stops processing and removes temporary files.
QObject *preprocessFileAsync(const ParsedPreUploadConfig &config, const QString &filePath,
                             QObject *context, std::function<void(Result)> completed);

// Synchronous convenience wrappers for callers that run their own local event loop.
Result preprocessFile(const QJsonObject &targetConfig, const QString &filePath);
Result preprocessFile(const ParsedPreUploadConfig &config, const QString &filePath);
}
