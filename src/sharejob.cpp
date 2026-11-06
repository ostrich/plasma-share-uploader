#include "sharejob.h"
#include "asyncfilecopy.h"

#include "constraintmatcher.h"
#include "shareinpututils.h"
#include "targetconfigparser.h"
#include "targetpickerdialog.h"
#include "targetregistry.h"

#include <KNotification>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QTemporaryDir>
#include <QTimer>
#include <QWidget>
#include <QWindow>

namespace {
QWindow *invokingWindow()
{
    auto *window = QGuiApplication::focusWindow();
    while (window && (window->type() == Qt::Popup || window->type() == Qt::ToolTip)) {
        window = window->transientParent();
    }
    if (window) return window;
    if (auto *modal = QApplication::activeModalWidget()) return modal->windowHandle();
    if (auto *active = QApplication::activeWindow()) return active->windowHandle();

    // Closing a Share menu can clear focus before Purpose starts the job.
    // Use an unambiguous visible main window during that transition.
    QWindow *candidate = nullptr;
    const auto windows = QGuiApplication::topLevelWindows();
    for (auto *visible : windows) {
        if (!visible->isVisible() || visible->type() != Qt::Window) continue;
        if (candidate) return nullptr;
        candidate = visible;
    }
    return candidate;
}
}

ShareJob::ShareJob(const QByteArray &configJson, QObject *parent, CredentialStore *credentials)
    : Purpose::Job(parent)
    , m_uploader()
    , m_credentials(credentials ? credentials : new CredentialStore(this))
{
    setCapabilities(KJob::Killable);
    const QJsonObject configObject = QJsonDocument::fromJson(configJson).object();
    if (!configObject.isEmpty()) {
        QList<TargetDiagnostic> diagnostics;
        TargetConfigParser::parse(configObject, &m_targetConfig, &diagnostics);
        m_uploader.setConfig(m_targetConfig);
    }
}

ShareJob::~ShareJob()
{
    if (m_picker) {
        m_picker->disconnect(this);
        delete m_picker.data();
    }
    delete m_copying.data();
    delete m_preprocessing.data();
}

void ShareJob::start()
{
    if (m_phase != Phase::Idle) {
        return;
    }
    m_phase = Phase::Staging;
    m_pickerParent = invokingWindow();
    // Purpose callers may remove their temporary input as soon as start() returns.
    m_originalFiles = collectSharedFilePaths(data());
    if (m_originalFiles.isEmpty()) {
        QTimer::singleShot(0, this, [this]() { finishError(QStringLiteral("No local files found to upload.")); });
        return;
    }
    stageInputFiles();
}

void ShareJob::prepareTarget()
{
    Q_ASSERT(m_phase == Phase::Staging || m_phase == Phase::SelectingTarget);
    m_phase = Phase::ResolvingCredentials;
    m_secretValues = CredentialStore::secretValues(m_targetConfig.request, {});
    const auto missing = CredentialStore::missingEnvironment(m_targetConfig.request);
    if (!missing.isEmpty()) {
        finishError(QStringLiteral("Missing environment values: %1").arg(missing.join(QStringLiteral(", "))));
        return;
    }
    m_credentials->read(CredentialStore::walletKeys(m_targetConfig.request), this,
        [this](CredentialStore::Values values, const QString &error) {
            if (m_phase != Phase::ResolvingCredentials) return;
            if (!error.isEmpty()) { finishError(error); return; }
            m_uploader.setConfig(m_targetConfig);
            m_uploader.setSecrets(values);
            m_secretValues = CredentialStore::secretValues(m_targetConfig.request, values);
            startNextUpload();
        });
}

