#include "targetuploader_utils.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QtTest>

class TargetUploaderUtilsTest final : public QObject
{
    Q_OBJECT

private slots:
    void objectAndFieldHelpersReturnExpectedValues();
    void substituteHelpersApplyEnvAndFilenameTemplates();
    void applyHeadersAndQueryParametersSubstituteValues();
    void substituteJsonValueWalksNestedObjects();
    void resolveJsonPointerHandlesObjectsArraysAndEscapes();
    void resolveXmlPathHandlesSimpleXPath();
    void encodesLiteralQueryAndFormValues();
    void environmentExpansionDoesNotRecurse();
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

void TargetUploaderUtilsTest::substituteHelpersApplyEnvAndFilenameTemplates()
{
    qputenv("IMSHARE_TEST_TOKEN", "secret");
    const QFileInfo fileInfo(QStringLiteral("/tmp/My File.png"));

    QCOMPARE(TargetUploaderUtils::substituteEnv(QStringLiteral("Bearer ${ENV:IMSHARE_TEST_TOKEN}")),
             QStringLiteral("Bearer secret"));
    QCOMPARE(TargetUploaderUtils::substituteRequestValue(
                 QStringLiteral("${FILENAME}:${ENV:IMSHARE_TEST_TOKEN}"), fileInfo),
             QStringLiteral("My File.png:secret"));
    QCOMPARE(TargetUploaderUtils::applyUrlTemplate(
                 QStringLiteral("https://example.test/${FILENAME}?token=${ENV:IMSHARE_TEST_TOKEN}"), fileInfo),
             QStringLiteral("https://example.test/My%20File.png?token=secret"));
}

void TargetUploaderUtilsTest::applyHeadersAndQueryParametersSubstituteValues()
{
    qputenv("IMSHARE_TEST_HEADER", "value123");
    const QFileInfo fileInfo(QStringLiteral("/tmp/My File.txt"));
    const QJsonObject requestConfig{
        {QStringLiteral("query"),
         QJsonObject{{QStringLiteral("name"), QStringLiteral("${FILENAME}")},
                     {QStringLiteral("token"), QStringLiteral("${ENV:IMSHARE_TEST_HEADER}")}}},
        {QStringLiteral("headers"),
         QJsonObject{{QStringLiteral("Authorization"), QStringLiteral("Bearer ${ENV:IMSHARE_TEST_HEADER}")},
                     {QStringLiteral("X-File"), QStringLiteral("${FILENAME}")},
                     {QStringLiteral("X-Test"), QStringLiteral("plain")}}}};

    QNetworkRequest request;
    TargetUploaderUtils::applyHeaders(requestConfig, fileInfo, request);

    QCOMPARE(request.rawHeader("Authorization"), QByteArray("Bearer value123"));
    QCOMPARE(request.rawHeader("X-File"), QByteArray("My File.txt"));
    QCOMPARE(request.rawHeader("X-Test"), QByteArray("plain"));

    const QUrl url = TargetUploaderUtils::applyQueryParameters(QStringLiteral("https://example.test/upload"),
                                                               requestConfig,
                                                               fileInfo);
    QCOMPARE(url.query(), QStringLiteral("name=My File.txt&token=value123"));
}

void TargetUploaderUtilsTest::substituteJsonValueWalksNestedObjects()
{
    const QFileInfo fileInfo(QStringLiteral("/tmp/My File.txt"));
    const QJsonObject input{
        {QStringLiteral("name"), QStringLiteral("${FILENAME}")},
        {QStringLiteral("nested"),
         QJsonArray{QStringLiteral("a"), QJsonObject{{QStringLiteral("path"), QStringLiteral("${FILENAME}")}}}}};

    const QJsonValue output = TargetUploaderUtils::substituteJsonValue(input, fileInfo);
    QVERIFY(output.isObject());
    QCOMPARE(output.toObject().value(QStringLiteral("name")).toString(), QStringLiteral("My File.txt"));
    QVERIFY(output.toObject().value(QStringLiteral("nested")).isArray());
    QCOMPARE(output.toObject()
                 .value(QStringLiteral("nested"))
                 .toArray()
                 .at(1)
                 .toObject()
                 .value(QStringLiteral("path"))
                 .toString(),
             QStringLiteral("My File.txt"));
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

void TargetUploaderUtilsTest::resolveXmlPathHandlesSimpleXPath()
{
    const QByteArray xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<files>
  <file>
    <url>https://example.test/one</url>
  </file>
  <file>
    <url>https://example.test/two</url>
  </file>
</files>)";

    QCOMPARE(TargetUploaderUtils::resolveXmlPath(xml, QStringLiteral("/files/file[2]/url")),
             QStringLiteral("https://example.test/two"));
    QVERIFY(TargetUploaderUtils::resolveXmlPath(xml, QStringLiteral("/files/missing/url")).isEmpty());
}

void TargetUploaderUtilsTest::encodesLiteralQueryAndFormValues()
{
    const QFileInfo fileInfo(QStringLiteral("/tmp/a+b%20c.txt"));
    const QMap<QString, QString> fields{{QStringLiteral("key+%"), QStringLiteral("a+b%20c & café")},
                                       {QStringLiteral("name"), QStringLiteral("${FILENAME}")}};
    const QByteArray expected("key%2B%25=a%2Bb%2520c%20%26%20caf%C3%A9&name=a%2Bb%2520c.txt");
    QCOMPARE(TargetUploaderUtils::createFormUrlencodedBody(fields, fileInfo), expected);
    const QString endpoint = QStringLiteral("https://example.test/upload?existing=x%2By");
    const auto url = TargetUploaderUtils::applyQueryParameters(endpoint, fields, fileInfo);
    QCOMPARE(url.query(QUrl::FullyEncoded).toUtf8(), QByteArray("existing=x%2By&") + expected);
    QJsonObject jsonFields;
    for (auto it = fields.begin(); it != fields.end(); ++it) {
        jsonFields.insert(it.key(), it.value());
    }
    QCOMPARE(TargetUploaderUtils::applyQueryParameters(endpoint,
        QJsonObject{{QStringLiteral("query"), jsonFields}}, fileInfo), url);
}

void TargetUploaderUtilsTest::environmentExpansionDoesNotRecurse()
{
    qputenv("IMSHARE_TEST_CYCLE", "${ENV:IMSHARE_TEST_CYCLE}");
    qputenv("IMSHARE_TEST_INDIRECT", "${ENV:IMSHARE_TEST_CYCLE}");
    QCOMPARE(TargetUploaderUtils::substituteEnv(QStringLiteral("x${ENV:IMSHARE_TEST_CYCLE}/${ENV:IMSHARE_TEST_INDIRECT}y")),
             QStringLiteral("x${ENV:IMSHARE_TEST_CYCLE}/${ENV:IMSHARE_TEST_CYCLE}y"));
    qunsetenv("IMSHARE_TEST_CYCLE");
    qunsetenv("IMSHARE_TEST_INDIRECT");
}

QTEST_APPLESS_MAIN(TargetUploaderUtilsTest)

#include "test_targetuploader_utils.moc"
