#pragma once

#include <QFileInfo>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkRequest>
#include <QString>
#include <QUrl>

namespace TargetUploaderUtils {
QJsonObject objectValue(const QJsonObject &parent, const char *key);
QString stringValue(const QJsonObject &parent, const char *key);
QJsonObject fieldMap(const QJsonObject &parent);
QString substituteEnv(const QString &value);
QString substituteRequestValue(const QString &value, const QFileInfo &fileInfo);
QString applyUrlTemplate(const QString &urlTemplate, const QFileInfo &fileInfo);
void applyHeaders(const QJsonObject &requestConfig, const QFileInfo &fileInfo, QNetworkRequest &requestObj);
QUrl applyQueryParameters(const QString &urlTemplate, const QJsonObject &requestConfig, const QFileInfo &fileInfo);
QJsonValue substituteJsonValue(const QJsonValue &value, const QFileInfo &fileInfo);
QJsonValue resolveJsonPointer(const QJsonValue &root, const QString &pointer);
QString resolveXmlPath(const QByteArray &xmlBytes, const QString &xpath);
}
