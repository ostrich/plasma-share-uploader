#include "preuploadprocessor.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonValue>
#include <QMimeDatabase>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>

namespace {
constexpr int kDefaultPreUploadTimeoutMs = 30000;

QJsonArray arrayValue(const QJsonObject &parent, const char *key)
{
    const QJsonValue value = parent.value(QLatin1StringView(key));
    return value.isArray() ? value.toArray() : QJsonArray();
}

QString stringValue(const QJsonObject &parent, const char *key)
{
    return parent.value(QLatin1StringView(key)).toString();
}

bool mimeMatchesPattern(const QString &mimeType, const QString &pattern)
{
    if (pattern == QLatin1StringView("*/*")) {
        return true;
    }

    if (pattern.endsWith(QLatin1StringView("/*"))) {
        const QStringView typePrefix = QStringView{pattern}.left(pattern.size() - 1);
        return mimeType.startsWith(typePrefix);
    }

    return mimeType == pattern;
}

QString substituteCommandArg(const QString &arg, const QString &filePath, const QString &outFilePath)
{
    QString result = arg;
    result.replace(QStringLiteral("${FILE}"), filePath);
    result.replace(QStringLiteral("${OUT_FILE}"), outFilePath);
    return result;
}

QString formatCommand(const QStringList &argv)
{
    static const QRegularExpression needsQuotes(QStringLiteral(R"([\s"])"));

    QStringList quoted;
    quoted.reserve(argv.size());
    for (const QString &arg : argv) {
        QString escaped = arg;
        escaped.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
        escaped.replace(QLatin1Char('"'), QStringLiteral("\\\""));
        if (escaped.contains(needsQuotes)) {
            escaped = QStringLiteral("\"%1\"").arg(escaped);
        }
        quoted.append(escaped);
    }
    return quoted.join(QLatin1Char(' '));
}

QString runCommand(const QStringList &argv, int timeoutMs)
{
    if (argv.isEmpty()) {
        return QStringLiteral("Pre-upload command is empty.");
    }

    QProcess process;
    process.setProgram(argv.first());
    process.setArguments(argv.mid(1));
    process.start();
    if (!process.waitForStarted(timeoutMs)) {
        return QStringLiteral("Failed to start pre-upload command: %1").arg(process.errorString());
    }

    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished();
        return QStringLiteral("Pre-upload command timed out: %1").arg(formatCommand(argv));
    }

    const QString stdErr = QString::fromUtf8(process.readAllStandardError()).trimmed();
    const QString stdOut = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        const QString details = !stdErr.isEmpty() ? stdErr : stdOut;
        return details.isEmpty()
            ? QStringLiteral("Pre-upload command failed: %1").arg(formatCommand(argv))
            : details;
    }

    return QString();
}
}

PreUploadProcessor::Result PreUploadProcessor::preprocessFile(const QJsonObject &targetConfig, const QString &filePath)
{
    Result result;
    result.ok = true;
    result.uploadPath = filePath;

    const QJsonArray preUploadRules = arrayValue(targetConfig, "preUpload");
    if (preUploadRules.isEmpty()) {
        return result;
    }

    const QString mimeType = QMimeDatabase{}.mimeTypeForFile(filePath, QMimeDatabase::MatchContent).name();

    QJsonObject selectedRule;
    for (const QJsonValue &value : preUploadRules) {
        if (!value.isObject()) {
            continue;
        }

        const QJsonObject rule = value.toObject();
        const QJsonArray mimePatterns = arrayValue(rule, "mime");
        for (const QJsonValue &patternValue : mimePatterns) {
            const QString pattern = patternValue.toString();
            if (!pattern.isEmpty() && mimeMatchesPattern(mimeType, pattern)) {
                selectedRule = rule;
                break;
            }
        }

        if (!selectedRule.isEmpty()) {
            break;
        }
    }

    if (selectedRule.isEmpty()) {
        return result;
    }

    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        result.ok = false;
        result.errorMessage = QStringLiteral("Failed to create temporary directory for pre-upload processing.");
        return result;
    }
    tempDir.setAutoRemove(false);

    const QFileInfo fileInfo(filePath);
    const QString fileHandling = stringValue(selectedRule, "fileHandling");
    result.tempDirPath = tempDir.path();
    const QString tempFilePath = QDir(result.tempDirPath).filePath(fileInfo.fileName());
    QString outFilePath;

    if (fileHandling == QLatin1StringView("inplace_copy")) {
        if (!QFile::copy(filePath, tempFilePath)) {
            result.ok = false;
            result.errorMessage = QStringLiteral("Failed to create temporary copy for %1").arg(filePath);
            QDir(result.tempDirPath).removeRecursively();
            result.tempDirPath.clear();
            return result;
        }
        result.uploadPath = tempFilePath;
    } else if (fileHandling == QLatin1StringView("output_file")) {
        outFilePath = QDir(result.tempDirPath).filePath(fileInfo.fileName());
    } else {
        result.ok = false;
        result.errorMessage = QStringLiteral("Unsupported pre-upload fileHandling.");
        QDir(result.tempDirPath).removeRecursively();
        result.tempDirPath.clear();
        return result;
    }

    const QJsonArray commands = arrayValue(selectedRule, "commands");
    const int timeoutMs = selectedRule.value(QStringLiteral("timeoutMs")).toInt(kDefaultPreUploadTimeoutMs);
    for (const QJsonValue &commandValue : commands) {
        const QJsonObject command = commandValue.toObject();
        const QJsonArray argvJson = arrayValue(command, "argv");

        QStringList argv;
        argv.reserve(argvJson.size());
        for (const QJsonValue &argValue : argvJson) {
            argv.append(substituteCommandArg(argValue.toString(), result.uploadPath, outFilePath));
        }

        const QString commandError = runCommand(argv, timeoutMs);
        if (!commandError.isEmpty()) {
            result.ok = false;
            result.errorMessage = commandError;
            QDir(result.tempDirPath).removeRecursively();
            result.tempDirPath.clear();
            return result;
        }
    }

    if (fileHandling == QLatin1StringView("output_file")) {
        QFileInfo outInfo(outFilePath);
        if (!outInfo.exists() || !outInfo.isFile()) {
            result.ok = false;
            result.errorMessage = QStringLiteral("Pre-upload command did not create an output file.");
            QDir(result.tempDirPath).removeRecursively();
            result.tempDirPath.clear();
            return result;
        }
        result.uploadPath = outInfo.absoluteFilePath();
    }

    return result;
}
