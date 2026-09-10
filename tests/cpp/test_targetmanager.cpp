#include "configwidgets.h"
#include "credentialstore.h"
#include "credentialpanel.h"
#include "sharejob.h"
#include "targeteditor.h"
#include "targetfilestore.h"
#include "targetmanagerwindow.h"
#include "targettestpanel.h"
#include "targetuploader_utils.h"
#include "uploadtestrunner.h"
#include "httpcaptureserver.h"
#include "testutils.h"

#include <QApplication>
#include <QAction>
#include <QComboBox>
#include <QFileDialog>
#include <QDialogButtonBox>
#include <QJsonDocument>
#include <QLineEdit>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QScopeGuard>
#include <QTableWidget>
#include <QTabWidget>
#include <QTimer>
#include <QtTest>

class MemoryCredentials final : public CredentialStore
{
public:
    Values values;
    int reads = 0;
    void read(const QStringList &keys, QObject *context, ReadCallback callback) override {
        ++reads;
        Values result; QString error;
        for (const auto &key : keys) {
            if (!values.contains(key)) { error = QStringLiteral("Missing credential: ") + key; break; }
            result.insert(key, values.value(key));
        }
        QTimer::singleShot(0, context, [callback, result, error]() { callback(result, error); });
    }
    void write(const QString &key, const QString &value, QObject *context, WriteCallback callback) override {
        values.insert(key, value); QTimer::singleShot(0, context, [callback]() { callback({}); });
    }
};

namespace {
QJsonObject target(const QUrl &url = QUrl(QStringLiteral("https://example.invalid/upload")))
{
    auto object = rawTarget(url);
    object.insert(QStringLiteral("displayName"), QStringLiteral("Test host"));
    object.insert(QStringLiteral("description"), QStringLiteral("A custom upload service"));
    object.insert(QStringLiteral("icon"), QStringLiteral("document-send"));
    return object;
}
QByteArray readBytes(const QString &path) { QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {}; return file.readAll(); }
void writeBytes(const QString &path, const QByteArray &bytes) { QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(bytes), bytes.size()); }
}

class TargetManagerTest final : public QObject
{
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
    void selectedErrorIconRetainsItsColors();
    void diagnosticsDoNotMoveEditorControls_data();
    void diagnosticsDoNotMoveEditorControls();
};

void TargetManagerTest::presetLifecycleUsesFilesAndNeverWritesThroughLinks()
{
    QTemporaryDir root, presets;
    const auto source = presets.filePath(QStringLiteral("raw.json"));
    const auto bytes = QJsonDocument(target()).toJson(); writeBytes(source, bytes);
    const auto active = root.filePath(QStringLiteral("targets")); TargetFileStore store(presets.path(), active);
    QVERIFY(!store.entries().isEmpty());
    const auto path = active + QStringLiteral("/raw.json"); QVERIFY(QFileInfo(path).isSymLink());
    auto entry = TargetFileStore::read(path, TargetFileStore::Kind::Active);
    QCOMPARE(store.setEnabled(entry, false), QString{}); QVERIFY(!QFileInfo(path).exists());
    store.entries(); QVERIFY(!QFileInfo(path).exists()); // An existing empty directory stays empty.
    QCOMPARE(store.enablePreset(source), QString{});
    entry = TargetFileStore::read(path, TargetFileStore::Kind::Active);
    auto edited = entry.object; edited.insert(QStringLiteral("displayName"), QStringLiteral("Custom name"));
    QCOMPARE(store.save(path, QJsonDocument(edited).toJson(), entry.revision), QString{});
    QVERIFY(!QFileInfo(path).isSymLink()); QCOMPARE(readBytes(source), bytes);
    entry = TargetFileStore::read(path, TargetFileStore::Kind::Active);
    QCOMPARE(store.setEnabled(entry, false), QString{});
    const auto disabled = active + QStringLiteral("/disabled/raw.json"); QVERIFY(QFileInfo::exists(disabled));
    QCOMPARE(store.setEnabled(TargetFileStore::read(disabled, TargetFileStore::Kind::Disabled), true), QString{});
    QCOMPARE(store.restorePreset(TargetFileStore::read(path, TargetFileStore::Kind::Active)), QString{});
    QVERIFY(QFileInfo(path).isSymLink()); QCOMPARE(readBytes(source), bytes);
}

