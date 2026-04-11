#include "targetuploader.h"

#include "targetuploader_utils.h"

#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrlQuery>

namespace {
constexpr int kUploadTimeoutMs = 30000;

UploadResponseInfo buildResponseInfo(QNetworkReply *reply, const QString &responseText)
{
    UploadResponseInfo info;
    if (!reply) {
        return info;
    }

    info.statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    info.reasonPhrase = reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString();
    info.responseUrl = reply->url().toString();

    const QList<QByteArray> rawHeaders = reply->rawHeaderList();
    for (const QByteArray &headerName : rawHeaders) {
        info.headers.insert(QString::fromUtf8(headerName).toLower(), QString::fromUtf8(reply->rawHeader(headerName)));
    }

    info.responseText = responseText;
    return info;
}

QByteArray createFormUrlencodedBody(const QJsonObject &fields, const QFileInfo &fileInfo)
{
    QUrlQuery query;
    for (auto it = fields.begin(); it != fields.end(); ++it) {
        query.addQueryItem(it.key(), TargetUploaderUtils::substituteRequestValue(it.value().toString(), fileInfo));
    }
    return query.toString(QUrl::FullyEncoded).toUtf8();
}

QByteArray createJsonBody(const QJsonObject &jsonConfig, const QFileInfo &fileInfo)
{
    const QJsonValue fieldsValue = jsonConfig.value(QStringLiteral("fields"));
    const QJsonValue substituted = TargetUploaderUtils::substituteJsonValue(fieldsValue, fileInfo);
    if (substituted.isObject()) {
        return QJsonDocument(substituted.toObject()).toJson(QJsonDocument::Compact);
    }
    if (substituted.isArray()) {
        return QJsonDocument(substituted.toArray()).toJson(QJsonDocument::Compact);
    }
    return {};
}

QString extractConfiguredValue(const QJsonObject &config, QNetworkReply *reply, const QByteArray &body, const QString &responseText)
{
    if (config.isEmpty()) {
        return {};
    }

    const QString type = config.value(QStringLiteral("type")).toString();
    if (type == QLatin1StringView("text_url")) {
        return responseText;
    }

    if (type == QLatin1StringView("regex")) {
        const QRegularExpression regex(config.value(QStringLiteral("pattern")).toString());
        const int group = config.value(QStringLiteral("group")).toInt(1);
        const QRegularExpressionMatch match = regex.match(responseText);
        return match.hasMatch() ? match.captured(group) : QString{};
    }

    if (type == QLatin1StringView("json_pointer")) {
        const QJsonDocument doc = QJsonDocument::fromJson(body);
        if (!doc.isObject() && !doc.isArray()) {
            return {};
        }
        const QString pointer = config.value(QStringLiteral("pointer")).toString();
        const QJsonValue value = TargetUploaderUtils::resolveJsonPointer(
            doc.isObject() ? QJsonValue(doc.object()) : QJsonValue(doc.array()), pointer);
        return value.isString() ? value.toString() : QString{};
    }

    if (type == QLatin1StringView("header")) {
        const QByteArray headerName = config.value(QStringLiteral("name")).toString().toUtf8();
        return QString::fromUtf8(reply->rawHeader(headerName));
    }

    if (type == QLatin1StringView("redirect_url")) {
        const QVariant redirectTarget = reply->attribute(QNetworkRequest::RedirectionTargetAttribute);
        if (redirectTarget.isValid()) {
            return redirectTarget.toUrl().toString();
        }
        return reply->url().toString();
    }

    if (type == QLatin1StringView("xml_xpath")) {
        return TargetUploaderUtils::resolveXmlPath(body, config.value(QStringLiteral("xpath")).toString());
    }

    return {};
}

QString extractErrorMessage(const QJsonObject &responseConfig, QNetworkReply *reply, const QByteArray &body, const QString &responseText)
{
    const QString extracted = extractConfiguredValue(
        TargetUploaderUtils::objectValue(responseConfig, "error"), reply, body, responseText);
    if (!extracted.isEmpty()) {
        return extracted;
    }
    return responseText.isEmpty() ? QStringLiteral("Upload failed with an empty response.") : responseText;
}
}

