#include "sharejob.h"

#include <KNotification>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeDatabase>
#include <QNetworkReply>
#include <QProcess>
#include <QTemporaryDir>

namespace {
constexpr int kDefaultPreUploadTimeoutMs = 30000;

QStringList collectFilePaths(const QJsonObject &data)
{
    QStringList paths;

    const QJsonValue urlsValue = data.value(QStringLiteral("urls"));
    if (urlsValue.isArray()) {
        const QJsonArray urlsArray = urlsValue.toArray();
        for (const QJsonValue &value : urlsArray) {
            const QString urlText = value.toString();
            if (urlText.isEmpty()) {
                continue;
            }
            const QUrl url = QUrl::fromUserInput(urlText, QString(), QUrl::AssumeLocalFile);
            if (!url.isLocalFile()) {
                continue;
            }
            const QString path = url.toLocalFile();
            QFileInfo info(path);
            if (info.exists() && info.isFile()) {
                paths.append(info.absoluteFilePath());
            }
        }
    }

    if (!paths.isEmpty()) {
        return paths;
    }

    const QString singleUrlText = data.value(QStringLiteral("url")).toString();
    if (singleUrlText.isEmpty()) {
        return paths;
    }

    const QUrl url = QUrl::fromUserInput(singleUrlText, QString(), QUrl::AssumeLocalFile);
    if (!url.isLocalFile()) {
        return paths;
    }

    const QString path = url.toLocalFile();
    QFileInfo info(path);
    if (info.exists() && info.isFile()) {
        paths.append(info.absoluteFilePath());
    }

    return paths;
}

QJsonObject objectValue(const QJsonObject &parent, const char *key)
{
    const QJsonValue value = parent.value(QLatin1StringView(key));
    return value.isObject() ? value.toObject() : QJsonObject();
}

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
    QStringList quoted;
    quoted.reserve(argv.size());
    for (const QString &arg : argv) {
        QString escaped = arg;
        escaped.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
        escaped.replace(QLatin1Char('"'), QStringLiteral("\\\""));
        if (escaped.contains(QRegularExpression(QStringLiteral(R"([\s"])")))) {
            escaped = QStringLiteral("\"%1\"").arg(escaped);
        }
        quoted.append(escaped);
    }
    return quoted.join(QLatin1Char(' '));
}

QString runPreUploadCommand(const QStringList &argv, int timeoutMs)
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

ShareJob::ShareJob(const QByteArray &configJson, QObject *parent)
    : Purpose::Job(parent)
    , m_targetConfig(QJsonDocument::fromJson(configJson).object())
    , m_uploader(m_targetConfig)
{
}

void ShareJob::start()
{
    m_files = collectFilePaths(data());
    if (m_files.isEmpty()) {
        finishError(QStringLiteral("No local files found to upload."));
        return;
    }

    if (m_targetConfig.isEmpty()) {
        finishError(QStringLiteral("Missing upload target configuration."));
        return;
    }

    startNextUpload();
}

void ShareJob::startNextUpload()
{
    if (m_nextIndex >= m_files.size()) {
        QJsonObject output;
        output.insert(QStringLiteral("urls"), QJsonArray::fromStringList(m_uploadedUrls));
        if (!m_uploadedUrls.isEmpty()) {
            output.insert(QStringLiteral("url"), m_uploadedUrls.first());
        }
        setOutput(output);

        if (!m_uploadedUrls.isEmpty()) {
            const QString clipboardText = m_uploadedUrls.join(QStringLiteral("\n"));
            if (QClipboard *clipboard = QGuiApplication::clipboard()) {
                clipboard->setText(clipboardText, QClipboard::Clipboard);
            }
        }

        const int count = m_uploadedUrls.size();
        const QString title = QStringLiteral("%1 Upload").arg(m_uploader.displayName());
        const QString text = count == 1
            ? QStringLiteral("Uploaded 1 file. URL copied to clipboard.")
            : QStringLiteral("Uploaded %1 files. URLs copied to clipboard.").arg(count);
        KNotification::event(KNotification::Notification, title, text, QStringLiteral("image-x-generic"));

        cleanupTempArtifacts();
        emitResult();
        return;
    }

    const QString originalPath = m_files.at(m_nextIndex);
    const PreparedUpload prepared = preprocessFile(originalPath);
    if (!prepared.ok) {
        finishError(prepared.errorMessage);
        return;
    }

    QNetworkReply *reply = m_uploader.upload(prepared.uploadPath, &m_network);
    if (!reply) {
        finishError(QStringLiteral("Failed to start upload for %1").arg(originalPath));
        return;
    }

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const QNetworkReply::NetworkError error = reply->error();
        if (error != QNetworkReply::NoError) {
            const QString message = reply->errorString();
            reply->deleteLater();
            finishError(message);
            return;
        }

        const UploadResult result = m_uploader.parseReply(reply);
        reply->deleteLater();

        if (!result.ok) {
            finishError(result.errorMessage);
            return;
        }

        m_uploadedUrls.append(result.url);
        ++m_nextIndex;
        startNextUpload();
    });
}

