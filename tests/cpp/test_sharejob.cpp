#include "sharejob.h"

#include <KJob>
#include <QClipboard>
#include <QApplication>
#include <QTimer>
#include <QStandardPaths>
#include "targetpickerdialog.h"
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
    void stagesInputFilesBeforeUpload();
    void preUploadTransformsUploadedBodyWithoutMutatingSource();
    void preUploadFailureStopsBeforeAnyUpload();
    void failedBatchPreservesCompletedLinks();
    void interruptedResponseFailsJob();
    void destroyingActiveUploadRemovesStagedFiles();
    void preprocessingKeepsEventLoopResponsive();
    void destroyingPreprocessingRemovesAllCopies();
    void pickerCancellationFinishesAfterStartReturns();
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
    QCOMPARE(resultSpy.count(), 0);

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
    QCOMPARE(resultSpy.count(), 0);

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
    QCOMPARE(resultSpy.count(), 0);

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

void ShareJobTest::stagesInputFilesBeforeUpload()
{
    HttpCaptureServer server;
    QVERIFY(server.start());
    server.enqueueResponse({200, "OK", "text/plain", {}, "https://files.example/staged"});

    QTemporaryDir dir;
    const QString filePath = writeTempFile(dir, QStringLiteral("sample.png"), tinyPng());
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
    job.setData(QJsonObject{{QStringLiteral("url"), QUrl::fromLocalFile(filePath).toString()}});

    QSignalSpy resultSpy(&job, &KJob::result);
    job.start();

    QVERIFY(QFile::remove(filePath));

    QTRY_COMPARE(resultSpy.count(), 1);
    QCOMPARE(job.error(), 0);
    QCOMPARE(server.requests().size(), 1);
    QCOMPARE(server.requests().first().path, QByteArray("/upload/sample.png"));
    QCOMPARE(server.requests().first().body, tinyPng());
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
    QCOMPARE(resultSpy.count(), 0);

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
    QCOMPARE(resultSpy.count(), 0);

    QTRY_COMPARE(resultSpy.count(), 1);
    QCOMPARE(job.error(), 1);
    QCOMPARE(job.errorText(), QStringLiteral("explode"));
    QCOMPARE(server.requests().size(), 0);
}

void ShareJobTest::failedBatchPreservesCompletedLinks()
{
    HttpCaptureServer server;
    QVERIFY(server.start());
    server.enqueueResponse({200, "OK", "text/plain",
        {{"X-Delete", "https://files.example/delete"}, {"X-Thumb", "https://files.example/thumb"}}, "https://files.example/one"});
    server.enqueueResponse({500, "Error", "text/plain", {}, "upload failed"});
    QTemporaryDir dir;
    const QString first = writeTempFile(dir, QStringLiteral("one.txt"), "one");
    const QString second = writeTempFile(dir, QStringLiteral("two.txt"), "two");
    auto config = rawTarget(server.url());
    auto response = config.value(QStringLiteral("response")).toObject();
    response.insert(QStringLiteral("deletion"), QJsonObject{{QStringLiteral("type"), QStringLiteral("header")}, {QStringLiteral("name"), QStringLiteral("X-Delete")}});
    response.insert(QStringLiteral("thumbnail"), QJsonObject{{QStringLiteral("type"), QStringLiteral("header")}, {QStringLiteral("name"), QStringLiteral("X-Thumb")}});
    config.insert(QStringLiteral("response"), response);
    ShareJob job(QJsonDocument(config).toJson());
    job.setAutoDelete(false);
    job.setData(QJsonObject{{QStringLiteral("urls"), QJsonArray{QUrl::fromLocalFile(first).toString(), QUrl::fromLocalFile(second).toString()}}});
    QGuiApplication::clipboard()->setText(QStringLiteral("sentinel"));
    QSignalSpy finished(&job, &KJob::result);
    job.start();
    QTRY_COMPARE(finished.count(), 1);
    QCOMPARE(job.error(), 1);
    QVERIFY(job.errorText().contains(QStringLiteral("Uploaded 1 of 2")));
    QCOMPARE(job.output().value(QStringLiteral("urls")).toArray(), QJsonArray{QStringLiteral("https://files.example/one")});
    QCOMPARE(job.output().value(QStringLiteral("deletionUrl")).toString(), QStringLiteral("https://files.example/delete"));
    QCOMPARE(job.output().value(QStringLiteral("thumbnailUrl")).toString(), QStringLiteral("https://files.example/thumb"));
    QCOMPARE(job.output().value(QStringLiteral("results")).toArray().size(), 1);
    QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("https://files.example/one"));
    QCOMPARE(server.requests().size(), 2);
}

