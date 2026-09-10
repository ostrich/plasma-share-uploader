#include "jsonutils.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJSValue>

QJsonValue ConfigJson::fromVariant(const QVariant& value)
{
    // QML object/array arguments arrive wrapped in QJSValue, unlike C++ QVariantMap/List.
    return QJsonValue::fromVariant(
        value.metaType() == QMetaType::fromType<QJSValue>() ? value.value<QJSValue>().toVariant() : value);
}

QString ConfigJson::text(const QJsonValue& value)
{
    if (value.isObject())
        return QString::fromUtf8(QJsonDocument(value.toObject()).toJson());
    if (value.isArray())
        return QString::fromUtf8(QJsonDocument(value.toArray()).toJson());
    const auto wrapped = QJsonDocument(QJsonArray { value }).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(wrapped.mid(1, wrapped.size() - 2));
}
bool ConfigJson::parse(const QString& text, QJsonValue* value, QString* error)
{
    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson(QByteArray("[") + text.toUtf8() + QByteArray("]"), &parseError);
    if (parseError.error != QJsonParseError::NoError || doc.array().size() != 1) {
        if (error)
            *error = QStringLiteral("Enter one JSON value: %1").arg(parseError.errorString());
        return false;
    }
    if (value)
        *value = doc.array().first();
    return true;
}
QJsonValue ConfigJson::get(QJsonValue root, const QStringList& path)
{
    for (const auto& part : path) {
        if (root.isArray()) {
            bool ok;
            const int index = part.toInt(&ok);
            root = ok && index >= 0 ? root.toArray().at(index) : QJsonValue(QJsonValue::Undefined);
        } else
            root = root.toObject().value(part);
    }
    return root;
}
QJsonValue ConfigJson::set(QJsonValue root, const QStringList& path, const QJsonValue& value)
{
    if (path.isEmpty())
        return value;
    if (root.isArray()) {
        auto array = root.toArray();
        bool ok;
        const int index = path.first().toInt(&ok);
        if (!ok || index < 0 || index >= array.size())
            return root;
        if (path.size() == 1 && value.isUndefined())
            array.removeAt(index);
        else
            array[index] = set(array.at(index), path.mid(1), value);
        return array;
    }
    auto object = root.toObject();
    if (path.size() == 1 && value.isUndefined())
        object.remove(path.first());
    else
        object.insert(path.first(), set(object.value(path.first()), path.mid(1), value));
    return object;
}
