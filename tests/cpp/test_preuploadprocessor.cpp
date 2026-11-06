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
    void literalPlaceholderFilename();
    void boundsCommandDiagnostics();
    void cancelsProcessTree_data();
    void cancelsProcessTree();
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

void PreUploadProcessorTest::literalPlaceholderFilename()
{
    QTemporaryDir dir;
    const auto source = writeTempFile(dir, QStringLiteral("${FILE}-${OUT_FILE}.txt"), "original");
    for (bool outputFile : {false, true}) {
        const auto command = outputFile
            ? commandObject({pythonExecutable(), fixtureScriptPath(QStringLiteral("copy_with_prefix.py")),
                             QStringLiteral("${FILE}"), QStringLiteral("${OUT_FILE}"), QStringLiteral("changed-")})
            : commandObject({pythonExecutable(), fixtureScriptPath(QStringLiteral("append_text.py")),
                             QStringLiteral("${FILE}"), QStringLiteral("-changed")});
        const QJsonObject config{{QStringLiteral("preUpload"), QJsonArray{QJsonObject{
            {QStringLiteral("mime"), QJsonArray{QStringLiteral("*/*")}},
            {QStringLiteral("fileHandling"), outputFile ? QStringLiteral("output_file") : QStringLiteral("inplace_copy")},
            {QStringLiteral("commands"), QJsonArray{command}}}}}};
        auto result = PreUploadProcessor::preprocessFile(config, source);
        QVERIFY2(result.ok, qPrintable(result.errorMessage));
        QFile output(result.uploadPath);
        QVERIFY(output.open(QIODevice::ReadOnly));
        QCOMPARE(output.readAll(), outputFile ? QByteArray("changed-original") : QByteArray("original-changed"));
    }
}
void PreUploadProcessorTest::boundsCommandDiagnostics()
{
    QTemporaryDir dir;
    const auto source = writeTempFile(dir, QStringLiteral("source.txt"), "original");
    const QJsonObject config{{QStringLiteral("preUpload"), QJsonArray{QJsonObject{
        {QStringLiteral("mime"), QJsonArray{QStringLiteral("*/*")}},
        {QStringLiteral("fileHandling"), QStringLiteral("inplace_copy")},
        {QStringLiteral("commands"), QJsonArray{commandObject({pythonExecutable(), QStringLiteral("-c"),
            QStringLiteral("import sys; sys.stdout.write('x'*2000000); sys.stderr.write('y'*2000000+'END'); sys.exit(1)"), QStringLiteral("${FILE}")})}}}}}};
    const auto result = PreUploadProcessor::preprocessFile(config, source);
    QVERIFY(!result.ok);
    QVERIFY(result.errorMessage.size() < 66000);
    QVERIFY(result.errorMessage.contains(QStringLiteral("truncated")));
    QVERIFY(result.errorMessage.endsWith(QStringLiteral("END")));
}
void PreUploadProcessorTest::cancelsProcessTree_data()
{
    QTest::addColumn<bool>("timeout");
    QTest::newRow("cancel") << false;
    QTest::newRow("timeout") << true;
}
void PreUploadProcessorTest::cancelsProcessTree()
{
    QFETCH(bool, timeout);
    QTemporaryDir dir;
    const auto source = writeTempFile(dir, QStringLiteral("source.txt"), "original");
    const auto marker = dir.filePath(QStringLiteral("child-started"));
    const auto escaped = dir.filePath(QStringLiteral("child-survived"));
    const QString child = QStringLiteral("import pathlib,sys,time; pathlib.Path(sys.argv[1]).write_text('ready'); time.sleep(1); pathlib.Path(sys.argv[2]).write_text('escaped')");
    const QJsonObject config{{QStringLiteral("preUpload"), QJsonArray{QJsonObject{
        {QStringLiteral("mime"), QJsonArray{QStringLiteral("*/*")}},
        {QStringLiteral("fileHandling"), QStringLiteral("inplace_copy")},
        {QStringLiteral("timeoutMs"), timeout ? 300 : 30000},
        {QStringLiteral("commands"), QJsonArray{commandObject({pythonExecutable(), QStringLiteral("-c"),
            QStringLiteral("import subprocess,sys,time; subprocess.Popen([sys.executable,'-c',sys.argv[1],sys.argv[2],sys.argv[3]]); time.sleep(10)"), child, marker, escaped, QStringLiteral("${FILE}")})}}}}}};
    ParsedPreUploadConfig parsed;
    QVERIFY(TargetPreUploadConfigParser::parse(config, &parsed));
    auto context = std::make_unique<QObject>();
    bool completed = false;
    PreUploadProcessor::preprocessFileAsync(parsed, source, context.get(), [&](auto) { completed = true; });
    QTRY_VERIFY(QFileInfo::exists(marker));
    if (timeout) QTRY_VERIFY(completed);
    else context.reset();
    QTest::qWait(1100);
    QCOMPARE(completed, timeout);
    QVERIFY(!QFileInfo::exists(escaped));
}

QTEST_GUILESS_MAIN(PreUploadProcessorTest)

#include "test_preuploadprocessor.moc"
