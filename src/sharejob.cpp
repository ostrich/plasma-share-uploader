#include "sharejob.h"

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

ShareJob::ShareJob(const QByteArray &configJson, QObject *parent, CredentialStore *credentials)
    : Purpose::Job(parent)
    , m_uploader()
    , m_credentials(credentials ? credentials : new CredentialStore(this))
{
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
    delete m_preprocessing.data();
}

void ShareJob::start()
{
    if (m_started) {
        return;
    }
    m_started = true;
    // Purpose callers may remove their temporary input as soon as start() returns.
    m_originalFiles = collectSharedFilePaths(data());
    const QString error = m_originalFiles.isEmpty()
        ? QStringLiteral("No local files found to upload.") : stageInputFiles();
    // Purpose sets its Running state after calling start(), so completion must be deferred.
    QTimer::singleShot(0, this, [this, error]() {
        if (!error.isEmpty()) {
            finishError(error);
        } else if (m_targetConfig.core.id.isEmpty()) {
            selectTarget();
        } else {
            prepareTarget();
        }
    });
}

void ShareJob::prepareTarget()
{
    const auto missing = CredentialStore::missingEnvironment(m_targetConfig.request);
    if (!missing.isEmpty()) {
        finishError(QStringLiteral("Missing environment values: %1").arg(missing.join(QStringLiteral(", "))));
        return;
    }
    m_credentials->read(CredentialStore::walletKeys(m_targetConfig.request), this,
        [this](CredentialStore::Values values, const QString &error) {
            if (!error.isEmpty()) { finishError(error); return; }
            m_uploader.setConfig(m_targetConfig);
            m_uploader.setSecrets(values);
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
    if (m_nextIndex >= m_files.size()) {
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

    m_preprocessing = PreUploadProcessor::preprocessFileAsync(
        m_targetConfig.preUpload, m_files.at(m_nextIndex), this,
        [this](PreUploadProcessor::Result prepared) {
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
    const QString sourcePath = m_originalFiles.value(m_nextIndex, m_files.at(m_nextIndex));
    QNetworkReply *reply = m_uploader.upload(m_prepared.uploadPath, &m_network);
    if (!reply) {
        finishError(QStringLiteral("%1: %2").arg(sourcePath, m_uploader.lastError()));
        return;
    }

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const UploadResult result = m_uploader.parseReply(reply);
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
    setError(1);
    publishResults();
    const QString details = m_uploadedUrls.isEmpty() ? message
        : QStringLiteral("%1\nUploaded %2 of %3 files before the failure. Completed URLs copied to clipboard.")
              .arg(message).arg(m_uploadedUrls.size()).arg(m_files.size());
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

QString ShareJob::stageInputFiles()
{
    m_files.clear();
    m_staging = std::make_unique<QTemporaryDir>(QDir::tempPath() + QStringLiteral("/plasma-share-staging-XXXXXX"));
    if (!m_staging->isValid()) {
        return QStringLiteral("Failed to create temporary directory for upload staging.");
    }
    const QString stagingRoot = m_staging->path();

    for (int i = 0; i < m_originalFiles.size(); ++i) {
        const QString originalPath = m_originalFiles.at(i);
        const QFileInfo originalInfo(originalPath);
        const QString subdirPath = QDir(stagingRoot).filePath(QString::number(i));
        if (!QDir().mkpath(subdirPath)) {
            return QStringLiteral("Failed to prepare temporary upload staging directory.");
        }

        const QString stagedPath = QDir(subdirPath).filePath(originalInfo.fileName());
        if (!QFile::copy(originalPath, stagedPath)) {
            return QStringLiteral("Failed to prepare temporary upload copy for %1").arg(originalPath);
        }
        m_files.append(stagedPath);
    }

    return {};
}

void ShareJob::selectTarget()
{
    TargetRegistry registry;
    const TargetRegistry::LoadResult loadResult = registry.loadTargets();
    const QList<TargetDefinition> compatibleTargets = ConstraintMatcher::filterTargets(loadResult.targets, m_files);

    QWidget *parentWidget = QApplication::activeWindow();
    m_picker = new TargetPickerDialog(compatibleTargets, loadResult.diagnostics, parentWidget);
    connect(m_picker, &TargetPickerDialog::reloadRequested, this, [this]() {
        m_picker->disconnect(this);
        m_picker->hide();
        m_picker->deleteLater();
        m_picker.clear();
        selectTarget();
    });
    connect(m_picker, &QDialog::finished, this, [this](int result) {
        const TargetDefinition selectedTarget = m_picker->selectedTarget();
        m_picker->deleteLater();
        m_picker.clear();
        if (result != QDialog::Accepted) {
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