void ShareJobTest::interruptedResponseFailsJob()
{
    HttpCaptureServer server;
    QVERIFY(server.start());
    server.enqueueResponse({200, "OK", "text/plain", {}, "https://files.example/incomplete", 1000});
    QTemporaryDir dir;
    const QString file = writeTempFile(dir, QStringLiteral("input.txt"), "body");
    ShareJob job(QJsonDocument(rawTarget(server.url())).toJson());
    job.setAutoDelete(false);
    job.setData(QJsonObject{{QStringLiteral("url"), QUrl::fromLocalFile(file).toString()}});
    QSignalSpy finished(&job, &KJob::result);
    job.start();
    QTRY_COMPARE(finished.count(), 1);
    QCOMPARE(job.error(), 1);
    QVERIFY(job.output().value(QStringLiteral("urls")).toArray().isEmpty());
}

static QStringList uploadTempDirectories()
{
    return QDir(QDir::tempPath()).entryList({QStringLiteral("plasma-share-*")}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
}

void ShareJobTest::destroyingActiveUploadRemovesStagedFiles()
{
    HttpCaptureServer server;
    QVERIFY(server.start());
    QTemporaryDir dir;
    const QString file = writeTempFile(dir, QStringLiteral("input.txt"), "body");
    const auto before = uploadTempDirectories();
    auto job = std::make_unique<ShareJob>(QJsonDocument(rawTarget(server.url())).toJson());
    job->setData(QJsonObject{{QStringLiteral("url"), QUrl::fromLocalFile(file).toString()}});
    connect(&server, &HttpCaptureServer::requestCaptured, this, [&]() { job.reset(); });
    job->start();
    QCOMPARE(uploadTempDirectories().size(), before.size() + 1);
    QTRY_VERIFY(!job);
    QCOMPARE(uploadTempDirectories(), before);
}

static QJsonArray slowPreprocessing(const QString &marker, const QString &seconds)
{
    return QJsonArray{QJsonObject{
        {QStringLiteral("mime"), QJsonArray{QStringLiteral("*/*")}},
        {QStringLiteral("fileHandling"), QStringLiteral("inplace_copy")},
        {QStringLiteral("commands"), QJsonArray{commandObject({pythonExecutable(), QStringLiteral("-c"),
            QStringLiteral("import pathlib,sys,time; pathlib.Path(sys.argv[2]).write_text(sys.argv[1]); time.sleep(float(sys.argv[3])); pathlib.Path(sys.argv[2]+'.done').touch()"),
            QStringLiteral("${FILE}"), marker, seconds})}}
    }};
}

void ShareJobTest::preprocessingKeepsEventLoopResponsive()
{
    QVERIFY(!pythonExecutable().isEmpty());
    HttpCaptureServer server;
    QVERIFY(server.start());
    server.enqueueResponse({200, "OK", "text/plain", {}, "https://files.example/one"});
    QTemporaryDir dir;
    const QString file = writeTempFile(dir, QStringLiteral("input.txt"), "body");
    const QString marker = dir.filePath(QStringLiteral("processing"));
    auto config = rawTarget(server.url());
    config.insert(QStringLiteral("preUpload"), slowPreprocessing(marker, QStringLiteral("0.3")));
    ShareJob job(QJsonDocument(config).toJson());
    job.setAutoDelete(false);
    job.setData(QJsonObject{{QStringLiteral("url"), QUrl::fromLocalFile(file).toString()}});
    QSignalSpy finished(&job, &KJob::result);
    bool tickDuringProcessing = false;
    QTimer timer;
    connect(&timer, &QTimer::timeout, this, [&]() {
        if (QFileInfo::exists(marker) && !QFileInfo::exists(marker + QStringLiteral(".done"))) {
            tickDuringProcessing = true;
        }
    });
    timer.start(10);
    job.start();
    QTRY_COMPARE(finished.count(), 1);
    QCOMPARE(job.error(), 0);
    QVERIFY(tickDuringProcessing);
}

void ShareJobTest::destroyingPreprocessingRemovesAllCopies()
{
    QVERIFY(!pythonExecutable().isEmpty());
    HttpCaptureServer server;
    QVERIFY(server.start());
    QTemporaryDir dir;
    const QString file = writeTempFile(dir, QStringLiteral("input.txt"), "body");
    const QString marker = dir.filePath(QStringLiteral("processing"));
    auto config = rawTarget(server.url());
    config.insert(QStringLiteral("preUpload"), slowPreprocessing(marker, QStringLiteral("10")));
    const auto before = uploadTempDirectories();
    auto job = std::make_unique<ShareJob>(QJsonDocument(config).toJson());
    job->setData(QJsonObject{{QStringLiteral("url"), QUrl::fromLocalFile(file).toString()}});
    job->start();
    QTRY_VERIFY(QFileInfo::exists(marker));
    QCOMPARE(uploadTempDirectories().size(), before.size() + 2);
    job.reset();
    QCOMPARE(uploadTempDirectories(), before);
    QVERIFY(QFileInfo::exists(file));
    QVERIFY(server.requests().isEmpty());
}

void ShareJobTest::pickerCancellationFinishesAfterStartReturns()
{
    QTemporaryDir dir;
    const QString file = writeTempFile(dir, QStringLiteral("input.txt"), "body");
    const QString icon = writeTempFile(dir, QStringLiteral("icon.png"), tinyPng());
    const QString configRoot = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/plasma-share-uploader");
    QVERIFY(QDir().mkpath(configRoot + QStringLiteral("/targets")));
    auto config = rawTarget(QUrl(QStringLiteral("http://127.0.0.1:1/upload")));
    config.insert(QStringLiteral("icon"), icon);
    QFile target(configRoot + QStringLiteral("/targets/raw.json"));
    QVERIFY(target.open(QIODevice::WriteOnly));
    target.write(QJsonDocument(config).toJson());
    target.close();
    QFile state(configRoot + QStringLiteral("/state.json"));
    QVERIFY(state.open(QIODevice::WriteOnly));
    state.write(R"({"disabledBundledTargets":["catbox","uguu"]})");
    state.close();
    ShareJob job(QByteArray{});
    job.setAutoDelete(false);
    job.setData(QJsonObject{{QStringLiteral("url"), QUrl::fromLocalFile(file).toString()}});
    QSignalSpy finished(&job, &KJob::result);
    job.start();
    QCOMPARE(finished.count(), 0);
    QTRY_VERIFY(qobject_cast<TargetPickerDialog *>(QApplication::activeModalWidget()));
    auto *picker = qobject_cast<TargetPickerDialog *>(QApplication::activeModalWidget());
    picker->reject();
    QTRY_COMPARE(finished.count(), 1);
    QCOMPARE(job.error(), 0);
    QCOMPARE(job.errorText(), QStringLiteral("Upload cancelled."));
}

int main(int argc, char **argv)
{
    QTemporaryDir isolated;
    qputenv("XDG_CONFIG_HOME", isolated.filePath(QStringLiteral("config")).toUtf8());
    qputenv("XDG_CACHE_HOME", isolated.filePath(QStringLiteral("cache")).toUtf8());
    const QString tempRoot = isolated.filePath(QStringLiteral("tmp"));
    QDir().mkpath(tempRoot);
    qputenv("TMPDIR", tempRoot.toUtf8());
    QApplication app(argc, argv);
    ShareJobTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_sharejob.moc"
