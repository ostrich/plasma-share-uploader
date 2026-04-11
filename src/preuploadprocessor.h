#pragma once

#include "targetpreuploadconfigparser.h"

#include <QJsonObject>
#include <QString>

namespace PreUploadProcessor {
struct Result {
    bool ok = false;
    QString uploadPath;
    QString tempDirPath;
    QString errorMessage;
};

Result preprocessFile(const QJsonObject &targetConfig, const QString &filePath);
Result preprocessFile(const ParsedPreUploadConfig &config, const QString &filePath);
}