void TargetManagerTest::disabledDraftsConflictsAndExternalChanges()
{
    QTemporaryDir root, presets, external; const auto active = root.filePath(QStringLiteral("targets")); QVERIFY(QDir().mkpath(active));
    TargetFileStore store(presets.path(), active);
    const auto path = active + QStringLiteral("/raw.json"); const auto bytes = QJsonDocument(target()).toJson();
    QCOMPARE(store.save(path, bytes, {}), QString{});
    const auto entry = TargetFileStore::read(path, TargetFileStore::Kind::Active);
    writeBytes(path, bytes + QByteArray("\n"));
    QVERIFY(store.save(path, bytes, entry.revision).contains(QStringLiteral("changed outside")));
    QVERIFY(!store.save(active + QStringLiteral("/duplicate.json"), bytes, {}).isEmpty());
    const auto disabled = active + QStringLiteral("/disabled/draft.json");
    QCOMPARE(store.save(disabled, QByteArray("{bad JSON"), {}, true), QString{});
    QVERIFY(!store.setEnabled(TargetFileStore::read(disabled, TargetFileStore::Kind::Disabled), true).isEmpty());
    QVERIFY(!store.save(path, QByteArray("{bad JSON"), TargetFileStore::revision(path), true).isEmpty());
    const auto foreign = external.filePath(QStringLiteral("original.json")); writeBytes(foreign, bytes);
    const auto link = active + QStringLiteral("/foreign.json"); QVERIFY(QFile::link(foreign, link));
    auto other = target(); other.insert(QStringLiteral("id"), QStringLiteral("foreign"));
    QCOMPARE(store.save(link, QJsonDocument(other).toJson(), TargetFileStore::revision(link)), QString{});
    QCOMPARE(readBytes(foreign), bytes); QVERIFY(!QFileInfo(link).isSymLink());
    QVERIFY(!store.save(root.filePath(QStringLiteral("outside.json")), bytes, {}).isEmpty());
}

void TargetManagerTest::exportPreservesReferencesAndRemovesRecognizedSecrets()
{
    auto object = target(QUrl(QStringLiteral("https://example.test/upload?api_key=literal-url-secret")));
    auto request = object.value(QStringLiteral("request")).toObject();
    request.insert(QStringLiteral("headers"), QJsonObject{{QStringLiteral("Authorization"), QStringLiteral("Bearer literal-header-secret")},
        {QStringLiteral("X-Token"), QStringLiteral("${WALLET:work.api}")}, {QStringLiteral("X-Name"), QStringLiteral("ordinary")}});
    object.insert(QStringLiteral("request"), request); object.insert(QStringLiteral("futureField"), QJsonArray{1, true});
    QStringList redacted; const auto exported = TargetFileStore::portable(object, &redacted); const auto data = QJsonDocument(exported).toJson();
    QVERIFY(!data.contains("literal-url-secret")); QVERIFY(!data.contains("literal-header-secret")); QVERIFY(data.contains("${WALLET:work.api}"));
    QCOMPARE(exported.value(QStringLiteral("futureField")), object.value(QStringLiteral("futureField")));
    QVERIFY(TargetFileStore::validate(data).isEmpty()); QCOMPARE(redacted.size(), 2);
    QVERIFY(QJsonDocument(object).toJson().contains("literal-header-secret"));
}

void TargetManagerTest::formsPreserveUnknownFieldsAndJsonTypes()
{
    const QDir examples(testSourceDir() + QStringLiteral("/../targets/examples"));
    for (const auto &path : examples.entryList({QStringLiteral("*.json")}, QDir::Files)) {
        auto object = QJsonDocument::fromJson(readBytes(examples.filePath(path))).object();
        object.insert(QStringLiteral("future"), QJsonArray{true, 42, QJsonObject{{QStringLiteral("nested"), QStringLiteral("value")}}});
        auto request = object.value(QStringLiteral("request")).toObject(); request.insert(QStringLiteral("futureRequest"), false); object.insert(QStringLiteral("request"), request);
        TargetEditor editor; const auto bytes = QJsonDocument(object).toJson(); editor.setBytes(bytes); QCOMPARE(editor.bytes(), bytes);
        auto *name = editor.findChild<QLineEdit *>(QStringLiteral("displayName")); QVERIFY(name);
        name->selectAll(); QTest::keyClicks(name, QStringLiteral("Edited name"));
        auto expected = object; expected.insert(QStringLiteral("displayName"), QStringLiteral("Edited name")); QCOMPARE(editor.document(), expected);
    }
    TargetEditor editor;
    auto object = target(); auto request = object.value(QStringLiteral("request")).toObject();
    request.insert(QStringLiteral("type"), QStringLiteral("json")); request.insert(QStringLiteral("json"), QJsonObject{{QStringLiteral("fields"), true}}); object.insert(QStringLiteral("request"), request);
    editor.setBytes(QJsonDocument(object).toJson()); auto *body = editor.findChild<QPlainTextEdit *>(QStringLiteral("jsonBody")); QVERIFY(body);
    body->setPlainText(QStringLiteral("[false, 1, null, {\"message\":\"hello\"}]"));
    QCOMPARE(editor.document().value(QStringLiteral("request")).toObject().value(QStringLiteral("json")).toObject().value(QStringLiteral("fields")), QJsonValue(QJsonArray{false, 1, QJsonValue(), QJsonObject{{QStringLiteral("message"), QStringLiteral("hello")}}}));
    QVERIFY(editor.problems().isEmpty());
    auto *raw = editor.findChild<QPlainTextEdit *>(QStringLiteral("targetJson")); raw->setPlainText(QStringLiteral("{broken")); QVERIFY(!editor.problems().isEmpty());
    QVERIFY(!editor.findChild<QLineEdit *>(QStringLiteral("displayName"))->isEnabled());
    raw->setPlainText(ConfigJson::text(object)); QVERIFY(editor.problems().isEmpty());
}

