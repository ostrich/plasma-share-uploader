#include "sharejob.h"

#include <KJob>
#include <QClipboard>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>

#include "httpcaptureserver.h"
#include "testutils.h"

class ShareJobTest final : public QObject
{
    Q_OBJECT

private slots:
    void noLocalFilesFails();
    void uploadsMultipleFilesSequentiallyAndUpdatesClipboard();
    void uploadsExposeVariantUrlsAndResponseMetadata();
    void preUploadTransformsUploadedBodyWithoutMutatingSource();
    void preUploadFailureStopsBeforeAnyUpload();
};

void ShareJobTest::noLocalFilesFails()
{
    const QJsonObject config{
        {QStringLiteral("id"), QStringLiteral("raw")},
        {QStringLiteral("displayName"), QStringLiteral("Raw Target")},
        {QStringLiteral("request"),
         QJsonObject{{QStringLiteral("url"), QStringLiteral("https://example.test/upload")},
                     {QStringLiteral("method"), QStringLiteral("POST")},
                     {QStringLiteral("type"), QStringLiteral("raw")}}},
        {QStringLiteral("response"), QJsonObject{{QStringLiteral("type"), QStringLiteral("text_url")}}}};

    ShareJob job(QJsonDocument(config).toJson(QJsonDocument::Compact));
    job.setAutoDelete(false);
    job.setData(QJsonObject{{QStringLiteral("url"), QStringLiteral("https://example.test/remote")}});

    QSignalSpy resultSpy(&job, &KJob::result);
    job.start();

    QTRY_COMPARE(resultSpy.count(), 1);
    QCOMPARE(job.error(), 1);
    QCOMPARE(job.errorText(), QStringLiteral("No local files found to upload."));
}

void ShareJobTest::uploadsMultipleFilesSequentiallyAndUpdatesClipboard()
{
    HttpCaptureServer server;
    QVERIFY(server.start());
    server.enqueueResponse({200, "OK", "text/plain", {}, "https://files.example/one"});
    server.enqueueResponse({200, "OK", "text/plain", {}, "https://files.example/two"});

    QTemporaryDir dir;
    const QString first = writeTempFile(dir, QStringLiteral("one.txt"), "body-one");
    const QString second = writeTempFile(dir, QStringLiteral("two.txt"), "body-two");
    QGuiApplication::clipboard()->clear();
    const QString uploadUrl =
        QStringLiteral("http://127.0.0.1:%1/upload/${FILENAME}").arg(server.serverPort());

    const QJsonObject config{
        {QStringLiteral("id"), QStringLiteral("raw")},
        {QStringLiteral("displayName"), QStringLiteral("Raw Target")},
        {QStringLiteral("request"),
         QJsonObject{{QStringLiteral("url"), uploadUrl},
                     {QStringLiteral("method"), QStringLiteral("PUT")},
                     {QStringLiteral("type"), QStringLiteral("raw")}}},
        {QStringLiteral("response"), QJsonObject{{QStringLiteral("type"), QStringLiteral("text_url")}}}};

    ShareJob job(QJsonDocument(config).toJson(QJsonDocument::Compact));
    job.setAutoDelete(false);
    job.setData(QJsonObject{
        {QStringLiteral("urls"),
         QJsonArray{QUrl::fromLocalFile(first).toString(), QUrl::fromLocalFile(second).toString()}}});

    QSignalSpy resultSpy(&job, &KJob::result);
    job.start();

    QTRY_COMPARE(resultSpy.count(), 1);
    QCOMPARE(job.error(), 0);
    const QJsonObject output = job.output();
    QCOMPARE(output.value(QStringLiteral("url")).toString(), QStringLiteral("https://files.example/one"));
    const QJsonArray urls = output.value(QStringLiteral("urls")).toArray();
    QCOMPARE(urls.size(), 2);
    QCOMPARE(urls.at(0).toString(), QStringLiteral("https://files.example/one"));
    QCOMPARE(urls.at(1).toString(), QStringLiteral("https://files.example/two"));
    QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("https://files.example/one\nhttps://files.example/two"));

    QCOMPARE(server.requests().size(), 2);
    QCOMPARE(server.requests().at(0).path, QByteArray("/upload/one.txt"));
    QCOMPARE(server.requests().at(0).body, QByteArray("body-one"));
    QCOMPARE(server.requests().at(1).path, QByteArray("/upload/two.txt"));
    QCOMPARE(server.requests().at(1).body, QByteArray("body-two"));
}

