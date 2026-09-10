#include "constraintmatcher.h"
#include "targetconfigvalidator.h"
#include "targetdiagnostic.h"
#include "targetregistry.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QImage>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>
#include <barrier>
#include <future>

#include "testutils.h"

class TargetRegistryTest final : public QObject {
    Q_OBJECT

private slots:
    void initializesBundledLinksOnce();
    void concurrentInitializationPublishesCompleteDirectories();
    void exampleTargetFilesValidate();
    void defaultActiveTargetsPathIsStable();
    void existingEmptyDirectoryStaysEmpty();
    void loadsOnlyActiveTargets();
    void invalidActiveTargetDoesNotFallBack();
    void linkedPresetTracksUpdatesAndCanBeDisabled();
    void customizedCopyIsIndependent();
    void disabledSubdirectoryIsIgnored();
    void missingPresetsLeaveInitializationRetryable();
    void invalidActiveDirectoryIsPreserved();
    void brokenLinkProducesDiagnostic();
    void duplicateIdsProduceDiagnostic();
    void invalidTargetsProduceErrorsButDoNotBlockValidTargets();
    void malformedJsonProducesFileSpecificError();
    void validatorAccumulatesMultipleDiagnosticsForOneTarget();
    void constraintMatcherFiltersByMimeType();
    void constraintMatcherFiltersByExtension();
    void acceptanceCombinesAlternativesCategoriesAndFiles();
    void validatesRegexExtractors_data();
    void validatesRegexExtractors();
};

static QString bundledTargetsPath()
{
    return QDir(QStringLiteral(IMSHARE_TEST_SOURCE_DIR) + QStringLiteral("/../targets")).absolutePath();
}

static QJsonObject presetConfig(const QString& name = QStringLiteral("Raw Target"))
{
    auto config = rawTarget(QUrl(QStringLiteral("https://example.test/upload")));
    config.insert(QStringLiteral("displayName"), name);
    return config;
}

static bool writeConfig(const QString& path, const QJsonObject& config)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    const auto bytes = QJsonDocument(config).toJson();
    return file.write(bytes) == bytes.size() && file.commit();
}

void TargetRegistryTest::initializesBundledLinksOnce()
{
    QTemporaryDir dir;
    const QString activePath = dir.filePath(QStringLiteral("config/targets"));
    TargetRegistry registry(bundledTargetsPath(), activePath);
    const auto result = registry.loadTargets();
    QVERIFY(result.diagnostics.isEmpty());
    QCOMPARE(result.targets.size(), 2);
    QCOMPARE(result.targets.at(0).id(), QStringLiteral("catbox"));
    QCOMPARE(result.targets.at(1).id(), QStringLiteral("uguu"));
    for (const auto& name : { QStringLiteral("catbox.json"), QStringLiteral("uguu.json") }) {
        const QFileInfo link(QDir(activePath).filePath(name));
        QVERIFY(link.isSymLink());
        QCOMPARE(link.symLinkTarget(), QDir(bundledTargetsPath()).filePath(name));
    }
    QCOMPARE(QDir(dir.filePath(QStringLiteral("config"))).entryList(QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot),
        QStringList { QStringLiteral("targets") });
    QVERIFY(!QDir(QDir(activePath).filePath(QStringLiteral("examples"))).exists());
    QCOMPARE(registry.loadTargets().targets.size(), 2);
}

void TargetRegistryTest::defaultActiveTargetsPathIsStable()
{
    TargetRegistry registry;
    QCOMPARE(registry.activeTargetsPath(),
        QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
            + QStringLiteral("/plasma-share-uploader/targets"));
}

void TargetRegistryTest::concurrentInitializationPublishesCompleteDirectories()
{
    QTemporaryDir bundled;
    QTemporaryDir dir;
    for (int i = 0; i < 20; ++i) {
        auto config = presetConfig();
        config.insert(QStringLiteral("id"), QStringLiteral("target-%1").arg(i));
        QVERIFY(writeConfig(bundled.filePath(QStringLiteral("target-%1.json").arg(i)), config));
    }
    const QString active = dir.filePath(QStringLiteral("targets"));
    std::barrier ready(2);
    const auto load = [&]() {
        ready.arrive_and_wait();
        return TargetRegistry(bundled.path(), active).loadTargets();
    };
    auto first = std::async(std::launch::async, load);
    auto second = std::async(std::launch::async, load);
    for (const auto& result : { first.get(), second.get() }) {
        QVERIFY(result.diagnostics.isEmpty());
        QCOMPARE(result.targets.size(), 20);
    }
    QCOMPARE(QDir(dir.path()).entryList(QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot),
        QStringList { QStringLiteral("targets") });
}