void TargetManagerTest::duplicateFormKeysCannotBeSilentlySaved()
{
    auto object = target(); auto request = object.value(QStringLiteral("request")).toObject();
    request.insert(QStringLiteral("headers"), QJsonObject{{QStringLiteral("X-Test"), QStringLiteral("one")}}); object.insert(QStringLiteral("request"), request);
    TargetEditor editor; editor.setBytes(QJsonDocument(object).toJson());
    auto *table = editor.findChild<StringTable *>(QStringLiteral("request.headers")); QVERIFY(table);
    for (auto *button : table->findChildren<QPushButton *>()) if (button->text() == QLatin1StringView("Add")) button->click();
    auto *grid = table->findChild<QTableWidget *>(); grid->item(1, 0)->setText(QStringLiteral("X-Test")); grid->item(1, 1)->setText(QStringLiteral("two"));
    QVERIFY(editor.problems().join(QLatin1Char('\n')).contains(QStringLiteral("Duplicate")));
    QCOMPARE(editor.document().value(QStringLiteral("request")).toObject().value(QStringLiteral("headers")), request.value(QStringLiteral("headers")));
}

void TargetManagerTest::emptyCommandArgumentsRemainRepairable()
{
    PreUploadEditor editor;
    editor.setValue(QJsonArray{QJsonObject{{QStringLiteral("mime"), QJsonArray{QStringLiteral("*/*")}}, {QStringLiteral("fileHandling"), QStringLiteral("inplace_copy")},
        {QStringLiteral("commands"), QJsonArray{QJsonObject{{QStringLiteral("argv"), QJsonArray{QStringLiteral("${FILE}")}}, {QStringLiteral("future"), true}}}}}});
    const auto tables = editor.findChildren<StringTable *>(); QCOMPARE(tables.size(), 2);
    auto *args = tables.last(); args->findChild<QTableWidget *>()->setCurrentCell(0, 0);
    for (auto *button : args->findChildren<QPushButton *>()) if (button->text() == QLatin1StringView("Remove")) button->click();
    const auto command = editor.value().first().toObject().value(QStringLiteral("commands")).toArray().first().toObject();
    QVERIFY(command.value(QStringLiteral("argv")).toArray().isEmpty()); QVERIFY(command.value(QStringLiteral("future")).toBool());
}

void TargetManagerTest::walletReferencesAreValidatedAndExpandedOnce()
{
    qputenv("IMSHARE_SHOULD_STAY_LITERAL", "expanded");
    const QString secret = QStringLiteral("${ENV:IMSHARE_SHOULD_STAY_LITERAL}/${FILENAME}/${WALLET:other}");
    QCOMPARE(TargetUploaderUtils::substituteRequestValue(QStringLiteral("Bearer ${WALLET:demo}"), QFileInfo(QStringLiteral("image.png")), {{QStringLiteral("demo"), secret}}), QStringLiteral("Bearer ") + secret);
    auto object = target(); auto request = object.value(QStringLiteral("request")).toObject();
    request.insert(QStringLiteral("headers"), QJsonObject{{QStringLiteral("Authorization"), QStringLiteral("${WALLET:bad name}")}}); object.insert(QStringLiteral("request"), request);
    QVERIFY(!TargetFileStore::validate(QJsonDocument(object).toJson()).isEmpty());
    request.insert(QStringLiteral("headers"), QJsonObject{{QStringLiteral("Authorization"), QStringLiteral("${WALLET:demo}")}}); object.insert(QStringLiteral("request"), request);
    QVERIFY(TargetFileStore::validate(QJsonDocument(object).toJson()).isEmpty());
}

