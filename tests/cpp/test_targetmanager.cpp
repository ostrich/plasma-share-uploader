#include "jsonutils.h"
#include "targetmanagercontroller.h"
#include <QAbstractItemModelTester>
#include "credentialstore.h"
#include "sharejob.h"
#include "targetfilestore.h"
#include "targetuploader_utils.h"
#include "uploadtestrunner.h"
#include "httpcaptureserver.h"
#include "testutils.h"

#include <QApplication>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QScopeGuard>
#include <QTimer>
#include <QtTest>

class MemoryCredentials final : public CredentialStore {
public:
    Values values;
    int reads = 0;
    void read(const QStringList& keys, QObject* context, ReadCallback callback) override
    {
        ++reads;
        Values result;
        QString error;
        for (const auto& key : keys) {
            if (!values.contains(key)) {
                error = QStringLiteral("Missing credential: ") + key;
                break;
            }
            result.insert(key, values.value(key));
        }
        QTimer::singleShot(0, context, [callback, result, error]() { callback(result, error); });
    }
    void write(const QString& key, const QString& value, QObject* context, WriteCallback callback) override
    {
        values.insert(key, value);
        QTimer::singleShot(0, context, [callback]() { callback({ }); });
    }
};

namespace {
QJsonObject target(const QUrl& url = QUrl(QStringLiteral("https://example.invalid/upload")))
{
    auto object = rawTarget(url);
    object.insert(QStringLiteral("displayName"), QStringLiteral("Test host"));
    object.insert(QStringLiteral("description"), QStringLiteral("A custom upload service"));
    object.insert(QStringLiteral("icon"), QStringLiteral("document-send"));
    return object;
}
QByteArray readBytes(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return { };
    return file.readAll();
}
void writeBytes(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(bytes), bytes.size());
}
}

class TargetManagerTest final : public QObject {
    Q_OBJECT
private slots:
    void presetLifecycleUsesFilesAndNeverWritesThroughLinks();
    void disabledDraftsConflictsAndExternalChanges();
    void exportPreservesReferencesAndRemovesRecognizedSecrets();
    void formsPreserveUnknownFieldsAndJsonTypes();
    void duplicateFormKeysCannotBeSilentlySaved();
    void emptyCommandArgumentsRemainRepairable();
    void walletReferencesAreValidatedAndExpandedOnce();
    void testRunnerUploadsWithCredentialsAndRedactsReplies();
    void purposeJobResolvesCredentialsBeforeUploading();
    void missingCredentialsAndCancellationDoNotUpload();
    void offlineParsingAndValidationPerformNoUpload();
    void managerSavesActualEditedTarget();
    void previewAndCancellationLeaveOriginalUntouched();
    void oversizedResponseStopsTheTest();
    void managerImportsExportsAndKeepsDraftsDisabled();
    void managerDiscardsEditsBeforeDuplicating();
    void credentialHelpersStoreReferencesAndHandleDraftChanges();
    void invalidDefinitionsDoNotBlockValidTargets();
    void listResetExposesOldDataBeforeReplacement();
    void managerPresetAndDraftActions();
};

void TargetManagerTest::presetLifecycleUsesFilesAndNeverWritesThroughLinks()
{
    QTemporaryDir root, presets;
    const auto source = presets.filePath(QStringLiteral("raw.json"));
    const auto bytes = QJsonDocument(target()).toJson();
    writeBytes(source, bytes);
    const auto active = root.filePath(QStringLiteral("targets"));
    TargetFileStore store(presets.path(), active);
    QVERIFY(!store.entries().isEmpty());
    const auto path = active + QStringLiteral("/raw.json");
    QVERIFY(QFileInfo(path).isSymLink());
    auto entry = TargetFileStore::read(path, TargetFileStore::Kind::Active);
    QCOMPARE(store.setEnabled(entry, false), QString { });
    QVERIFY(!QFileInfo(path).exists());
    store.entries();
    QVERIFY(!QFileInfo(path).exists()); // An existing empty directory stays empty.
    QCOMPARE(store.enablePreset(source), QString { });
    entry = TargetFileStore::read(path, TargetFileStore::Kind::Active);
    auto edited = entry.object;
    edited.insert(QStringLiteral("displayName"), QStringLiteral("Custom name"));
    QCOMPARE(store.save(path, QJsonDocument(edited).toJson(), entry.revision), QString { });
    QVERIFY(!QFileInfo(path).isSymLink());
    QCOMPARE(readBytes(source), bytes);
    entry = TargetFileStore::read(path, TargetFileStore::Kind::Active);
    QCOMPARE(store.setEnabled(entry, false), QString { });
    const auto disabled = active + QStringLiteral("/disabled/raw.json");
    QVERIFY(QFileInfo::exists(disabled));
    QCOMPARE(store.setEnabled(TargetFileStore::read(disabled, TargetFileStore::Kind::Disabled), true), QString { });
    QCOMPARE(store.restorePreset(TargetFileStore::read(path, TargetFileStore::Kind::Active)), QString { });
    QVERIFY(QFileInfo(path).isSymLink());
    QCOMPARE(readBytes(source), bytes);
}

