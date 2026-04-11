#include "targetuploader.h"

#include "targetconfigparser.h"
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

QString extractConfiguredValue(const ParsedResponseExtractor &config, QNetworkReply *reply, const QByteArray &body, const QString &responseText)
{
    if (!config.valid) {
        return {};
    }

    if (config.type == ResponseExtractorType::TextUrl) {
        return responseText;
    }

    if (config.type == ResponseExtractorType::Regex) {
        const QRegularExpression regex(config.pattern);
        const QRegularExpressionMatch match = regex.match(responseText);
        return match.hasMatch() ? match.captured(config.group) : QString{};
    }

    if (config.type == ResponseExtractorType::JsonPointer) {
        const QJsonDocument doc = QJsonDocument::fromJson(body);
        if (!doc.isObject() && !doc.isArray()) {
            return {};
        }
        const QJsonValue value = TargetUploaderUtils::resolveJsonPointer(
            doc.isObject() ? QJsonValue(doc.object()) : QJsonValue(doc.array()), config.pointer);
        return value.isString() ? value.toString() : QString{};
    }

    if (config.type == ResponseExtractorType::Header) {
        return QString::fromUtf8(reply->rawHeader(config.name.toUtf8()));
    }

    if (config.type == ResponseExtractorType::RedirectUrl) {
        const QVariant redirectTarget = reply->attribute(QNetworkRequest::RedirectionTargetAttribute);
        if (redirectTarget.isValid()) {
            return redirectTarget.toUrl().toString();
        }
        return reply->url().toString();
    }

    if (config.type == ResponseExtractorType::XmlXpath) {
        return TargetUploaderUtils::resolveXmlPath(body, config.xpath);
    }

    return {};
}

