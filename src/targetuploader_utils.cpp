#include "targetuploader_utils.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QMap>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>
#include <QtXml/QDomDocument>

namespace {
QString decodeJsonPointerToken(const QString& token)
{
    QString out = token;
    out.replace(QStringLiteral("~1"), QStringLiteral("/"));
    out.replace(QStringLiteral("~0"), QStringLiteral("~"));
    return out;
}

QString expandRequest(
    const QString& value, const QFileInfo& fileInfo, const QMap<QString, QString>& secrets, bool encodeFilename)
{
    static const QRegularExpression pattern(
        QStringLiteral(R"(\$\{(FILENAME|ENV:[A-Za-z_][A-Za-z0-9_]*|WALLET:[A-Za-z0-9_.-]+)\})"));
    QString result;
    qsizetype offset = 0;
    auto matches = pattern.globalMatch(value);
    while (matches.hasNext()) {
        const auto match = matches.next();
        result += QStringView(value).mid(offset, match.capturedStart() - offset);
        const auto token = match.captured(1);
        if (token == QLatin1StringView("FILENAME")) {
            result += encodeFilename ? QString::fromLatin1(QUrl::toPercentEncoding(fileInfo.fileName()))
                                     : fileInfo.fileName();
        } else if (token.startsWith(QLatin1StringView("ENV:"))) {
            result += QString::fromLocal8Bit(qgetenv(token.mid(4).toUtf8().constData()));
        } else {
            result += secrets.value(token.mid(7));
        }
        offset = match.capturedEnd();
    }
    result += QStringView(value).mid(offset);
    return result;
}

QDomNode resolveXmlSegment(const QDomNode& parent, const QString& segment)
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
        return { };
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
    return { };
}
}

QJsonObject TargetUploaderUtils::objectValue(const QJsonObject& parent, const char* key)
{
    const QJsonValue value = parent.value(QLatin1StringView(key));
    return value.isObject() ? value.toObject() : QJsonObject();
}

QString TargetUploaderUtils::stringValue(const QJsonObject& parent, const char* key)
{
    return parent.value(QLatin1StringView(key)).toString();
}

QJsonObject TargetUploaderUtils::fieldMap(const QJsonObject& parent)
{
    const QJsonValue value = parent.value(QLatin1StringView("fields"));
    return value.isObject() ? value.toObject() : QJsonObject();
}

void TargetUploaderUtils::applyHeaders(const QMap<QString, QString>& headers, const QFileInfo& fileInfo,
    QNetworkRequest& requestObj, const QMap<QString, QString>& secrets)
{
    for (auto it = headers.begin(); it != headers.end(); ++it) {
        requestObj.setRawHeader(it.key().toUtf8(), substituteRequestValue(it.value(), fileInfo, secrets).toUtf8());
    }
}

QString TargetUploaderUtils::substituteEnv(const QString& value)
{
    static const QRegularExpression pattern(QStringLiteral(R"(\$\{ENV:([A-Za-z_][A-Za-z0-9_]*)\})"));
    const QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    QString result;
    qsizetype offset = 0;
    auto matches = pattern.globalMatch(value);
    while (matches.hasNext()) {
        const auto match = matches.next();
        result += QStringView(value).mid(offset, match.capturedStart() - offset);
        result += environment.value(match.captured(1));
        offset = match.capturedEnd();
    }
    result += QStringView(value).mid(offset);
    return result;
}

QString TargetUploaderUtils::substituteRequestValue(
    const QString& value, const QFileInfo& fileInfo, const QMap<QString, QString>& secrets)
{
    return expandRequest(value, fileInfo, secrets, false);
}

QString TargetUploaderUtils::applyUrlTemplate(
    const QString& urlTemplate, const QFileInfo& fileInfo, const QMap<QString, QString>& secrets)
{
    return expandRequest(urlTemplate, fileInfo, secrets, true);
}

void TargetUploaderUtils::applyHeaders(const QJsonObject& requestConfig, const QFileInfo& fileInfo,
    QNetworkRequest& requestObj, const QMap<QString, QString>& secrets)
{
    const QJsonValue headersValue = requestConfig.value(QLatin1StringView("headers"));
    if (!headersValue.isObject()) {
        return;
    }
    const QJsonObject headers = headersValue.toObject();
    for (auto it = headers.begin(); it != headers.end(); ++it) {
        const QByteArray name = it.key().toUtf8();
        const QString rawValue = it.value().toString();
        const QByteArray value = substituteRequestValue(rawValue, fileInfo, secrets).toUtf8();
        requestObj.setRawHeader(name, value);
    }
}

QUrl TargetUploaderUtils::applyQueryParameters(const QString& urlTemplate, const QJsonObject& requestConfig,
    const QFileInfo& fileInfo, const QMap<QString, QString>& secrets)
{
    QUrl url = QUrl(applyUrlTemplate(urlTemplate, fileInfo, secrets), QUrl::StrictMode);
    // Mutating a QUrl can clear its strict-parse error; preserve it for the caller.
    if (!url.isValid())
        return url;
    const QJsonValue queryValue = requestConfig.value(QLatin1StringView("query"));
    if (!queryValue.isObject()) {
        return url;
    }

    QUrlQuery query(url);
    const QJsonObject fields = queryValue.toObject();
    for (auto it = fields.begin(); it != fields.end(); ++it) {
        query.addQueryItem(QString::fromLatin1(QUrl::toPercentEncoding(it.key())),
            QString::fromLatin1(
                QUrl::toPercentEncoding(substituteRequestValue(it.value().toString(), fileInfo, secrets))));
    }
    url.setQuery(query);
    return url;
}

