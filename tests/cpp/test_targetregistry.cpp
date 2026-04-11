#include "constraintmatcher.h"
#include "targetcoreconfigparser.h"
#include "targetconfigvalidator.h"
#include "targetdiagnostic.h"
#include "targetregistry.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

#include "testutils.h"

class TargetRegistryTest final : public QObject
{
    Q_OBJECT

private slots:
    void loadsBundledSystemTargets();
    void exampleTargetFilesValidate();
    void defaultUserTargetsPathIsStable();
    void userTargetsOverrideSystemTargetsById();
    void invalidUserOverrideFallsBackToSystemTarget();
    void invalidTargetsProduceErrorsButDoNotBlockValidTargets();
    void malformedJsonProducesFileSpecificError();
    void validatorAccumulatesMultipleDiagnosticsForOneTarget();
    void constraintMatcherFiltersByMimeType();
    void constraintMatcherFiltersByExtension();
};

void TargetRegistryTest::loadsBundledSystemTargets()
{
    TargetRegistry registry(QStringLiteral(IMSHARE_TEST_SOURCE_DIR) + QStringLiteral("/../data/targets.d"), QStringLiteral("/nonexistent"));
    const TargetRegistry::LoadResult result = registry.loadTargets();

    QVERIFY(result.diagnostics.isEmpty());
    QCOMPARE(result.targets.size(), 2);
    QCOMPARE(result.targets.at(0).id(), QStringLiteral("catbox"));
    QCOMPARE(result.targets.at(1).id(), QStringLiteral("uguu"));
}

void TargetRegistryTest::exampleTargetFilesValidate()
{
    const QDir dir(QStringLiteral(IMSHARE_TEST_SOURCE_DIR) + QStringLiteral("/../data/targets.d/examples"));
    QVERIFY(dir.exists());

    const QStringList fileNames = dir.entryList(QStringList{QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    QCOMPARE(fileNames.size(), 4);

    for (const QString &fileName : fileNames) {
        QFile file(dir.filePath(fileName));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        QVERIFY2(doc.isObject(), qPrintable(fileName));

        QList<TargetDiagnostic> diagnostics;
        QVERIFY2(TargetConfigValidator::validateTarget(doc.object(), &diagnostics),
                 qPrintable(fileName + QStringLiteral(": ")
                            + [&diagnostics]() {
                                  QStringList lines;
                                  for (const TargetDiagnostic &diagnostic : diagnostics) {
                                      lines.append(diagnostic.displayText());
                                  }
                                  return lines.join(QStringLiteral("; "));
                              }()));
    }
}

void TargetRegistryTest::defaultUserTargetsPathIsStable()
{
    TargetRegistry registry;
    QCOMPARE(registry.userTargetsPath(),
             QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                 + QStringLiteral("/plasma-share-uploader/targets.d"));
}

void TargetRegistryTest::userTargetsOverrideSystemTargetsById()
{
    QTemporaryDir dir;
    const QString userDir = dir.filePath(QStringLiteral("targets.d"));
    QVERIFY(QDir().mkpath(userDir));
    const QString userPath = userDir + QStringLiteral("/catbox.json");
    QFile userFile(userPath);
    QVERIFY(userFile.open(QIODevice::WriteOnly));
    userFile.write(R"({
      "id": "catbox",
      "displayName": "My Catbox",
      "description": "override",
      "icon": "image-x-generic",
      "request": {
        "url": "https://override.test/upload",
        "method": "POST",
        "multipart": {
          "fields": {},
          "fileField": "file"
        }
      },
      "response": {
        "type": "text_url"
      }
    })");
    userFile.close();

    TargetRegistry registry(QStringLiteral(IMSHARE_TEST_SOURCE_DIR) + QStringLiteral("/../data/targets.d"), userDir);
    const TargetRegistry::LoadResult result = registry.loadTargets();

    QCOMPARE(result.targets.size(), 2);
    QCOMPARE(result.targets.at(0).id(), QStringLiteral("catbox"));
    QCOMPARE(result.targets.at(0).displayName(), QStringLiteral("My Catbox"));
}

void TargetRegistryTest::invalidUserOverrideFallsBackToSystemTarget()
{
    QTemporaryDir dir;
    const QString userDir = dir.filePath(QStringLiteral("targets.d"));
    QVERIFY(QDir().mkpath(userDir));

    QFile userFile(userDir + QStringLiteral("/catbox.json"));
    QVERIFY(userFile.open(QIODevice::WriteOnly));
    userFile.write(R"({
      "id": "catbox",
      "displayName": "Broken Catbox",
      "request": {
        "url": "",
        "method": "POST",
        "multipart": {
          "fields": {},
          "fileField": "file"
        }
      },
      "response": {
        "type": "text_url"
      }
    })");
    userFile.close();

    TargetRegistry registry(QStringLiteral(IMSHARE_TEST_SOURCE_DIR) + QStringLiteral("/../data/targets.d"), userDir);
    const TargetRegistry::LoadResult result = registry.loadTargets();

    QCOMPARE(result.targets.size(), 2);
    QCOMPARE(result.targets.at(0).id(), QStringLiteral("catbox"));
    QCOMPARE(result.targets.at(0).displayName(), QStringLiteral("Catbox"));
    QVERIFY(std::any_of(result.diagnostics.begin(), result.diagnostics.end(), [](const TargetDiagnostic &diagnostic) {
        return diagnostic.filePath.endsWith(QStringLiteral("/catbox.json"))
            && diagnostic.jsonPath == QLatin1StringView("/request/url")
            && diagnostic.code == QLatin1StringView("request.url.empty");
    }));
}

