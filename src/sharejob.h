#pragma once

#include "preuploadprocessor.h"
#include "targetdefinition.h"
#include "targetuploader.h"

#include <Purpose/Job>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QStringList>

class ShareJob final : public Purpose::Job
{
    Q_OBJECT
public:
    explicit ShareJob(const QByteArray &configJson, QObject *parent = nullptr);

    void start() override;

private:
    void startNextUpload();
    bool ensureTargetSelected();
    void cleanupTempArtifacts();
    void finishError(const QString &message);

    QJsonObject m_targetConfig;
    TargetUploader m_uploader;
    QStringList m_files;
    QStringList m_uploadedUrls;
    QStringList m_tempDirs;
    int m_nextIndex = 0;
    QNetworkAccessManager m_network;
};
