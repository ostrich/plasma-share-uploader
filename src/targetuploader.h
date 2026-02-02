#pragma once

#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkAccessManager>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

struct UploadResult {
    bool ok = false;
    QString url;
    QString errorMessage;
};

class TargetUploader
{
public:
    explicit TargetUploader(const QJsonObject &config);

    QString id() const;
    QString displayName() const;

    QNetworkReply *upload(const QString &filePath, QNetworkAccessManager *manager);
    UploadResult parseReply(QNetworkReply *reply) const;

private:
    QJsonObject m_config;
    QRegularExpression m_responseRegex;
    int m_responseGroup = 1;
    QString m_jsonPointer;
};