void TargetRegistryTest::invalidTargetsProduceErrorsButDoNotBlockValidTargets()
{
    QTemporaryDir dir;
    const QString userDir = dir.filePath(QStringLiteral("targets.d"));
    QVERIFY(QDir().mkpath(userDir));

    QFile badFile(userDir + QStringLiteral("/bad.json"));
    QVERIFY(badFile.open(QIODevice::WriteOnly));
    badFile.write(R"({
      "id": "bad target",
      "request": {
        "url": "https://bad.test",
        "method": "POST",
        "multipart": {
          "fields": {},
          "fileField": "file"
        }
      },
      "response": {
        "type": "text_url"
      }
    })");
    badFile.close();

    QFile goodFile(userDir + QStringLiteral("/good.json"));
    QVERIFY(goodFile.open(QIODevice::WriteOnly));
    goodFile.write(R"({
      "id": "good",
      "displayName": "Good",
      "request": {
        "url": "https://good.test",
        "method": "POST",
        "multipart": {
          "fields": {},
          "fileField": "file"
        }
      },
      "response": {
        "type": "text_url"
      }
    })");
    goodFile.close();

    TargetRegistry registry(QString(), userDir);
    const TargetRegistry::LoadResult result = registry.loadTargets();

    QVERIFY(!result.diagnostics.isEmpty());
    QCOMPARE(result.targets.size(), 3);
    QVERIFY(std::any_of(result.targets.begin(), result.targets.end(), [](const TargetDefinition &target) {
        return target.id() == QLatin1StringView("good");
    }));
    QVERIFY(std::any_of(result.diagnostics.begin(), result.diagnostics.end(), [](const TargetDiagnostic &diagnostic) {
        return diagnostic.filePath.endsWith(QStringLiteral("/bad.json"));
    }));
}

void TargetRegistryTest::malformedJsonProducesFileSpecificError()
{
    QTemporaryDir dir;
    const QString userDir = dir.filePath(QStringLiteral("targets.d"));
    QVERIFY(QDir().mkpath(userDir));

    QFile badFile(userDir + QStringLiteral("/broken.json"));
    QVERIFY(badFile.open(QIODevice::WriteOnly));
    badFile.write("{ not valid json");
    badFile.close();

    TargetRegistry registry(QString(), userDir);
    const TargetRegistry::LoadResult result = registry.loadTargets();

    QVERIFY(std::any_of(result.diagnostics.begin(), result.diagnostics.end(), [](const TargetDiagnostic &diagnostic) {
        return diagnostic.filePath.endsWith(QStringLiteral("/broken.json"))
            && diagnostic.code == QLatin1StringView("file.invalid_json_object");
    }));
}

