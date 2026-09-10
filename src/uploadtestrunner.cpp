#include "uploadtestrunner.h"
#include "constraintmatcher.h"
#include "targetfilestore.h"
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QMimeDatabase>
#include <QNetworkAccessManager>

UploadTestRunner::UploadTestRunner(QObject *parent, CredentialStore *credentials)
    : QObject(parent), m_credentials(credentials ? credentials : new CredentialStore(this)) {}
UploadTestRunner::~UploadTestRunner() { delete m_run.data(); }

void UploadTestRunner::start(const QJsonObject &object, const QString &file, bool previewOnly)
{
    if (busy()) return;
    const auto errors = TargetFileStore::validate(QJsonDocument(object).toJson());
    if (!errors.isEmpty()) { emit message(errors.join(QLatin1Char('\n'))); return; }
    ParsedTargetConfig config; TargetConfigParser::parse(object, &config);
    if (!previewOnly) {
        const auto missing = CredentialStore::missingEnvironment(config.request);
        if (!missing.isEmpty()) { emit message(tr("Missing environment values: %1").arg(missing.join(QStringLiteral(", ")))); return; }
    }
    m_prepared = {}; m_secrets.clear(); m_stage = std::make_unique<QTemporaryDir>();
    if (!m_stage->isValid()) { emit message(tr("Could not create a test directory.")); return; }
    m_file = m_stage->filePath(file.isEmpty() ? QStringLiteral("test.png") : QFileInfo(file).fileName());
    if (file.isEmpty()) {
        QImage sample(32, 32, QImage::Format_RGB32); sample.fill(QColor(70, 110, 170));
        if (!sample.save(m_file)) { emit message(tr("Could not create the test image.")); return; }
    } else if (!QFile::copy(file, m_file)) { emit message(tr("Could not copy the selected file for testing.")); return; }
    if (!previewOnly && ConstraintMatcher::filterTargets({TargetDefinition{config}}, {m_file}).isEmpty()) {
        emit message(tr("The sample does not match this target's file constraints.")); return;
    }
    m_run = new QObject(this); emit busyChanged(true);
    if (previewOnly) prepare(config, true, {});
    else {
        emit message(tr("Resolving credentials..."));
        m_credentials->read(CredentialStore::walletKeys(config.request), m_run,
            [this, config](CredentialStore::Values values, const QString &error) {
                if (!error.isEmpty()) { emit message(error); finish(); return; }
                prepare(config, false, values);
            });
    }
}

void UploadTestRunner::prepare(const ParsedTargetConfig &config, bool previewOnly, const CredentialStore::Values &values)
{
    m_secrets = CredentialStore::secretValues(config.request, values);
    emit message(tr("Preparing a temporary copy of %1 (%2 bytes)...").arg(QFileInfo(m_file).fileName()).arg(QFileInfo(m_file).size()));
    PreUploadProcessor::preprocessFileAsync(config.preUpload, m_file, m_run,
        [this, config, previewOnly, values](PreUploadProcessor::Result prepared) {
            m_prepared = std::move(prepared);
            if (!m_prepared.ok) { emit message(CredentialStore::redact(m_prepared.errorMessage, m_secrets)); finish(); return; }
            const auto info = QFileInfo(m_prepared.uploadPath);
            emit message(tr("Prepared %1: %2 bytes, %3").arg(info.fileName()).arg(info.size()).arg(QMimeDatabase().mimeTypeForFile(info).name()));
            if (previewOnly) { emit previewReady(m_prepared.uploadPath); finish(); return; }
            auto uploader = std::make_shared<TargetUploader>(config); uploader->setSecrets(values);
            auto *network = new QNetworkAccessManager(m_run);
            auto *reply = uploader->upload(m_prepared.uploadPath, network);
            if (!reply) { emit message(uploader->lastError()); finish(); return; }
            constexpr qint64 limit = 1024 * 1024;
            reply->setReadBufferSize(limit + 1);
            connect(reply, &QNetworkReply::readyRead, m_run, [reply]() {
                if (reply->bytesAvailable() > limit) { reply->setProperty("responseTooLarge", true); reply->abort(); }
            });
            connect(reply, &QNetworkReply::uploadProgress, this, &UploadTestRunner::progress);
            connect(reply, &QNetworkReply::finished, m_run, [this, reply, uploader]() {
                auto result = uploader->parseReply(reply);
                if (reply->property("responseTooLarge").toBool()) { result.ok = false; result.errorMessage = tr("Test stopped: response exceeds 1 MiB."); }
                result.url = CredentialStore::redact(result.url, m_secrets);
                result.thumbnailUrl = CredentialStore::redact(result.thumbnailUrl, m_secrets);
                result.deletionUrl = CredentialStore::redact(result.deletionUrl, m_secrets);
                result.errorMessage = CredentialStore::redact(result.errorMessage, m_secrets);
                result.responseInfo.responseText = CredentialStore::redact(result.responseInfo.responseText.left(1024 * 1024), m_secrets);
                result.responseInfo.responseUrl = CredentialStore::redact(result.responseInfo.responseUrl, m_secrets);
                for (auto it = result.responseInfo.headers.begin(); it != result.responseInfo.headers.end(); ++it) {
                    if (it.key().compare(QStringLiteral("set-cookie"), Qt::CaseInsensitive) == 0) it.value() = QStringLiteral("[redacted]");
                    else it.value() = CredentialStore::redact(it.value().toString(), m_secrets);
                }
                emit completed(result); finish();
            });
        });
}

void UploadTestRunner::finish()
{
    if (m_run) { m_run->deleteLater(); m_run.clear(); }
    emit busyChanged(false);
}

void UploadTestRunner::cancel()
{
    if (!busy()) return;
    delete m_run.data(); m_run.clear(); m_prepared = {}; m_stage.reset();
    emit message(tr("Test cancelled. A remote upload may already have completed."));
    emit busyChanged(false);
}