void TargetRegistryTest::existingEmptyDirectoryStaysEmpty()
{
    QTemporaryDir active;
    TargetRegistry registry(QStringLiteral("/nonexistent/presets"), active.path());
    const auto result = registry.loadTargets();
    QVERIFY(result.targets.isEmpty());
    QVERIFY(result.diagnostics.isEmpty());
    QVERIFY(QDir(active.path()).isEmpty());
}

void TargetRegistryTest::loadsOnlyActiveTargets()
{
    QTemporaryDir active;
    auto config = presetConfig(QStringLiteral("My Catbox"));
    config.insert(QStringLiteral("id"), QStringLiteral("catbox"));
    QVERIFY(writeConfig(active.filePath(QStringLiteral("catbox.json")), config));
    TargetRegistry registry(bundledTargetsPath(), active.path());
    const auto result = registry.loadTargets();
    QVERIFY(result.diagnostics.isEmpty());
    QCOMPARE(result.targets.size(), 1);
    QCOMPARE(result.targets.first().displayName(), QStringLiteral("My Catbox"));
    QVERIFY(!QFileInfo(active.filePath(QStringLiteral("catbox.json"))).isSymLink());
}

void TargetRegistryTest::invalidActiveTargetDoesNotFallBack()
{
    QTemporaryDir active;
    auto config = presetConfig();
    config.insert(QStringLiteral("id"), QStringLiteral("catbox"));
    auto request = config.value(QStringLiteral("request")).toObject();
    request.insert(QStringLiteral("url"), QString());
    config.insert(QStringLiteral("request"), request);
    QVERIFY(writeConfig(active.filePath(QStringLiteral("catbox.json")), config));
    TargetRegistry registry(bundledTargetsPath(), active.path());
    const auto result = registry.loadTargets();
    QVERIFY(result.targets.isEmpty());
    QVERIFY(std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
        [](const auto& diagnostic) { return diagnostic.jsonPath == QLatin1StringView("/request/url"); }));
}

void TargetRegistryTest::linkedPresetTracksUpdatesAndCanBeDisabled()
{
    QTemporaryDir bundled;
    QTemporaryDir dir;
    const QString preset = bundled.filePath(QStringLiteral("raw.json"));
    QVERIFY(writeConfig(preset, presetConfig()));
    const QString active = dir.filePath(QStringLiteral("targets"));
    const QString link = QDir(active).filePath(QStringLiteral("raw.json"));
    TargetRegistry registry(bundled.path(), active);
    QCOMPARE(registry.loadTargets().targets.first().displayName(), QStringLiteral("Raw Target"));
    // Atomic replacement models a package upgrade without changing the enabled link.
    QVERIFY(writeConfig(preset, presetConfig(QStringLiteral("Updated Target"))));
    auto added = presetConfig();
    added.insert(QStringLiteral("id"), QStringLiteral("new-preset"));
    QVERIFY(writeConfig(bundled.filePath(QStringLiteral("new.json")), added));
    auto result = registry.loadTargets();
    QVERIFY(result.diagnostics.isEmpty());
    QCOMPARE(result.targets.size(), 1);
    QCOMPARE(result.targets.first().displayName(), QStringLiteral("Updated Target"));
    QVERIFY(QFileInfo(link).isSymLink());
    QVERIFY(QFile::remove(link));
    QVERIFY(registry.loadTargets().targets.isEmpty());
    QVERIFY(QFileInfo::exists(preset));
    QVERIFY(QFile::link(preset, link));
    result = registry.loadTargets();
    QVERIFY(result.diagnostics.isEmpty());
    QCOMPARE(result.targets.size(), 1);
}

