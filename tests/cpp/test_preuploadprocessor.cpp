#include "preuploadprocessor.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

#include "testutils.h"

class PreUploadProcessorTest final : public QObject {
    Q_OBJECT

private slots:
    void returnsOriginalPathWhenNoRulesMatch();
    void inplaceCopyRunsMultipleCommandsAndKeepsOriginalUntouched();
    void outputFileProducesSeparateUploadFile();
    void reportsCommandFailure();
    void reportsCommandTimeout();
    void readOnlyInputCopyIsWritableAndOwned();
    void reportsMissingExecutable();
    void readOnlyJpegSupportsBundledPreprocessing();
};

void PreUploadProcessorTest::returnsOriginalPathWhenNoRulesMatch()
{
    QTemporaryDir dir;
    const QString filePath = writeTempFile(dir, QStringLiteral("sample.txt"), "hello");
    const QJsonObject config { { QStringLiteral("preUpload"),
        QJsonArray { QJsonObject { { QStringLiteral("mime"), QJsonArray { QStringLiteral("image/*") } },
            { QStringLiteral("fileHandling"), QStringLiteral("inplace_copy") },
            { QStringLiteral("commands"),
                QJsonArray { commandObject({ QStringLiteral("tool"), QStringLiteral("${FILE}") }) } } } } } };

    const PreUploadProcessor::Result result = PreUploadProcessor::preprocessFile(config, filePath);

    QVERIFY(result.ok);
    QCOMPARE(result.uploadPath, filePath);
    QVERIFY(result.tempDirPath.isEmpty());
}

void PreUploadProcessorTest::inplaceCopyRunsMultipleCommandsAndKeepsOriginalUntouched()
{
    QVERIFY2(!pythonExecutable().isEmpty(), "python3 is required for preupload tests");

    QTemporaryDir dir;
    const QString filePath = writeTempFile(dir, QStringLiteral("sample.txt"), "hello");
    const QJsonObject config { { QStringLiteral("preUpload"),
        QJsonArray { QJsonObject { { QStringLiteral("mime"), QJsonArray { QStringLiteral("text/plain") } },
            { QStringLiteral("fileHandling"), QStringLiteral("inplace_copy") },
            { QStringLiteral("commands"),
                QJsonArray {
                    commandObject({ pythonExecutable(), fixtureScriptPath(QStringLiteral("append_text.py")),
                        QStringLiteral("${FILE}"), QStringLiteral("-a") }),
                    commandObject({ pythonExecutable(), fixtureScriptPath(QStringLiteral("append_text.py")),
                        QStringLiteral("${FILE}"), QStringLiteral("-b") }),
                } } } } } };

    const PreUploadProcessor::Result result = PreUploadProcessor::preprocessFile(config, filePath);

    QVERIFY(result.ok);
    QVERIFY(result.uploadPath != filePath);
    QVERIFY(!result.tempDirPath.isEmpty());
    QFile original(filePath);
    QVERIFY(original.open(QIODevice::ReadOnly));
    QCOMPARE(original.readAll(), QByteArray("hello"));
    QFile processed(result.uploadPath);
    QVERIFY(processed.open(QIODevice::ReadOnly));
    QCOMPARE(processed.readAll(), QByteArray("hello-a-b"));
    QVERIFY(QDir(result.tempDirPath).removeRecursively());
}

void PreUploadProcessorTest::outputFileProducesSeparateUploadFile()
{
    QVERIFY2(!pythonExecutable().isEmpty(), "python3 is required for preupload tests");

    QTemporaryDir dir;
    const QString filePath = writeTempFile(dir, QStringLiteral("input.txt"), "payload");
    const QJsonObject config { { QStringLiteral("preUpload"),
        QJsonArray { QJsonObject { { QStringLiteral("mime"), QJsonArray { QStringLiteral("*/*") } },
            { QStringLiteral("fileHandling"), QStringLiteral("output_file") },
            { QStringLiteral("commands"),
                QJsonArray { commandObject({ pythonExecutable(),
                    fixtureScriptPath(QStringLiteral("copy_with_prefix.py")), QStringLiteral("${FILE}"),
                    QStringLiteral("${OUT_FILE}"), QStringLiteral("prefix:") }) } } } } } };

    const PreUploadProcessor::Result result = PreUploadProcessor::preprocessFile(config, filePath);

    QVERIFY(result.ok);
    QVERIFY(result.uploadPath != filePath);
    QFile processed(result.uploadPath);
    QVERIFY(processed.open(QIODevice::ReadOnly));
    QCOMPARE(processed.readAll(), QByteArray("prefix:payload"));
    QVERIFY(QDir(result.tempDirPath).removeRecursively());
}

void PreUploadProcessorTest::reportsCommandFailure()
{
    QVERIFY2(!pythonExecutable().isEmpty(), "python3 is required for preupload tests");

    QTemporaryDir dir;
    const QString filePath = writeTempFile(dir, QStringLiteral("input.txt"), "payload");
    const QJsonObject config { { QStringLiteral("preUpload"),
        QJsonArray { QJsonObject { { QStringLiteral("mime"), QJsonArray { QStringLiteral("*/*") } },
            { QStringLiteral("fileHandling"), QStringLiteral("inplace_copy") },
            { QStringLiteral("commands"),
                QJsonArray { commandObject({ pythonExecutable(), fixtureScriptPath(QStringLiteral("fail.py")),
                    QStringLiteral("${FILE}"), QStringLiteral("boom") }) } } } } } };

    const PreUploadProcessor::Result result = PreUploadProcessor::preprocessFile(config, filePath);

    QVERIFY(!result.ok);
    QCOMPARE(result.errorMessage, QStringLiteral("boom"));
    QVERIFY(result.tempDirPath.isEmpty());
}