void TargetManagerTest::disabledDraftsConflictsAndExternalChanges()
{
    QTemporaryDir root, presets, external;
    const auto active = root.filePath(QStringLiteral("targets"));
    QVERIFY(QDir().mkpath(active));
    TargetFileStore store(presets.path(), active);
    const auto path = active + QStringLiteral("/raw.json");
    const auto bytes = QJsonDocument(target()).toJson();
    QCOMPARE(store.save(path, bytes, { }), QString { });
    const auto entry = TargetFileStore::read(path, TargetFileStore::Kind::Active);
    writeBytes(path, bytes + QByteArray("\n"));
    QVERIFY(store.save(path, bytes, entry.revision).contains(QStringLiteral("changed outside")));
    QVERIFY(!store.save(active + QStringLiteral("/duplicate.json"), bytes, { }).isEmpty());
    const auto disabled = active + QStringLiteral("/disabled/draft.json");
    QCOMPARE(store.save(disabled, QByteArray("{bad JSON"), { }, true), QString { });
    QVERIFY(!store.setEnabled(TargetFileStore::read(disabled, TargetFileStore::Kind::Disabled), true).isEmpty());
    QVERIFY(!store.save(path, QByteArray("{bad JSON"), TargetFileStore::revision(path), true).isEmpty());
    const auto foreign = external.filePath(QStringLiteral("original.json"));
    writeBytes(foreign, bytes);
    const auto link = active + QStringLiteral("/foreign.json");
    QVERIFY(QFile::link(foreign, link));
    auto other = target();
    other.insert(QStringLiteral("id"), QStringLiteral("foreign"));
    QCOMPARE(store.save(link, QJsonDocument(other).toJson(), TargetFileStore::revision(link)), QString { });
    QCOMPARE(readBytes(foreign), bytes);
    QVERIFY(!QFileInfo(link).isSymLink());
    QVERIFY(!store.save(root.filePath(QStringLiteral("outside.json")), bytes, { }).isEmpty());
}

void TargetManagerTest::exportPreservesReferencesAndRemovesRecognizedSecrets()
{
    auto object = target(QUrl(QStringLiteral("https://example.test/upload?api_key=literal-url-secret")));
    auto request = object.value(QStringLiteral("request")).toObject();
    request.insert(QStringLiteral("headers"),
        QJsonObject { { QStringLiteral("Authorization"), QStringLiteral("Bearer literal-header-secret") },
            { QStringLiteral("X-Token"), QStringLiteral("${WALLET:work.api}") },
            { QStringLiteral("X-Name"), QStringLiteral("ordinary") } });
    object.insert(QStringLiteral("request"), request);
    object.insert(QStringLiteral("futureField"), QJsonArray { 1, true });
    QStringList redacted;
    const auto exported = TargetFileStore::portable(object, &redacted);
    const auto data = QJsonDocument(exported).toJson();
    QVERIFY(!data.contains("literal-url-secret"));
    QVERIFY(!data.contains("literal-header-secret"));
    QVERIFY(data.contains("${WALLET:work.api}"));
    QCOMPARE(exported.value(QStringLiteral("futureField")), object.value(QStringLiteral("futureField")));
    QVERIFY(TargetFileStore::validate(data).isEmpty());
    QCOMPARE(redacted.size(), 2);
    QVERIFY(QJsonDocument(object).toJson().contains("literal-header-secret"));
}

