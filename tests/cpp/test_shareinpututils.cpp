#include "shareinpututils.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>

#include "testutils.h"

class ShareInputUtilsTest final : public QObject
{
    Q_OBJECT

private slots:
    void collectsUrlsArrayOfExistingLocalFiles();
    void fallsBackToSingleUrlWhenArrayHasNoFiles();
    void ignoresRemoteAndMissingFiles();
};

void ShareInputUtilsTest::collectsUrlsArrayOfExistingLocalFiles()
{
    QTemporaryDir dir;
    const QString first = writeTempFile(dir, QStringLiteral("a.txt"), "a");
    const QString second = writeTempFile(dir, QStringLiteral("b.txt"), "b");

    const QJsonObject data{
        {QStringLiteral("urls"),
         QJsonArray{QUrl::fromLocalFile(first).toString(), QUrl::fromLocalFile(second).toString()}}};

    const QStringList paths = collectSharedFilePaths(data);

    QCOMPARE(paths, QStringList({first, second}));
}

void ShareInputUtilsTest::fallsBackToSingleUrlWhenArrayHasNoFiles()
{
    QTemporaryDir dir;
    const QString filePath = writeTempFile(dir, QStringLiteral("single.txt"), "hello");
    const QJsonObject data{
        {QStringLiteral("urls"), QJsonArray{QStringLiteral("https://example.test/file")}},
        {QStringLiteral("url"), QUrl::fromLocalFile(filePath).toString()}};

    const QStringList paths = collectSharedFilePaths(data);

    QCOMPARE(paths, QStringList({filePath}));
}

void ShareInputUtilsTest::ignoresRemoteAndMissingFiles()
{
    const QJsonObject data{
        {QStringLiteral("urls"),
         QJsonArray{
             QStringLiteral("https://example.test/file"),
             QUrl::fromLocalFile(QStringLiteral("/definitely/missing.txt")).toString(),
         }}};

    QVERIFY(collectSharedFilePaths(data).isEmpty());
}

QTEST_APPLESS_MAIN(ShareInputUtilsTest)

#include "test_shareinpututils.moc"
