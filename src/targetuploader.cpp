#include "targetuploader.h"

#include "targetcoreconfigparser.h"
#include "targetrequestconfigparser.h"
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
{
    setConfig(config);
}

void TargetUploader::setConfig(const QJsonObject &config)
{
    m_config = config;
    QList<TargetDiagnostic> diagnostics;
    m_coreConfig = {};
    m_requestConfig = {};
    TargetCoreConfigParser::parse(config, &m_coreConfig, &diagnostics);
    m_requestConfigValid = TargetRequestConfigParser::parse(config, &m_requestConfig, &diagnostics);
}

QString TargetUploader::id() const
{
    return m_coreConfig.id;
}

QString TargetUploader::displayName() const
{
    return m_coreConfig.displayName;
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

    if (!m_requestConfigValid || m_requestConfig.url.isEmpty() || m_requestConfig.method.isEmpty()) {
        return nullptr;
    }

    QNetworkRequest requestObj{TargetUploaderUtils::applyQueryParameters(m_requestConfig.url, m_requestConfig.query, fileInfo)};
    requestObj.setHeader(
        QNetworkRequest::UserAgentHeader,
        QStringLiteral("plasma-share-uploader/" PLASMA_SHARE_UPLOADER_VERSION));
    requestObj.setTransferTimeout(kUploadTimeoutMs);
    if (TargetUploaderUtils::stringValue(TargetUploaderUtils::objectValue(m_config, "response"), "type")
        == QLatin1StringView("redirect_url")) {
        requestObj.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    }
    TargetUploaderUtils::applyHeaders(m_requestConfig.headers, fileInfo, requestObj);

    if (m_requestConfig.type == RequestBodyType::Raw) {
        auto *file = new QFile(filePath);
        if (!file->open(QIODevice::ReadOnly)) {
            file->deleteLater();
            return nullptr;
        }

        requestObj.setHeader(QNetworkRequest::ContentTypeHeader,
                             m_requestConfig.contentType.isEmpty() ? QStringLiteral("application/octet-stream")
                                                                   : m_requestConfig.contentType);

        QNetworkReply *reply = nullptr;
        if (m_requestConfig.method == QLatin1StringView("PUT")) {
            reply = manager->put(requestObj, file);
        } else if (m_requestConfig.method == QLatin1StringView("POST")) {
            reply = manager->post(requestObj, file);
        }

        if (!reply) {
            file->deleteLater();
            return nullptr;
        }
        file->setParent(reply);
        return reply;
    }

    if (m_requestConfig.type == RequestBodyType::Multipart) {
        if (m_requestConfig.method != QLatin1StringView("POST")) {
            return nullptr;
        }
        if (m_requestConfig.fileField.isEmpty()) {
            return nullptr;
        }

        auto *multi = new QHttpMultiPart(QHttpMultiPart::FormDataType);
        for (auto it = m_requestConfig.multipartFields.begin(); it != m_requestConfig.multipartFields.end(); ++it) {
            QHttpPart fieldPart;
            const QString disposition = QStringLiteral("form-data; name=\"%1\"").arg(it.key());
            fieldPart.setHeader(QNetworkRequest::ContentDispositionHeader, disposition);
            fieldPart.setBody(TargetUploaderUtils::substituteRequestValue(it.value(), fileInfo).toUtf8());
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
                                       .arg(m_requestConfig.fileField, fileInfo.fileName());
        filePart.setHeader(QNetworkRequest::ContentDispositionHeader, disposition);
        filePart.setBodyDevice(file);
        file->setParent(multi);
        multi->append(filePart);

        QNetworkReply *reply = manager->post(requestObj, multi);
        multi->setParent(reply);
        return reply;
    }

    if (m_requestConfig.type == RequestBodyType::FormUrlencoded) {
        const QByteArray body = TargetUploaderUtils::createFormUrlencodedBody(m_requestConfig.formFields, fileInfo);
        auto *buffer = new QBuffer;
        buffer->setData(body);
        buffer->open(QIODevice::ReadOnly);
        requestObj.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));

        QNetworkReply *reply = nullptr;
        if (m_requestConfig.method == QLatin1StringView("POST")) {
            reply = manager->post(requestObj, buffer);
        } else if (m_requestConfig.method == QLatin1StringView("PUT")) {
            reply = manager->put(requestObj, buffer);
        }
        if (!reply) {
            buffer->deleteLater();
            return nullptr;
        }
        buffer->setParent(reply);
        return reply;
    }

    if (m_requestConfig.type == RequestBodyType::Json) {
        const QByteArray body = createJsonBody(QJsonObject{{QStringLiteral("fields"), m_requestConfig.jsonFields}}, fileInfo);
        auto *buffer = new QBuffer;
        buffer->setData(body);
        buffer->open(QIODevice::ReadOnly);
        requestObj.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

        QNetworkReply *reply = nullptr;
        if (m_requestConfig.method == QLatin1StringView("POST")) {
            reply = manager->post(requestObj, buffer);
        } else if (m_requestConfig.method == QLatin1StringView("PUT")) {
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