void TargetManagerTest::formsPreserveUnknownFieldsAndJsonTypes()
{

    const QDir examples(testSourceDir() + QStringLiteral("/../targets/examples"));
    for (const auto& path : examples.entryList({ QStringLiteral("*.json") }, QDir::Files)) {
        auto object = QJsonDocument::fromJson(readBytes(examples.filePath(path))).object();
        object.insert(QStringLiteral("future"),
            QJsonArray { true, 42, QJsonObject { { QStringLiteral("nested"), QStringLiteral("value") } } });
        TargetDraft draft;
        const auto bytes = QJsonDocument(object).toJson();
        draft.load(bytes);
        QCOMPARE(draft.bytes(), bytes);
        draft.setValue({ QStringLiteral("displayName") }, QStringLiteral("Edited name"));
        object.insert(QStringLiteral("displayName"), QStringLiteral("Edited name"));
        QCOMPARE(draft.document(), object);
    }
    TargetDraft draft;
    draft.load(QJsonDocument(target()).toJson());
    draft.setBodyType(QStringLiteral("json"));
    draft.setJsonBody(QStringLiteral("[false, 1, null, {\"message\":\"hello\"}]"));
    QCOMPARE(ConfigJson::get(
                 draft.document(), { QStringLiteral("request"), QStringLiteral("body"), QStringLiteral("value") })
                 .toArray()
                 .size(),
        4);
    QVERIFY(draft.problems().isEmpty());
    draft.setJsonBody(QStringLiteral("[bad"));
    QVERIFY(!draft.problems().isEmpty());
    QCOMPARE(draft.jsonBody(), QStringLiteral("[bad"));
    draft.setRawText(QStringLiteral("{broken"));
    QVERIFY(!draft.hasObject());
    draft.setRawText(ConfigJson::text(target()));
    QVERIFY(draft.hasObject());
    QVERIFY(draft.problems().isEmpty());
    const QStringList extractorPath { QStringLiteral("response"), QStringLiteral("url") };
    draft.setExtractorType(extractorPath, QStringLiteral("json_pointer"));
    const auto pointer = ConfigJson::get(draft.document(), extractorPath + QStringList { QStringLiteral("pointer") });
    QVERIFY(pointer.isString());
    QCOMPARE(pointer.toString(), QString());
    QVERIFY(draft.problems().isEmpty());
    draft.setBodyType(QStringLiteral("multipart"));
    draft.setField({ QStringLiteral("request"), QStringLiteral("body"), QStringLiteral("fields") },
        QJsonObject { { QStringLiteral("token"), QStringLiteral("kept") } });
    draft.setBodyType(QStringLiteral("json"));
    draft.setJsonBody(QStringLiteral("null"));
    draft.setBodyType(QStringLiteral("multipart"));
    QCOMPARE(
        ConfigJson::get(draft.document(),
            { QStringLiteral("request"), QStringLiteral("body"), QStringLiteral("fields"), QStringLiteral("token") })
            .toString(),
        QStringLiteral("kept"));
    draft.setBodyType(QStringLiteral("json"));
    QVERIFY(ConfigJson::get(
        draft.document(), { QStringLiteral("request"), QStringLiteral("body"), QStringLiteral("value") })
            .isNull());
    QVERIFY(draft.problems().isEmpty());
}

void TargetManagerTest::duplicateFormKeysCannotBeSilentlySaved()
{

    QTemporaryDir active, presets;
    auto object = target();
    auto request = object.value(QStringLiteral("request")).toObject();
    request.insert(QStringLiteral("headers"), QJsonObject { { QStringLiteral("X-Test"), QStringLiteral("one") } });
    object.insert(QStringLiteral("request"), request);
    writeBytes(active.filePath(QStringLiteral("raw.json")), QJsonDocument(object).toJson());
    TargetManagerController manager(presets.path(), active.path());
    JsonRowsModel rows;
    rows.setMap(true);
    rows.setPath({ QStringLiteral("request"), QStringLiteral("headers") });
    rows.setDraft(manager.draft());
    QAbstractItemModelTester tester(&rows, QAbstractItemModelTester::FailureReportingMode::QtTest);
    rows.append();
    rows.edit(1, 0, QStringLiteral("X-Test"));
    rows.edit(1, 1, QStringLiteral("two"));
    QVERIFY(manager.draft()->problems().join(QLatin1Char('\n')).contains(QStringLiteral("Duplicate")));
    QVERIFY(!manager.save());
    QCOMPARE(ConfigJson::get(manager.draft()->document(), rows.path()), request.value(QStringLiteral("headers")));
    manager.draft()->setValue({ QStringLiteral("description") }, QStringLiteral("Other edit"));
    QCOMPARE(rows.count(), 2);
    rows.edit(1, 0, QStringLiteral("X-Other"));
    QVERIFY(manager.save());
    QCOMPARE(ConfigJson::get(manager.draft()->document(), rows.path())
                 .toObject()
                 .value(QStringLiteral("X-Other"))
                 .toString(),
        QStringLiteral("two"));
}