void TargetRegistryTest::customizedCopyIsIndependent()
{
    QTemporaryDir bundled;
    QTemporaryDir active;
    const QString preset = bundled.filePath(QStringLiteral("raw.json"));
    const QString custom = active.filePath(QStringLiteral("raw.json"));
    QVERIFY(writeConfig(preset, presetConfig()));
    QVERIFY(QFile::link(preset, custom));
    QVERIFY(QFile::remove(custom));
    QVERIFY(QFile::copy(preset, custom));
    QVERIFY(writeConfig(custom, presetConfig(QStringLiteral("Custom"))));
    QVERIFY(writeConfig(preset, presetConfig(QStringLiteral("Updated Preset"))));
    const auto result = TargetRegistry(bundled.path(), active.path()).loadTargets();
    QVERIFY(result.diagnostics.isEmpty());
    QCOMPARE(result.targets.size(), 1);
    QCOMPARE(result.targets.first().displayName(), QStringLiteral("Custom"));
    QVERIFY(!QFileInfo(custom).isSymLink());
}

void TargetRegistryTest::disabledSubdirectoryIsIgnored()
{
    QTemporaryDir active;
    const QString disabled = active.filePath(QStringLiteral("disabled"));
    QVERIFY(QDir().mkpath(disabled));
    const QString custom = active.filePath(QStringLiteral("raw.json"));
    QVERIFY(writeConfig(custom, presetConfig()));
    const QString link = active.filePath(QStringLiteral("catbox.json"));
    QVERIFY(QFile::link(QDir(bundledTargetsPath()).filePath(QStringLiteral("catbox.json")), link));
    QVERIFY(QFile::rename(custom, QDir(disabled).filePath(QStringLiteral("raw.json"))));
    QVERIFY(QFile::rename(link, QDir(disabled).filePath(QStringLiteral("catbox.json"))));
    const auto result = TargetRegistry(bundledTargetsPath(), active.path()).loadTargets();
    QVERIFY(result.targets.isEmpty());
    QVERIFY(result.diagnostics.isEmpty());
    QVERIFY(QFileInfo::exists(QDir(disabled).filePath(QStringLiteral("raw.json"))));
}

void TargetRegistryTest::missingPresetsLeaveInitializationRetryable()
{
    QTemporaryDir dir;
    const QString bundled = dir.filePath(QStringLiteral("presets"));
    const QString active = dir.filePath(QStringLiteral("targets"));
    TargetRegistry registry(bundled, active);
    const auto failed = registry.loadTargets();
    QVERIFY(failed.targets.isEmpty());
    QCOMPARE(failed.diagnostics.size(), 1);
    QVERIFY(!QFileInfo::exists(active));
    QVERIFY(QDir().mkpath(bundled));
    QVERIFY(writeConfig(QDir(bundled).filePath(QStringLiteral("raw.json")), presetConfig()));
    const auto retried = registry.loadTargets();
    QVERIFY(retried.diagnostics.isEmpty());
    QCOMPARE(retried.targets.size(), 1);
}

void TargetRegistryTest::invalidActiveDirectoryIsPreserved()
{
    QTemporaryDir dir;
    const QString active = writeTempFile(dir, QStringLiteral("targets"), "keep this file");
    const auto result = TargetRegistry(bundledTargetsPath(), active).loadTargets();
    QVERIFY(result.targets.isEmpty());
    QCOMPARE(result.diagnostics.size(), 1);
    QFile file(active);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArray("keep this file"));
}

void TargetRegistryTest::brokenLinkProducesDiagnostic()
{
    QTemporaryDir active;
    const QString link = active.filePath(QStringLiteral("missing.json"));
    QVERIFY(QFile::link(active.filePath(QStringLiteral("no-such-preset")), link));
    const auto result = TargetRegistry(bundledTargetsPath(), active.path()).loadTargets();
    QVERIFY(result.targets.isEmpty());
    QCOMPARE(result.diagnostics.size(), 1);
    QCOMPARE(result.diagnostics.first().filePath, link);
    QCOMPARE(result.diagnostics.first().code, QStringLiteral("file.unavailable"));
    QVERIFY(QFileInfo(link).isSymLink());
}

void TargetRegistryTest::duplicateIdsProduceDiagnostic()
{
    QTemporaryDir active;
    QVERIFY(writeConfig(active.filePath(QStringLiteral("first.json")), presetConfig(QStringLiteral("First"))));
    QVERIFY(writeConfig(active.filePath(QStringLiteral("second.json")), presetConfig(QStringLiteral("Second"))));
    const auto result = TargetRegistry(bundledTargetsPath(), active.path()).loadTargets();
    QCOMPARE(result.targets.size(), 1);
    QCOMPARE(result.targets.first().displayName(), QStringLiteral("First"));
    QCOMPARE(result.diagnostics.size(), 1);
    QCOMPARE(result.diagnostics.first().code, QStringLiteral("target.duplicate_id"));
}