void TargetManagerTest::testRunnerUploadsWithCredentialsAndRedactsReplies()
{
    HttpCaptureServer server; QVERIFY(server.start());
    server.enqueueResponse({200, "OK", "application/json", {{"Set-Cookie", "private-cookie"}}, R"({"url":"https://files.example/image.png","echo":"private-token"})"});
    auto object = target(server.url()); auto request = object.value(QStringLiteral("request")).toObject();
    request.insert(QStringLiteral("headers"), QJsonObject{{QStringLiteral("Authorization"), QStringLiteral("Bearer ${WALLET:demo}")}}); object.insert(QStringLiteral("request"), request);
    object.insert(QStringLiteral("response"), QJsonObject{{QStringLiteral("type"), QStringLiteral("json_pointer")}, {QStringLiteral("pointer"), QStringLiteral("/url")}});
    MemoryCredentials credentials; credentials.values.insert(QStringLiteral("demo"), QStringLiteral("private-token"));
    UploadTestRunner runner(nullptr, &credentials); QSignalSpy done(&runner, &UploadTestRunner::completed);
    runner.start(object, {}, false); QTRY_COMPARE(done.size(), 1);
    const auto result = qvariant_cast<UploadResult>(done.first().first()); QVERIFY2(result.ok, qPrintable(result.errorMessage));
    QCOMPARE(server.requests().first().headers.value("authorization"), QByteArray("Bearer private-token"));
    QVERIFY(!QJsonDocument(result.toJson()).toJson().contains("private-token")); QVERIFY(!QJsonDocument(result.toJson()).toJson().contains("private-cookie"));
    QCOMPARE(result.url, QStringLiteral("https://files.example/image.png"));
    TargetUploader uploader(object); const auto offline = uploader.parseResponse(result.responseInfo); QCOMPARE(offline.url, result.url); QCOMPARE(offline.ok, result.ok);
}

void TargetManagerTest::purposeJobResolvesCredentialsBeforeUploading()
{
    HttpCaptureServer server; QVERIFY(server.start()); server.enqueueResponse({200, "OK", "text/plain", {}, "https://files.example/shared"});
    auto object = target(server.url()); auto request = object.value(QStringLiteral("request")).toObject();
    request.insert(QStringLiteral("headers"), QJsonObject{{QStringLiteral("Authorization"), QStringLiteral("Bearer ${WALLET:demo}")}}); object.insert(QStringLiteral("request"), request);
    MemoryCredentials credentials; credentials.values.insert(QStringLiteral("demo"), QStringLiteral("job-secret"));
    QTemporaryDir dir; const auto file = writeTempFile(dir, QStringLiteral("sample.txt"), QByteArray("sample"));
    ShareJob job(QJsonDocument(object).toJson(), nullptr, &credentials);
    job.setAutoDelete(false);
    job.setData(QJsonObject{{QStringLiteral("urls"), QJsonArray{QUrl::fromLocalFile(file).toString()}}});
    QSignalSpy done(&job, &KJob::result); job.start(); QTRY_COMPARE(done.size(), 1);
    QCOMPARE(job.error(), 0); QCOMPARE(credentials.reads, 1);
    QCOMPARE(server.requests().first().headers.value("authorization"), QByteArray("Bearer job-secret"));
}

void TargetManagerTest::missingCredentialsAndCancellationDoNotUpload()
{
    HttpCaptureServer server; QVERIFY(server.start());
    auto object = target(server.url()); auto request = object.value(QStringLiteral("request")).toObject();
    request.insert(QStringLiteral("headers"), QJsonObject{{QStringLiteral("Authorization"), QStringLiteral("${WALLET:missing}")}}); object.insert(QStringLiteral("request"), request);
    MemoryCredentials credentials; UploadTestRunner runner(nullptr, &credentials); QSignalSpy messages(&runner, &UploadTestRunner::message);
    runner.start(object, {}, false); QTRY_VERIFY(!runner.busy()); QVERIFY(server.requests().isEmpty());
    QVERIFY(messages.last().first().toString().contains(QStringLiteral("Missing credential")));
    runner.start(object, {}, false); runner.cancel(); QTest::qWait(10); QVERIFY(!runner.busy()); QVERIFY(server.requests().isEmpty());
}

void TargetManagerTest::offlineParsingAndValidationPerformNoUpload()
{
    HttpCaptureServer server; QVERIFY(server.start());
    auto object = target(server.url()); object.insert(QStringLiteral("response"), QJsonObject{{QStringLiteral("type"), QStringLiteral("json_pointer")},
        {QStringLiteral("pointer"), QStringLiteral("/files/0/url")}, {QStringLiteral("error"), QJsonObject{{QStringLiteral("type"), QStringLiteral("json_pointer")}, {QStringLiteral("pointer"), QStringLiteral("/description")}}}});
    TargetEditor editor; editor.setBytes(QJsonDocument(object).toJson()); MemoryCredentials credentials;
    TargetTestPanel panel(&editor, &credentials); panel.reset(); panel.validateDraft();
    panel.findChild<QPlainTextEdit *>(QStringLiteral("testResponse"))->setPlainText(QStringLiteral("{\"files\":[{\"url\":\"https://files.example/offline\"}]}"));
    panel.findChild<QPushButton *>(QStringLiteral("testResponseParsing"))->click();
    QVERIFY(panel.findChild<QPlainTextEdit *>(QStringLiteral("testLog"))->toPlainText().contains(QStringLiteral("https://files.example/offline")));
    QVERIFY(server.requests().isEmpty()); QCOMPARE(credentials.reads, 0);
    TargetUploader uploader(object); UploadResponseInfo error; error.statusCode = 400; error.responseText = QStringLiteral("{\"description\":\"File too big\"}");
    QCOMPARE(uploader.parseResponse(error).errorMessage, QStringLiteral("File too big"));
}

void TargetManagerTest::managerSavesActualEditedTarget()
{
    QTemporaryDir active, presets; auto object = target(); const auto file = active.filePath(QStringLiteral("raw.json")); writeBytes(file, QJsonDocument(object).toJson());
    MemoryCredentials credentials; TargetManagerWindow window(presets.path(), active.path(), nullptr, &credentials);
    window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto *editor = window.findChild<TargetEditor *>(); QVERIFY(editor);
    auto *name = editor->findChild<QLineEdit *>(QStringLiteral("displayName")); name->selectAll(); QTest::keyClicks(name, QStringLiteral("My upload service"));
    auto *save = window.findChild<QPushButton *>(QStringLiteral("saveTarget")); QVERIFY(save->isEnabled()); save->click();
    QCOMPARE(QJsonDocument::fromJson(readBytes(file)).object().value(QStringLiteral("displayName")).toString(), QStringLiteral("My upload service"));
    QVERIFY(!save->isEnabled());
    if (qEnvironmentVariableIsSet("IMSHARE_SCREENSHOTS")) {
        const QString directory = QString::fromLocal8Bit(qgetenv("IMSHARE_SCREENSHOTS")); QDir().mkpath(directory);
        for (int i = 0; i < editor->tabs()->count(); ++i) {
            editor->tabs()->setCurrentIndex(i); QTest::qWait(30);
            QVERIFY(window.grab().save(QDir(directory).filePath(QStringLiteral("manager-%1.png").arg(i))));
        }
    }
    window.close();
}

void TargetManagerTest::previewAndCancellationLeaveOriginalUntouched()
{
    QVERIFY(!pythonExecutable().isEmpty());
    HttpCaptureServer server; QVERIFY(server.start()); QTemporaryDir files;
    const auto original = writeTempFile(files, QStringLiteral("sample.txt"), QByteArray("original"));
    auto object = target(server.url());
    auto rule = QJsonObject{{QStringLiteral("mime"), QJsonArray{QStringLiteral("*/*")}}, {QStringLiteral("fileHandling"), QStringLiteral("inplace_copy")},
        {QStringLiteral("commands"), QJsonArray{commandObject({pythonExecutable(), fixtureScriptPath(QStringLiteral("append_text.py")), QStringLiteral("${FILE}"), QStringLiteral("-prepared")})}}};
    object.insert(QStringLiteral("preUpload"), QJsonArray{rule});
    MemoryCredentials credentials; UploadTestRunner runner(nullptr, &credentials);
    QSignalSpy previews(&runner, &UploadTestRunner::previewReady); QSignalSpy messages(&runner, &UploadTestRunner::message);
    runner.start(object, original, true); QTRY_COMPARE(previews.size(), 1);
    const auto prepared = previews.first().first().toString();
    QCOMPARE(readBytes(prepared), QByteArray("original-prepared")); QCOMPARE(readBytes(original), QByteArray("original"));
    QCOMPARE(credentials.reads, 0); QVERIFY(server.requests().isEmpty());
    // Observe an actual running command before cancelling it.
    const auto started = files.filePath(QStringLiteral("started"));
    rule.insert(QStringLiteral("commands"), QJsonArray{commandObject({pythonExecutable(), QStringLiteral("-c"),
        QStringLiteral("import pathlib,sys,time; pathlib.Path(sys.argv[2]).write_text('started'); time.sleep(10); pathlib.Path(sys.argv[1]).write_text('wrong')"), QStringLiteral("${FILE}"), started})});
    object.insert(QStringLiteral("preUpload"), QJsonArray{rule});
    runner.start(object, original, false); QTRY_VERIFY(QFileInfo::exists(started)); QVERIFY(runner.busy());
    runner.cancel(); QVERIFY(!runner.busy()); QVERIFY(!QFileInfo::exists(prepared));
    QCOMPARE(previews.size(), 1); QVERIFY(server.requests().isEmpty()); QCOMPARE(readBytes(original), QByteArray("original"));
    rule.insert(QStringLiteral("commands"), QJsonArray{commandObject({QStringLiteral("imshare-nonexistent-command"), QStringLiteral("${FILE}")})});
    object.insert(QStringLiteral("preUpload"), QJsonArray{rule}); runner.start(object, original, true); QTRY_VERIFY(!runner.busy());
    QVERIFY(messages.last().first().toString().contains(QStringLiteral("imshare-nonexistent-command"))); QCOMPARE(previews.size(), 1);
}

void TargetManagerTest::oversizedResponseStopsTheTest()
{
    HttpCaptureServer server; QVERIFY(server.start());
    server.enqueueResponse({200, "OK", "text/plain", {}, QByteArray(2 * 1024 * 1024, 'x')});
    MemoryCredentials credentials; UploadTestRunner runner(nullptr, &credentials); QSignalSpy done(&runner, &UploadTestRunner::completed);
    runner.start(target(server.url()), {}, false); QTRY_COMPARE(done.size(), 1);
    const auto result = qvariant_cast<UploadResult>(done.first().first());
    QVERIFY(!result.ok); QVERIFY(result.errorMessage.contains(QStringLiteral("1 MiB")));
    QVERIFY(result.responseInfo.responseText.size() <= 1024 * 1024); QVERIFY(!runner.busy());
}

void TargetManagerTest::managerImportsExportsAndKeepsDraftsDisabled()
{
    QTemporaryDir active, presets, transfer; MemoryCredentials credentials;
    auto object = target(); object.insert(QStringLiteral("future"), QJsonArray{true, 12});
    const auto source = transfer.filePath(QStringLiteral("import.json")); writeBytes(source, QJsonDocument(object).toJson());
    TargetManagerWindow window(presets.path(), active.path(), nullptr, &credentials); window.show();
    auto *import = window.findChild<QAction *>(QStringLiteral("importTarget")); QVERIFY(import);
    QTimer::singleShot(0, &window, [&]() {
        auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget()); QVERIFY(dialog);
        dialog->selectFile(source); QMetaObject::invokeMethod(dialog, "accept");
    });
    import->trigger();
    QVERIFY(QDir(active.path()).entryList({QStringLiteral("*.json")}, QDir::Files).isEmpty());
    QCOMPARE(window.findChild<QListWidget *>(QStringLiteral("targetList"))->currentRow(), -1);
    auto *save = window.findChild<QPushButton *>(QStringLiteral("saveTarget")); QVERIFY(save->isEnabled()); save->click();
    const auto saved = active.filePath(QStringLiteral("disabled/raw.json")); QVERIFY(QFileInfo::exists(saved));
    QCOMPARE(QJsonDocument::fromJson(readBytes(saved)).object(), object);
    const auto exported = transfer.filePath(QStringLiteral("export.json"));
    QTimer::singleShot(0, &window, [&]() {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); QVERIFY(dialog);
        auto *buttons = dialog->findChild<QDialogButtonBox *>(); QVERIFY(buttons);
        QTimer::singleShot(0, &window, [&]() {
            auto *file = qobject_cast<QFileDialog *>(QApplication::activeModalWidget()); QVERIFY(file);
            file->selectFile(exported); QMetaObject::invokeMethod(file, "accept");
        });
        buttons->button(QDialogButtonBox::Save)->click();
    });
    window.findChild<QPushButton *>(QStringLiteral("exportTarget"))->click();
    QCOMPARE(QJsonDocument::fromJson(readBytes(exported)).object(), object);
    window.findChild<QPushButton *>(QStringLiteral("toggleTarget"))->click();
    QVERIFY(QFileInfo::exists(active.filePath(QStringLiteral("raw.json")))); QVERIFY(!QFileInfo::exists(saved));
    window.close();
}

