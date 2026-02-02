#pragma once

#include "targetuploader.h"

#include <Purpose/Job>
#include <QNetworkAccessManager>
#include <QJsonObject>
#include <QStringList>

class ShareJob final : public Purpose::Job
{
    Q_OBJECT
public:
    explicit ShareJob(const QByteArray &configJson, QObject *parent = nullptr);

    void start() override;

private:
    void startNextUpload();
    void finishError(const QString &message);

    QJsonObject m_targetConfig;
    TargetUploader m_uploader;
    QStringList m_files;
    QStringList m_uploadedUrls;
    int m_nextIndex = 0;
    QNetworkAccessManager m_network;
};
