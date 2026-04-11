#pragma once

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

inline QString testSourceDir()
{
    return QString::fromUtf8(IMSHARE_TEST_SOURCE_DIR);
}

inline QString fixtureScriptPath(const QString &fileName)
{
    return QDir(testSourceDir()).filePath(QStringLiteral("fixtures/scripts/%1").arg(fileName));
}

inline QString pythonExecutable()
{
    return QStandardPaths::findExecutable(QStringLiteral("python3"));
}

inline QByteArray tinyPng()
{
    return QByteArray::fromHex(
        "89504E470D0A1A0A"
        "0000000D49484452000000010000000108060000001F15C489"
        "0000000A49444154789C6360000002000154A24F5D00000000"
        "49454E44AE426082");
}

inline QString writeTempFile(QTemporaryDir &dir, const QString &name, const QByteArray &data)
{
    const QString path = dir.filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return QString();
    }
    file.write(data);
    file.close();
    return path;
}

inline QJsonArray stringArray(std::initializer_list<QString> values)
{
    QJsonArray array;
    for (const QString &value : values) {
        array.append(value);
    }
    return array;
}

inline QJsonObject commandObject(std::initializer_list<QString> argv)
{
    return QJsonObject{{QStringLiteral("argv"), stringArray(argv)}};
}