void ShareJob::finishError(const QString &message)
{
    setError(1);
    setErrorText(message);
    KNotification::event(KNotification::Error,
                         QStringLiteral("%1 Upload Failed").arg(m_uploader.displayName()),
                         message,
                         QStringLiteral("dialog-error"));
    cleanupTempArtifacts();
    emitResult();
}

ShareJob::PreparedUpload ShareJob::preprocessFile(const QString &filePath)
{
    PreparedUpload result;
    result.ok = true;
    result.uploadPath = filePath;

    const QJsonArray preUploadRules = arrayValue(m_targetConfig, "preUpload");
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
        bool matched = false;
        for (const QJsonValue &patternValue : mimePatterns) {
            const QString pattern = patternValue.toString();
            if (!pattern.isEmpty() && mimeMatchesPattern(mimeType, pattern)) {
                matched = true;
                break;
            }
        }

        if (matched) {
            selectedRule = rule;
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
    const QString tempDirPath = tempDir.path();
    const QString tempFilePath = QDir(tempDirPath).filePath(fileInfo.fileName());
    QString outFilePath;

    if (fileHandling == QLatin1StringView("inplace_copy")) {
        if (!QFile::copy(filePath, tempFilePath)) {
            result.ok = false;
            result.errorMessage = QStringLiteral("Failed to create temporary copy for %1").arg(filePath);
            QDir(tempDirPath).removeRecursively();
            return result;
        }
        result.uploadPath = tempFilePath;
    } else if (fileHandling == QLatin1StringView("output_file")) {
        outFilePath = QDir(tempDirPath).filePath(fileInfo.fileName());
    } else {
        result.ok = false;
        result.errorMessage = QStringLiteral("Unsupported pre-upload fileHandling.");
        QDir(tempDirPath).removeRecursively();
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

        const QString commandError = runPreUploadCommand(argv, timeoutMs);
        if (!commandError.isEmpty()) {
            result.ok = false;
            result.errorMessage = commandError;
            QDir(tempDirPath).removeRecursively();
            return result;
        }
    }

    if (fileHandling == QLatin1StringView("output_file")) {
        QFileInfo outInfo(outFilePath);
        if (!outInfo.exists() || !outInfo.isFile()) {
            result.ok = false;
            result.errorMessage = QStringLiteral("Pre-upload command did not create an output file.");
            QDir(tempDirPath).removeRecursively();
            return result;
        }
        result.uploadPath = outInfo.absoluteFilePath();
    }

    m_tempDirs.append(tempDirPath);
    return result;
}

void ShareJob::cleanupTempArtifacts()
{
    for (const QString &path : std::as_const(m_tempDirs)) {
        QDir(path).removeRecursively();
    }
    m_tempDirs.clear();
}