void TargetRegistryTest::exampleTargetFilesValidate()
{
    const QDir dir(QStringLiteral(IMSHARE_TEST_SOURCE_DIR) + QStringLiteral("/../targets/examples"));
    QVERIFY(dir.exists());

    const QStringList fileNames = dir.entryList(QStringList { QStringLiteral("*.json") }, QDir::Files, QDir::Name);
    QCOMPARE(fileNames.size(), 4);

    for (const QString& fileName : fileNames) {
        QFile file(dir.filePath(fileName));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        QVERIFY2(doc.isObject(), qPrintable(fileName));

        QList<TargetDiagnostic> diagnostics;
        QVERIFY2(TargetConfigValidator::validateTarget(doc.object(), &diagnostics),
            qPrintable(fileName + QStringLiteral(": ") + [&diagnostics]() {
                QStringList lines;
                for (const TargetDiagnostic& diagnostic : diagnostics) {
                    lines.append(diagnostic.displayText());
                }
                return lines.join(QStringLiteral("; "));
            }()));
    }
}

void TargetRegistryTest::invalidTargetsProduceErrorsButDoNotBlockValidTargets()
{
    QTemporaryDir dir;
    const QString userDir = dir.filePath(QStringLiteral("targets"));
    QVERIFY(QDir().mkpath(userDir));

    QFile badFile(userDir + QStringLiteral("/bad.json"));
    QVERIFY(badFile.open(QIODevice::WriteOnly));
    badFile.write(R"({
  "schemaVersion": 1,
  "id": "bad target",
  "request": {
    "url": "https://bad.test",
    "method": "POST",
    "body": {
      "type": "multipart",
      "fields": {},
      "fileField": "file"
    }
  },
  "response": {
    "url": {
      "type": "text_url"
    }
  }
})");
    badFile.close();

    QFile goodFile(userDir + QStringLiteral("/good.json"));
    QVERIFY(goodFile.open(QIODevice::WriteOnly));
    goodFile.write(R"({
  "schemaVersion": 1,
  "id": "good",
  "displayName": "Good",
  "request": {
    "url": "https://good.test",
    "method": "POST",
    "body": {
      "type": "multipart",
      "fields": {},
      "fileField": "file"
    }
  },
  "response": {
    "url": {
      "type": "text_url"
    }
  }
})");
    goodFile.close();

    TargetRegistry registry(QString(), userDir);
    const TargetRegistry::LoadResult result = registry.loadTargets();

    QVERIFY(!result.diagnostics.isEmpty());
    QCOMPARE(result.targets.size(), 1);
    QVERIFY(std::any_of(result.targets.begin(), result.targets.end(),
        [](const TargetDefinition& target) { return target.id() == QLatin1StringView("good"); }));
    QVERIFY(std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
        [](const TargetDiagnostic& diagnostic) { return diagnostic.filePath.endsWith(QStringLiteral("/bad.json")); }));
}

void TargetRegistryTest::malformedJsonProducesFileSpecificError()
{
    QTemporaryDir dir;
    const QString userDir = dir.filePath(QStringLiteral("targets"));
    QVERIFY(QDir().mkpath(userDir));

    QFile badFile(userDir + QStringLiteral("/broken.json"));
    QVERIFY(badFile.open(QIODevice::WriteOnly));
    badFile.write("{ not valid json");
    badFile.close();

    TargetRegistry registry(QString(), userDir);
    const TargetRegistry::LoadResult result = registry.loadTargets();

    QVERIFY(std::any_of(result.diagnostics.begin(), result.diagnostics.end(), [](const TargetDiagnostic& diagnostic) {
        return diagnostic.filePath.endsWith(QStringLiteral("/broken.json"))
            && diagnostic.code == QLatin1StringView("file.invalid_json_object");
    }));
}

