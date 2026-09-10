#pragma once

#include "targetrequestconfigparser.h"
#include <QObject>
#include <QMap>
#include <functional>

// References are shared by name; deleting a target never deletes its credentials.
class CredentialStore : public QObject
{
public:
    using Values = QMap<QString, QString>;
    using ReadCallback = std::function<void(Values, QString)>;
    using WriteCallback = std::function<void(QString)>;
    using QObject::QObject;
    virtual void read(const QStringList &keys, QObject *context, ReadCallback callback);
    virtual void write(const QString &key, const QString &value, QObject *context, WriteCallback callback);
    virtual void remove(const QString &key, QObject *context, WriteCallback callback);

    static QStringList requestStrings(const ParsedRequestConfig &request);
    static QStringList walletKeys(const ParsedRequestConfig &request);
    static QStringList missingEnvironment(const ParsedRequestConfig &request);
    static QStringList secretValues(const ParsedRequestConfig &request, const Values &walletValues);
    static QString redact(QString text, const QStringList &secrets);
    static bool validKey(const QString &key);
};
