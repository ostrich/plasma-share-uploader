#include "targetuploader.h"

#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QNetworkRequest>
#include <QProcessEnvironment>

namespace {
constexpr int kUploadTimeoutMs = 30000;

QJsonObject objectValue(const QJsonObject &parent, const char *key)
{
    const QJsonValue value = parent.value(QLatin1StringView(key));
    return value.isObject() ? value.toObject() : QJsonObject();
}

QString stringValue(const QJsonObject &parent, const char *key)
{
    return parent.value(QLatin1StringView(key)).toString();
}

QJsonObject fieldMap(const QJsonObject &parent)
{
    const QJsonValue value = parent.value(QLatin1StringView("fields"));
    return value.isObject() ? value.toObject() : QJsonObject();
}

QString substituteEnv(const QString &value)
{
    static const QRegularExpression pattern(QStringLiteral(R"(\$\{ENV:([A-Za-z_][A-Za-z0-9_]*)\})"));
    QString result = value;
    QRegularExpressionMatch match = pattern.match(result);
    while (match.hasMatch()) {
        const QString varName = match.captured(1);
        const QString envValue = QProcessEnvironment::systemEnvironment().value(varName);
        result.replace(match.captured(0), envValue);
        match = pattern.match(result);
    }
    return result;
}

QString applyUrlTemplate(const QString &urlTemplate, const QFileInfo &fileInfo)
{
    QString url = substituteEnv(urlTemplate);
    const QByteArray encodedName = QUrl::toPercentEncoding(fileInfo.fileName());
    url.replace(QStringLiteral("${FILENAME}"), QString::fromUtf8(encodedName));
    return url;
}

void applyHeaders(const QJsonObject &requestConfig, QNetworkRequest &requestObj)
{
    const QJsonValue headersValue = requestConfig.value(QLatin1StringView("headers"));
    if (!headersValue.isObject()) {
        return;
    }
    const QJsonObject headers = headersValue.toObject();
    for (auto it = headers.begin(); it != headers.end(); ++it) {
        const QByteArray name = it.key().toUtf8();
        const QString rawValue = it.value().toString();
        const QByteArray value = substituteEnv(rawValue).toUtf8();
        requestObj.setRawHeader(name, value);
    }
}

QString decodeJsonPointerToken(const QString &token)
{
    QString out = token;
    out.replace(QStringLiteral("~1"), QStringLiteral("/"));
    out.replace(QStringLiteral("~0"), QStringLiteral("~"));
    return out;
}

QJsonValue resolveJsonPointer(const QJsonValue &root, const QString &pointer)
{
    if (pointer.isEmpty() || pointer == QStringLiteral("/")) {
        return root;
    }

    if (!pointer.startsWith(QLatin1Char('/'))) {
        return QJsonValue();
    }

    QJsonValue current = root;
    const QStringList parts = pointer.mid(1).split(QLatin1Char('/'), Qt::KeepEmptyParts);
    for (const QString &part : parts) {
        const QString key = decodeJsonPointerToken(part);
        if (current.isObject()) {
            current = current.toObject().value(key);
        } else if (current.isArray()) {
            bool ok = false;
            const int index = key.toInt(&ok);
            if (!ok) {
                return QJsonValue();
            }
            const QJsonArray array = current.toArray();
            if (index < 0 || index >= array.size()) {
                return QJsonValue();
            }
            current = array.at(index);
        } else {
            return QJsonValue();
        }
    }

    return current;
}
}

TargetUploader::TargetUploader(const QJsonObject &config)
    : m_config(config)
{
    const QJsonObject response = objectValue(m_config, "response");
    if (response.value(QLatin1StringView("type")).toString() == QLatin1StringView("regex")) {
        const QString pattern = response.value(QLatin1StringView("pattern")).toString();
        m_responseRegex = QRegularExpression(pattern);
        m_responseGroup = response.value(QLatin1StringView("group")).toInt(1);
    }

    if (response.value(QLatin1StringView("type")).toString() == QLatin1StringView("json_pointer")) {
        m_jsonPointer = response.value(QLatin1StringView("pointer")).toString();
    }
}

QString TargetUploader::id() const
{
    return stringValue(m_config, "id");
}

QString TargetUploader::displayName() const
{
    const QString name = stringValue(m_config, "displayName");
    return name.isEmpty() ? id() : name;
}