void TargetRegistryTest::validatorAccumulatesMultipleDiagnosticsForOneTarget()
{
    const QJsonObject target { { QStringLiteral("schemaVersion"), 1 },
        { QStringLiteral("id"), QStringLiteral("broken") },
        { QStringLiteral("request"),
            QJsonObject { { QStringLiteral("url"), QStringLiteral("") },
                { QStringLiteral("method"), QStringLiteral("GET") },
                { QStringLiteral("body"), QJsonObject { { QStringLiteral("type"), QStringLiteral("json") } } } } },
        { QStringLiteral("response"),
            QJsonObject { { QStringLiteral("url"),
                              QJsonObject { { QStringLiteral("type"), QStringLiteral("json_pointer") },
                                  { QStringLiteral("pointer"), QStringLiteral("bad") } } },
                { QStringLiteral("thumbnail"),
                    QJsonObject { { QStringLiteral("type"), QStringLiteral("header") } } } } },
        { QStringLiteral("accept"),
            QJsonObject {
                { QStringLiteral("extensions"), QJsonArray { QStringLiteral(""), QStringLiteral("bad/ext") } } } } };

    QList<TargetDiagnostic> diagnostics;
    QVERIFY(!TargetConfigValidator::validateTarget(target, &diagnostics));
    QVERIFY(std::any_of(diagnostics.begin(), diagnostics.end(),
        [](const TargetDiagnostic& diagnostic) { return diagnostic.jsonPath == QLatin1StringView("/request/url"); }));
    QVERIFY(std::any_of(diagnostics.begin(), diagnostics.end(), [](const TargetDiagnostic& diagnostic) {
        return diagnostic.jsonPath == QLatin1StringView("/request/method");
    }));
    QVERIFY(std::any_of(diagnostics.begin(), diagnostics.end(), [](const TargetDiagnostic& diagnostic) {
        return diagnostic.jsonPath == QLatin1StringView("/request/body/value");
    }));
    QVERIFY(std::any_of(diagnostics.begin(), diagnostics.end(), [](const TargetDiagnostic& diagnostic) {
        return diagnostic.jsonPath == QLatin1StringView("/response/url/pointer");
    }));
    QVERIFY(std::any_of(diagnostics.begin(), diagnostics.end(), [](const TargetDiagnostic& diagnostic) {
        return diagnostic.jsonPath == QLatin1StringView("/response/thumbnail/name");
    }));
    QVERIFY(std::any_of(diagnostics.begin(), diagnostics.end(), [](const TargetDiagnostic& diagnostic) {
        return diagnostic.jsonPath.startsWith(QLatin1StringView("/accept/extensions/"));
    }));
}

void TargetRegistryTest::constraintMatcherFiltersByMimeType()
{
    QTemporaryDir dir;
    const QString imagePath = writeTempFile(dir, QStringLiteral("image.png"), tinyPng());
    const QString textPath = writeTempFile(dir, QStringLiteral("file.txt"), "hello");

    TargetDefinition imageTarget;
    imageTarget.target.core.id = QStringLiteral("images");
    imageTarget.target.core.mimeTypes = QStringList { QStringLiteral("image/*") };
    imageTarget.target.valid = true;

    TargetDefinition anyTarget;
    anyTarget.target.core.id = QStringLiteral("any");
    anyTarget.target.valid = true;

    QVERIFY(ConstraintMatcher::targetMatchesFiles(imageTarget, QStringList { imagePath }));
    QVERIFY(!ConstraintMatcher::targetMatchesFiles(imageTarget, QStringList { textPath }));

    const QList<TargetDefinition> filtered = ConstraintMatcher::filterTargets(
        QList<TargetDefinition> { imageTarget, anyTarget }, QStringList { textPath });
    QCOMPARE(filtered.size(), 1);
    QCOMPARE(filtered.first().id(), QStringLiteral("any"));
}

void TargetRegistryTest::constraintMatcherFiltersByExtension()
{
    QTemporaryDir dir;
    const QString imagePath = writeTempFile(dir, QStringLiteral("photo.jpeg"), "jpeg");
    const QString textPath = writeTempFile(dir, QStringLiteral("notes.txt"), "text");

    TargetDefinition imageTarget;
    imageTarget.target.core.id = QStringLiteral("images");
    imageTarget.target.core.extensions = QStringList { QStringLiteral("png"), QStringLiteral(".jpeg") };
    imageTarget.target.valid = true;

    QVERIFY(ConstraintMatcher::targetMatchesFiles(imageTarget, QStringList { imagePath }));
    QVERIFY(!ConstraintMatcher::targetMatchesFiles(imageTarget, QStringList { textPath }));
}