QUrl TargetUploaderUtils::applyQueryParameters(const QString& urlTemplate, const QMap<QString, QString>& queryItems,
    const QFileInfo& fileInfo, const QMap<QString, QString>& secrets)
{
    QUrl url = QUrl(applyUrlTemplate(urlTemplate, fileInfo, secrets), QUrl::StrictMode);
    // Mutating a QUrl can clear its strict-parse error; preserve it for the caller.
    if (!url.isValid())
        return url;
    QUrlQuery query(url);
    for (auto it = queryItems.begin(); it != queryItems.end(); ++it) {
        query.addQueryItem(QString::fromLatin1(QUrl::toPercentEncoding(it.key())),
            QString::fromLatin1(QUrl::toPercentEncoding(substituteRequestValue(it.value(), fileInfo, secrets))));
    }
    url.setQuery(query);
    return url;
}

QByteArray TargetUploaderUtils::createFormUrlencodedBody(
    const QMap<QString, QString>& fields, const QFileInfo& fileInfo, const QMap<QString, QString>& secrets)
{
    QByteArray body;
    for (auto it = fields.begin(); it != fields.end(); ++it) {
        if (!body.isEmpty()) {
            body += '&';
        }
        body += QUrl::toPercentEncoding(it.key());
        body += '=';
        body += QUrl::toPercentEncoding(substituteRequestValue(it.value(), fileInfo, secrets));
    }
    return body;
}

QJsonValue TargetUploaderUtils::substituteJsonValue(
    const QJsonValue& value, const QFileInfo& fileInfo, const QMap<QString, QString>& secrets)
{
    if (value.isString()) {
        return substituteRequestValue(value.toString(), fileInfo, secrets);
    }
    if (value.isArray()) {
        QJsonArray array;
        for (const QJsonValue& entry : value.toArray()) {
            array.append(substituteJsonValue(entry, fileInfo, secrets));
        }
        return array;
    }
    if (value.isObject()) {
        QJsonObject object;
        const QJsonObject source = value.toObject();
        for (auto it = source.begin(); it != source.end(); ++it) {
            object.insert(it.key(), substituteJsonValue(it.value(), fileInfo, secrets));
        }
        return object;
    }
    return value;
}

bool TargetUploaderUtils::isValidJsonPointer(const QString& pointer)
{
    static const QRegularExpression pattern(QStringLiteral(R"(\A(?:/(?:[^~/]|~[01])*)*\z)"));
    return pattern.match(pointer).hasMatch();
}

bool TargetUploaderUtils::isValidXmlPath(const QString& path)
{
    static const QRegularExpression pattern(
        QStringLiteral(R"(\A/[A-Za-z_][A-Za-z0-9_.:-]*(?:/[A-Za-z_][A-Za-z0-9_.:-]*(?:\[[1-9][0-9]*\])?)*\z)"));
    return pattern.match(path).hasMatch();
}

QJsonValue TargetUploaderUtils::resolveJsonPointer(const QJsonValue& root, const QString& pointer)
{
    if (pointer.isEmpty()) {
        return root;
    }

    if (!isValidJsonPointer(pointer)) {
        return QJsonValue();
    }

    QJsonValue current = root;
    const QStringList parts = pointer.mid(1).split(QLatin1Char('/'), Qt::KeepEmptyParts);
    for (const QString& part : parts) {
        const QString key = decodeJsonPointerToken(part);
        if (current.isObject()) {
            current = current.toObject().value(key);
        } else if (current.isArray()) {
            static const QRegularExpression indexPattern(QStringLiteral("\\A(?:0|[1-9][0-9]*)\\z"));
            bool ok = false;
            const auto index = key.toULongLong(&ok);
            if (!ok || !indexPattern.match(key).hasMatch()) {
                return QJsonValue();
            }
            const QJsonArray array = current.toArray();
            if (index >= static_cast<qulonglong>(array.size())) {
                return QJsonValue();
            }
            current = array.at(static_cast<qsizetype>(index));
        } else {
            return QJsonValue();
        }
    }

    return current;
}

QString TargetUploaderUtils::resolveXmlPath(const QByteArray& xmlBytes, const QString& path)
{
    if (!isValidXmlPath(path)) {
        return { };
    }

    QDomDocument xml;
    const QDomDocument::ParseResult parseResult = xml.setContent(xmlBytes);
    if (!parseResult) {
        return { };
    }

    const QStringList parts = path.mid(1).split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.isEmpty()) {
        return { };
    }

    QDomNode current = xml.documentElement();
    if (current.isNull() || current.nodeName() != parts.first()) {
        return { };
    }

    for (int i = 1; i < parts.size(); ++i) {
        current = resolveXmlSegment(current, parts.at(i));
        if (current.isNull()) {
            return { };
        }
    }

    return current.toElement().text();
}
