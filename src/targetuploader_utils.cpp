#include "targetuploader_utils.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>
#include <QtXml/QDomDocument>

namespace {
QString decodeJsonPointerToken(const QString &token)
{
    QString out = token;
    out.replace(QStringLiteral("~1"), QStringLiteral("/"));
    out.replace(QStringLiteral("~0"), QStringLiteral("~"));
    return out;
}

QString substituteFilename(const QString &value, const QFileInfo &fileInfo, bool urlEncode)
{
    QString result = value;
    const QString fileName = urlEncode
        ? QString::fromUtf8(QUrl::toPercentEncoding(fileInfo.fileName()))
        : fileInfo.fileName();
    result.replace(QStringLiteral("${FILENAME}"), fileName);
    return result;
}

QDomNode resolveXmlSegment(const QDomNode &parent, const QString &segment)
{
    static const QRegularExpression indexedSegment(QStringLiteral(R"(^(.*)\[(\d+)\]$)"));

    QString name = segment;
    int index = 1;
    const QRegularExpressionMatch match = indexedSegment.match(segment);
    if (match.hasMatch()) {
        name = match.captured(1);
        index = match.captured(2).toInt();
    }
    if (name.isEmpty() || index <= 0) {
        return {};
    }

    int seen = 0;
    for (QDomNode child = parent.firstChild(); !child.isNull(); child = child.nextSibling()) {
        if (!child.isElement() || child.nodeName() != name) {
            continue;
        }
        ++seen;
        if (seen == index) {
            return child;
        }
    }
    return {};
}
}

QJsonObject TargetUploaderUtils::objectValue(const QJsonObject &parent, const char *key)
{
    const QJsonValue value = parent.value(QLatin1StringView(key));
    return value.isObject() ? value.toObject() : QJsonObject();
}

QString TargetUploaderUtils::stringValue(const QJsonObject &parent, const char *key)
{
    return parent.value(QLatin1StringView(key)).toString();
}

QJsonObject TargetUploaderUtils::fieldMap(const QJsonObject &parent)
{
    const QJsonValue value = parent.value(QLatin1StringView("fields"));
    return value.isObject() ? value.toObject() : QJsonObject();
}

QString TargetUploaderUtils::substituteEnv(const QString &value)
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

QString TargetUploaderUtils::substituteRequestValue(const QString &value, const QFileInfo &fileInfo)
{
    return substituteFilename(substituteEnv(value), fileInfo, false);
}

QString TargetUploaderUtils::applyUrlTemplate(const QString &urlTemplate, const QFileInfo &fileInfo)
{
    return substituteFilename(substituteEnv(urlTemplate), fileInfo, true);
}

void TargetUploaderUtils::applyHeaders(const QJsonObject &requestConfig,
                                       const QFileInfo &fileInfo,
                                       QNetworkRequest &requestObj)
{
    const QJsonValue headersValue = requestConfig.value(QLatin1StringView("headers"));
    if (!headersValue.isObject()) {
        return;
    }
    const QJsonObject headers = headersValue.toObject();
    for (auto it = headers.begin(); it != headers.end(); ++it) {
        const QByteArray name = it.key().toUtf8();
        const QString rawValue = it.value().toString();
        const QByteArray value = substituteRequestValue(rawValue, fileInfo).toUtf8();
        requestObj.setRawHeader(name, value);
    }
}

QUrl TargetUploaderUtils::applyQueryParameters(const QString &urlTemplate,
                                               const QJsonObject &requestConfig,
                                               const QFileInfo &fileInfo)
{
    QUrl url = QUrl::fromUserInput(applyUrlTemplate(urlTemplate, fileInfo));
    const QJsonValue queryValue = requestConfig.value(QLatin1StringView("query"));
    if (!queryValue.isObject()) {
        return url;
    }

    QUrlQuery query(url);
    const QJsonObject fields = queryValue.toObject();
    for (auto it = fields.begin(); it != fields.end(); ++it) {
        query.addQueryItem(it.key(), substituteRequestValue(it.value().toString(), fileInfo));
    }
    url.setQuery(query);
    return url;
}

QJsonValue TargetUploaderUtils::substituteJsonValue(const QJsonValue &value, const QFileInfo &fileInfo)
{
    if (value.isString()) {
        return substituteRequestValue(value.toString(), fileInfo);
    }
    if (value.isArray()) {
        QJsonArray array;
        for (const QJsonValue &entry : value.toArray()) {
            array.append(substituteJsonValue(entry, fileInfo));
        }
        return array;
    }
    if (value.isObject()) {
        QJsonObject object;
        const QJsonObject source = value.toObject();
        for (auto it = source.begin(); it != source.end(); ++it) {
            object.insert(it.key(), substituteJsonValue(it.value(), fileInfo));
        }
        return object;
    }
    return value;
}

QJsonValue TargetUploaderUtils::resolveJsonPointer(const QJsonValue &root, const QString &pointer)
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

QString TargetUploaderUtils::resolveXmlPath(const QByteArray &xmlBytes, const QString &xpath)
{
    if (xpath.isEmpty() || !xpath.startsWith(QLatin1Char('/'))) {
        return {};
    }

    QDomDocument xml;
    const QDomDocument::ParseResult parseResult = xml.setContent(xmlBytes);
    if (!parseResult) {
        return {};
    }

    const QStringList parts = xpath.mid(1).split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.isEmpty()) {
        return {};
    }

    QDomNode current = xml.documentElement();
    if (current.isNull() || current.nodeName() != parts.first()) {
        return {};
    }

    for (int i = 1; i < parts.size(); ++i) {
        current = resolveXmlSegment(current, parts.at(i));
        if (current.isNull()) {
            return {};
        }
    }

    return current.firstChild().nodeValue();
}
