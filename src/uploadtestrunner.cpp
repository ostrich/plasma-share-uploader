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
    const auto previous = m_generation;
    clearPreview();
    if (busy() || previous != m_generation) return;
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
    const auto run = ++m_generation;
    m_run = new QObject(this);
    m_phase = previewOnly ? Phase::Preprocessing : Phase::ResolvingCredentials;
    emit busyChanged(true);
    if (!current(run)) return; // Signals may cancel and start a replacement run.
    if (previewOnly) prepare(config, true, {}, run);
    else {
        emit message(tr("Resolving credentials..."));
        if (!current(run)) return;
        m_credentials->read(CredentialStore::walletKeys(config.request), m_run,
            [this, config, run](CredentialStore::Values values, const QString &error) {
                if (!current(run) || m_phase != Phase::ResolvingCredentials) return;
                if (!error.isEmpty()) { emit message(error); finish(run); return; }
                prepare(config, false, values, run);
            });
    }
}

void UploadTestRunner::prepare(const ParsedTargetConfig &config, bool previewOnly, const CredentialStore::Values &values, quint64 run)
{
    if (!current(run)) return;
    Q_ASSERT(m_phase == Phase::ResolvingCredentials || m_phase == Phase::Preprocessing);
    m_phase = Phase::Preprocessing;
    m_secrets = CredentialStore::secretValues(config.request, values);
    emit message(tr("Preparing a temporary copy of %1 (%2 bytes)...").arg(QFileInfo(m_file).fileName()).arg(QFileInfo(m_file).size()));
    if (!current(run)) return;
    PreUploadProcessor::preprocessFileAsync(config.preUpload, m_file, m_run,
        [this, config, previewOnly, values, run](PreUploadProcessor::Result prepared) {
            if (!current(run) || m_phase != Phase::Preprocessing) return;
            m_prepared = std::move(prepared);
            if (!m_prepared.ok) { emit message(CredentialStore::redact(m_prepared.errorMessage, m_secrets)); finish(run); return; }
            const auto info = QFileInfo(m_prepared.uploadPath);
            emit message(tr("Prepared %1: %2 bytes, %3").arg(info.fileName()).arg(info.size()).arg(QMimeDatabase().mimeTypeForFile(info).name()));
            if (!current(run)) return;
            if (previewOnly) {
                m_previewPath = m_prepared.uploadPath;
                emit previewChanged();
                if (!current(run)) return;
                const auto path = m_previewPath;
                emit previewReady(path);
                finish(run);
                return;
            }
            auto uploader = std::make_shared<TargetUploader>(config); uploader->setSecrets(values);
            m_phase = Phase::Uploading;
            auto *network = new QNetworkAccessManager(m_run);
            auto *reply = uploader->upload(m_prepared.uploadPath, network);
            if (!reply) { emit message(uploader->lastError()); finish(run); return; }
            constexpr qint64 limit = 1024 * 1024;
            reply->setReadBufferSize(limit + 1);
            connect(reply, &QNetworkReply::readyRead, m_run, [reply]() {
                if (reply->bytesAvailable() > limit) { reply->setProperty("responseTooLarge", true); reply->abort(); }
            });
            connect(reply, &QNetworkReply::uploadProgress, m_run, [this, run](qint64 sent, qint64 total) {
                if (current(run)) emit progress(sent, total);
            });
            connect(reply, &QNetworkReply::finished, m_run, [this, reply, uploader, run]() {
                if (!current(run)) return;
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
                emit completed(result); finish(run);
            });
        });
}

void UploadTestRunner::finish(quint64 run)
{
    if (!current(run)) return;
    if (m_run) { m_run->deleteLater(); m_run.clear(); }
    m_phase = Phase::Idle;
    emit busyChanged(false);
}

void UploadTestRunner::clearPreview()
{
    if (busy()) return;
    m_prepared = {};
    m_stage.reset();
    if (m_previewPath.isEmpty()) return;
    m_previewPath.clear();
    emit previewChanged();
}

void UploadTestRunner::cancel()
{
    if (!busy()) return;
    const auto generation = ++m_generation;
    m_phase = Phase::Idle;
    auto *run = m_run.data();
    m_run.clear();
    delete run;
    m_prepared = {}; m_stage.reset(); m_previewPath.clear();
    emit busyChanged(false);
    if (generation != m_generation) return;
    emit previewChanged();
    if (generation != m_generation) return;
    emit message(tr("Test cancelled. A remote upload may already have completed."));
}
