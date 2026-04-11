#pragma once

#include "targetconfigparser.h"

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
    TargetUploader() = default;
    explicit TargetUploader(const QJsonObject &config);
    explicit TargetUploader(const ParsedTargetConfig &config);
    void setConfig(const QJsonObject &config);
    void setConfig(const ParsedTargetConfig &config);

    QString id() const;
    QString displayName() const;

    QNetworkReply *upload(const QString &filePath, QNetworkAccessManager *manager);
    UploadResult parseReply(QNetworkReply *reply) const;

private:
    ParsedTargetConfig m_targetConfig;
    bool m_requestConfigValid = false;
    bool m_responseConfigValid = false;
};
