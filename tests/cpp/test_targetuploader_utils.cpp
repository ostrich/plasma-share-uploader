#include "targetuploader_utils.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QtTest>

class TargetUploaderUtilsTest final : public QObject
{
    Q_OBJECT

private slots:
    void objectAndFieldHelpersReturnExpectedValues();
    void substituteEnvAndApplyUrlTemplateWork();
    void applyHeadersSubstitutesEnvironmentValues();
    void resolveJsonPointerHandlesObjectsArraysAndEscapes();
};

void TargetUploaderUtilsTest::objectAndFieldHelpersReturnExpectedValues()
{
    const QJsonObject fields{{QStringLiteral("token"), QStringLiteral("abc")}};
    const QJsonObject parent{
        {QStringLiteral("child"), QJsonObject{{QStringLiteral("name"), QStringLiteral("demo")}}},
        {QStringLiteral("fields"), fields},
        {QStringLiteral("plain"), QStringLiteral("value")}};

    QCOMPARE(TargetUploaderUtils::stringValue(parent, "plain"), QStringLiteral("value"));
    QCOMPARE(TargetUploaderUtils::objectValue(parent, "child").value(QStringLiteral("name")).toString(),
             QStringLiteral("demo"));
    QCOMPARE(TargetUploaderUtils::fieldMap(parent), fields);
}

void TargetUploaderUtilsTest::substituteEnvAndApplyUrlTemplateWork()
{
    qputenv("IMSHARE_TEST_TOKEN", "secret");
    const QFileInfo fileInfo(QStringLiteral("/tmp/My File.png"));

    QCOMPARE(TargetUploaderUtils::substituteEnv(QStringLiteral("Bearer ${ENV:IMSHARE_TEST_TOKEN}")),
             QStringLiteral("Bearer secret"));
    QCOMPARE(TargetUploaderUtils::applyUrlTemplate(
                 QStringLiteral("https://example.test/${FILENAME}?token=${ENV:IMSHARE_TEST_TOKEN}"), fileInfo),
             QStringLiteral("https://example.test/My%20File.png?token=secret"));
}

void TargetUploaderUtilsTest::applyHeadersSubstitutesEnvironmentValues()
{
    qputenv("IMSHARE_TEST_HEADER", "value123");
    const QJsonObject requestConfig{
        {QStringLiteral("headers"),
         QJsonObject{{QStringLiteral("Authorization"), QStringLiteral("Bearer ${ENV:IMSHARE_TEST_HEADER}")},
                     {QStringLiteral("X-Test"), QStringLiteral("plain")}}}};

    QNetworkRequest request;
    TargetUploaderUtils::applyHeaders(requestConfig, request);

    QCOMPARE(request.rawHeader("Authorization"), QByteArray("Bearer value123"));
    QCOMPARE(request.rawHeader("X-Test"), QByteArray("plain"));
}

void TargetUploaderUtilsTest::resolveJsonPointerHandlesObjectsArraysAndEscapes()
{
    const QJsonObject root{
        {QStringLiteral("data"),
         QJsonArray{
             QJsonObject{{QStringLiteral("url"), QStringLiteral("https://example.test/1")}},
             QJsonObject{{QStringLiteral("a/b"), QStringLiteral("slash")},
                         {QStringLiteral("m~n"), QStringLiteral("tilde")}},
         }}};

    QCOMPARE(TargetUploaderUtils::resolveJsonPointer(QJsonValue(root), QStringLiteral("/data/0/url")).toString(),
             QStringLiteral("https://example.test/1"));
    QCOMPARE(TargetUploaderUtils::resolveJsonPointer(QJsonValue(root), QStringLiteral("/data/1/a~1b")).toString(),
             QStringLiteral("slash"));
    QCOMPARE(TargetUploaderUtils::resolveJsonPointer(QJsonValue(root), QStringLiteral("/data/1/m~0n")).toString(),
             QStringLiteral("tilde"));
    QVERIFY(TargetUploaderUtils::resolveJsonPointer(QJsonValue(root), QStringLiteral("/data/5")).isNull());
}

QTEST_APPLESS_MAIN(TargetUploaderUtilsTest)

#include "test_targetuploader_utils.moc"
