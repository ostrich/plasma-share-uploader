#pragma once
#include <QJsonValue>
#include <QStringList>
namespace ConfigJson {
QJsonValue fromVariant(const QVariant& value);
QString text(const QJsonValue& value);
bool parse(const QString& text, QJsonValue* value, QString* error);
QJsonValue get(QJsonValue root, const QStringList& path);
QJsonValue set(QJsonValue root, const QStringList& path, const QJsonValue& value);
}
