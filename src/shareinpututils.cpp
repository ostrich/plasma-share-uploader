#include "shareinpututils.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonValue>
#include <QUrl>

namespace {
void appendLocalFilePath(QStringList &paths, const QString &urlText)
{
    if (urlText.isEmpty()) {
        return;
    }

    const QUrl url = QUrl::fromUserInput(urlText, QString(), QUrl::AssumeLocalFile);
    if (!url.isLocalFile()) {
        return;
    }

    const QString path = url.toLocalFile();
    QFileInfo info(path);
    if (info.exists() && info.isFile()) {
        paths.append(info.absoluteFilePath());
    }
}
}

QStringList collectSharedFilePaths(const QJsonObject &data)
{
    QStringList paths;

    const QJsonValue urlsValue = data.value(QStringLiteral("urls"));
    if (urlsValue.isArray()) {
        const QJsonArray urlsArray = urlsValue.toArray();
        for (const QJsonValue &value : urlsArray) {
            appendLocalFilePath(paths, value.toString());
        }
    }

    if (!paths.isEmpty()) {
        return paths;
    }

    appendLocalFilePath(paths, data.value(QStringLiteral("url")).toString());
    return paths;
}
