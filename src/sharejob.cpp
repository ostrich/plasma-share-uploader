#include "sharejob.h"

#include <KNotification>
#include <QClipboard>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>

namespace {
QStringList collectFilePaths(const QJsonObject &data)
{
    QStringList paths;

    const QJsonValue urlsValue = data.value(QStringLiteral("urls"));
    if (urlsValue.isArray()) {
        const QJsonArray urlsArray = urlsValue.toArray();
        for (const QJsonValue &value : urlsArray) {
            const QString urlText = value.toString();
            if (urlText.isEmpty()) {
                continue;
            }
            const QUrl url = QUrl::fromUserInput(urlText, QString(), QUrl::AssumeLocalFile);
            if (!url.isLocalFile()) {
                continue;
            }
            const QString path = url.toLocalFile();
            QFileInfo info(path);
            if (info.exists() && info.isFile()) {
                paths.append(info.absoluteFilePath());
            }
        }
    }

    if (!paths.isEmpty()) {
        return paths;
    }

    const QString singleUrlText = data.value(QStringLiteral("url")).toString();
    if (singleUrlText.isEmpty()) {
        return paths;
    }

    const QUrl url = QUrl::fromUserInput(singleUrlText, QString(), QUrl::AssumeLocalFile);
    if (!url.isLocalFile()) {
        return paths;
    }

    const QString path = url.toLocalFile();
    QFileInfo info(path);
    if (info.exists() && info.isFile()) {
        paths.append(info.absoluteFilePath());
    }

    return paths;
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
    m_files = collectFilePaths(data());
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
            ? QStringLiteral("Uploaded 1 image. URL copied to clipboard.")
            : QStringLiteral("Uploaded %1 images. URLs copied to clipboard.").arg(count);
        KNotification::event(KNotification::Notification, title, text, QStringLiteral("image-x-generic"));

        emitResult();
        return;
    }

    const QString filePath = m_files.at(m_nextIndex);
    QNetworkReply *reply = m_uploader.upload(filePath, &m_network);
    if (!reply) {
        finishError(QStringLiteral("Failed to start upload for %1").arg(filePath));
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
    emitResult();
}