void TargetManagerTest::managerDiscardsEditsBeforeDuplicating()
{
    QTemporaryDir active, presets; MemoryCredentials credentials;
    writeBytes(active.filePath(QStringLiteral("raw.json")), QJsonDocument(target()).toJson());
    TargetManagerWindow window(presets.path(), active.path(), nullptr, &credentials); window.show();
    auto *name = window.findChild<QLineEdit *>(QStringLiteral("displayName")); name->selectAll(); QTest::keyClicks(name, QStringLiteral("Discard me"));
    QTimer::singleShot(0, &window, []() {
        auto *dialog = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); QVERIFY(dialog);
        dialog->button(QMessageBox::Discard)->click();
    });
    window.findChild<QPushButton *>(QStringLiteral("duplicateTarget"))->click();
    QCOMPARE(name->text(), QStringLiteral("Test host (copy)"));
    QCOMPARE(window.findChild<QListWidget *>(QStringLiteral("targetList"))->currentRow(), -1);
    window.findChild<QPushButton *>(QStringLiteral("saveTarget"))->click();
    QVERIFY(QFileInfo::exists(active.filePath(QStringLiteral("disabled/raw_2.json")))); window.close();
}

void TargetManagerTest::credentialHelpersStoreReferencesAndHandleDraftChanges()
{
    MemoryCredentials credentials; TargetEditor editor; editor.setBytes(QJsonDocument(target()).toJson());
    CredentialPanel panel(&editor, &credentials); panel.reset();
    auto field = [&panel](const QString &name) -> QLineEdit * {
        for (auto *label : panel.findChildren<QLabel *>()) if (label->text() == name) return qobject_cast<QLineEdit *>(label->buddy());
        return nullptr;
    };
    auto click = [&panel](const QString &name) {
        for (auto *button : panel.findChildren<QPushButton *>()) if (button->text() == name) { button->click(); return; }
        QFAIL("Credential button not found");
    };
    auto *secret = panel.findChild<QLineEdit *>(QStringLiteral("credentialSecret")); QVERIFY(secret);
    QCOMPARE(secret->echoMode(), QLineEdit::Password);
    auto *key = field(QStringLiteral("Wallet credential name")); QVERIFY(key); key->setText(QStringLiteral("testing.api"));
    secret->setText(QStringLiteral("first-secret"));
    auto *mode = panel.findChildren<QComboBox *>().first(); mode->setCurrentIndex(1);
    click(QStringLiteral("Store secret and use"));
    QTRY_VERIFY(editor.bytes().contains("Bearer ${WALLET:testing.api}"));
    QCOMPARE(credentials.values.value(QStringLiteral("testing.api")), QStringLiteral("first-secret"));
    QVERIFY(secret->text().isEmpty()); QVERIFY(!editor.bytes().contains("first-secret"));
    auto *username = field(QStringLiteral("Username")); QVERIFY(username); mode->setCurrentIndex(2);
    username->setText(QStringLiteral("user")); secret->setText(QStringLiteral("pass")); key->setText(QStringLiteral("testing.basic"));
    click(QStringLiteral("Store secret and use")); QTRY_VERIFY(editor.bytes().contains("Basic ${WALLET:testing.basic}"));
    QCOMPARE(credentials.values.value(QStringLiteral("testing.basic")), QString::fromLatin1(QByteArray("user:pass").toBase64()));
    click(QStringLiteral("Remove this request field")); QVERIFY(!editor.bytes().contains("testing.basic")); QVERIFY(credentials.values.contains(QStringLiteral("testing.basic")));
    click(QStringLiteral("Use existing credential"));
    editor.setField({QStringLiteral("displayName")}, QStringLiteral("Changed while opening wallet"));
    QCoreApplication::processEvents(); QVERIFY(!editor.bytes().contains("testing.basic"));
    click(QStringLiteral("Use existing credential")); QTRY_VERIFY(editor.bytes().contains("Basic ${WALLET:testing.basic}"));
}

