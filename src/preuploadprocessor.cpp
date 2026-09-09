#include "preuploadprocessor.h"

#include "targetpreuploadconfigparser.h"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTimer>

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

namespace {
class ProcessingTask final : public QObject
{
public:
    ProcessingTask(const ParsedPreUploadConfig &config, const QString &filePath,
                   QObject *context, std::function<void(PreUploadProcessor::Result)> completed)
        : QObject(context), m_completed(std::move(completed))
    {
        m_timeout.setSingleShot(true);
        connect(&m_timeout, &QTimer::timeout, this, [this]() {
            m_timedOut = true;
            m_process.kill();
        });
        connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) {
                finish(QStringLiteral("Failed to start pre-upload command: %1").arg(m_process.errorString()));
            }
        });
        connect(&m_process, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
            m_timeout.stop();
            if (m_timedOut) {
                finish(QStringLiteral("Pre-upload command timed out: %1").arg(formatCommand(m_argv)));
                return;
            }
            const QString stdErr = QString::fromUtf8(m_process.readAllStandardError()).trimmed();
            const QString stdOut = QString::fromUtf8(m_process.readAllStandardOutput()).trimmed();
            if (status != QProcess::NormalExit || exitCode != 0) {
                const QString details = !stdErr.isEmpty() ? stdErr : stdOut;
                finish(details.isEmpty() ? QStringLiteral("Pre-upload command failed: %1").arg(formatCommand(m_argv)) : details);
                return;
            }
            runNextCommand();
        });
        QTimer::singleShot(0, this, [this, config, filePath]() { prepare(config, filePath); });
    }

    ~ProcessingTask() override
    {
        // Stop the writer before the owned temporary directory is removed.
        m_process.disconnect(this);
        if (m_process.state() != QProcess::NotRunning) {
            m_process.kill();
            m_process.waitForFinished(1000);
        }
    }

private:
    void prepare(const ParsedPreUploadConfig &config, const QString &filePath)
    {
        m_result.uploadPath = filePath;
        if (config.rules.isEmpty()) {
            finish();
            return;
        }
        const QString mimeType = QMimeDatabase{}.mimeTypeForFile(filePath, QMimeDatabase::MatchContent).name();
        bool foundRule = false;
        for (const auto &rule : config.rules) {
            for (const auto &pattern : rule.mimePatterns) {
                if (mimeMatchesPattern(mimeType, pattern)) {
                    m_rule = rule;
                    foundRule = true;
                    break;
                }
            }
            if (foundRule) {
                break;
            }
        }
        if (!foundRule) {
            finish();
            return;
        }

        m_result.tempDir = std::make_shared<QTemporaryDir>(QDir::tempPath() + QStringLiteral("/plasma-share-preupload-XXXXXX"));
        if (!m_result.tempDir->isValid()) {
            finish(QStringLiteral("Failed to create temporary directory for pre-upload processing."));
            return;
        }
        m_result.tempDirPath = m_result.tempDir->path();
        const QString tempFilePath = QDir(m_result.tempDirPath).filePath(QFileInfo(filePath).fileName());
        if (m_rule.fileHandling == PreUploadFileHandling::InplaceCopy) {
            if (!QFile::copy(filePath, tempFilePath)) {
                finish(QStringLiteral("Failed to create temporary copy for %1").arg(filePath));
                return;
            }
            if (!QFile::setPermissions(tempFilePath, QFile::permissions(tempFilePath) | QFileDevice::WriteOwner)) {
                finish(QStringLiteral("Failed to make temporary upload copy writable."));
                return;
            }
            m_result.uploadPath = tempFilePath;
        } else if (m_rule.fileHandling == PreUploadFileHandling::OutputFile) {
            m_outFilePath = tempFilePath;
        } else {
            finish(QStringLiteral("Unsupported pre-upload fileHandling."));
            return;
        }
        runNextCommand();
    }

    void runNextCommand()
    {
        if (m_nextCommand >= m_rule.commands.size()) {
            if (m_rule.fileHandling == PreUploadFileHandling::OutputFile) {
                const QFileInfo outInfo(m_outFilePath);
                if (!outInfo.exists() || !outInfo.isFile()) {
                    finish(QStringLiteral("Pre-upload command did not create an output file."));
                    return;
                }
                m_result.uploadPath = outInfo.absoluteFilePath();
            }
            finish();
            return;
        }
        m_argv.clear();
        for (const auto &arg : m_rule.commands.at(m_nextCommand++).argv) {
            m_argv.append(substituteCommandArg(arg, m_result.uploadPath, m_outFilePath));
        }
        if (m_argv.isEmpty()) {
            finish(QStringLiteral("Pre-upload command is empty."));
            return;
        }
        m_timedOut = false;
        m_process.start(m_argv.first(), m_argv.mid(1));
        m_timeout.start(m_rule.timeoutMs > 0 ? m_rule.timeoutMs : kDefaultPreUploadTimeoutMs);
    }

    void finish(const QString &error = {})
    {
        if (!m_completed) {
            return;
        }
        m_timeout.stop();
        m_result.ok = error.isEmpty();
        m_result.errorMessage = error;
        if (!m_result.ok) {
            m_result.tempDir.reset();
            m_result.tempDirPath.clear();
        }
        auto completed = std::move(m_completed);
        auto result = std::move(m_result);
        deleteLater();
        completed(std::move(result));
    }

    std::function<void(PreUploadProcessor::Result)> m_completed;
    PreUploadProcessor::Result m_result;
    ParsedPreUploadRule m_rule;
    QString m_outFilePath;
    QStringList m_argv;
    int m_nextCommand = 0;
    bool m_timedOut = false;
    QProcess m_process;
    QTimer m_timeout;
};
}

QObject *PreUploadProcessor::preprocessFileAsync(const ParsedPreUploadConfig &config, const QString &filePath,
                                                QObject *context, std::function<void(Result)> completed)
{
    return new ProcessingTask(config, filePath, context, std::move(completed));
}

PreUploadProcessor::Result PreUploadProcessor::preprocessFile(const ParsedPreUploadConfig &config, const QString &filePath)
{
    QEventLoop loop;
    Result result;
    preprocessFileAsync(config, filePath, &loop, [&](Result prepared) {
        result = std::move(prepared);
        loop.quit();
    });
    loop.exec();
    return result;
}