void TargetRegistryTest::validatorAccumulatesMultipleDiagnosticsForOneTarget()
{
    const QJsonObject target{
        {QStringLiteral("id"), QStringLiteral("broken")},
        {QStringLiteral("request"),
         QJsonObject{
             {QStringLiteral("url"), QStringLiteral("")},
             {QStringLiteral("method"), QStringLiteral("GET")},
             {QStringLiteral("type"), QStringLiteral("json")},
             {QStringLiteral("json"), QJsonObject{}}}},
        {QStringLiteral("response"),
         QJsonObject{
             {QStringLiteral("type"), QStringLiteral("json_pointer")},
             {QStringLiteral("pointer"), QStringLiteral("bad")},
             {QStringLiteral("thumbnail"), QJsonObject{{QStringLiteral("type"), QStringLiteral("header")}}}}},
        {QStringLiteral("extensions"), QJsonArray{QStringLiteral(""), QStringLiteral("bad/ext")}}};

    QList<TargetDiagnostic> diagnostics;
    QVERIFY(!TargetConfigValidator::validateTarget(target, &diagnostics));
    QVERIFY(std::any_of(diagnostics.begin(), diagnostics.end(), [](const TargetDiagnostic &diagnostic) {
        return diagnostic.code == QLatin1StringView("request.url.empty");
    }));
    QVERIFY(std::any_of(diagnostics.begin(), diagnostics.end(), [](const TargetDiagnostic &diagnostic) {
        return diagnostic.code == QLatin1StringView("request.method.json");
    }));
    QVERIFY(std::any_of(diagnostics.begin(), diagnostics.end(), [](const TargetDiagnostic &diagnostic) {
        return diagnostic.code == QLatin1StringView("request.json.missing");
    }));
    QVERIFY(std::any_of(diagnostics.begin(), diagnostics.end(), [](const TargetDiagnostic &diagnostic) {
        return diagnostic.code == QLatin1StringView("response.pointer.invalid");
    }));
    QVERIFY(std::any_of(diagnostics.begin(), diagnostics.end(), [](const TargetDiagnostic &diagnostic) {
        return diagnostic.code == QLatin1StringView("response.thumbnail.name.empty");
    }));
    QVERIFY(std::any_of(diagnostics.begin(), diagnostics.end(), [](const TargetDiagnostic &diagnostic) {
        return diagnostic.code == QLatin1StringView("extensions.invalid") || diagnostic.code == QLatin1StringView("extensions.empty");
    }));
}

void TargetRegistryTest::constraintMatcherFiltersByMimeType()
{
    QTemporaryDir dir;
    const QString imagePath = writeTempFile(dir, QStringLiteral("image.png"), tinyPng());
    const QString textPath = writeTempFile(dir, QStringLiteral("file.txt"), "hello");

    TargetDefinition imageTarget;
    imageTarget.config = QJsonObject{
        {QStringLiteral("id"), QStringLiteral("images")},
        {QStringLiteral("constraints"), QJsonArray{QStringLiteral("mimeType:image/*")}}};
    QVERIFY(TargetCoreConfigParser::parse(imageTarget.config, &imageTarget.core));

    TargetDefinition anyTarget;
    anyTarget.config = QJsonObject{{QStringLiteral("id"), QStringLiteral("any")}};
    QVERIFY(TargetCoreConfigParser::parse(anyTarget.config, &anyTarget.core));

    QVERIFY(ConstraintMatcher::targetMatchesFiles(imageTarget, QStringList{imagePath}));
    QVERIFY(!ConstraintMatcher::targetMatchesFiles(imageTarget, QStringList{textPath}));

    const QList<TargetDefinition> filtered =
        ConstraintMatcher::filterTargets(QList<TargetDefinition>{imageTarget, anyTarget}, QStringList{textPath});
    QCOMPARE(filtered.size(), 1);
    QCOMPARE(filtered.first().id(), QStringLiteral("any"));
}

void TargetRegistryTest::constraintMatcherFiltersByExtension()
{
    QTemporaryDir dir;
    const QString imagePath = writeTempFile(dir, QStringLiteral("photo.jpeg"), "jpeg");
    const QString textPath = writeTempFile(dir, QStringLiteral("notes.txt"), "text");

    TargetDefinition imageTarget;
    imageTarget.config = QJsonObject{
        {QStringLiteral("id"), QStringLiteral("images")},
        {QStringLiteral("extensions"), QJsonArray{QStringLiteral("png"), QStringLiteral(".jpeg")}}};
    QVERIFY(TargetCoreConfigParser::parse(imageTarget.config, &imageTarget.core));

    QVERIFY(ConstraintMatcher::targetMatchesFiles(imageTarget, QStringList{imagePath}));
    QVERIFY(!ConstraintMatcher::targetMatchesFiles(imageTarget, QStringList{textPath}));
}

QTEST_APPLESS_MAIN(TargetRegistryTest)

#include "test_targetregistry.moc"
