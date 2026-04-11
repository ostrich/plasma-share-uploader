#include "constraintmatcher.h"
#include "targetconfigvalidator.h"
#include "targetregistry.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>

#include "testutils.h"

class TargetRegistryTest final : public QObject
{
    Q_OBJECT

private slots:
    void loadsBundledSystemTargets();
    void exampleTargetFilesValidate();
    void userTargetsOverrideSystemTargetsById();
    void invalidTargetsProduceErrorsButDoNotBlockValidTargets();
    void constraintMatcherFiltersByMimeType();
    void constraintMatcherFiltersByExtension();
};

void TargetRegistryTest::loadsBundledSystemTargets()
{
    TargetRegistry registry(QStringLiteral(IMSHARE_TEST_SOURCE_DIR) + QStringLiteral("/../data/targets.d"), QStringLiteral("/nonexistent"));
    const TargetRegistry::LoadResult result = registry.loadTargets();

    QVERIFY(result.errors.isEmpty());
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

        QStringList errors;
        QVERIFY2(TargetConfigValidator::validateTarget(doc.object(), &errors), qPrintable(fileName + QStringLiteral(": ") + errors.join(QStringLiteral("; "))));
    }
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

    QVERIFY(!result.errors.isEmpty());
    QCOMPARE(result.targets.size(), 3);
    QVERIFY(std::any_of(result.targets.begin(), result.targets.end(), [](const TargetDefinition &target) {
        return target.id() == QLatin1StringView("good");
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

    TargetDefinition anyTarget;
    anyTarget.config = QJsonObject{{QStringLiteral("id"), QStringLiteral("any")}};

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

    QVERIFY(ConstraintMatcher::targetMatchesFiles(imageTarget, QStringList{imagePath}));
    QVERIFY(!ConstraintMatcher::targetMatchesFiles(imageTarget, QStringList{textPath}));
}

QTEST_APPLESS_MAIN(TargetRegistryTest)

#include "test_targetregistry.moc"