void TargetManagerTest::invalidDefinitionsDoNotBlockValidTargets()
{
    QTemporaryDir active, presets; TargetFileStore store(presets.path(), active.path());
    const auto goodPath = active.filePath(QStringLiteral("a-valid.json"));
    const auto otherPath = active.filePath(QStringLiteral("b-invalid.json"));
    const auto bytes = QJsonDocument(target()).toJson(); writeBytes(goodPath, bytes);
    auto invalid = target(); auto request = invalid.value(QStringLiteral("request")).toObject();
    request.insert(QStringLiteral("method"), QStringLiteral("GET")); invalid.insert(QStringLiteral("request"), request);
    writeBytes(otherPath, QJsonDocument(invalid).toJson());
    auto entries = store.entries(); QCOMPARE(entries.size(), 2);
    QVERIFY(entries.first().problems.isEmpty()); QVERIFY(!entries.last().problems.isEmpty());
    QCOMPARE(store.save(goodPath, bytes, entries.first().revision), QString{});
    writeBytes(otherPath, bytes); // A valid duplicate is ignored by the runtime.
    entries = store.entries(); QVERIFY(entries.first().problems.isEmpty());
    QVERIFY(entries.last().problems.join(QLatin1Char('\n')).contains(QStringLiteral("already defined")));
    QVERIFY(!store.save(otherPath, bytes, entries.last().revision).isEmpty());
    const auto runtime = TargetRegistry(presets.path(), active.path()).loadTargets();
    QCOMPARE(runtime.targets.size(), 1); QCOMPARE(runtime.diagnostics.size(), 1);
    QCOMPARE(runtime.diagnostics.first().filePath, otherPath);
}

