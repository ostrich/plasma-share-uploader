#include "constraintmatcher.h"
#include "targetregistry.h"

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
    void userTargetsOverrideSystemTargetsById();
    void invalidTargetsProduceErrorsButDoNotBlockValidTargets();
    void constraintMatcherFiltersByMimeType();
};

void TargetRegistryTest::loadsBundledSystemTargets()
{
    TargetRegistry registry(QStringLiteral(IMSHARE_TEST_SOURCE_DIR) + QStringLiteral("/../data/targets.json"), QStringLiteral("/nonexistent"));
    const TargetRegistry::LoadResult result = registry.loadTargets();

    QVERIFY(result.errors.isEmpty());
    QCOMPARE(result.targets.size(), 2);
    QCOMPARE(result.targets.at(0).id(), QStringLiteral("catbox"));
    QCOMPARE(result.targets.at(1).id(), QStringLiteral("uguu"));
}

void TargetRegistryTest::userTargetsOverrideSystemTargetsById()
{
    QTemporaryDir dir;
    const QString userPath = dir.filePath(QStringLiteral("targets.json"));
    QFile userFile(userPath);
    QVERIFY(userFile.open(QIODevice::WriteOnly));
    userFile.write(R"({
      "targets": [
        {
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
        }
      ]
    })");
    userFile.close();

    TargetRegistry registry(QStringLiteral(IMSHARE_TEST_SOURCE_DIR) + QStringLiteral("/../data/targets.json"), userPath);
    const TargetRegistry::LoadResult result = registry.loadTargets();

    QCOMPARE(result.targets.size(), 2);
    QCOMPARE(result.targets.at(0).id(), QStringLiteral("catbox"));
    QCOMPARE(result.targets.at(0).displayName(), QStringLiteral("My Catbox"));
}

void TargetRegistryTest::invalidTargetsProduceErrorsButDoNotBlockValidTargets()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("targets.json"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(R"({
      "targets": [
        {
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
        },
        {
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
        }
      ]
    })");
    file.close();

    TargetRegistry registry(QString(), path);
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

QTEST_APPLESS_MAIN(TargetRegistryTest)

#include "test_targetregistry.moc"
