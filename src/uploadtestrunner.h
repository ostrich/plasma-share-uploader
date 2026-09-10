#pragma once
#include "credentialstore.h"
#include "preuploadprocessor.h"
#include "targetuploader.h"
#include <QPointer>

class UploadTestRunner final : public QObject
{
    Q_OBJECT
public:
    explicit UploadTestRunner(QObject *parent = nullptr, CredentialStore *credentials = nullptr);
    ~UploadTestRunner() override;
    bool busy() const { return !m_run.isNull(); }
    void start(const QJsonObject &config, const QString &file, bool previewOnly);
    void cancel();
signals:
    void busyChanged(bool busy);
    void message(const QString &text);
    void progress(qint64 sent, qint64 total);
    void completed(const UploadResult &result);
    void previewReady(const QString &path);
private:
    void prepare(const ParsedTargetConfig &config, bool previewOnly, const CredentialStore::Values &values);
    void finish();
    CredentialStore *m_credentials;
    QPointer<QObject> m_run;
    std::unique_ptr<QTemporaryDir> m_stage;
    PreUploadProcessor::Result m_prepared;
    QString m_file;
    QStringList m_secrets;
};
