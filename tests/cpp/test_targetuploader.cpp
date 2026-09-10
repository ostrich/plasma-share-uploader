#include "preuploadprocessor.h"
#include "targetuploader.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>

#include "httpcaptureserver.h"
#include "testutils.h"

class TargetUploaderTest final : public QObject {
    Q_OBJECT

private slots:
    void rawUploadSendsExpectedRequestAndParsesTextResponse();
    void multipartUploadSendsFieldsAndParsesJsonPointer();
    void formUrlencodedUploadSendsFieldsAndParsesHeaderResponse();
    void jsonUploadSendsStructuredBodyAndParsesXmlXPath();
    void rawUploadUsesPreprocessedFileContents();
    void regexParserExtractsUrl();
    void redirectUrlParserExtractsLocationHeader();
    void errorExtractorSurfacesStructuredServerErrors();
    void successExtractorsPopulateVariantUrlsAndResponseMetadata();
    void parserErrorsAreReported();
    void rejectsMissingFileAndNullManager();
    void idAndDisplayNameFallbacksWork();
    void interruptedResponseFails();
    void relativeRedirectResolvesAgainstReplyUrl();
    void multipartEscapesHeaderParameters();
    void jsonScalarBodies_data();
    void jsonScalarBodies();
    void cheveretoExampleExtractsDocumentedResponse();
    void responseUrlsMustBeUsable_data();
    void responseUrlsMustBeUsable();
    void responseJsonPointerSupportsRootAndEmptyKey();
    void invalidEndpointsDoNotSendRequests();

private:
    UploadResult runUpload(TargetUploader& uploader, const QString& filePath, QNetworkAccessManager& manager);
};

UploadResult TargetUploaderTest::runUpload(
    TargetUploader& uploader, const QString& filePath, QNetworkAccessManager& manager)
{
    QNetworkReply* reply = uploader.upload(filePath, &manager);
    if (!reply) {
        QTest::qFail("upload() returned nullptr", __FILE__, __LINE__);
        return { };
    }

    QSignalSpy finishedSpy(reply, &QNetworkReply::finished);
    if (finishedSpy.isEmpty() && !finishedSpy.wait(5000)) {
        QTest::qFail("Timed out waiting for upload reply", __FILE__, __LINE__);
        reply->deleteLater();
        return { };
    }
    if (reply->error() != QNetworkReply::NoError
        && !reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).isValid()) {
        const QByteArray error = reply->errorString().toUtf8();
        QTest::qFail(error.constData(), __FILE__, __LINE__);
        reply->deleteLater();
        return { };
    }

    const UploadResult result = uploader.parseReply(reply);
    reply->deleteLater();
    return result;
}

