#pragma once

#include <QFileInfo>
#include <QJsonObject>
#include <QJsonValue>
#include <QMap>
#include <QNetworkRequest>
#include <QString>
#include <QUrl>

namespace TargetUploaderUtils {
QJsonObject objectValue(const QJsonObject &parent, const char *key);
QString stringValue(const QJsonObject &parent, const char *key);
QJsonObject fieldMap(const QJsonObject &parent);
void applyHeaders(const QMap<QString, QString> &headers, const QFileInfo &fileInfo, QNetworkRequest &requestObj, const QMap<QString, QString> &secrets = {});
QString substituteEnv(const QString &value);
QString substituteRequestValue(const QString &value, const QFileInfo &fileInfo, const QMap<QString, QString> &secrets = {});
QString applyUrlTemplate(const QString &urlTemplate, const QFileInfo &fileInfo, const QMap<QString, QString> &secrets = {});
void applyHeaders(const QJsonObject &requestConfig, const QFileInfo &fileInfo, QNetworkRequest &requestObj, const QMap<QString, QString> &secrets = {});
QUrl applyQueryParameters(const QString &urlTemplate, const QJsonObject &requestConfig, const QFileInfo &fileInfo, const QMap<QString, QString> &secrets = {});
QUrl applyQueryParameters(const QString &urlTemplate, const QMap<QString, QString> &queryItems, const QFileInfo &fileInfo, const QMap<QString, QString> &secrets = {});
QByteArray createFormUrlencodedBody(const QMap<QString, QString> &fields, const QFileInfo &fileInfo, const QMap<QString, QString> &secrets = {});
QJsonValue substituteJsonValue(const QJsonValue &value, const QFileInfo &fileInfo, const QMap<QString, QString> &secrets = {});
QJsonValue resolveJsonPointer(const QJsonValue &root, const QString &pointer);
QString resolveXmlPath(const QByteArray &xmlBytes, const QString &xpath);
}
