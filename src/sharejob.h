#pragma once

#include "preuploadprocessor.h"
#include "targetconfigparser.h"
#include "targetdefinition.h"
#include "targetuploader.h"

#include <Purpose/Job>
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

    ParsedTargetConfig m_targetConfig;
    TargetUploader m_uploader;
    QStringList m_files;
    QList<UploadResult> m_uploadResults;
    QStringList m_uploadedUrls;
    QStringList m_tempDirs;
    int m_nextIndex = 0;
    QNetworkAccessManager m_network;
};
