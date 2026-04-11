#pragma once

#include "targetdiagnostic.h"

#include <QJsonValue>
#include <QMap>
#include <QList>
#include <QString>

enum class RequestBodyType
{
    Multipart,
    Raw,
    FormUrlencoded,
    Json
};

struct ParsedRequestConfig
{
    QString url;
    QString method;
    RequestBodyType type = RequestBodyType::Multipart;
    QMap<QString, QString> headers;
    QMap<QString, QString> query;
    QString contentType;
    QString fileField;
    QMap<QString, QString> multipartFields;
    QMap<QString, QString> formFields;
    QJsonValue jsonFields;
};

namespace TargetRequestConfigParser {
bool parse(const QJsonObject &target, ParsedRequestConfig *parsed, QList<TargetDiagnostic> *diagnostics = nullptr);
}