void PreUploadProcessorTest::reportsCommandTimeout()
{
    QVERIFY2(!pythonExecutable().isEmpty(), "python3 is required for preupload tests");

    QTemporaryDir dir;
    const QString filePath = writeTempFile(dir, QStringLiteral("input.txt"), "payload");
    const QJsonObject config { { QStringLiteral("preUpload"),
        QJsonArray { QJsonObject { { QStringLiteral("mime"), QJsonArray { QStringLiteral("*/*") } },
            { QStringLiteral("fileHandling"), QStringLiteral("inplace_copy") }, { QStringLiteral("timeoutMs"), 100 },
            { QStringLiteral("commands"),
                QJsonArray { commandObject({ pythonExecutable(), fixtureScriptPath(QStringLiteral("sleep.py")),
                    QStringLiteral("${FILE}"), QStringLiteral("0.5") }) } } } } } };

    const PreUploadProcessor::Result result = PreUploadProcessor::preprocessFile(config, filePath);

    QVERIFY(!result.ok);
    QVERIFY(result.errorMessage.contains(QStringLiteral("timed out")));
}

void PreUploadProcessorTest::readOnlyInputCopyIsWritableAndOwned()
{
    QVERIFY(!pythonExecutable().isEmpty());
    QTemporaryDir dir;
    const QString source = writeTempFile(dir, QStringLiteral("readonly.txt"), "original");
    QVERIFY(QFile::setPermissions(source, QFileDevice::ReadOwner | QFileDevice::ReadGroup | QFileDevice::ReadOther));
    const auto originalPermissions = QFile::permissions(source);
    const QJsonObject config { { QStringLiteral("preUpload"),
        QJsonArray { QJsonObject { { QStringLiteral("mime"), QJsonArray { QStringLiteral("*/*") } },
            { QStringLiteral("fileHandling"), QStringLiteral("inplace_copy") },
            { QStringLiteral("commands"),
                QJsonArray { commandObject({ pythonExecutable(), fixtureScriptPath(QStringLiteral("append_text.py")),
                    QStringLiteral("${FILE}"), QStringLiteral("-changed") }) } } } } } };
    QString processedDir;
    {
        const auto result = PreUploadProcessor::preprocessFile(config, source);
        QVERIFY2(result.ok, qPrintable(result.errorMessage));
        processedDir = result.tempDirPath;
        QFile processed(result.uploadPath);
        QVERIFY(processed.open(QIODevice::ReadOnly));
        QCOMPARE(processed.readAll(), QByteArray("original-changed"));
        QVERIFY(QFile::permissions(result.uploadPath).testFlag(QFileDevice::WriteOwner));
    }
    QVERIFY(!QFileInfo::exists(processedDir));
    QCOMPARE(QFile::permissions(source), originalPermissions);
    QFile original(source);
    QVERIFY(original.open(QIODevice::ReadOnly));
    QCOMPARE(original.readAll(), QByteArray("original"));
}

void PreUploadProcessorTest::reportsMissingExecutable()
{
    QTemporaryDir dir;
    const QString source = writeTempFile(dir, QStringLiteral("input.txt"), "payload");
    const QJsonObject config { { QStringLiteral("preUpload"),
        QJsonArray { QJsonObject { { QStringLiteral("mime"), QJsonArray { QStringLiteral("*/*") } },
            { QStringLiteral("fileHandling"), QStringLiteral("inplace_copy") },
            { QStringLiteral("commands"),
                QJsonArray {
                    commandObject({ dir.filePath(QStringLiteral("missing")), QStringLiteral("${FILE}") }) } } } } } };
    const auto result = PreUploadProcessor::preprocessFile(config, source);
    QVERIFY(!result.ok);
    QVERIFY(result.errorMessage.startsWith(QStringLiteral("Failed to start")));
    QVERIFY(result.tempDirPath.isEmpty());
}

void PreUploadProcessorTest::readOnlyJpegSupportsBundledPreprocessing()
{
    if (QStandardPaths::findExecutable(QStringLiteral("exiv2")).isEmpty()) {
        QSKIP("exiv2 is required to exercise the bundled JPEG preprocessing rule");
    }
    QTemporaryDir dir;
    const QString source = dir.filePath(QStringLiteral("readonly.jpg"));
    QImage image(2, 2, QImage::Format_RGB32);
    image.fill(Qt::white);
    QVERIFY(image.save(source));
    QVERIFY(QFile::setPermissions(source, QFileDevice::ReadOwner | QFileDevice::ReadGroup | QFileDevice::ReadOther));
    const auto permissions = QFile::permissions(source);
    QFile original(source);
    QVERIFY(original.open(QIODevice::ReadOnly));
    const auto bytes = original.readAll();
    original.close();
    QFile target(QStringLiteral(IMSHARE_TEST_SOURCE_DIR) + QStringLiteral("/../targets/catbox.json"));
    QVERIFY(target.open(QIODevice::ReadOnly));
    const auto result = PreUploadProcessor::preprocessFile(QJsonDocument::fromJson(target.readAll()).object(), source);
    QVERIFY2(result.ok, qPrintable(result.errorMessage));
    QVERIFY(result.uploadPath != source);
    QVERIFY(!QImage(result.uploadPath).isNull());
    QCOMPARE(QFile::permissions(source), permissions);
    QVERIFY(original.open(QIODevice::ReadOnly));
    QCOMPARE(original.readAll(), bytes);
}

QTEST_GUILESS_MAIN(PreUploadProcessorTest)

#include "test_preuploadprocessor.moc"
