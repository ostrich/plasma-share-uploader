#pragma once

#include "preuploadprocessor.h"
#include "targetconfigparser.h"
#include "targetdefinition.h"
#include "targetuploader.h"
#include "credentialstore.h"

#include <Purpose/Job>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QStringList>

class TargetPickerDialog;
class QWindow;

class ShareJob final : public Purpose::Job
{
    Q_OBJECT
public:
    explicit ShareJob(const QByteArray &configJson, QObject *parent = nullptr, CredentialStore *credentials = nullptr);
    ~ShareJob() override;

    void start() override;

private:
    void startNextUpload();
    void prepareTarget();
    void selectTarget();
    void uploadPreparedFile();
    void publishResults();
    void cleanupTempArtifacts();
    void finishCancelled();
    void finishError(const QString &message);
    QString stageInputFiles();

    ParsedTargetConfig m_targetConfig;
    TargetUploader m_uploader;
    QStringList m_files;
    QStringList m_originalFiles;
    QList<UploadResult> m_uploadResults;
    QStringList m_uploadedUrls;
    std::unique_ptr<QTemporaryDir> m_staging;
    PreUploadProcessor::Result m_prepared;
    QPointer<QObject> m_preprocessing;
    QPointer<TargetPickerDialog> m_picker;
    QPointer<QWindow> m_pickerParent;
    bool m_started = false;
    int m_nextIndex = 0;
    QNetworkAccessManager m_network;
    CredentialStore *m_credentials;
};
