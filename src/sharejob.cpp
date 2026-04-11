#include "sharejob.h"

#include "shareinpututils.h"

#include <KNotification>
#include <QClipboard>
#include <QDir>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>

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
        finishError(QStringLiteral("Missing upload target configuration."));
        return;
    }

    startNextUpload();
}

void ShareJob::startNextUpload()
{
    if (m_nextIndex >= m_files.size()) {
        QJsonObject output;
        output.insert(QStringLiteral("urls"), QJsonArray::fromStringList(m_uploadedUrls));
        if (!m_uploadedUrls.isEmpty()) {
            output.insert(QStringLiteral("url"), m_uploadedUrls.first());
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
        if (error != QNetworkReply::NoError) {
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