void ShareJob::publishResults()
{
    QJsonObject output;
    QJsonArray resultsArray;
    QJsonArray thumbnailUrls;
    QJsonArray deletionUrls;
    for (const UploadResult &result : std::as_const(m_uploadResults)) {
        resultsArray.append(result.toJson());
        if (!result.thumbnailUrl.isEmpty()) {
            thumbnailUrls.append(result.thumbnailUrl);
        }
        if (!result.deletionUrl.isEmpty()) {
            deletionUrls.append(result.deletionUrl);
        }
    }

    output.insert(QStringLiteral("results"), resultsArray);
    output.insert(QStringLiteral("urls"), QJsonArray::fromStringList(m_uploadedUrls));
    if (!m_uploadedUrls.isEmpty()) {
        output.insert(QStringLiteral("url"), m_uploadedUrls.first());
    }
    if (!thumbnailUrls.isEmpty()) {
        output.insert(QStringLiteral("thumbnailUrls"), thumbnailUrls);
        output.insert(QStringLiteral("thumbnailUrl"), thumbnailUrls.first());
    }
    if (!deletionUrls.isEmpty()) {
        output.insert(QStringLiteral("deletionUrls"), deletionUrls);
        output.insert(QStringLiteral("deletionUrl"), deletionUrls.first());
    }
    setOutput(output);

    if (!m_uploadedUrls.isEmpty()) {
        const QString clipboardText = m_uploadedUrls.join(QStringLiteral("\n"));
        if (QClipboard *clipboard = QGuiApplication::clipboard()) {
            clipboard->setText(clipboardText, QClipboard::Clipboard);
        }
    }
}

void ShareJob::startNextUpload()
{
    if (m_phase == Phase::Finished) return;
    Q_ASSERT(m_phase == Phase::ResolvingCredentials || m_phase == Phase::Uploading);
    if (m_nextIndex >= m_files.size()) {
        m_phase = Phase::Finished;
        publishResults();

        const int count = m_uploadedUrls.size();
        const QString title = QStringLiteral("%1 Upload").arg(m_uploader.displayName());
        const QString text = count == 1
            ? QStringLiteral("Uploaded 1 file. URL copied to clipboard.")
            : QStringLiteral("Uploaded %1 files. URLs copied to clipboard.").arg(count);
        KNotification::event(KNotification::Notification, title, text, QStringLiteral("image-x-generic"));

        cleanupTempArtifacts();
        emitResult();
        return;
    }

    m_phase = Phase::Preprocessing;
    m_preprocessing = PreUploadProcessor::preprocessFileAsync(
        m_targetConfig.preUpload, m_files.at(m_nextIndex), this,
        [this](PreUploadProcessor::Result prepared) {
            if (m_phase != Phase::Preprocessing) return;
            m_prepared = std::move(prepared);
            if (!m_prepared.ok) {
                finishError(m_prepared.errorMessage);
                return;
            }
            uploadPreparedFile();
        });
}

void ShareJob::uploadPreparedFile()
{
    if (m_phase != Phase::Preprocessing) return;
    m_phase = Phase::Uploading;
    const QString sourcePath = m_originalFiles.value(m_nextIndex, m_files.at(m_nextIndex));
    QNetworkReply *reply = m_uploader.upload(m_prepared.uploadPath, &m_network);
    if (!reply) {
        finishError(QStringLiteral("%1: %2").arg(sourcePath, m_uploader.lastError()));
        return;
    }

    m_reply = reply;
    connect(reply, &QNetworkReply::uploadProgress, this, [this](qint64 sent, qint64 total) {
        if (total > 0) setPercent(20 + 80 * (m_nextIndex + double(sent) / total) / m_files.size());
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (m_phase != Phase::Uploading) { reply->deleteLater(); return; }
        UploadResult result = m_uploader.parseReply(reply);
        result.errorMessage = CredentialStore::redact(result.errorMessage, m_secretValues);
        result.responseInfo.responseText = CredentialStore::redact(result.responseInfo.responseText, m_secretValues);
        result.responseInfo.responseUrl = CredentialStore::redact(result.responseInfo.responseUrl, m_secretValues);
        result.responseInfo.reasonPhrase = CredentialStore::redact(result.responseInfo.reasonPhrase, m_secretValues);
        for (auto it = result.responseInfo.headers.begin(); it != result.responseInfo.headers.end(); ++it)
            it.value() = it.key().compare(QStringLiteral("set-cookie"), Qt::CaseInsensitive) == 0
                ? QStringLiteral("[redacted]") : CredentialStore::redact(it.value().toString(), m_secretValues);
        reply->deleteLater();

        if (!result.ok) {
            finishError(result.errorMessage);
            return;
        }

        m_uploadResults.append(result);
        m_uploadedUrls.append(result.url);
        ++m_nextIndex;
        startNextUpload();
    });
}

