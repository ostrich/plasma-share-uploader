#pragma once

#include "targetcoreconfigparser.h"
#include "targetrequestconfigparser.h"

#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QString>

struct UploadResponseInfo {
    int statusCode = 0;
    QString reasonPhrase;
    QString responseUrl;
    QJsonObject headers;
    QString responseText;

    QJsonObject toJson() const;
};

struct UploadResult {
    bool ok = false;
    QString url;
    QString thumbnailUrl;
    QString deletionUrl;
    QString errorMessage;
    UploadResponseInfo responseInfo;

    QJsonObject toJson() const;
};

class TargetUploader
{
public:
    explicit TargetUploader(const QJsonObject &config);
    void setConfig(const QJsonObject &config);

    QString id() const;
    QString displayName() const;

    QNetworkReply *upload(const QString &filePath, QNetworkAccessManager *manager);
    UploadResult parseReply(QNetworkReply *reply) const;

private:
    QJsonObject m_config;
    ParsedTargetCoreConfig m_coreConfig;
    ParsedRequestConfig m_requestConfig;
    bool m_requestConfigValid = false;
};