void TargetUploaderTest::rawUploadSendsExpectedRequestAndParsesTextResponse()
{
    HttpCaptureServer server;
    QVERIFY(server.start());
    server.enqueueResponse({ 200, "OK", "text/plain", { }, "https://files.example/raw" });

    qputenv("IMSHARE_UPLOAD_TOKEN", "secret");
    QTemporaryDir dir;
    const QString filePath = writeTempFile(dir, QStringLiteral("My File.txt"), "raw-body");
    const QString uploadUrl = QStringLiteral("http://127.0.0.1:%1/upload/${FILENAME}").arg(server.serverPort());
    const QJsonObject config { { QStringLiteral("schemaVersion"), 1 }, { QStringLiteral("id"), QStringLiteral("raw") },
        { QStringLiteral("displayName"), QStringLiteral("Raw Target") },
        { QStringLiteral("request"),
            QJsonObject { { QStringLiteral("url"), uploadUrl }, { QStringLiteral("method"), QStringLiteral("PUT") },
                { QStringLiteral("headers"),
                    QJsonObject {
                        { QStringLiteral("Authorization"), QStringLiteral("Bearer ${ENV:IMSHARE_UPLOAD_TOKEN}") } } },
                { QStringLiteral("body"), QJsonObject { { QStringLiteral("type"), QStringLiteral("raw") } } } } },
        { QStringLiteral("response"),
            QJsonObject {
                { QStringLiteral("url"), QJsonObject { { QStringLiteral("type"), QStringLiteral("text_url") } } } } } };

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
    server.enqueueResponse({ 200, "OK", "application/json", { }, R"({"data":{"url":"https://files.example/json"}})" });

    QTemporaryDir dir;
    const QString filePath = writeTempFile(dir, QStringLiteral("image.png"), tinyPng());
    const QJsonObject config { { QStringLiteral("schemaVersion"), 1 },
        { QStringLiteral("id"), QStringLiteral("multipart") },
        { QStringLiteral("request"),
            QJsonObject { { QStringLiteral("url"), server.url(QStringLiteral("/multipart")).toString() },
                { QStringLiteral("method"), QStringLiteral("POST") },
                { QStringLiteral("body"),
                    QJsonObject { { QStringLiteral("type"), QStringLiteral("multipart") },
                        { QStringLiteral("fileField"), QStringLiteral("file") },
                        { QStringLiteral("fields"),
                            QJsonObject { { QStringLiteral("token"), QStringLiteral("abc123") } } } } } } },
        { QStringLiteral("response"),
            QJsonObject { { QStringLiteral("url"),
                QJsonObject { { QStringLiteral("type"), QStringLiteral("json_pointer") },
                    { QStringLiteral("pointer"), QStringLiteral("/data/url") } } } } } };

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

void TargetUploaderTest::formUrlencodedUploadSendsFieldsAndParsesHeaderResponse()
{
    HttpCaptureServer server;
    QVERIFY(server.start());
    server.enqueueResponse(
        { 200, "OK", "text/plain", { { "Location", "https://files.example/from-header" } }, "done" });

    QTemporaryDir dir;
    const QString filePath = writeTempFile(dir, QStringLiteral("note name.txt"), "body");
    const QJsonObject formFields { { QStringLiteral("title"), QStringLiteral("${FILENAME}") },
        { QStringLiteral("kind"), QStringLiteral("note") } };
    const QJsonObject config { { QStringLiteral("schemaVersion"), 1 }, { QStringLiteral("id"), QStringLiteral("form") },
        { QStringLiteral("request"),
            QJsonObject { { QStringLiteral("url"), server.url(QStringLiteral("/submit")).toString() },
                { QStringLiteral("method"), QStringLiteral("POST") },
                { QStringLiteral("query"),
                    QJsonObject { { QStringLiteral("source"), QStringLiteral("${FILENAME}") } } },
                { QStringLiteral("body"),
                    QJsonObject { { QStringLiteral("type"), QStringLiteral("form_urlencoded") },
                        { QStringLiteral("fields"), formFields } } } } },
        { QStringLiteral("response"),
            QJsonObject { { QStringLiteral("url"),
                QJsonObject { { QStringLiteral("type"), QStringLiteral("header") },
                    { QStringLiteral("name"), QStringLiteral("Location") } } } } } };

    TargetUploader uploader(config);
    QNetworkAccessManager manager;
    const UploadResult result = runUpload(uploader, filePath, manager);

    QVERIFY(result.ok);
    QCOMPARE(result.url, QStringLiteral("https://files.example/from-header"));
    QCOMPARE(server.requests().size(), 1);
    const CapturedHttpRequest request = server.requests().first();
    QCOMPARE(request.method, QByteArray("POST"));
    QCOMPARE(request.path, QByteArray("/submit?source=note%20name.txt"));
    QCOMPARE(request.headers.value("content-type"), QByteArray("application/x-www-form-urlencoded"));
    QVERIFY(request.body.contains("title=note+name.txt") || request.body.contains("title=note%20name.txt"));
    QVERIFY(request.body.contains("kind=note"));
}

void TargetUploaderTest::jsonUploadSendsStructuredBodyAndParsesXmlXPath()
{
    HttpCaptureServer server;
    QVERIFY(server.start());
    server.enqueueResponse({ 200, "OK", "application/xml", { },
        R"(<?xml version="1.0"?><files><file><url>https://files.example/xml</url></file></files>)" });

    QTemporaryDir dir;
    const QString filePath = writeTempFile(dir, QStringLiteral("clip.txt"), "body");
    const QJsonObject jsonFields { { QStringLiteral("name"), QStringLiteral("${FILENAME}") },
        { QStringLiteral("meta"),
            QJsonObject {
                { QStringLiteral("tags"), QJsonArray { QStringLiteral("one"), QStringLiteral("${FILENAME}") } } } } };
    const QJsonObject config { { QStringLiteral("schemaVersion"), 1 }, { QStringLiteral("id"), QStringLiteral("json") },
        { QStringLiteral("request"),
            QJsonObject { { QStringLiteral("url"), server.url(QStringLiteral("/json")).toString() },
                { QStringLiteral("method"), QStringLiteral("POST") },
                { QStringLiteral("body"),
                    QJsonObject { { QStringLiteral("type"), QStringLiteral("json") },
                        { QStringLiteral("value"), jsonFields } } } } },
        { QStringLiteral("response"),
            QJsonObject { { QStringLiteral("url"),
                QJsonObject { { QStringLiteral("type"), QStringLiteral("xml_path") },
                    { QStringLiteral("path"), QStringLiteral("/files/file[1]/url") } } } } } };

    TargetUploader uploader(config);
    QNetworkAccessManager manager;
    const UploadResult result = runUpload(uploader, filePath, manager);

    QVERIFY(result.ok);
    QCOMPARE(result.url, QStringLiteral("https://files.example/xml"));
    QCOMPARE(server.requests().size(), 1);
    const CapturedHttpRequest request = server.requests().first();
    QCOMPARE(request.headers.value("content-type"), QByteArray("application/json"));
    const QJsonDocument sentDoc = QJsonDocument::fromJson(request.body);
    QVERIFY(sentDoc.isObject());
    QCOMPARE(sentDoc.object().value(QStringLiteral("name")).toString(), QStringLiteral("clip.txt"));
    QCOMPARE(sentDoc.object()
                 .value(QStringLiteral("meta"))
                 .toObject()
                 .value(QStringLiteral("tags"))
                 .toArray()
                 .at(1)
                 .toString(),
        QStringLiteral("clip.txt"));
}

void TargetUploaderTest::rawUploadUsesPreprocessedFileContents()
{
    QVERIFY2(!pythonExecutable().isEmpty(), "python3 is required for realistic preupload integration tests");

    HttpCaptureServer server;
    QVERIFY(server.start());
    server.enqueueResponse({ 200, "OK", "text/plain", { }, "https://files.example/processed" });

    QTemporaryDir dir;
    const QString sourcePath = writeTempFile(dir, QStringLiteral("payload.txt"), "ORIGINAL");
    const QJsonObject preprocessConfig { { QStringLiteral("preUpload"),
        QJsonArray { QJsonObject { { QStringLiteral("mime"), QJsonArray { QStringLiteral("*/*") } },
            { QStringLiteral("fileHandling"), QStringLiteral("inplace_copy") },
            { QStringLiteral("commands"),
                QJsonArray {
                    commandObject({ pythonExecutable(), fixtureScriptPath(QStringLiteral("append_text.py")),
                        QStringLiteral("${FILE}"), QStringLiteral("-UPLOADED") }),
                } } } } } };

    const PreUploadProcessor::Result prepared = PreUploadProcessor::preprocessFile(preprocessConfig, sourcePath);
    QVERIFY(prepared.ok);
    QVERIFY(prepared.uploadPath != sourcePath);

    const QJsonObject uploadConfig { { QStringLiteral("schemaVersion"), 1 },
        { QStringLiteral("id"), QStringLiteral("raw") },
        { QStringLiteral("displayName"), QStringLiteral("Raw Target") },
        { QStringLiteral("request"),
            QJsonObject { { QStringLiteral("url"), server.url(QStringLiteral("/upload")).toString() },
                { QStringLiteral("method"), QStringLiteral("PUT") },
                { QStringLiteral("body"), QJsonObject { { QStringLiteral("type"), QStringLiteral("raw") } } } } },
        { QStringLiteral("response"),
            QJsonObject {
                { QStringLiteral("url"), QJsonObject { { QStringLiteral("type"), QStringLiteral("text_url") } } } } } };

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
    server.enqueueResponse({ 200, "OK", "text/plain", { }, "Uploaded to https://files.example/regex" });

    QTemporaryDir dir;
    const QString filePath = writeTempFile(dir, QStringLiteral("note.txt"), "regex");
    const QJsonObject config { { QStringLiteral("schemaVersion"), 1 },
        { QStringLiteral("id"), QStringLiteral("regex") },
        { QStringLiteral("request"),
            QJsonObject { { QStringLiteral("url"), server.url(QStringLiteral("/regex")).toString() },
                { QStringLiteral("method"), QStringLiteral("POST") },
                { QStringLiteral("body"), QJsonObject { { QStringLiteral("type"), QStringLiteral("raw") } } } } },
        { QStringLiteral("response"),
            QJsonObject { { QStringLiteral("url"),
                QJsonObject { { QStringLiteral("type"), QStringLiteral("regex") },
                    { QStringLiteral("pattern"), QStringLiteral(R"(Uploaded to (https://\S+))") },
                    { QStringLiteral("group"), 1 } } } } } };

    TargetUploader uploader(config);
    QNetworkAccessManager manager;
    const UploadResult result = runUpload(uploader, filePath, manager);

    QVERIFY(result.ok);
    QCOMPARE(result.url, QStringLiteral("https://files.example/regex"));
}

void TargetUploaderTest::redirectUrlParserExtractsLocationHeader()
{
    HttpCaptureServer server;
    QVERIFY(server.start());
    server.enqueueResponse({ 302, "Found", "text/plain", { { "Location", "https://files.example/redirected" } }, "" });

    QTemporaryDir dir;
    const QString filePath = writeTempFile(dir, QStringLiteral("note.txt"), "body");
    const QJsonObject config { { QStringLiteral("schemaVersion"), 1 },
        { QStringLiteral("id"), QStringLiteral("redirect") },
        { QStringLiteral("request"),
            QJsonObject { { QStringLiteral("url"), server.url(QStringLiteral("/redirect")).toString() },
                { QStringLiteral("method"), QStringLiteral("POST") },
                { QStringLiteral("body"), QJsonObject { { QStringLiteral("type"), QStringLiteral("raw") } } } } },
        { QStringLiteral("response"),
            QJsonObject { { QStringLiteral("url"),
                QJsonObject { { QStringLiteral("type"), QStringLiteral("redirect_url") } } } } } };

    TargetUploader uploader(config);
    QNetworkAccessManager manager;
    const UploadResult result = runUpload(uploader, filePath, manager);

    QVERIFY(result.ok);
    QCOMPARE(result.url, QStringLiteral("https://files.example/redirected"));
}

void TargetUploaderTest::errorExtractorSurfacesStructuredServerErrors()
{
    HttpCaptureServer server;
    QVERIFY(server.start());
    server.enqueueResponse({ 400, "Bad Request", "application/json", { }, R"({"error":{"message":"upload denied"}})" });

    QTemporaryDir dir;
    const QString filePath = writeTempFile(dir, QStringLiteral("note.txt"), "body");
    const QJsonObject config { { QStringLiteral("schemaVersion"), 1 },
        { QStringLiteral("id"), QStringLiteral("errorjson") },
        { QStringLiteral("request"),
            QJsonObject { { QStringLiteral("url"), server.url(QStringLiteral("/error")).toString() },
                { QStringLiteral("method"), QStringLiteral("POST") },
                { QStringLiteral("body"), QJsonObject { { QStringLiteral("type"), QStringLiteral("raw") } } } } },
        { QStringLiteral("response"),
            QJsonObject {
                { QStringLiteral("url"), QJsonObject { { QStringLiteral("type"), QStringLiteral("text_url") } } },
                { QStringLiteral("error"),
                    QJsonObject { { QStringLiteral("type"), QStringLiteral("json_pointer") },
                        { QStringLiteral("pointer"), QStringLiteral("/error/message") } } } } } };

    TargetUploader uploader(config);
    QNetworkAccessManager manager;
    const UploadResult result = runUpload(uploader, filePath, manager);

    QVERIFY(!result.ok);
    QCOMPARE(result.errorMessage, QStringLiteral("upload denied"));
}

void TargetUploaderTest::successExtractorsPopulateVariantUrlsAndResponseMetadata()
{
    HttpCaptureServer server;
    QVERIFY(server.start());
    server.enqueueResponse({ 200, "OK", "application/json", { { "X-Delete", "https://files.example/delete/123" } },
        R"({"data":{"url":"https://files.example/main","thumb":"https://files.example/thumb"}})" });

    QTemporaryDir dir;
    const QString filePath = writeTempFile(dir, QStringLiteral("note.txt"), "body");
    const QJsonObject thumbnailResponse { { QStringLiteral("type"), QStringLiteral("json_pointer") },
        { QStringLiteral("pointer"), QStringLiteral("/data/thumb") } };
    const QJsonObject deletionResponse { { QStringLiteral("type"), QStringLiteral("header") },
        { QStringLiteral("name"), QStringLiteral("X-Delete") } };
    const QJsonObject response { { QStringLiteral("url"),
                                     QJsonObject { { QStringLiteral("type"), QStringLiteral("json_pointer") },
                                         { QStringLiteral("pointer"), QStringLiteral("/data/url") } } },
        { QStringLiteral("thumbnail"), thumbnailResponse }, { QStringLiteral("deletion"), deletionResponse } };
    TargetUploader uploader(
        QJsonObject { { QStringLiteral("schemaVersion"), 1 }, { QStringLiteral("id"), QStringLiteral("variants") },
            { QStringLiteral("request"),
                QJsonObject { { QStringLiteral("url"), server.url(QStringLiteral("/variants")).toString() },
                    { QStringLiteral("method"), QStringLiteral("POST") },
                    { QStringLiteral("body"), QJsonObject { { QStringLiteral("type"), QStringLiteral("raw") } } } } },
            { QStringLiteral("response"), response } });

    QNetworkAccessManager manager;
    const UploadResult result = runUpload(uploader, filePath, manager);

    QVERIFY(result.ok);
    QCOMPARE(result.url, QStringLiteral("https://files.example/main"));
    QCOMPARE(result.thumbnailUrl, QStringLiteral("https://files.example/thumb"));
    QCOMPARE(result.deletionUrl, QStringLiteral("https://files.example/delete/123"));
    QCOMPARE(result.responseInfo.statusCode, 200);
    QCOMPARE(result.responseInfo.reasonPhrase, QStringLiteral("OK"));
    const QString expectedResponseText = QString::fromUtf8(
        "{\"data\":{\"url\":\"https://files.example/main\",\"thumb\":\"https://files.example/thumb\"}}");
    QCOMPARE(result.responseInfo.responseText, expectedResponseText);
    QCOMPARE(result.responseInfo.headers.value(QStringLiteral("x-delete")),
        QStringLiteral("https://files.example/delete/123"));
}

void TargetUploaderTest::parserErrorsAreReported()
{
    QTemporaryDir dir;
    const QString filePath = writeTempFile(dir, QStringLiteral("note.txt"), "body");
    QNetworkAccessManager manager;

    {
        HttpCaptureServer server;
        QVERIFY(server.start());
        server.enqueueResponse({ 200, "OK", "text/plain", { }, "not-a-url" });
        TargetUploader uploader(QJsonObject { { QStringLiteral("schemaVersion"), 1 },
            { QStringLiteral("id"), QStringLiteral("badtext") },
            { QStringLiteral("request"),
                QJsonObject { { QStringLiteral("url"), server.url(QStringLiteral("/badtext")).toString() },
                    { QStringLiteral("method"), QStringLiteral("POST") },
                    { QStringLiteral("body"), QJsonObject { { QStringLiteral("type"), QStringLiteral("raw") } } } } },
            { QStringLiteral("response"),
                QJsonObject { { QStringLiteral("url"),
                    QJsonObject { { QStringLiteral("type"), QStringLiteral("text_url") } } } } } });
        const UploadResult result = runUpload(uploader, filePath, manager);
        QVERIFY(!result.ok);
        QCOMPARE(result.errorMessage, QStringLiteral("Response URL must be an absolute HTTP or HTTPS URL."));
    }

    {
        HttpCaptureServer server;
        QVERIFY(server.start());
        server.enqueueResponse({ 200, "OK", "application/json", { }, "not json" });
        TargetUploader uploader(QJsonObject { { QStringLiteral("schemaVersion"), 1 },
            { QStringLiteral("id"), QStringLiteral("badjson") },
            { QStringLiteral("request"),
                QJsonObject { { QStringLiteral("url"), server.url(QStringLiteral("/badjson")).toString() },
                    { QStringLiteral("method"), QStringLiteral("POST") },
                    { QStringLiteral("body"), QJsonObject { { QStringLiteral("type"), QStringLiteral("raw") } } } } },
            { QStringLiteral("response"),
                QJsonObject { { QStringLiteral("url"),
                    QJsonObject { { QStringLiteral("type"), QStringLiteral("json_pointer") },
                        { QStringLiteral("pointer"), QStringLiteral("/data/url") } } } } } });
        const UploadResult result = runUpload(uploader, filePath, manager);
        QVERIFY(!result.ok);
        QCOMPARE(result.errorMessage, QStringLiteral("Upload response was not valid JSON."));
    }

    {
        HttpCaptureServer server;
        QVERIFY(server.start());
        server.enqueueResponse({ 200, "OK", "application/json", { }, R"({"data":{"url":42}})" });
        TargetUploader uploader(QJsonObject { { QStringLiteral("schemaVersion"), 1 },
            { QStringLiteral("id"), QStringLiteral("missingurl") },
            { QStringLiteral("request"),
                QJsonObject { { QStringLiteral("url"), server.url(QStringLiteral("/missingurl")).toString() },
                    { QStringLiteral("method"), QStringLiteral("POST") },
                    { QStringLiteral("body"), QJsonObject { { QStringLiteral("type"), QStringLiteral("raw") } } } } },
            { QStringLiteral("response"),
                QJsonObject { { QStringLiteral("url"),
                    QJsonObject { { QStringLiteral("type"), QStringLiteral("json_pointer") },
                        { QStringLiteral("pointer"), QStringLiteral("/data/url") } } } } } });
        const UploadResult result = runUpload(uploader, filePath, manager);
        QVERIFY(!result.ok);
        QCOMPARE(result.errorMessage, QStringLiteral("Response URL must be an absolute HTTP or HTTPS URL."));
    }

    {
        HttpCaptureServer server;
        QVERIFY(server.start());
        server.enqueueResponse({ 200, "OK", "text/plain", { }, "no match here" });
        TargetUploader uploader(QJsonObject { { QStringLiteral("schemaVersion"), 1 },
            { QStringLiteral("id"), QStringLiteral("nomatch") },
            { QStringLiteral("request"),
                QJsonObject { { QStringLiteral("url"), server.url(QStringLiteral("/nomatch")).toString() },
                    { QStringLiteral("method"), QStringLiteral("POST") },
                    { QStringLiteral("body"), QJsonObject { { QStringLiteral("type"), QStringLiteral("raw") } } } } },
            { QStringLiteral("response"),
                QJsonObject { { QStringLiteral("url"),
                    QJsonObject { { QStringLiteral("type"), QStringLiteral("regex") },
                        { QStringLiteral("pattern"), QStringLiteral(R"((https://\S+))") } } } } } });
        const UploadResult result = runUpload(uploader, filePath, manager);
        QVERIFY(!result.ok);
        QCOMPARE(result.errorMessage, QStringLiteral("Response URL must be an absolute HTTP or HTTPS URL."));
    }

    {
        HttpCaptureServer server;
        QVERIFY(server.start());
        server.enqueueResponse({ 200, "OK", "application/xml", { }, "<nope />" });
        TargetUploader uploader(QJsonObject { { QStringLiteral("schemaVersion"), 1 },
            { QStringLiteral("id"), QStringLiteral("badxml") },
            { QStringLiteral("request"),
                QJsonObject { { QStringLiteral("url"), server.url(QStringLiteral("/badxml")).toString() },
                    { QStringLiteral("method"), QStringLiteral("POST") },
                    { QStringLiteral("body"), QJsonObject { { QStringLiteral("type"), QStringLiteral("raw") } } } } },
            { QStringLiteral("response"),
                QJsonObject { { QStringLiteral("url"),
                    QJsonObject { { QStringLiteral("type"), QStringLiteral("xml_path") },
                        { QStringLiteral("path"), QStringLiteral("/files/file[1]/url") } } } } } });
        const UploadResult result = runUpload(uploader, filePath, manager);
        QVERIFY(!result.ok);
        QCOMPARE(result.errorMessage, QStringLiteral("Response URL must be an absolute HTTP or HTTPS URL."));
    }
}

void TargetUploaderTest::rejectsMissingFileAndNullManager()
{
    const QJsonObject config { { QStringLiteral("schemaVersion"), 1 }, { QStringLiteral("id"), QStringLiteral("raw") },
        { QStringLiteral("request"),
            QJsonObject { { QStringLiteral("url"), QStringLiteral("https://example.test/upload") },
                { QStringLiteral("method"), QStringLiteral("POST") },
                { QStringLiteral("body"), QJsonObject { { QStringLiteral("type"), QStringLiteral("raw") } } } } },
        { QStringLiteral("response"),
            QJsonObject {
                { QStringLiteral("url"), QJsonObject { { QStringLiteral("type"), QStringLiteral("text_url") } } } } } };

    TargetUploader uploader(config);
    QNetworkAccessManager manager;

    QVERIFY(uploader.upload(QStringLiteral("/missing/file.txt"), &manager) == nullptr);
    QVERIFY(uploader.upload(QStringLiteral("/missing/file.txt"), nullptr) == nullptr);
}

void TargetUploaderTest::idAndDisplayNameFallbacksWork()
{
    TargetUploader uploader(
        QJsonObject { { QStringLiteral("schemaVersion"), 1 }, { QStringLiteral("id"), QStringLiteral("target-id") },
            { QStringLiteral("request"),
                QJsonObject { { QStringLiteral("url"), QStringLiteral("https://example.test/upload") },
                    { QStringLiteral("method"), QStringLiteral("POST") },
                    { QStringLiteral("body"), QJsonObject { { QStringLiteral("type"), QStringLiteral("raw") } } } } },
            { QStringLiteral("response"),
                QJsonObject { { QStringLiteral("url"),
                    QJsonObject { { QStringLiteral("type"), QStringLiteral("text_url") } } } } } });

    QCOMPARE(uploader.id(), QStringLiteral("target-id"));
    QCOMPARE(uploader.displayName(), QStringLiteral("target-id"));
}

void TargetUploaderTest::interruptedResponseFails()
{
    HttpCaptureServer server;
    QVERIFY(server.start());
    server.enqueueResponse({ 200, "OK", "text/plain", { }, "https://files.example/incomplete", 1000 });
    QTemporaryDir dir;
    const QString filePath = writeTempFile(dir, QStringLiteral("input.txt"), "body");
    TargetUploader uploader(rawTarget(server.url()));
    QNetworkAccessManager manager;
    const auto result = runUpload(uploader, filePath, manager);
    QVERIFY(!result.ok);
    QVERIFY(!result.errorMessage.isEmpty());
    QVERIFY(result.url.isEmpty());
    QCOMPARE(result.responseInfo.statusCode, 200);
}

void TargetUploaderTest::relativeRedirectResolvesAgainstReplyUrl()
{
    HttpCaptureServer server;
    QVERIFY(server.start());
    server.enqueueResponse({ 302, "Found", "text/plain", { { "Location", "../files/result?token=a%2Bb" } }, { } });
    QTemporaryDir dir;
    const QString filePath = writeTempFile(dir, QStringLiteral("input.txt"), "body");
    auto config = rawTarget(server.url(QStringLiteral("/api/upload")));
    config.insert(QStringLiteral("response"),
        QJsonObject {
            { QStringLiteral("url"), QJsonObject { { QStringLiteral("type"), QStringLiteral("redirect_url") } } } });
    TargetUploader uploader(config);
    QNetworkAccessManager manager;
    const auto result = runUpload(uploader, filePath, manager);
    QVERIFY(result.ok);
    QCOMPARE(result.url, server.url(QStringLiteral("/files/result")).toString() + QStringLiteral("?token=a%2Bb"));
    QCOMPARE(server.requests().size(), 1);
}

void TargetUploaderTest::multipartEscapesHeaderParameters()
{
    HttpCaptureServer server;
    QVERIFY(server.start());
    server.enqueueResponse({ 200, "OK", "text/plain", { }, "https://files.example/upload" });
    QTemporaryDir dir;
    const QString filePath = writeTempFile(dir, QStringLiteral("report\"; name=\"other\\\r\n.txt"), "body");
    QVERIFY(!filePath.isEmpty());
    auto config = rawTarget(server.url());
    config.insert(QStringLiteral("request"),
        QJsonObject { { QStringLiteral("url"), server.url().toString() },
            { QStringLiteral("method"), QStringLiteral("POST") },
            { QStringLiteral("body"),
                QJsonObject { { QStringLiteral("type"), QStringLiteral("multipart") },
                    { QStringLiteral("fileField"), QStringLiteral("fi\"le") },
                    { QStringLiteral("fields"),
                        QJsonObject { { QStringLiteral("to\"ken"), QStringLiteral("abc") } } } } } });
    TargetUploader uploader(config);
    QNetworkAccessManager manager;
    const auto result = runUpload(uploader, filePath, manager);
    QVERIFY(result.ok);
    const auto body = server.requests().first().body;
    QVERIFY(body.contains("name=\"to\\\"ken\""));
    QVERIFY(body.contains("name=\"fi\\\"le\"; filename=\"report\\\"; name=\\\"other\\\\%0D%0A.txt\"\r\n"));
}

void TargetUploaderTest::jsonScalarBodies_data()
{
    QTest::addColumn<QJsonValue>("fields");
    QTest::addColumn<QByteArray>("expected");
    QTest::newRow("string-template") << QJsonValue(QStringLiteral("${FILENAME}")) << QByteArray("\"a\\\"b.txt\"");
    QTest::newRow("number") << QJsonValue(42) << QByteArray("42");
    QTest::newRow("boolean") << QJsonValue(true) << QByteArray("true");
    QTest::newRow("null") << QJsonValue(QJsonValue::Null) << QByteArray("null");
}

void TargetUploaderTest::jsonScalarBodies()
{
    QFETCH(QJsonValue, fields);
    QFETCH(QByteArray, expected);
    HttpCaptureServer server;
    QVERIFY(server.start());
    server.enqueueResponse({ 200, "OK", "text/plain", { }, "https://files.example/upload" });
    QTemporaryDir dir;
    const QString filePath = writeTempFile(dir, QStringLiteral("a\"b.txt"), "body");
    auto config = rawTarget(server.url());
    auto request = config.value(QStringLiteral("request")).toObject();
    request.insert(QStringLiteral("body"),
        QJsonObject { { QStringLiteral("type"), QStringLiteral("json") }, { QStringLiteral("value"), fields } });
    config.insert(QStringLiteral("request"), request);
    TargetUploader uploader(config);
    QNetworkAccessManager manager;
    const auto result = runUpload(uploader, filePath, manager);
    QVERIFY(result.ok);
    QCOMPARE(server.requests().first().body, expected);
}

void TargetUploaderTest::cheveretoExampleExtractsDocumentedResponse()
{
    QFile example(QStringLiteral(IMSHARE_TEST_SOURCE_DIR) + QStringLiteral("/../targets/examples/chevereto.json"));
    QVERIFY(example.open(QIODevice::ReadOnly));
    auto config = QJsonDocument::fromJson(example.readAll()).object();
    HttpCaptureServer server;
    QVERIFY(server.start());
    server.enqueueResponse({ 200, "OK", "application/json", { },
        R"({"image":{"url_viewer":"https://images.example/image/1","thumb":{"url":"https://images.example/thumb/1"}}})" });
    auto request = config.value(QStringLiteral("request")).toObject();
    request.insert(QStringLiteral("url"), server.url().toString());
    config.insert(QStringLiteral("request"), request);
    QTemporaryDir dir;
    const QString filePath = writeTempFile(dir, QStringLiteral("image.png"), tinyPng());
    TargetUploader uploader(config);
    QNetworkAccessManager manager;
    const auto result = runUpload(uploader, filePath, manager);
    QVERIFY2(result.ok, qPrintable(result.errorMessage));
    QCOMPARE(result.url, QStringLiteral("https://images.example/image/1"));
    QCOMPARE(result.thumbnailUrl, QStringLiteral("https://images.example/thumb/1"));
}

void TargetUploaderTest::responseUrlsMustBeUsable_data()
{
    QTest::addColumn<QString>("type");
    QTest::addColumn<QString>("output");
    QTest::addColumn<QString>("value");
    QTest::addColumn<bool>("valid");
    for (const auto& type : { QStringLiteral("text_url"), QStringLiteral("regex"), QStringLiteral("json_pointer"),
             QStringLiteral("header"), QStringLiteral("xml_path") }) {
        for (const auto& output : { QStringLiteral("url"), QStringLiteral("thumbnail"), QStringLiteral("deletion") }) {
            for (const auto& value : { QStringLiteral("https://files.example/a%20b?x=1"),
                     QStringLiteral("http://files.example/a"), QStringLiteral("/relative"),
                     QStringLiteral("//files.example/a"), QStringLiteral("ftp://files.example/a"),
                     QStringLiteral("https:///missing-host"), QStringLiteral("https://files.example/bad%escape"),
                     QStringLiteral("https://files.example/has space"), QStringLiteral("upload failed"), QString() }) {
                const bool valid = value.startsWith(QLatin1StringView("https://files.example/a%20"))
                    || value == QLatin1StringView("http://files.example/a")
                    || (value.isEmpty() && output != QLatin1StringView("url"));
                const auto name = (type + QLatin1Char('-') + output + QLatin1Char('-') + value).toUtf8();
                QTest::newRow(name.constData()) << type << output << value << valid;
            }
        }
    }
}

void TargetUploaderTest::responseUrlsMustBeUsable()
{
    QFETCH(QString, type);
    QFETCH(QString, output);
    QFETCH(QString, value);
    QFETCH(bool, valid);
    auto config = rawTarget(QUrl(QStringLiteral("https://example.test/upload")));
    QJsonObject extractor { { QStringLiteral("type"), type } };
    UploadResponseInfo response;
    response.statusCode = 200;
    response.responseUrl = QStringLiteral("https://example.test/upload");
    response.headers.insert(QStringLiteral("x-main"), QStringLiteral("https://files.example/main"));
    if (type == QLatin1StringView("json_pointer")) {
        extractor.insert(QStringLiteral("pointer"), QStringLiteral("/value"));
        response.responseText
            = QString::fromUtf8(QJsonDocument(QJsonObject { { QStringLiteral("value"), value } }).toJson());
    } else if (type == QLatin1StringView("xml_path")) {
        extractor.insert(QStringLiteral("path"), QStringLiteral("/value"));
        response.responseText = QStringLiteral("<value>") + value.toHtmlEscaped() + QStringLiteral("</value>");
    } else if (type == QLatin1StringView("header")) {
        extractor.insert(QStringLiteral("name"), QStringLiteral("X-Value"));
        response.headers.insert(QStringLiteral("x-value"), value);
    } else {
        response.responseText = value;
        if (type == QLatin1StringView("regex"))
            extractor.insert(QStringLiteral("pattern"), QStringLiteral("(.*)"));
    }
    QJsonObject extractors { { QStringLiteral("url"),
        QJsonObject { { QStringLiteral("type"), QStringLiteral("header") },
            { QStringLiteral("name"), QStringLiteral("X-Main") } } } };
    extractors.insert(output, extractor);
    config.insert(QStringLiteral("response"), extractors);
    const auto result = TargetUploader(config).parseResponse(response);
    QCOMPARE(result.ok, valid);
    if (!valid) {
        QVERIFY(result.errorMessage.contains(QStringLiteral("absolute HTTP or HTTPS")));
        QVERIFY(result.url.isEmpty());
        QVERIFY(result.thumbnailUrl.isEmpty());
        QVERIFY(result.deletionUrl.isEmpty());
    } else {
        const auto extracted = output == QLatin1StringView("url") ? result.url
            : output == QLatin1StringView("thumbnail")            ? result.thumbnailUrl
                                                                  : result.deletionUrl;
        QCOMPARE(extracted, value);
    }
}

void TargetUploaderTest::responseJsonPointerSupportsRootAndEmptyKey()
{
    auto config = rawTarget(QUrl(QStringLiteral("https://example.test/upload")));
    UploadResponseInfo response;
    response.statusCode = 200;
    for (const auto& pointer : { QString(), QStringLiteral("/") }) {
        config.insert(QStringLiteral("response"),
            QJsonObject { { QStringLiteral("url"),
                QJsonObject { { QStringLiteral("type"), QStringLiteral("json_pointer") },
                    { QStringLiteral("pointer"), pointer } } } });
        response.responseText = pointer.isEmpty() ? QStringLiteral("\"https://files.example/root\"")
                                                  : QStringLiteral("{\"\":\"https://files.example/root\"}");
        const auto result = TargetUploader(config).parseResponse(response);
        QVERIFY2(result.ok, qPrintable(result.errorMessage));
        QCOMPARE(result.url, QStringLiteral("https://files.example/root"));
    }
}

void TargetUploaderTest::invalidEndpointsDoNotSendRequests()
{
    QTemporaryDir dir;
    const auto file = writeTempFile(dir, QStringLiteral("file.txt"), "test");
    QNetworkAccessManager manager;
    for (const auto& endpoint : { QStringLiteral("relative/path"), QStringLiteral("example.test"),
             QStringLiteral("file:///tmp/upload"), QStringLiteral("ftp://example.test/upload"),
             QStringLiteral("https://example.test/bad%escape"), QStringLiteral("https:///missing-host") }) {
        auto config = rawTarget(QUrl(QStringLiteral("https://example.test/upload")));
        auto request = config.value(QStringLiteral("request")).toObject();
        request.insert(QStringLiteral("url"), endpoint);
        config.insert(QStringLiteral("request"), request);
        TargetUploader uploader(config);
        QVERIFY2(!uploader.upload(file, &manager), qPrintable(endpoint));
        QVERIFY(uploader.lastError().contains(QStringLiteral("absolute HTTP or HTTPS")));
    }
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    TargetUploaderTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_targetuploader.moc"