void TargetManagerTest::emptyCommandArgumentsRemainRepairable()
{

    TargetDraft draft;
    auto object = target();
    object.insert(QStringLiteral("preUpload"),
        QJsonArray { QJsonObject { { QStringLiteral("mime"), QJsonArray { QStringLiteral("*/*") } },
            { QStringLiteral("fileHandling"), QStringLiteral("inplace_copy") },
            { QStringLiteral("commands"),
                QJsonArray { QJsonObject { { QStringLiteral("argv"), QJsonArray { QStringLiteral("${FILE}") } },
                    { QStringLiteral("future"), true } } } } } });
    draft.load(QJsonDocument(object).toJson());
    JsonRowsModel rows;
    rows.setPath({ QStringLiteral("preUpload"), QStringLiteral("0"), QStringLiteral("commands"), QStringLiteral("0"),
        QStringLiteral("argv") });
    rows.setDraft(&draft);
    QAbstractItemModelTester tester(&rows, QAbstractItemModelTester::FailureReportingMode::QtTest);
    rows.remove(0);
    QCOMPARE(rows.count(), 0);
    QVERIFY(ConfigJson::get(draft.document(), rows.path()).toArray().isEmpty());
    auto parent = rows.path();
    parent.removeLast();
    parent.append(QStringLiteral("future"));
    QVERIFY(ConfigJson::get(draft.document(), parent).toBool());
    rows.append(QStringLiteral("program"));
    rows.append(QStringLiteral("${FILE}"));
    rows.move(1, -1);
    QCOMPARE(rows.data(rows.index(0), JsonRowsModel::TextRole).toString(), QStringLiteral("${FILE}"));
}

void TargetManagerTest::walletReferencesAreValidatedAndExpandedOnce()
{
    qputenv("IMSHARE_SHOULD_STAY_LITERAL", "expanded");
    const QString secret = QStringLiteral("${ENV:IMSHARE_SHOULD_STAY_LITERAL}/${FILENAME}/${WALLET:other}");
    QCOMPARE(TargetUploaderUtils::substituteRequestValue(QStringLiteral("Bearer ${WALLET:demo}"),
                 QFileInfo(QStringLiteral("image.png")), { { QStringLiteral("demo"), secret } }),
        QStringLiteral("Bearer ") + secret);
    auto object = target();
    auto request = object.value(QStringLiteral("request")).toObject();
    request.insert(QStringLiteral("headers"),
        QJsonObject { { QStringLiteral("Authorization"), QStringLiteral("${WALLET:bad name}") } });
    object.insert(QStringLiteral("request"), request);
    QVERIFY(!TargetFileStore::validate(QJsonDocument(object).toJson()).isEmpty());
    request.insert(QStringLiteral("headers"),
        QJsonObject { { QStringLiteral("Authorization"), QStringLiteral("${WALLET:demo}") } });
    object.insert(QStringLiteral("request"), request);
    QVERIFY(TargetFileStore::validate(QJsonDocument(object).toJson()).isEmpty());
}

