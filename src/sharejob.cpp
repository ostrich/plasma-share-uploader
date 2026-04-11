#include "sharejob.h"

#include "constraintmatcher.h"
#include "shareinpututils.h"
#include "targetpickerdialog.h"
#include "targetregistry.h"

#include <KNotification>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QNetworkReply>

namespace {
QString diagnosticsText(const QList<TargetDiagnostic> &diagnostics)
{
    QStringList lines;
    lines.reserve(diagnostics.size());
    for (const TargetDiagnostic &diagnostic : diagnostics) {
        lines.append(diagnostic.displayText());
    }
    return lines.join(QLatin1Char('\n'));
}
}

ShareJob::ShareJob(const QByteArray &configJson, QObject *parent)
    : Purpose::Job(parent)
    , m_targetConfig(QJsonDocument::fromJson(configJson).object())
    , m_uploader(m_targetConfig)
{
}

void ShareJob::start()
{
    m_files = collectSharedFilePaths(data());
    if (m_files.isEmpty()) {
        finishError(QStringLiteral("No local files found to upload."));
        return;
    }

    if (m_targetConfig.isEmpty()) {
        if (!ensureTargetSelected()) {
            return;
        }
    } else {
        m_uploader.setConfig(m_targetConfig);
    }

    if (m_targetConfig.isEmpty()) {
        finishError(QStringLiteral("Missing upload target configuration."));
        return;
    }

    startNextUpload();
}

void ShareJob::startNextUpload()
{
    if (m_nextIndex >= m_files.size()) {
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

    const QString originalPath = m_files.at(m_nextIndex);
    const PreUploadProcessor::Result prepared = PreUploadProcessor::preprocessFile(m_targetConfig, originalPath);
    if (!prepared.ok) {
        finishError(prepared.errorMessage);
        return;
    }
    if (!prepared.tempDirPath.isEmpty()) {
        m_tempDirs.append(prepared.tempDirPath);
    }

    QNetworkReply *reply = m_uploader.upload(prepared.uploadPath, &m_network);
    if (!reply) {
        finishError(QStringLiteral("Failed to start upload for %1").arg(originalPath));
        return;
    }

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const QNetworkReply::NetworkError error = reply->error();
        const bool hasHttpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).isValid();
        if (error != QNetworkReply::NoError && !hasHttpStatus) {
            const QString message = reply->errorString();
            reply->deleteLater();
            finishError(message);
            return;
        }

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
    setErrorText(message);
    KNotification::event(KNotification::Error,
                         QStringLiteral("%1 Upload Failed").arg(m_uploader.displayName()),
                         message,
                         QStringLiteral("dialog-error"));
    cleanupTempArtifacts();
    emitResult();
}

void ShareJob::cleanupTempArtifacts()
{
    for (const QString &path : std::as_const(m_tempDirs)) {
        QDir(path).removeRecursively();
    }
    m_tempDirs.clear();
}

bool ShareJob::ensureTargetSelected()
{
    TargetRegistry registry;
    const QString systemTargetsPath = registry.systemTargetsPath();
    const QString userTargetsPath = registry.userTargetsPath();
    const TargetRegistry::LoadResult loadResult = registry.loadTargets();
    const QList<TargetDefinition> compatibleTargets = ConstraintMatcher::filterTargets(loadResult.targets, m_files);

    if (compatibleTargets.isEmpty()) {
        QString message = QStringLiteral("No compatible upload targets available.");
        message.append(QStringLiteral("\n\nSystem targets: %1").arg(systemTargetsPath));
        message.append(QStringLiteral("\nUser targets: %1").arg(userTargetsPath));
        if (!loadResult.diagnostics.isEmpty()) {
            message.append(QStringLiteral("\n\n"));
            message.append(diagnosticsText(loadResult.diagnostics));
        }
        finishError(message);
        return false;
    }

    QWidget *parentWidget = QApplication::activeWindow();
    TargetPickerDialog dialog(compatibleTargets, loadResult.diagnostics, systemTargetsPath, userTargetsPath, parentWidget);
    if (dialog.exec() != QDialog::Accepted) {
        finishError(QStringLiteral("Upload cancelled."));
        return false;
    }

    const TargetDefinition selectedTarget = dialog.selectedTarget();
    if (selectedTarget.config.isEmpty()) {
        finishError(QStringLiteral("No upload target selected."));
        return false;
    }

    m_targetConfig = selectedTarget.config;
    m_uploader.setConfig(m_targetConfig);
    return true;
}
