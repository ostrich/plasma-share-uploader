#include "preuploadprocessor.h"

#include "targetpreuploadconfigparser.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>

namespace {
constexpr int kDefaultPreUploadTimeoutMs = 30000;

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

QString diagnosticsText(const QList<TargetDiagnostic> &diagnostics)
{
    QStringList lines;
    lines.reserve(diagnostics.size());
    for (const TargetDiagnostic &diagnostic : diagnostics) {
        lines.append(diagnostic.displayText());
    }
    return lines.join(QLatin1Char('\n'));
}
}

PreUploadProcessor::Result PreUploadProcessor::preprocessFile(const QJsonObject &targetConfig, const QString &filePath)
{
    QList<TargetDiagnostic> diagnostics;
    ParsedPreUploadConfig parsed;
    if (!TargetPreUploadConfigParser::parse(targetConfig, &parsed, &diagnostics)) {
        Result result;
        result.errorMessage = diagnosticsText(diagnostics);
        return result;
    }
    return preprocessFile(parsed, filePath);
}

PreUploadProcessor::Result PreUploadProcessor::preprocessFile(const ParsedPreUploadConfig &config, const QString &filePath)
{
    Result result;
    result.ok = true;
    result.uploadPath = filePath;

    if (config.rules.isEmpty()) {
        return result;
    }

    const QString mimeType = QMimeDatabase{}.mimeTypeForFile(filePath, QMimeDatabase::MatchContent).name();

    ParsedPreUploadRule selectedRule;
    bool foundRule = false;
    for (const ParsedPreUploadRule &rule : config.rules) {
        for (const QString &pattern : rule.mimePatterns) {
            if (!pattern.isEmpty() && mimeMatchesPattern(mimeType, pattern)) {
                selectedRule = rule;
                foundRule = true;
                break;
            }
        }

        if (foundRule) {
            break;
        }
    }

    if (!foundRule) {
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
    result.tempDirPath = tempDir.path();
    const QString tempFilePath = QDir(result.tempDirPath).filePath(fileInfo.fileName());
    QString outFilePath;

    if (selectedRule.fileHandling == PreUploadFileHandling::InplaceCopy) {
        if (!QFile::copy(filePath, tempFilePath)) {
            result.ok = false;
            result.errorMessage = QStringLiteral("Failed to create temporary copy for %1").arg(filePath);
            QDir(result.tempDirPath).removeRecursively();
            result.tempDirPath.clear();
            return result;
        }
        result.uploadPath = tempFilePath;
    } else if (selectedRule.fileHandling == PreUploadFileHandling::OutputFile) {
        outFilePath = QDir(result.tempDirPath).filePath(fileInfo.fileName());
    } else {
        result.ok = false;
        result.errorMessage = QStringLiteral("Unsupported pre-upload fileHandling.");
        QDir(result.tempDirPath).removeRecursively();
        result.tempDirPath.clear();
        return result;
    }

    const int timeoutMs = selectedRule.timeoutMs > 0 ? selectedRule.timeoutMs : kDefaultPreUploadTimeoutMs;
    for (const ParsedPreUploadCommand &command : selectedRule.commands) {
        QStringList argv;
        argv.reserve(command.argv.size());
        for (const QString &arg : command.argv) {
            argv.append(substituteCommandArg(arg, result.uploadPath, outFilePath));
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

    if (selectedRule.fileHandling == PreUploadFileHandling::OutputFile) {
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
