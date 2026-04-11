#include "targetuploader_utils.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QUrl>

namespace {
QString decodeJsonPointerToken(const QString &token)
{
    QString out = token;
    out.replace(QStringLiteral("~1"), QStringLiteral("/"));
    out.replace(QStringLiteral("~0"), QStringLiteral("~"));
    return out;
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

QString TargetUploaderUtils::applyUrlTemplate(const QString &urlTemplate, const QFileInfo &fileInfo)
{
    QString url = substituteEnv(urlTemplate);
    const QByteArray encodedName = QUrl::toPercentEncoding(fileInfo.fileName());
    url.replace(QStringLiteral("${FILENAME}"), QString::fromUtf8(encodedName));
    return url;
}

void TargetUploaderUtils::applyHeaders(const QJsonObject &requestConfig, QNetworkRequest &requestObj)
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
