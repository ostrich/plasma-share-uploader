#include "preuploadprocessor.h"
#include "targetuploader.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>

#include "httpcaptureserver.h"
#include "testutils.h"

class TargetUploaderTest final : public QObject
{
    Q_OBJECT

private slots:
    void rawUploadSendsExpectedRequestAndParsesTextResponse();
    void multipartUploadSendsFieldsAndParsesJsonPointer();
    void rawUploadUsesPreprocessedFileContents();
    void regexParserExtractsUrl();
    void parserErrorsAreReported();
    void rejectsMissingFileAndNullManager();
    void idAndDisplayNameFallbacksWork();

private:
    UploadResult runUpload(TargetUploader &uploader, const QString &filePath, QNetworkAccessManager &manager);
};

UploadResult TargetUploaderTest::runUpload(TargetUploader &uploader, const QString &filePath, QNetworkAccessManager &manager)
{
    QNetworkReply *reply = uploader.upload(filePath, &manager);
    if (!reply) {
        QTest::qFail("upload() returned nullptr", __FILE__, __LINE__);
        return {};
    }

    QSignalSpy finishedSpy(reply, &QNetworkReply::finished);
    if (finishedSpy.isEmpty() && !finishedSpy.wait(5000)) {
        QTest::qFail("Timed out waiting for upload reply", __FILE__, __LINE__);
        reply->deleteLater();
        return {};
    }
    if (reply->error() != QNetworkReply::NoError) {
        const QByteArray error = reply->errorString().toUtf8();
        QTest::qFail(error.constData(), __FILE__, __LINE__);
        reply->deleteLater();
        return {};
    }

    const UploadResult result = uploader.parseReply(reply);
    reply->deleteLater();
    return result;
}

void TargetUploaderTest::rawUploadSendsExpectedRequestAndParsesTextResponse()
{
    HttpCaptureServer server;
    QVERIFY(server.start());
    server.enqueueResponse({200, "OK", "text/plain", "https://files.example/raw"});

    qputenv("IMSHARE_UPLOAD_TOKEN", "secret");
    QTemporaryDir dir;
    const QString filePath = writeTempFile(dir, QStringLiteral("My File.txt"), "raw-body");
    const QString uploadUrl =
        QStringLiteral("http://127.0.0.1:%1/upload/${FILENAME}").arg(server.serverPort());
    const QJsonObject config{
        {QStringLiteral("id"), QStringLiteral("raw")},
        {QStringLiteral("displayName"), QStringLiteral("Raw Target")},
        {QStringLiteral("request"),
         QJsonObject{
             {QStringLiteral("url"), uploadUrl},
             {QStringLiteral("method"), QStringLiteral("PUT")},
             {QStringLiteral("type"), QStringLiteral("raw")},
             {QStringLiteral("headers"),
              QJsonObject{{QStringLiteral("Authorization"), QStringLiteral("Bearer ${ENV:IMSHARE_UPLOAD_TOKEN}")}}},
         }},
        {QStringLiteral("response"), QJsonObject{{QStringLiteral("type"), QStringLiteral("text_url")}}}};

    TargetUploader uploader(config);
    QNetworkAccessManager manager;
    const UploadResult result = runUpload(uploader, filePath, manager);

    QVERIFY(result.ok);
    QCOMPARE(result.url, QStringLiteral("https://files.example/raw"));
    QCOMPARE(server.requests().size(), 1);
    const CapturedHttpRequest request = server.requests().first();
    QCOMPARE(request.method, QByteArray("PUT"));
    QCOMPARE(request.path, QByteArray("/upload/My%20File.txt"));
    QCOMPARE(request.body, QByteArray("raw-body"));
    QCOMPARE(request.headers.value("authorization"), QByteArray("Bearer secret"));
}

