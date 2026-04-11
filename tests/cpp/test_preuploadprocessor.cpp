#include "preuploadprocessor.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

#include "testutils.h"

class PreUploadProcessorTest final : public QObject
{
    Q_OBJECT

private slots:
    void returnsOriginalPathWhenNoRulesMatch();
    void inplaceCopyRunsMultipleCommandsAndKeepsOriginalUntouched();
    void outputFileProducesSeparateUploadFile();
    void reportsCommandFailure();
    void reportsCommandTimeout();
};

void PreUploadProcessorTest::returnsOriginalPathWhenNoRulesMatch()
{
    QTemporaryDir dir;
    const QString filePath = writeTempFile(dir, QStringLiteral("sample.txt"), "hello");
    const QJsonObject config{
        {QStringLiteral("preUpload"),
         QJsonArray{QJsonObject{
             {QStringLiteral("mime"), QJsonArray{QStringLiteral("image/*")}},
             {QStringLiteral("fileHandling"), QStringLiteral("inplace_copy")},
             {QStringLiteral("commands"), QJsonArray{commandObject({QStringLiteral("tool"), QStringLiteral("${FILE}")})}},
         }}}};

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
    const QJsonObject config{
        {QStringLiteral("preUpload"),
         QJsonArray{QJsonObject{
             {QStringLiteral("mime"), QJsonArray{QStringLiteral("text/plain")}},
             {QStringLiteral("fileHandling"), QStringLiteral("inplace_copy")},
             {QStringLiteral("commands"),
              QJsonArray{
                  commandObject({pythonExecutable(), fixtureScriptPath(QStringLiteral("append_text.py")),
                                 QStringLiteral("${FILE}"), QStringLiteral("-a")}),
                  commandObject({pythonExecutable(), fixtureScriptPath(QStringLiteral("append_text.py")),
                                 QStringLiteral("${FILE}"), QStringLiteral("-b")}),
              }},
         }}}};

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
    const QJsonObject config{
        {QStringLiteral("preUpload"),
         QJsonArray{QJsonObject{
             {QStringLiteral("mime"), QJsonArray{QStringLiteral("*/*")}},
             {QStringLiteral("fileHandling"), QStringLiteral("output_file")},
             {QStringLiteral("commands"),
              QJsonArray{commandObject(
                  {pythonExecutable(), fixtureScriptPath(QStringLiteral("copy_with_prefix.py")),
                   QStringLiteral("${FILE}"), QStringLiteral("${OUT_FILE}"), QStringLiteral("prefix:")})}},
         }}}};

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
    const QJsonObject config{
        {QStringLiteral("preUpload"),
         QJsonArray{QJsonObject{
             {QStringLiteral("mime"), QJsonArray{QStringLiteral("*/*")}},
             {QStringLiteral("fileHandling"), QStringLiteral("inplace_copy")},
             {QStringLiteral("commands"),
              QJsonArray{commandObject({pythonExecutable(), fixtureScriptPath(QStringLiteral("fail.py")),
                                        QStringLiteral("${FILE}"), QStringLiteral("boom")})}},
         }}}};

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
    const QJsonObject config{
        {QStringLiteral("preUpload"),
         QJsonArray{QJsonObject{
             {QStringLiteral("mime"), QJsonArray{QStringLiteral("*/*")}},
             {QStringLiteral("fileHandling"), QStringLiteral("inplace_copy")},
             {QStringLiteral("timeoutMs"), 100},
             {QStringLiteral("commands"),
              QJsonArray{commandObject({pythonExecutable(), fixtureScriptPath(QStringLiteral("sleep.py")),
                                        QStringLiteral("${FILE}"), QStringLiteral("0.5")})}},
         }}}};

    const PreUploadProcessor::Result result = PreUploadProcessor::preprocessFile(config, filePath);

    QVERIFY(!result.ok);
    QVERIFY(result.errorMessage.contains(QStringLiteral("timed out")));
}

QTEST_APPLESS_MAIN(PreUploadProcessorTest)

#include "test_preuploadprocessor.moc"