QNetworkReply *TargetUploader::upload(const QString &filePath, QNetworkAccessManager *manager)
{
    if (!manager) {
        return nullptr;
    }

    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        return nullptr;
    }

    const QJsonObject request = objectValue(m_config, "request");
    const QString urlTemplate = stringValue(request, "url");
    const QString method = stringValue(request, "method").toUpper();
    const QString requestType = stringValue(request, "type").isEmpty()
        ? QStringLiteral("multipart")
        : stringValue(request, "type");
    if (urlTemplate.isEmpty() || method.isEmpty()) {
        return nullptr;
    }

    const QString url = applyUrlTemplate(urlTemplate, fileInfo);
    QNetworkRequest requestObj{QUrl::fromUserInput(url)};
    requestObj.setHeader(
        QNetworkRequest::UserAgentHeader,
        QStringLiteral("plasma-share-uploader/" PLASMA_SHARE_UPLOADER_VERSION));
    requestObj.setTransferTimeout(kUploadTimeoutMs);
    applyHeaders(request, requestObj);

    if (requestType == QLatin1StringView("raw")) {
        auto *file = new QFile(filePath);
        if (!file->open(QIODevice::ReadOnly)) {
            file->deleteLater();
            return nullptr;
        }

        const QString contentType = stringValue(request, "contentType");
        if (!contentType.isEmpty()) {
            requestObj.setHeader(QNetworkRequest::ContentTypeHeader, contentType);
        }

        QNetworkReply *reply = nullptr;
        if (method == QLatin1StringView("PUT")) {
            reply = manager->put(requestObj, file);
        } else if (method == QLatin1StringView("POST")) {
            reply = manager->post(requestObj, file);
        }

        if (!reply) {
            file->deleteLater();
            return nullptr;
        }
        file->setParent(reply);
        return reply;
    }

    if (requestType != QLatin1StringView("multipart") || method != QLatin1StringView("POST")) {
        return nullptr;
    }

    const QJsonObject multipart = objectValue(request, "multipart");
    const QString fileField = stringValue(multipart, "fileField");
    if (fileField.isEmpty()) {
        return nullptr;
    }

    auto *multi = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    const QJsonObject fields = fieldMap(multipart);
    for (auto it = fields.begin(); it != fields.end(); ++it) {
        QHttpPart fieldPart;
        const QString disposition = QStringLiteral("form-data; name=\"%1\"").arg(it.key());
        fieldPart.setHeader(QNetworkRequest::ContentDispositionHeader, disposition);
        fieldPart.setBody(it.value().toString().toUtf8());
        multi->append(fieldPart);
    }

    auto *file = new QFile(filePath);
    if (!file->open(QIODevice::ReadOnly)) {
        file->deleteLater();
        delete multi;
        return nullptr;
    }

    QHttpPart filePart;
    const QString disposition = QStringLiteral("form-data; name=\"%1\"; filename=\"%2\"")
                                   .arg(fileField, fileInfo.fileName());
    filePart.setHeader(QNetworkRequest::ContentDispositionHeader, disposition);
    filePart.setBodyDevice(file);
    file->setParent(multi);
    multi->append(filePart);

    QNetworkReply *reply = manager->post(requestObj, multi);
    multi->setParent(reply);
    return reply;
}

UploadResult TargetUploader::parseReply(QNetworkReply *reply) const
{
    UploadResult result;
    if (!reply) {
        result.errorMessage = QStringLiteral("No reply received from server.");
        return result;
    }

    const QByteArray body = reply->readAll().trimmed();
    const QString responseText = QString::fromUtf8(body);

    const QJsonObject responseConfig = objectValue(m_config, "response");
    const QString type = responseConfig.value(QLatin1StringView("type")).toString();

    if (type == QLatin1StringView("text_url")) {
        if (responseText.startsWith(QLatin1StringView("http://"))
            || responseText.startsWith(QLatin1StringView("https://"))) {
            result.ok = true;
            result.url = responseText;
            return result;
        }
        result.errorMessage = responseText.isEmpty()
            ? QStringLiteral("Upload failed with an empty response.")
            : responseText;
        return result;
    }

    if (type == QLatin1StringView("regex") && m_responseRegex.isValid()) {
        const QRegularExpressionMatch match = m_responseRegex.match(responseText);
        if (match.hasMatch()) {
            result.ok = true;
            result.url = match.captured(m_responseGroup);
            return result;
        }
        result.errorMessage = responseText.isEmpty()
            ? QStringLiteral("Upload failed with an empty response.")
            : responseText;
        return result;
    }

    if (type == QLatin1StringView("json_pointer") && !m_jsonPointer.isEmpty()) {
        const QJsonDocument doc = QJsonDocument::fromJson(body);
        if (!doc.isObject() && !doc.isArray()) {
            result.errorMessage = QStringLiteral("Upload response was not valid JSON.");
            return result;
        }
        const QJsonValue value = resolveJsonPointer(doc.isObject() ? QJsonValue(doc.object()) : QJsonValue(doc.array()), m_jsonPointer);
        if (value.isString()) {
            result.ok = true;
            result.url = value.toString();
            return result;
        }
        result.errorMessage = QStringLiteral("Upload response did not contain a URL.");
        return result;
    }

    result.errorMessage = QStringLiteral("Unsupported response parser.");
    return result;
}
