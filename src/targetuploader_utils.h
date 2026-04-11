#pragma once

#include <QFileInfo>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkRequest>
#include <QString>

namespace TargetUploaderUtils {
QJsonObject objectValue(const QJsonObject &parent, const char *key);
QString stringValue(const QJsonObject &parent, const char *key);
QJsonObject fieldMap(const QJsonObject &parent);
QString substituteEnv(const QString &value);
QString applyUrlTemplate(const QString &urlTemplate, const QFileInfo &fileInfo);
void applyHeaders(const QJsonObject &requestConfig, QNetworkRequest &requestObj);
QJsonValue resolveJsonPointer(const QJsonValue &root, const QString &pointer);
}