void ShareJob::finishError(const QString &message)
{
    const auto safeMessage = CredentialStore::redact(message, m_secretValues);
    if (m_phase == Phase::Finished) return;
    m_phase = Phase::Finished;
    setError(1);
    publishResults();
    const QString details = m_uploadedUrls.isEmpty() ? safeMessage
        : QStringLiteral("%1\nUploaded %2 of %3 files before the failure. Completed URLs copied to clipboard.")
              .arg(safeMessage).arg(m_uploadedUrls.size()).arg(m_files.size());
    setErrorText(details);
    KNotification::event(KNotification::Error,
                         QStringLiteral("%1 Upload Failed").arg(m_uploader.displayName()),
                         details,
                         QStringLiteral("dialog-error"));
    cleanupTempArtifacts();
    emitResult();
}

void ShareJob::finishCancelled()
{
    if (m_phase == Phase::Finished) return;
    m_phase = Phase::Finished;
    setError(0);
    setErrorText(QStringLiteral("Upload cancelled."));
    cleanupTempArtifacts();
    emitResult();
}

void ShareJob::cleanupTempArtifacts()
{
    m_prepared = {};
    m_staging.reset();
}

void ShareJob::stageInputFiles()
{
    m_staging = std::make_shared<QTemporaryDir>(QDir::tempPath() + QStringLiteral("/plasma-share-staging-XXXXXX"));
    m_copying = AsyncFileCopy::start(m_originalFiles, m_staging, this, [this](AsyncFileCopy::Result result) {
        m_copying.clear();
        if (m_phase != Phase::Staging) return;
        if (!result.error.isEmpty()) { finishError(result.error); return; }
        m_files = result.files;
        setPercent(20);
        if (m_phase != Phase::Staging) return;
        if (m_targetConfig.core.id.isEmpty()) selectTarget();
        else prepareTarget();
    }, [this](qint64 copied, qint64 total) {
        if (total > 0) setPercent(20.0 * copied / total);
    });
}

bool ShareJob::doKill()
{
    if (m_phase == Phase::Finished) return false;
    m_phase = Phase::Finished;
    if (m_picker) { m_picker->disconnect(this); delete m_picker.data(); }
    delete m_copying.data();
    delete m_preprocessing.data();
    if (m_reply) { m_reply->disconnect(this); m_reply->abort(); m_reply->deleteLater(); }
    publishResults();
    cleanupTempArtifacts();
    return true;
}

void ShareJob::selectTarget()
{
    m_phase = Phase::SelectingTarget;
    if (!m_pickerParent) m_pickerParent = invokingWindow();
    TargetRegistry registry;
    const TargetRegistry::LoadResult loadResult = registry.loadTargets();
    const QList<TargetDefinition> compatibleTargets = ConstraintMatcher::filterTargets(loadResult.targets, m_files);

    m_picker = new TargetPickerDialog(compatibleTargets, loadResult.diagnostics, m_pickerParent, this);
    connect(m_picker, &TargetPickerDialog::reloadRequested, this, [this]() {
        m_picker->disconnect(this);
        m_picker->hide();
        m_picker->deleteLater();
        m_picker.clear();
        selectTarget();
    });
    connect(m_picker, &TargetPickerDialog::loadFailed, this, [this](const QString &error) {
        m_picker->deleteLater(); m_picker.clear(); finishError(error);
    });
    connect(m_picker, &TargetPickerDialog::finished, this, [this](bool accepted) {
        const TargetDefinition selectedTarget = m_picker->selectedTarget();
        m_picker->hide();
        m_picker->deleteLater();
        m_picker.clear();
        if (!accepted) {
            finishCancelled();
        } else if (selectedTarget.id().isEmpty()) {
            finishError(QStringLiteral("No upload target selected."));
        } else {
            m_targetConfig = selectedTarget.target;
            prepareTarget();
        }
    });
    m_picker->open();
}