void TargetManagerTest::testRunnerUploadsWithCredentialsAndRedactsReplies()
{
    HttpCaptureServer server;
    QVERIFY(server.start());
    server.enqueueResponse({ 200, "OK", "application/json", { { "Set-Cookie", "private-cookie" } },
        R"({"url":"https://files.example/image.png","echo":"private-token"})" });
    auto object = target(server.url());
    auto request = object.value(QStringLiteral("request")).toObject();
    request.insert(QStringLiteral("headers"),
        QJsonObject { { QStringLiteral("Authorization"), QStringLiteral("Bearer ${WALLET:demo}") } });
    object.insert(QStringLiteral("request"), request);
    object.insert(QStringLiteral("response"),
        QJsonObject { { QStringLiteral("url"),
            QJsonObject { { QStringLiteral("type"), QStringLiteral("json_pointer") },
                { QStringLiteral("pointer"), QStringLiteral("/url") } } } });
    MemoryCredentials credentials;
    credentials.values.insert(QStringLiteral("demo"), QStringLiteral("private-token"));
    UploadTestRunner runner(nullptr, &credentials);
    QSignalSpy done(&runner, &UploadTestRunner::completed);
    runner.start(object, { }, false);
    QTRY_COMPARE(done.size(), 1);
    const auto result = qvariant_cast<UploadResult>(done.first().first());
    QVERIFY2(result.ok, qPrintable(result.errorMessage));
    QCOMPARE(server.requests().first().headers.value("authorization"), QByteArray("Bearer private-token"));
    QVERIFY(!QJsonDocument(result.toJson()).toJson().contains("private-token"));
    QVERIFY(!QJsonDocument(result.toJson()).toJson().contains("private-cookie"));
    QCOMPARE(result.url, QStringLiteral("https://files.example/image.png"));
    TargetUploader uploader(object);
    const auto offline = uploader.parseResponse(result.responseInfo);
    QCOMPARE(offline.url, result.url);
    QCOMPARE(offline.ok, result.ok);
}

void TargetManagerTest::purposeJobResolvesCredentialsBeforeUploading()
{
    HttpCaptureServer server;
    QVERIFY(server.start());
    server.enqueueResponse({ 200, "OK", "text/plain", { }, "https://files.example/shared" });
    auto object = target(server.url());
    auto request = object.value(QStringLiteral("request")).toObject();
    request.insert(QStringLiteral("headers"),
        QJsonObject { { QStringLiteral("Authorization"), QStringLiteral("Bearer ${WALLET:demo}") } });
    object.insert(QStringLiteral("request"), request);
    MemoryCredentials credentials;
    credentials.values.insert(QStringLiteral("demo"), QStringLiteral("job-secret"));
    QTemporaryDir dir;
    const auto file = writeTempFile(dir, QStringLiteral("sample.txt"), QByteArray("sample"));
    ShareJob job(QJsonDocument(object).toJson(), nullptr, &credentials);
    job.setAutoDelete(false);
    job.setData(QJsonObject { { QStringLiteral("urls"), QJsonArray { QUrl::fromLocalFile(file).toString() } } });
    QSignalSpy done(&job, &KJob::result);
    job.start();
    QTRY_COMPARE(done.size(), 1);
    QCOMPARE(job.error(), 0);
    QCOMPARE(credentials.reads, 1);
    QCOMPARE(server.requests().first().headers.value("authorization"), QByteArray("Bearer job-secret"));
}

void TargetManagerTest::missingCredentialsAndCancellationDoNotUpload()
{
    HttpCaptureServer server;
    QVERIFY(server.start());
    auto object = target(server.url());
    auto request = object.value(QStringLiteral("request")).toObject();
    request.insert(QStringLiteral("headers"),
        QJsonObject { { QStringLiteral("Authorization"), QStringLiteral("${WALLET:missing}") } });
    object.insert(QStringLiteral("request"), request);
    MemoryCredentials credentials;
    UploadTestRunner runner(nullptr, &credentials);
    QSignalSpy messages(&runner, &UploadTestRunner::message);
    runner.start(object, { }, false);
    QTRY_VERIFY(!runner.busy());
    QVERIFY(server.requests().isEmpty());
    QVERIFY(messages.last().first().toString().contains(QStringLiteral("Missing credential")));
    runner.start(object, { }, false);
    runner.cancel();
    QTest::qWait(10);
    QVERIFY(!runner.busy());
    QVERIFY(server.requests().isEmpty());
}

void TargetManagerTest::offlineParsingAndValidationPerformNoUpload()
{

    HttpCaptureServer server;
    QVERIFY(server.start());
    auto object = target(server.url());
    object.insert(QStringLiteral("response"),
        QJsonObject { { QStringLiteral("url"),
            QJsonObject { { QStringLiteral("type"), QStringLiteral("json_pointer") },
                { QStringLiteral("pointer"), QStringLiteral("/files/0/url") } } } });
    TargetDraft draft;
    draft.load(QJsonDocument(object).toJson());
    MemoryCredentials credentials;
    TestController panel(&draft, &credentials);
    panel.reset();
    panel.validate();
    panel.setResponseBody(QStringLiteral("{\"files\":[{\"url\":\"https://files.example/offline\"}]}"));
    panel.parseResponse();
    QVERIFY(panel.diagnostics().contains(QStringLiteral("https://files.example/offline")));
    QVERIFY(server.requests().isEmpty());
    QCOMPARE(credentials.reads, 0);
    panel.setResponseBody(QStringLiteral("{\"a/b~c\":\"https://files.example/escaped\"}"));
    const auto selected = panel.tree()->index(0, 1, panel.tree()->index(0, 0));
    panel.usePointer(selected, QStringLiteral("thumbnail"));
    QCOMPARE(ConfigJson::get(draft.document(),
                 { QStringLiteral("response"), QStringLiteral("thumbnail"), QStringLiteral("pointer") })
                 .toString(),
        QStringLiteral("/a~1b~0c"));
    panel.setResponseBody(QStringLiteral("\"https://files.example/root\""));
    panel.usePointer(panel.tree()->index(0, 0), { });
    QCOMPARE(ConfigJson::get(
                 draft.document(), { QStringLiteral("response"), QStringLiteral("url"), QStringLiteral("pointer") })
                 .toString(),
        QString { });
    // Empty JSON Pointer selects a scalar root and is valid in schema version 1.
    QVERIFY(draft.problems().isEmpty());
    panel.parseResponse();
    QVERIFY(panel.diagnostics().contains(QStringLiteral("https://files.example/root")));
}

void TargetManagerTest::managerSavesActualEditedTarget()
{

    QTemporaryDir active, presets;
    const auto file = active.filePath(QStringLiteral("raw.json"));
    writeBytes(file, QJsonDocument(target()).toJson());
    TargetManagerController manager(presets.path(), active.path());
    manager.draft()->setValue({ QStringLiteral("displayName") }, QStringLiteral("My upload service"));
    QVERIFY(manager.dirty());
    QVERIFY(manager.save());
    QVERIFY(!manager.dirty());
    QCOMPARE(QJsonDocument::fromJson(readBytes(file)).object().value(QStringLiteral("displayName")).toString(),
        QStringLiteral("My upload service"));
    manager.draft()->setValue({ QStringLiteral("displayName") }, QStringLiteral("Do not overwrite"));
    writeBytes(file, readBytes(file) + QByteArray("\n"));
    QTRY_VERIFY(manager.diagnosticText().contains(QStringLiteral("changed on disk")));
    QVERIFY(!manager.save());
    QVERIFY(manager.dirty());
}

void TargetManagerTest::previewAndCancellationLeaveOriginalUntouched()
{
    QVERIFY(!pythonExecutable().isEmpty());
    HttpCaptureServer server;
    QVERIFY(server.start());
    QTemporaryDir files;
    const auto original = writeTempFile(files, QStringLiteral("sample.txt"), QByteArray("original"));
    auto object = target(server.url());
    auto rule = QJsonObject { { QStringLiteral("mime"), QJsonArray { QStringLiteral("*/*") } },
        { QStringLiteral("fileHandling"), QStringLiteral("inplace_copy") },
        { QStringLiteral("commands"),
            QJsonArray { commandObject({ pythonExecutable(), fixtureScriptPath(QStringLiteral("append_text.py")),
                QStringLiteral("${FILE}"), QStringLiteral("-prepared") }) } } };
    object.insert(QStringLiteral("preUpload"), QJsonArray { rule });
    MemoryCredentials credentials;
    UploadTestRunner runner(nullptr, &credentials);
    QSignalSpy previews(&runner, &UploadTestRunner::previewReady);
    QSignalSpy messages(&runner, &UploadTestRunner::message);
    runner.start(object, original, true);
    QTRY_COMPARE(previews.size(), 1);
    const auto prepared = previews.first().first().toString();
    QCOMPARE(readBytes(prepared), QByteArray("original-prepared"));
    QCOMPARE(readBytes(original), QByteArray("original"));
    QCOMPARE(credentials.reads, 0);
    QVERIFY(server.requests().isEmpty());
    // Observe an actual running command before cancelling it.
    const auto started = files.filePath(QStringLiteral("started"));
    rule.insert(QStringLiteral("commands"),
        QJsonArray { commandObject({ pythonExecutable(), QStringLiteral("-c"),
            QStringLiteral("import pathlib,sys,time; pathlib.Path(sys.argv[2]).write_text('started'); time.sleep(10); "
                           "pathlib.Path(sys.argv[1]).write_text('wrong')"),
            QStringLiteral("${FILE}"), started }) });
    object.insert(QStringLiteral("preUpload"), QJsonArray { rule });
    runner.start(object, original, false);
    QTRY_VERIFY(QFileInfo::exists(started));
    QVERIFY(runner.busy());
    runner.cancel();
    QVERIFY(!runner.busy());
    QVERIFY(!QFileInfo::exists(prepared));
    QCOMPARE(previews.size(), 1);
    QVERIFY(server.requests().isEmpty());
    QCOMPARE(readBytes(original), QByteArray("original"));
    rule.insert(QStringLiteral("commands"),
        QJsonArray { commandObject({ QStringLiteral("imshare-nonexistent-command"), QStringLiteral("${FILE}") }) });
    object.insert(QStringLiteral("preUpload"), QJsonArray { rule });
    runner.start(object, original, true);
    QTRY_VERIFY(!runner.busy());
    QVERIFY(messages.last().first().toString().contains(QStringLiteral("imshare-nonexistent-command")));
    QCOMPARE(previews.size(), 1);
}

void TargetManagerTest::oversizedResponseStopsTheTest()
{
    HttpCaptureServer server;
    QVERIFY(server.start());
    server.enqueueResponse({ 200, "OK", "text/plain", { }, QByteArray(2 * 1024 * 1024, 'x') });
    MemoryCredentials credentials;
    UploadTestRunner runner(nullptr, &credentials);
    QSignalSpy done(&runner, &UploadTestRunner::completed);
    runner.start(target(server.url()), { }, false);
    QTRY_COMPARE(done.size(), 1);
    const auto result = qvariant_cast<UploadResult>(done.first().first());
    QVERIFY(!result.ok);
    QVERIFY(result.errorMessage.contains(QStringLiteral("1 MiB")));
    QVERIFY(result.responseInfo.responseText.size() <= 1024 * 1024);
    QVERIFY(!runner.busy());
}

void TargetManagerTest::managerImportsExportsAndKeepsDraftsDisabled()
{

    QTemporaryDir active, presets, transfer;
    auto object = target();
    object.insert(QStringLiteral("future"), QJsonArray { true, 12 });
    const auto source = transfer.filePath(QStringLiteral("import.json"));
    writeBytes(source, QJsonDocument(object).toJson());
    TargetManagerController manager(presets.path(), active.path());
    manager.importFile(QUrl::fromLocalFile(source));
    QVERIFY(QDir(active.path()).entryList({ QStringLiteral("*.json") }, QDir::Files).isEmpty());
    QVERIFY(manager.save());
    const auto saved = active.filePath(QStringLiteral("disabled/raw.json"));
    QCOMPARE(QJsonDocument::fromJson(readBytes(saved)).object(), object);
    QSignalSpy review(&manager, &TargetManagerController::exportReviewRequested);
    manager.request(QStringLiteral("export"));
    QCOMPARE(review.size(), 1);
    manager.acceptExport(review.first().first().toString());
    const auto exported = transfer.filePath(QStringLiteral("export.json"));
    manager.exportFile(QUrl::fromLocalFile(exported));
    QCOMPARE(QJsonDocument::fromJson(readBytes(exported)).object(), object);
    manager.request(QStringLiteral("toggle"));
    QVERIFY(QFileInfo::exists(active.filePath(QStringLiteral("raw.json"))));
    QVERIFY(!QFileInfo::exists(saved));
}

void TargetManagerTest::managerDiscardsEditsBeforeDuplicating()
{

    QTemporaryDir active, presets;
    writeBytes(active.filePath(QStringLiteral("raw.json")), QJsonDocument(target()).toJson());
    TargetManagerController manager(presets.path(), active.path());
    QSignalSpy ask(&manager, &TargetManagerController::discardRequested);
    manager.draft()->setValue({ QStringLiteral("displayName") }, QStringLiteral("Discard me"));
    manager.request(QStringLiteral("duplicate"));
    QCOMPARE(ask.size(), 1);
    manager.resolveDraft(QStringLiteral("cancel"));
    QCOMPARE(manager.draft()->stringValue({ QStringLiteral("displayName") }), QStringLiteral("Discard me"));
    manager.request(QStringLiteral("duplicate"));
    manager.resolveDraft(QStringLiteral("discard"));
    QCOMPARE(manager.draft()->stringValue({ QStringLiteral("displayName") }), QStringLiteral("Test host (copy)"));
    QVERIFY(manager.save());
    QVERIFY(QFileInfo::exists(active.filePath(QStringLiteral("disabled/raw_2.json"))));
    manager.draft()->setValue({ QStringLiteral("description") }, QStringLiteral("Saved before closing"));
    QSignalSpy closed(&manager, &TargetManagerController::closeReady);
    manager.request(QStringLiteral("close"));
    manager.resolveDraft(QStringLiteral("save"));
    QCOMPARE(closed.size(), 1);
    QVERIFY(!manager.dirty());
}

void TargetManagerTest::credentialHelpersStoreReferencesAndHandleDraftChanges()
{

    MemoryCredentials credentials;
    TargetDraft draft;
    draft.load(QJsonDocument(target()).toJson());
    CredentialController panel(&draft, &credentials);
    panel.bind(1, 0, QStringLiteral("Authorization"), QStringLiteral("demo"), QStringLiteral("secret-one"), { }, true);
    QTRY_VERIFY(!panel.busy());
    QCOMPARE(credentials.values.value(QStringLiteral("demo")), QStringLiteral("secret-one"));
    QCOMPARE(ConfigJson::get(draft.document(),
                 { QStringLiteral("request"), QStringLiteral("headers"), QStringLiteral("Authorization") })
                 .toString(),
        QStringLiteral("Bearer ${WALLET:demo}"));
    QVERIFY(!draft.bytes().contains("secret-one"));
    panel.bind(1, 0, { }, QStringLiteral("other"), QStringLiteral("secret-two"), { }, true);
    draft.setValue({ QStringLiteral("description") }, QStringLiteral("Changed while waiting"));
    QTRY_VERIFY(!panel.busy());
    QVERIFY(!draft.bytes().contains("WALLET:other"));
    QVERIFY(panel.message().contains(QStringLiteral("draft changed")));
    panel.bind(1, 0, { }, QStringLiteral("other"), { }, { }, false);
    draft.load(QJsonDocument(target()).toJson());
    panel.reset();
    QTRY_VERIFY(!panel.busy());
    QVERIFY(panel.message().isEmpty());
    QVERIFY(!draft.bytes().contains("WALLET:other"));
    draft.setBodyType(QStringLiteral("json"));
    panel.bind(0, 4, QStringLiteral("token"), QStringLiteral("demo"), { }, { }, false);
    QTRY_VERIFY(!panel.busy());
    QCOMPARE(
        ConfigJson::get(draft.document(),
            { QStringLiteral("request"), QStringLiteral("body"), QStringLiteral("value"), QStringLiteral("token") })
            .toString(),
        QStringLiteral("${WALLET:demo}"));
}

void TargetManagerTest::invalidDefinitionsDoNotBlockValidTargets()
{
    QTemporaryDir active, presets;
    TargetFileStore store(presets.path(), active.path());
    const auto goodPath = active.filePath(QStringLiteral("a-valid.json"));
    const auto otherPath = active.filePath(QStringLiteral("b-invalid.json"));
    const auto bytes = QJsonDocument(target()).toJson();
    writeBytes(goodPath, bytes);
    auto invalid = target();
    auto request = invalid.value(QStringLiteral("request")).toObject();
    request.insert(QStringLiteral("method"), QStringLiteral("GET"));
    invalid.insert(QStringLiteral("request"), request);
    writeBytes(otherPath, QJsonDocument(invalid).toJson());
    auto entries = store.entries();
    QCOMPARE(entries.size(), 2);
    QVERIFY(entries.first().problems.isEmpty());
    QVERIFY(!entries.last().problems.isEmpty());
    QCOMPARE(store.save(goodPath, bytes, entries.first().revision), QString { });
    writeBytes(otherPath, bytes); // A valid duplicate is ignored by the runtime.
    entries = store.entries();
    QVERIFY(entries.first().problems.isEmpty());
    QVERIFY(entries.last().problems.join(QLatin1Char('\n')).contains(QStringLiteral("already defined")));
    QVERIFY(!store.save(otherPath, bytes, entries.last().revision).isEmpty());
    const auto runtime = TargetRegistry(presets.path(), active.path()).loadTargets();
    QCOMPARE(runtime.targets.size(), 1);
    QCOMPARE(runtime.diagnostics.size(), 1);
    QCOMPARE(runtime.diagnostics.first().filePath, otherPath);
}

void TargetManagerTest::listResetExposesOldDataBeforeReplacement()
{
    TargetListModel model;
    TargetFileStore::Entry entry;
    entry.path = QStringLiteral("old.json");
    entry.object = target();
    model.setEntries({ entry });
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    int observed = 0;
    connect(&model, &QAbstractItemModel::modelAboutToBeReset, this, [&]() {
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.pathAt(0), QStringLiteral("old.json"));
        ++observed;
    });
    model.setEntries({ });
    QCOMPARE(observed, 1);
    QCOMPARE(model.rowCount(), 0);
}

void TargetManagerTest::managerPresetAndDraftActions()
{
    QTemporaryDir root, presets;
    const auto source = presets.filePath(QStringLiteral("raw.json"));
    const auto original = QJsonDocument(target()).toJson();
    writeBytes(source, original);
    TargetManagerController manager(presets.path(), root.filePath(QStringLiteral("targets")));
    const auto active = manager.selectedPath();
    QVERIFY(QFileInfo(active).isSymLink());
    QVERIFY(!manager.draft()->editable());
    manager.request(QStringLiteral("customize"));
    manager.draft()->setValue({ QStringLiteral("displayName") }, QStringLiteral("Custom"));
    QVERIFY(manager.save());
    QVERIFY(!QFileInfo(active).isSymLink());
    QCOMPARE(readBytes(source), original);
    manager.request(QStringLiteral("restore"));
    manager.confirm(true);
    QVERIFY(QFileInfo(active).isSymLink());
    manager.request(QStringLiteral("delete"));
    manager.confirm(false);
    QVERIFY(QFileInfo(active).exists());
    manager.request(QStringLiteral("delete"));
    manager.confirm(true);
    QVERIFY(!QFileInfo(active).exists());
    manager.request(QStringLiteral("new"));
    QVERIFY(manager.dirty());
    QVERIFY(manager.selectedPath().contains(QStringLiteral("/disabled/")));
    QVERIFY(manager.save());
    const auto saved = manager.selectedPath();
    QVERIFY(QFileInfo(saved).isFile());
    QSignalSpy fileDialog(&manager, &TargetManagerController::fileDialogRequested);
    manager.acceptExport(ConfigJson::text(manager.draft()->document()));
    QCOMPARE(fileDialog.size(), 1);
    const QUrl destination(fileDialog.first().at(1).toString());
    QVERIFY(destination.isLocalFile());
    QVERIFY(QFileInfo(destination.toLocalFile()).absolutePath() != QLatin1StringView("/"));
    manager.draft()->setRawText(QStringLiteral("{broken"));
    QVERIFY(manager.save()); // Disabled malformed drafts stay repairable.
    manager.request(QStringLiteral("toggle"));
    QCOMPARE(manager.selectedPath(), saved);
    QVERIFY(QFileInfo(saved).exists());
}

QTEST_MAIN(TargetManagerTest)
#include "test_targetmanager.moc"