QString extractErrorMessage(const ParsedResponseConfig &responseConfig, QNetworkReply *reply, const QByteArray &body, const QString &responseText)
{
    const QString extracted = extractConfiguredValue(responseConfig.error, reply, body, responseText);
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

TargetUploader::TargetUploader(const ParsedTargetConfig &config)
{
    setConfig(config);
}

void TargetUploader::setConfig(const QJsonObject &config)
{
    QList<TargetDiagnostic> diagnostics;
    ParsedTargetConfig parsed;
    const bool valid = TargetConfigParser::parse(config, &parsed, &diagnostics);
    setConfig(parsed);
    m_requestConfigValid = valid;
    m_responseConfigValid = valid && parsed.response.success.valid;
}

void TargetUploader::setConfig(const ParsedTargetConfig &config)
{
    m_targetConfig = config;
    m_requestConfigValid = config.valid;
    m_responseConfigValid = config.valid && config.response.success.valid;
}

QString TargetUploader::id() const
{
    return m_targetConfig.core.id;
}

QString TargetUploader::displayName() const
{
    return m_targetConfig.core.displayName;
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

    if (!m_requestConfigValid || m_targetConfig.request.url.isEmpty() || m_targetConfig.request.method.isEmpty()) {
        return nullptr;
    }

    QNetworkRequest requestObj{TargetUploaderUtils::applyQueryParameters(m_targetConfig.request.url, m_targetConfig.request.query, fileInfo)};
    requestObj.setHeader(
        QNetworkRequest::UserAgentHeader,
        QStringLiteral("plasma-share-uploader/" PLASMA_SHARE_UPLOADER_VERSION));
    requestObj.setTransferTimeout(kUploadTimeoutMs);
    if (m_targetConfig.response.success.valid && m_targetConfig.response.success.type == ResponseExtractorType::RedirectUrl) {
        requestObj.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    }
    TargetUploaderUtils::applyHeaders(m_targetConfig.request.headers, fileInfo, requestObj);

    if (m_targetConfig.request.type == RequestBodyType::Raw) {
        auto *file = new QFile(filePath);
        if (!file->open(QIODevice::ReadOnly)) {
            file->deleteLater();
            return nullptr;
        }

        requestObj.setHeader(QNetworkRequest::ContentTypeHeader,
                             m_targetConfig.request.contentType.isEmpty() ? QStringLiteral("application/octet-stream")
                                                                          : m_targetConfig.request.contentType);

        QNetworkReply *reply = nullptr;
        if (m_targetConfig.request.method == QLatin1StringView("PUT")) {
            reply = manager->put(requestObj, file);
        } else if (m_targetConfig.request.method == QLatin1StringView("POST")) {
            reply = manager->post(requestObj, file);
        }

        if (!reply) {
            file->deleteLater();
            return nullptr;
        }
        file->setParent(reply);
        return reply;
    }

    if (m_targetConfig.request.type == RequestBodyType::Multipart) {
        if (m_targetConfig.request.method != QLatin1StringView("POST")) {
            return nullptr;
        }
        if (m_targetConfig.request.fileField.isEmpty()) {
            return nullptr;
        }

        auto *multi = new QHttpMultiPart(QHttpMultiPart::FormDataType);
        for (auto it = m_targetConfig.request.multipartFields.begin(); it != m_targetConfig.request.multipartFields.end(); ++it) {
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
                                       .arg(m_targetConfig.request.fileField, fileInfo.fileName());
        filePart.setHeader(QNetworkRequest::ContentDispositionHeader, disposition);
        filePart.setBodyDevice(file);
        file->setParent(multi);
        multi->append(filePart);

        QNetworkReply *reply = manager->post(requestObj, multi);
        multi->setParent(reply);
        return reply;
    }

    if (m_targetConfig.request.type == RequestBodyType::FormUrlencoded) {
        const QByteArray body = TargetUploaderUtils::createFormUrlencodedBody(m_targetConfig.request.formFields, fileInfo);
        auto *buffer = new QBuffer;
        buffer->setData(body);
        buffer->open(QIODevice::ReadOnly);
        requestObj.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));

        QNetworkReply *reply = nullptr;
        if (m_targetConfig.request.method == QLatin1StringView("POST")) {
            reply = manager->post(requestObj, buffer);
        } else if (m_targetConfig.request.method == QLatin1StringView("PUT")) {
            reply = manager->put(requestObj, buffer);
        }
        if (!reply) {
            buffer->deleteLater();
            return nullptr;
        }
        buffer->setParent(reply);
        return reply;
    }

    if (m_targetConfig.request.type == RequestBodyType::Json) {
        const QByteArray body = createJsonBody(QJsonObject{{QStringLiteral("fields"), m_targetConfig.request.jsonFields}}, fileInfo);
        auto *buffer = new QBuffer;
        buffer->setData(body);
        buffer->open(QIODevice::ReadOnly);
        requestObj.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

        QNetworkReply *reply = nullptr;
        if (m_targetConfig.request.method == QLatin1StringView("POST")) {
            reply = manager->post(requestObj, buffer);
        } else if (m_targetConfig.request.method == QLatin1StringView("PUT")) {
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
    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    result.responseInfo = buildResponseInfo(reply, responseText);

    if (statusCode >= 400) {
        result.errorMessage = extractErrorMessage(m_targetConfig.response, reply, body, responseText);
        return result;
    }

    if (!m_responseConfigValid || !m_targetConfig.response.success.valid) {
        result.errorMessage = QStringLiteral("Missing valid response extractor configuration.");
        return result;
    }

    const QString extracted = extractConfiguredValue(m_targetConfig.response.success, reply, body, responseText);
    const ResponseExtractorType type = m_targetConfig.response.success.type;

    if (!extracted.isEmpty()) {
        if (type != ResponseExtractorType::TextUrl
            || extracted.startsWith(QLatin1StringView("http://"))
            || extracted.startsWith(QLatin1StringView("https://"))) {
            result.ok = true;
            result.url = extracted;
            result.thumbnailUrl = extractConfiguredValue(m_targetConfig.response.thumbnail, reply, body, responseText);
            result.deletionUrl = extractConfiguredValue(m_targetConfig.response.deletion, reply, body, responseText);
            return result;
        }
    }

    if (type == ResponseExtractorType::JsonPointer) {
        const QJsonDocument doc = QJsonDocument::fromJson(body);
        if (!doc.isObject() && !doc.isArray()) {
            result.errorMessage = QStringLiteral("Upload response was not valid JSON.");
            return result;
        }
        result.errorMessage = extractConfiguredValue(m_targetConfig.response.error, reply, body, responseText);
        if (result.errorMessage.isEmpty()) {
            result.errorMessage = QStringLiteral("Upload response did not contain a URL.");
        }
        return result;
    }

    if (type == ResponseExtractorType::XmlXpath) {
        result.errorMessage = extractErrorMessage(m_targetConfig.response, reply, body, responseText);
        if (result.errorMessage.isEmpty()) {
            result.errorMessage = QStringLiteral("Upload response did not contain a URL.");
        }
        return result;
    }

    if (type == ResponseExtractorType::Header
        || type == ResponseExtractorType::RedirectUrl
        || type == ResponseExtractorType::Regex
        || type == ResponseExtractorType::TextUrl) {
        result.errorMessage = extractErrorMessage(m_targetConfig.response, reply, body, responseText);
        return result;
    }

    result.errorMessage = QStringLiteral("Unsupported response parser.");
    return result;
}
