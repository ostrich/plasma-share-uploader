#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

struct TargetDefinition {
    QJsonObject config;

    QString id() const;
    QString displayName() const;
    QString description() const;
    QString icon() const;
    QStringList constraints() const;
    QStringList extensions() const;
};