void TargetUploaderTest::multipartUploadSendsFieldsAndParsesJsonPointer()
{
    HttpCaptureServer server;
    QVERIFY(server.start());
    server.enqueueResponse({200, "OK", "application/json", R"({"data":{"url":"https://files.example/json"}})"});

    QTemporaryDir dir;
    const QString filePath = writeTempFile(dir, QStringLiteral("image.png"), tinyPng());
    const QJsonObject config{
        {QStringLiteral("id"), QStringLiteral("multipart")},
        {QStringLiteral("request"),
         QJsonObject{
             {QStringLiteral("url"), server.url(QStringLiteral("/multipart")).toString()},
             {QStringLiteral("method"), QStringLiteral("POST")},
             {QStringLiteral("multipart"),
              QJsonObject{
                  {QStringLiteral("fileField"), QStringLiteral("file")},
                  {QStringLiteral("fields"), QJsonObject{{QStringLiteral("token"), QStringLiteral("abc123")}}},
              }},
         }},
        {QStringLiteral("response"),
         QJsonObject{{QStringLiteral("type"), QStringLiteral("json_pointer")},
                     {QStringLiteral("pointer"), QStringLiteral("/data/url")}}}};

    TargetUploader uploader(config);
    QNetworkAccessManager manager;
    const UploadResult result = runUpload(uploader, filePath, manager);

    QVERIFY(result.ok);
    QCOMPARE(result.url, QStringLiteral("https://files.example/json"));
    QCOMPARE(server.requests().size(), 1);
    const CapturedHttpRequest request = server.requests().first();
    QCOMPARE(request.method, QByteArray("POST"));
    QVERIFY(request.headers.value("content-type").startsWith("multipart/form-data; boundary="));
    QVERIFY(request.body.contains("name=\"token\""));
    QVERIFY(request.body.contains("abc123"));
    QVERIFY(request.body.contains("name=\"file\"; filename=\"image.png\""));
    QVERIFY(request.body.contains(tinyPng()));
}

void TargetUploaderTest::rawUploadUsesPreprocessedFileContents()
{
    QVERIFY2(!pythonExecutable().isEmpty(), "python3 is required for realistic preupload integration tests");

    HttpCaptureServer server;
    QVERIFY(server.start());
    server.enqueueResponse({200, "OK", "text/plain", "https://files.example/processed"});

    QTemporaryDir dir;
    const QString sourcePath = writeTempFile(dir, QStringLiteral("payload.txt"), "ORIGINAL");
    const QJsonObject preprocessConfig{
        {QStringLiteral("preUpload"),
         QJsonArray{QJsonObject{
             {QStringLiteral("mime"), QJsonArray{QStringLiteral("*/*")}},
             {QStringLiteral("fileHandling"), QStringLiteral("inplace_copy")},
             {QStringLiteral("commands"),
              QJsonArray{
                  commandObject({pythonExecutable(), fixtureScriptPath(QStringLiteral("append_text.py")),
                                 QStringLiteral("${FILE}"), QStringLiteral("-UPLOADED")}),
              }},
         }}}};

    const PreUploadProcessor::Result prepared =
        PreUploadProcessor::preprocessFile(preprocessConfig, sourcePath);
    QVERIFY(prepared.ok);
    QVERIFY(prepared.uploadPath != sourcePath);

    const QJsonObject uploadConfig{
        {QStringLiteral("id"), QStringLiteral("raw")},
        {QStringLiteral("displayName"), QStringLiteral("Raw Target")},
        {QStringLiteral("request"),
         QJsonObject{
             {QStringLiteral("url"), server.url(QStringLiteral("/upload")).toString()},
             {QStringLiteral("method"), QStringLiteral("PUT")},
             {QStringLiteral("type"), QStringLiteral("raw")},
         }},
        {QStringLiteral("response"), QJsonObject{{QStringLiteral("type"), QStringLiteral("text_url")}}}};

    TargetUploader uploader(uploadConfig);
    QNetworkAccessManager manager;
    const UploadResult result = runUpload(uploader, prepared.uploadPath, manager);

    QVERIFY(result.ok);
    QCOMPARE(server.requests().size(), 1);
    QCOMPARE(server.requests().first().body, QByteArray("ORIGINAL-UPLOADED"));

    QFile original(sourcePath);
    QVERIFY(original.open(QIODevice::ReadOnly));
    QCOMPARE(original.readAll(), QByteArray("ORIGINAL"));

    QVERIFY(QDir(prepared.tempDirPath).removeRecursively());
}

void TargetUploaderTest::regexParserExtractsUrl()
{
    HttpCaptureServer server;
    QVERIFY(server.start());
    server.enqueueResponse({200, "OK", "text/plain", "Uploaded to https://files.example/regex"});

    QTemporaryDir dir;
    const QString filePath = writeTempFile(dir, QStringLiteral("note.txt"), "regex");
    const QJsonObject config{
        {QStringLiteral("id"), QStringLiteral("regex")},
        {QStringLiteral("request"),
         QJsonObject{
             {QStringLiteral("url"), server.url(QStringLiteral("/regex")).toString()},
             {QStringLiteral("method"), QStringLiteral("POST")},
             {QStringLiteral("type"), QStringLiteral("raw")},
         }},
        {QStringLiteral("response"),
         QJsonObject{{QStringLiteral("type"), QStringLiteral("regex")},
                     {QStringLiteral("pattern"), QStringLiteral(R"(Uploaded to (https://\S+))")},
                     {QStringLiteral("group"), 1}}}};

    TargetUploader uploader(config);
    QNetworkAccessManager manager;
    const UploadResult result = runUpload(uploader, filePath, manager);

    QVERIFY(result.ok);
    QCOMPARE(result.url, QStringLiteral("https://files.example/regex"));
}

void TargetUploaderTest::parserErrorsAreReported()
{
    QTemporaryDir dir;
    const QString filePath = writeTempFile(dir, QStringLiteral("note.txt"), "body");
    QNetworkAccessManager manager;

    {
        HttpCaptureServer server;
        QVERIFY(server.start());
        server.enqueueResponse({200, "OK", "text/plain", "not-a-url"});
        TargetUploader uploader(QJsonObject{
            {QStringLiteral("id"), QStringLiteral("badtext")},
            {QStringLiteral("request"),
             QJsonObject{{QStringLiteral("url"), server.url(QStringLiteral("/badtext")).toString()},
                         {QStringLiteral("method"), QStringLiteral("POST")},
                         {QStringLiteral("type"), QStringLiteral("raw")}}},
            {QStringLiteral("response"), QJsonObject{{QStringLiteral("type"), QStringLiteral("text_url")}}}});
        const UploadResult result = runUpload(uploader, filePath, manager);
        QVERIFY(!result.ok);
        QCOMPARE(result.errorMessage, QStringLiteral("not-a-url"));
    }

    {
        HttpCaptureServer server;
        QVERIFY(server.start());
        server.enqueueResponse({200, "OK", "application/json", "not json"});
        TargetUploader uploader(QJsonObject{
            {QStringLiteral("id"), QStringLiteral("badjson")},
            {QStringLiteral("request"),
             QJsonObject{{QStringLiteral("url"), server.url(QStringLiteral("/badjson")).toString()},
                         {QStringLiteral("method"), QStringLiteral("POST")},
                         {QStringLiteral("type"), QStringLiteral("raw")}}},
            {QStringLiteral("response"),
             QJsonObject{{QStringLiteral("type"), QStringLiteral("json_pointer")},
                         {QStringLiteral("pointer"), QStringLiteral("/data/url")}}}});
        const UploadResult result = runUpload(uploader, filePath, manager);
        QVERIFY(!result.ok);
        QCOMPARE(result.errorMessage, QStringLiteral("Upload response was not valid JSON."));
    }

    {
        HttpCaptureServer server;
        QVERIFY(server.start());
        server.enqueueResponse({200, "OK", "application/json", R"({"data":{"url":42}})"});
        TargetUploader uploader(QJsonObject{
            {QStringLiteral("id"), QStringLiteral("missingurl")},
            {QStringLiteral("request"),
             QJsonObject{{QStringLiteral("url"), server.url(QStringLiteral("/missingurl")).toString()},
                         {QStringLiteral("method"), QStringLiteral("POST")},
                         {QStringLiteral("type"), QStringLiteral("raw")}}},
            {QStringLiteral("response"),
             QJsonObject{{QStringLiteral("type"), QStringLiteral("json_pointer")},
                         {QStringLiteral("pointer"), QStringLiteral("/data/url")}}}});
        const UploadResult result = runUpload(uploader, filePath, manager);
        QVERIFY(!result.ok);
        QCOMPARE(result.errorMessage, QStringLiteral("Upload response did not contain a URL."));
    }

    {
        HttpCaptureServer server;
        QVERIFY(server.start());
        server.enqueueResponse({200, "OK", "text/plain", "no match here"});
        TargetUploader uploader(QJsonObject{
            {QStringLiteral("id"), QStringLiteral("nomatch")},
            {QStringLiteral("request"),
             QJsonObject{{QStringLiteral("url"), server.url(QStringLiteral("/nomatch")).toString()},
                         {QStringLiteral("method"), QStringLiteral("POST")},
                         {QStringLiteral("type"), QStringLiteral("raw")}}},
            {QStringLiteral("response"),
             QJsonObject{{QStringLiteral("type"), QStringLiteral("regex")},
                         {QStringLiteral("pattern"), QStringLiteral(R"((https://\S+))")}}}});
        const UploadResult result = runUpload(uploader, filePath, manager);
        QVERIFY(!result.ok);
        QCOMPARE(result.errorMessage, QStringLiteral("no match here"));
    }
}

void TargetUploaderTest::rejectsMissingFileAndNullManager()
{
    const QJsonObject config{
        {QStringLiteral("id"), QStringLiteral("raw")},
        {QStringLiteral("request"),
         QJsonObject{{QStringLiteral("url"), QStringLiteral("https://example.test/upload")},
                     {QStringLiteral("method"), QStringLiteral("POST")},
                     {QStringLiteral("type"), QStringLiteral("raw")}}},
        {QStringLiteral("response"), QJsonObject{{QStringLiteral("type"), QStringLiteral("text_url")}}}};

    TargetUploader uploader(config);
    QNetworkAccessManager manager;

    QVERIFY(uploader.upload(QStringLiteral("/missing/file.txt"), &manager) == nullptr);
    QVERIFY(uploader.upload(QStringLiteral("/missing/file.txt"), nullptr) == nullptr);
}

void TargetUploaderTest::idAndDisplayNameFallbacksWork()
{
    TargetUploader uploader(QJsonObject{
        {QStringLiteral("id"), QStringLiteral("target-id")},
        {QStringLiteral("request"),
         QJsonObject{{QStringLiteral("url"), QStringLiteral("https://example.test/upload")},
                     {QStringLiteral("method"), QStringLiteral("POST")},
                     {QStringLiteral("type"), QStringLiteral("raw")}}},
        {QStringLiteral("response"), QJsonObject{{QStringLiteral("type"), QStringLiteral("text_url")}}}});

    QCOMPARE(uploader.id(), QStringLiteral("target-id"));
    QCOMPARE(uploader.displayName(), QStringLiteral("target-id"));
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    TargetUploaderTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_targetuploader.moc"
