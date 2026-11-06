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
    bool busy() const { return m_phase != Phase::Idle; }
    void start(const QJsonObject &config, const QString &file, bool previewOnly);
    void cancel();
    void clearPreview();
    QString previewPath() const { return m_previewPath; }
signals:
    void busyChanged(bool busy);
    void message(const QString &text);
    void progress(qint64 sent, qint64 total);
    void completed(const UploadResult &result);
    void previewChanged();
    void previewReady(const QString &path);
private:
    void staged(const ParsedTargetConfig &config, bool previewOnly, quint64 run);
    void prepare(const ParsedTargetConfig &config, bool previewOnly, const CredentialStore::Values &values, quint64 run);
    void finish(quint64 run);
    bool current(quint64 run) const { return busy() && run == m_generation; }
    quint64 m_generation = 0;
    QString m_previewPath;
    // Completing or cancelling returns to Idle; prepared previews remain owned separately.
    enum class Phase { Idle, Staging, ResolvingCredentials, Preprocessing, Uploading };
    Phase m_phase = Phase::Idle;
    CredentialStore *m_credentials;
    QPointer<QObject> m_run;
    std::shared_ptr<QTemporaryDir> m_stage;
    PreUploadProcessor::Result m_prepared;
    QString m_file;
    QStringList m_secrets;
};