void ShareJobTest::uploadsExposeVariantUrlsAndResponseMetadata()
{
    HttpCaptureServer server;
    QVERIFY(server.start());
    server.enqueueResponse({200,
                            "OK",
                            "application/json",
                            {{"X-Delete", "https://files.example/delete/1"}},
                            R"({"data":{"url":"https://files.example/main","thumb":"https://files.example/thumb"}})"});

    QTemporaryDir dir;
    const QString filePath = writeTempFile(dir, QStringLiteral("one.txt"), "body-one");
    const QJsonObject config{
        {QStringLiteral("id"), QStringLiteral("raw")},
        {QStringLiteral("displayName"), QStringLiteral("Raw Target")},
        {QStringLiteral("request"),
         QJsonObject{{QStringLiteral("url"), server.url(QStringLiteral("/upload")).toString()},
                     {QStringLiteral("method"), QStringLiteral("PUT")},
                     {QStringLiteral("type"), QStringLiteral("raw")}}},
        {QStringLiteral("response"),
         QJsonObject{{QStringLiteral("type"), QStringLiteral("json_pointer")},
                     {QStringLiteral("pointer"), QStringLiteral("/data/url")},
                     {QStringLiteral("thumbnail"),
                      QJsonObject{{QStringLiteral("type"), QStringLiteral("json_pointer")},
                                  {QStringLiteral("pointer"), QStringLiteral("/data/thumb")}}},
                     {QStringLiteral("deletion"),
                      QJsonObject{{QStringLiteral("type"), QStringLiteral("header")},
                                  {QStringLiteral("name"), QStringLiteral("X-Delete")}}}}}};

    ShareJob job(QJsonDocument(config).toJson(QJsonDocument::Compact));
    job.setAutoDelete(false);
    job.setData(QJsonObject{{QStringLiteral("url"), QUrl::fromLocalFile(filePath).toString()}});

    QSignalSpy resultSpy(&job, &KJob::result);
    job.start();

    QTRY_COMPARE(resultSpy.count(), 1);
    QCOMPARE(job.error(), 0);
    const QJsonObject output = job.output();
    QCOMPARE(output.value(QStringLiteral("url")).toString(), QStringLiteral("https://files.example/main"));
    QCOMPARE(output.value(QStringLiteral("thumbnailUrl")).toString(), QStringLiteral("https://files.example/thumb"));
    QCOMPARE(output.value(QStringLiteral("deletionUrl")).toString(), QStringLiteral("https://files.example/delete/1"));
    const QJsonArray results = output.value(QStringLiteral("results")).toArray();
    QCOMPARE(results.size(), 1);
    const QJsonObject result = results.first().toObject();
    QCOMPARE(result.value(QStringLiteral("url")).toString(), QStringLiteral("https://files.example/main"));
    QCOMPARE(result.value(QStringLiteral("thumbnailUrl")).toString(), QStringLiteral("https://files.example/thumb"));
    QCOMPARE(result.value(QStringLiteral("deletionUrl")).toString(), QStringLiteral("https://files.example/delete/1"));
    const QJsonObject response = result.value(QStringLiteral("response")).toObject();
    QCOMPARE(response.value(QStringLiteral("statusCode")).toInt(), 200);
    QCOMPARE(response.value(QStringLiteral("responseUrl")).toString(), server.url(QStringLiteral("/upload")).toString());
    QCOMPARE(response.value(QStringLiteral("headers")).toObject().value(QStringLiteral("x-delete")).toString(),
             QStringLiteral("https://files.example/delete/1"));
}

void ShareJobTest::preUploadTransformsUploadedBodyWithoutMutatingSource()
{
    QVERIFY2(!pythonExecutable().isEmpty(), "python3 is required for sharejob preupload tests");

    HttpCaptureServer server;
    QVERIFY(server.start());
    server.enqueueResponse({200, "OK", "text/plain", {}, "https://files.example/processed"});

    QTemporaryDir dir;
    const QString filePath = writeTempFile(dir, QStringLiteral("sample.txt"), "payload");
    const QJsonObject config{
        {QStringLiteral("id"), QStringLiteral("raw")},
        {QStringLiteral("displayName"), QStringLiteral("Raw Target")},
        {QStringLiteral("request"),
         QJsonObject{{QStringLiteral("url"), server.url(QStringLiteral("/upload")).toString()},
                     {QStringLiteral("method"), QStringLiteral("PUT")},
                     {QStringLiteral("type"), QStringLiteral("raw")}}},
        {QStringLiteral("preUpload"),
         QJsonArray{QJsonObject{
             {QStringLiteral("mime"), QJsonArray{QStringLiteral("*/*")}},
             {QStringLiteral("fileHandling"), QStringLiteral("inplace_copy")},
             {QStringLiteral("commands"),
              QJsonArray{commandObject({pythonExecutable(), fixtureScriptPath(QStringLiteral("append_text.py")),
                                        QStringLiteral("${FILE}"), QStringLiteral("-extra")})}},
         }}},
        {QStringLiteral("response"), QJsonObject{{QStringLiteral("type"), QStringLiteral("text_url")}}}};

    ShareJob job(QJsonDocument(config).toJson(QJsonDocument::Compact));
    job.setAutoDelete(false);
    job.setData(QJsonObject{{QStringLiteral("url"), QUrl::fromLocalFile(filePath).toString()}});

    QSignalSpy resultSpy(&job, &KJob::result);
    job.start();

    QTRY_COMPARE(resultSpy.count(), 1);
    QCOMPARE(job.error(), 0);
    QCOMPARE(server.requests().size(), 1);
    QCOMPARE(server.requests().first().body, QByteArray("payload-extra"));
    QFile original(filePath);
    QVERIFY(original.open(QIODevice::ReadOnly));
    QCOMPARE(original.readAll(), QByteArray("payload"));
}

void ShareJobTest::preUploadFailureStopsBeforeAnyUpload()
{
    QVERIFY2(!pythonExecutable().isEmpty(), "python3 is required for sharejob preupload tests");

    HttpCaptureServer server;
    QVERIFY(server.start());

    QTemporaryDir dir;
    const QString filePath = writeTempFile(dir, QStringLiteral("sample.txt"), "payload");
    const QJsonObject config{
        {QStringLiteral("id"), QStringLiteral("raw")},
        {QStringLiteral("displayName"), QStringLiteral("Raw Target")},
        {QStringLiteral("request"),
         QJsonObject{{QStringLiteral("url"), server.url(QStringLiteral("/upload")).toString()},
                     {QStringLiteral("method"), QStringLiteral("PUT")},
                     {QStringLiteral("type"), QStringLiteral("raw")}}},
        {QStringLiteral("preUpload"),
         QJsonArray{QJsonObject{
             {QStringLiteral("mime"), QJsonArray{QStringLiteral("*/*")}},
             {QStringLiteral("fileHandling"), QStringLiteral("inplace_copy")},
             {QStringLiteral("commands"),
              QJsonArray{commandObject({pythonExecutable(), fixtureScriptPath(QStringLiteral("fail.py")),
                                        QStringLiteral("${FILE}"), QStringLiteral("explode")})}},
         }}},
        {QStringLiteral("response"), QJsonObject{{QStringLiteral("type"), QStringLiteral("text_url")}}}};

    ShareJob job(QJsonDocument(config).toJson(QJsonDocument::Compact));
    job.setAutoDelete(false);
    job.setData(QJsonObject{{QStringLiteral("url"), QUrl::fromLocalFile(filePath).toString()}});

    QSignalSpy resultSpy(&job, &KJob::result);
    job.start();

    QTRY_COMPARE(resultSpy.count(), 1);
    QCOMPARE(job.error(), 1);
    QCOMPARE(job.errorText(), QStringLiteral("explode"));
    QCOMPARE(server.requests().size(), 0);
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    ShareJobTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_sharejob.moc"
