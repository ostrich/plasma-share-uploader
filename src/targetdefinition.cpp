#include "targetdefinition.h"

#include <QJsonArray>

namespace {
QStringList stringListValue(const QJsonObject &object, const char *key)
{
    QStringList values;
    const QJsonValue value = object.value(QLatin1StringView(key));
    if (!value.isArray()) {
        return values;
    }

    for (const QJsonValue &entry : value.toArray()) {
        const QString text = entry.toString();
        if (!text.isEmpty()) {
            values.append(text);
        }
    }
    return values;
}
}

QString TargetDefinition::id() const
{
    return config.value(QStringLiteral("id")).toString();
}

QString TargetDefinition::displayName() const
{
    const QString name = config.value(QStringLiteral("displayName")).toString();
    return name.isEmpty() ? id() : name;
}

QString TargetDefinition::description() const
{
    return config.value(QStringLiteral("description")).toString();
}

QString TargetDefinition::icon() const
{
    const QString value = config.value(QStringLiteral("icon")).toString();
    return value.isEmpty() ? QStringLiteral("image-x-generic") : value;
}

QStringList TargetDefinition::constraints() const
{
    return stringListValue(config, "constraints");
}

QStringList TargetDefinition::extensions() const
{
    return stringListValue(config, "extensions");
}