void TargetRegistryTest::acceptanceCombinesAlternativesCategoriesAndFiles()
{
    QTemporaryDir dir;
    const auto png = writeTempFile(dir, QStringLiteral("image.PNG"), tinyPng());
    const auto jpeg = dir.filePath(QStringLiteral("photo.jpg"));
    QImage image(1, 1, QImage::Format_RGB32);
    image.fill(Qt::red);
    QVERIFY(image.save(jpeg, "JPEG"));
    const auto renamedPng = writeTempFile(dir, QStringLiteral("image.txt"), tinyPng());
    const auto fakePng = writeTempFile(dir, QStringLiteral("not-an-image.png"), "plain text");
    auto config = rawTarget(QUrl(QStringLiteral("https://example.test/upload")));
    config.insert(QStringLiteral("accept"),
        QJsonObject {
            { QStringLiteral("mimeTypes"), QJsonArray { QStringLiteral("IMAGE/PNG"), QStringLiteral("image/jpeg") } },
            { QStringLiteral("extensions"), QJsonArray { QStringLiteral(".PNG"), QStringLiteral("jpg") } } });
    ParsedTargetConfig parsed;
    QVERIFY(TargetConfigParser::parse(config, &parsed));
    TargetDefinition target;
    target.target = parsed;
    QVERIFY(ConstraintMatcher::targetMatchesFiles(target, { png, jpeg }));
    QVERIFY(!ConstraintMatcher::targetMatchesFiles(target, { png, renamedPng }));
    QVERIFY(!ConstraintMatcher::targetMatchesFiles(target, { jpeg, fakePng }));
    target.target.core.extensions.clear();
    QVERIFY(ConstraintMatcher::targetMatchesFiles(target, { png, jpeg, renamedPng }));
    QVERIFY(!ConstraintMatcher::targetMatchesFiles(target, { fakePng }));
    target.target.core.mimeTypes.clear();
    QVERIFY(ConstraintMatcher::targetMatchesFiles(target, { fakePng, renamedPng }));
}

void TargetRegistryTest::validatesRegexExtractors_data()
{
    QTest::addColumn<QString>("pattern");
    QTest::addColumn<QJsonValue>("group");
    QTest::addColumn<bool>("valid");
    QTest::newRow("invalid-pattern") << QStringLiteral("(") << QJsonValue(1) << false;
    QTest::newRow("missing-capture") << QStringLiteral("https://.*") << QJsonValue(1) << false;
    QTest::newRow("out-of-range") << QStringLiteral("(https://.*)") << QJsonValue(2) << false;
    QTest::newRow("fractional") << QStringLiteral("(https://.*)") << QJsonValue(0.5) << false;
    QTest::newRow("whole-match") << QStringLiteral("https://.*") << QJsonValue(0) << true;
    QTest::newRow("capture") << QStringLiteral("(https://.*)") << QJsonValue(1) << true;
}

void TargetRegistryTest::validatesRegexExtractors()
{
    QFETCH(QString, pattern);
    QFETCH(QJsonValue, group);
    QFETCH(bool, valid);
    const QJsonObject extractor { { QStringLiteral("type"), QStringLiteral("regex") },
        { QStringLiteral("pattern"), pattern }, { QStringLiteral("group"), group } };
    for (const auto& field :
        { QString(), QStringLiteral("error"), QStringLiteral("thumbnail"), QStringLiteral("deletion") }) {
        auto config = rawTarget(QUrl(QStringLiteral("https://example.test/upload")));
        auto response = config.value(QStringLiteral("response")).toObject();
        if (field.isEmpty()) {
            response.insert(QStringLiteral("url"), extractor);
        } else {
            response.insert(field, extractor);
        }
        config.insert(QStringLiteral("response"), response);
        QList<TargetDiagnostic> diagnostics;
        QCOMPARE(TargetConfigValidator::validateTarget(config, &diagnostics), valid);
        QCOMPARE(diagnostics.isEmpty(), valid);
        if (!valid) {
            QVERIFY(diagnostics.first().jsonPath.endsWith(QStringLiteral("/pattern"))
                || diagnostics.first().jsonPath.endsWith(QStringLiteral("/group")));
        }
    }
}

QTEST_APPLESS_MAIN(TargetRegistryTest)

#include "test_targetregistry.moc"