void TargetManagerTest::selectedErrorIconRetainsItsColors()
{
    const auto previousTheme = QIcon::themeName();
    const auto restore = qScopeGuard([previousTheme]() { QIcon::setThemeName(previousTheme); });
    QIcon::setThemeName(QStringLiteral("breeze"));
    if (!QIcon::hasThemeIcon(QStringLiteral("dialog-error"))) QSKIP("Breeze icons are required for the selection rendering regression");
    QTemporaryDir active, presets; MemoryCredentials credentials;
    writeBytes(active.filePath(QStringLiteral("a-invalid.json")), QByteArray("{invalid"));
    writeBytes(active.filePath(QStringLiteral("b-valid.json")), QJsonDocument(target()).toJson());
    TargetManagerWindow window(presets.path(), active.path(), nullptr, &credentials);
    window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto *list = window.findChild<QListWidget *>(QStringLiteral("targetList"));
    auto capture = [&](int selected, const QString &suffix) {
        list->setCurrentRow(selected); QCoreApplication::processEvents();
        if (qEnvironmentVariableIsSet("IMSHARE_SCREENSHOTS")) {
            const QDir directory(QString::fromLocal8Bit(qgetenv("IMSHARE_SCREENSHOTS")));
            QDir().mkpath(directory.path()); window.grab().save(directory.filePath(QStringLiteral("error-%1.png").arg(suffix)));
        }
        auto rect = list->visualItemRect(list->item(0)); rect.setWidth(28);
        return list->viewport()->grab(rect).toImage();
    };
    const auto normal = capture(1, QStringLiteral("normal"));
    const auto selected = capture(0, QStringLiteral("selected"));
    auto redPixels = [](const QImage &image) {
        int count = 0;
        for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x) {
            const auto color = image.pixelColor(x, y);
            if (color.red() > 150 && color.green() < 130 && color.blue() < 160) ++count;
        }
        return count;
    };
    QVERIFY(redPixels(normal) > 10); QCOMPARE(redPixels(selected), redPixels(normal));
    window.close();
}

void TargetManagerTest::diagnosticsDoNotMoveEditorControls_data()
{
    QTest::addColumn<QSize>("windowSize");
    QTest::newRow("normal") << QSize(1180, 860);
    QTest::newRow("compact") << QSize(960, 720);
}