QJsonObject UploadResponseInfo::toJson() const
{
    return QJsonObject{
        {QStringLiteral("statusCode"), statusCode},
        {QStringLiteral("reasonPhrase"), reasonPhrase},
        {QStringLiteral("responseUrl"), responseUrl},
        {QStringLiteral("headers"), headers},
        {QStringLiteral("responseText"), responseText},
    };
}

QJsonObject UploadResult::toJson() const
{
    QJsonObject object{
        {QStringLiteral("ok"), ok},
        {QStringLiteral("url"), url},
        {QStringLiteral("errorMessage"), errorMessage},
        {QStringLiteral("response"), responseInfo.toJson()},
    };
    if (!thumbnailUrl.isEmpty()) {
        object.insert(QStringLiteral("thumbnailUrl"), thumbnailUrl);
    }
    if (!deletionUrl.isEmpty()) {
        object.insert(QStringLiteral("deletionUrl"), deletionUrl);
    }
    return object;
}

TargetUploader::TargetUploader(const QJsonObject &config)
    : m_config(config)
{
}

void TargetUploader::setConfig(const QJsonObject &config)
{
    m_config = config;
}

QString TargetUploader::id() const
{
    return TargetUploaderUtils::stringValue(m_config, "id");
}

QString TargetUploader::displayName() const
{
    const QString name = TargetUploaderUtils::stringValue(m_config, "displayName");
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

    const QJsonObject request = TargetUploaderUtils::objectValue(m_config, "request");
    const QString urlTemplate = TargetUploaderUtils::stringValue(request, "url");
    const QString method = TargetUploaderUtils::stringValue(request, "method").toUpper();
    const QString requestType = TargetUploaderUtils::stringValue(request, "type").isEmpty()
        ? QStringLiteral("multipart")
        : TargetUploaderUtils::stringValue(request, "type");
    if (urlTemplate.isEmpty() || method.isEmpty()) {
        return nullptr;
    }

    QNetworkRequest requestObj{TargetUploaderUtils::applyQueryParameters(urlTemplate, request, fileInfo)};
    requestObj.setHeader(
        QNetworkRequest::UserAgentHeader,
        QStringLiteral("plasma-share-uploader/" PLASMA_SHARE_UPLOADER_VERSION));
    requestObj.setTransferTimeout(kUploadTimeoutMs);
    if (TargetUploaderUtils::stringValue(TargetUploaderUtils::objectValue(m_config, "response"), "type")
        == QLatin1StringView("redirect_url")) {
        requestObj.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    }
    TargetUploaderUtils::applyHeaders(request, fileInfo, requestObj);

    if (requestType == QLatin1StringView("raw")) {
        auto *file = new QFile(filePath);
        if (!file->open(QIODevice::ReadOnly)) {
            file->deleteLater();
            return nullptr;
        }

        const QString contentType = TargetUploaderUtils::stringValue(request, "contentType");
        requestObj.setHeader(QNetworkRequest::ContentTypeHeader,
                             contentType.isEmpty() ? QStringLiteral("application/octet-stream") : contentType);

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

    if (requestType == QLatin1StringView("multipart")) {
        if (method != QLatin1StringView("POST")) {
            return nullptr;
        }

        const QJsonObject multipart = TargetUploaderUtils::objectValue(request, "multipart");
        const QString fileField = TargetUploaderUtils::stringValue(multipart, "fileField");
        if (fileField.isEmpty()) {
            return nullptr;
        }

        auto *multi = new QHttpMultiPart(QHttpMultiPart::FormDataType);
        const QJsonObject fields = TargetUploaderUtils::fieldMap(multipart);
        for (auto it = fields.begin(); it != fields.end(); ++it) {
            QHttpPart fieldPart;
            const QString disposition = QStringLiteral("form-data; name=\"%1\"").arg(it.key());
            fieldPart.setHeader(QNetworkRequest::ContentDispositionHeader, disposition);
            fieldPart.setBody(TargetUploaderUtils::substituteRequestValue(it.value().toString(), fileInfo).toUtf8());
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

    if (requestType == QLatin1StringView("form_urlencoded")) {
        const QJsonObject formConfig = TargetUploaderUtils::objectValue(request, "formUrlencoded");
        const QByteArray body = createFormUrlencodedBody(TargetUploaderUtils::fieldMap(formConfig), fileInfo);
        auto *buffer = new QBuffer;
        buffer->setData(body);
        buffer->open(QIODevice::ReadOnly);
        requestObj.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));

        QNetworkReply *reply = nullptr;
        if (method == QLatin1StringView("POST")) {
            reply = manager->post(requestObj, buffer);
        } else if (method == QLatin1StringView("PUT")) {
            reply = manager->put(requestObj, buffer);
        }
        if (!reply) {
            buffer->deleteLater();
            return nullptr;
        }
        buffer->setParent(reply);
        return reply;
    }

    if (requestType == QLatin1StringView("json")) {
        const QByteArray body = createJsonBody(TargetUploaderUtils::objectValue(request, "json"), fileInfo);
        auto *buffer = new QBuffer;
        buffer->setData(body);
        buffer->open(QIODevice::ReadOnly);
        requestObj.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

        QNetworkReply *reply = nullptr;
        if (method == QLatin1StringView("POST")) {
            reply = manager->post(requestObj, buffer);
        } else if (method == QLatin1StringView("PUT")) {
            reply = manager->put(requestObj, buffer);
        }
        if (!reply) {
            buffer->deleteLater();
            return nullptr;
        }
        buffer->setParent(reply);
        return reply;
    }

    return nullptr;
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
    const QJsonObject responseConfig = TargetUploaderUtils::objectValue(m_config, "response");
    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    result.responseInfo = buildResponseInfo(reply, responseText);

    if (statusCode >= 400) {
        result.errorMessage = extractErrorMessage(responseConfig, reply, body, responseText);
        return result;
    }

    const QString extracted = extractConfiguredValue(responseConfig, reply, body, responseText);
    const QString type = responseConfig.value(QStringLiteral("type")).toString();

    if (!extracted.isEmpty()) {
        if (type != QLatin1StringView("text_url")
            || extracted.startsWith(QLatin1StringView("http://"))
            || extracted.startsWith(QLatin1StringView("https://"))) {
            result.ok = true;
            result.url = extracted;
            result.thumbnailUrl = extractConfiguredValue(
                TargetUploaderUtils::objectValue(responseConfig, "thumbnail"), reply, body, responseText);
            result.deletionUrl = extractConfiguredValue(
                TargetUploaderUtils::objectValue(responseConfig, "deletion"), reply, body, responseText);
            return result;
        }
    }

    if (type == QLatin1StringView("json_pointer")) {
        const QJsonDocument doc = QJsonDocument::fromJson(body);
        if (!doc.isObject() && !doc.isArray()) {
            result.errorMessage = QStringLiteral("Upload response was not valid JSON.");
            return result;
        }
        result.errorMessage = extractConfiguredValue(
            TargetUploaderUtils::objectValue(responseConfig, "error"), reply, body, responseText);
        if (result.errorMessage.isEmpty()) {
            result.errorMessage = QStringLiteral("Upload response did not contain a URL.");
        }
        return result;
    }

    if (type == QLatin1StringView("xml_xpath")) {
        result.errorMessage = extractErrorMessage(responseConfig, reply, body, responseText);
        if (result.errorMessage.isEmpty()) {
            result.errorMessage = QStringLiteral("Upload response did not contain a URL.");
        }
        return result;
    }

    if (type == QLatin1StringView("header")
        || type == QLatin1StringView("redirect_url")
        || type == QLatin1StringView("regex")
        || type == QLatin1StringView("text_url")) {
        result.errorMessage = extractErrorMessage(responseConfig, reply, body, responseText);
        return result;
    }

    result.errorMessage = QStringLiteral("Unsupported response parser.");
    return result;
}