void TargetManagerTest::diagnosticsDoNotMoveEditorControls()
{
    QFETCH(QSize, windowSize);
    QTemporaryDir active, presets; MemoryCredentials credentials;
    auto good = target(); good.insert(QStringLiteral("displayName"), QStringLiteral("Catbox"));
    const auto goodPath = active.filePath(QStringLiteral("a-catbox.json")); writeBytes(goodPath, QJsonDocument(good).toJson());
    auto bad = target(); bad.insert(QStringLiteral("displayName"), QStringLiteral("Broken Fields"));
    bad.insert(QStringLiteral("id"), QStringLiteral("broken_fields"));
    bad.insert(QStringLiteral("extensions"), QJsonArray{QStringLiteral("invalid/extension")});
    auto request = bad.value(QStringLiteral("request")).toObject();
    request.insert(QStringLiteral("method"), QStringLiteral("GET")); request.insert(QStringLiteral("headers"), false);
    bad.insert(QStringLiteral("request"), request);
    writeBytes(active.filePath(QStringLiteral("b-broken-fields.json")), QJsonDocument(bad).toJson());
    auto longName = bad; longName.insert(QStringLiteral("displayName"), QStringLiteral("A very long target name ").repeated(15));
    writeBytes(active.filePath(QStringLiteral("c-") + QString(180, QLatin1Char('x')) + QStringLiteral(".json")), QJsonDocument(longName).toJson());
    writeBytes(active.filePath(QStringLiteral("d-broken-json.json")), QByteArray("{invalid"));
    TargetManagerWindow window(presets.path(), active.path(), nullptr, &credentials);
    window.resize(windowSize); window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto *list = window.findChild<QListWidget *>(QStringLiteral("targetList"));
    auto *editor = window.findChild<TargetEditor *>();
    auto *status = editor->findChild<QLabel *>(QStringLiteral("targetStatus"));
    auto *details = editor->findChild<QPushButton *>(QStringLiteral("targetDiagnostics"));
    QVERIFY(status); QVERIFY(details);
    auto settle = [&]() { QCoreApplication::processEvents(); QCoreApplication::processEvents(); };
    auto geometry = [&]() {
        QList<QRect> bounds;
        const QList<QWidget *> widgets{editor->tabs(), window.findChild<QPushButton *>(QStringLiteral("toggleTarget")),
            window.findChild<QPushButton *>(QStringLiteral("saveTarget")), window.findChild<QPushButton *>(QStringLiteral("exportTarget")),
            window.findChild<QLabel *>(QStringLiteral("targetPath")), status, details};
        for (auto *widget : widgets) bounds.append(QRect(widget->mapTo(&window, QPoint{}), widget->size()));
        return bounds;
    };
    auto screenshot = [&](const QString &name) {
        if (!qEnvironmentVariableIsSet("IMSHARE_SCREENSHOTS")) return;
        QDir directory(QString::fromLocal8Bit(qgetenv("IMSHARE_SCREENSHOTS"))); QDir().mkpath(directory.path());
        window.grab().save(directory.filePath(QStringLiteral("layout-%1-%2.png").arg(QString::fromLatin1(QTest::currentDataTag()), name)));
    };
    settle(); const auto initial = geometry(); const auto initialWindowSize = window.size();
    QVERIFY(!details->isEnabled()); screenshot(QStringLiteral("valid"));
    for (int row : {1, 2, 3, 0}) {
        list->setCurrentRow(row); settle();
        QCOMPARE(window.size(), initialWindowSize); QCOMPARE(geometry(), initial);
        QCOMPARE(details->isEnabled(), row != 0);
        if (row != 0) QVERIFY(status->text().startsWith(QStringLiteral("Needs attention:")));
        screenshot(QString::number(row));
    }
    list->setCurrentRow(1); settle();
    QTimer::singleShot(0, &window, [&]() {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); QVERIFY(dialog);
        const auto closeDialog = qScopeGuard([dialog]() { dialog->reject(); });
        auto *text = dialog->findChild<QPlainTextEdit *>(QStringLiteral("targetDiagnosticText")); QVERIFY(text);
        const auto diagnostics = text->toPlainText();
        for (const auto &problem : TargetFileStore::validate(QJsonDocument(bad).toJson()))
            QVERIFY2(diagnostics.contains(problem), qPrintable(diagnostics));
        QVERIFY(diagnostics.contains(QStringLiteral("/extensions")));
        QVERIFY(diagnostics.contains(QStringLiteral("/request/headers")));
    });
    details->click(); settle(); QCOMPARE(geometry(), initial);
    list->setCurrentRow(0); settle();
    writeBytes(goodPath, QJsonDocument(good).toJson() + QByteArray("\n"));
    QTRY_VERIFY(details->isEnabled()); settle();
    QVERIFY(status->toolTip().contains(QStringLiteral("changed on disk"))); QCOMPARE(geometry(), initial);
    window.close();
}

QTEST_MAIN(TargetManagerTest)
#include "test_targetmanager.moc"
