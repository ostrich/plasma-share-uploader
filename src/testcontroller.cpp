#include "testcontroller.h"
#include "jsonutils.h"
#include "targetfilestore.h"
#include <QClipboard>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QJsonArray>
#include <QStandardPaths>

namespace {
constexpr int PointerRole = Qt::UserRole + 1;
}
TestController::TestController(TargetDraft* draft, CredentialStore* credentials, QObject* parent)
    : QObject(parent)
    , m_draft(draft)
    , m_runner(this, credentials)
    , m_headers(this)
    , m_tree(this)
{
    m_headers.setMap(true);
    connect(&m_runner, &UploadTestRunner::message, this, &TestController::log);
    connect(&m_runner, &UploadTestRunner::busyChanged, this, [this](bool busy) {
        if (busy)
            m_progress = -1;
        else if (m_progress < 0)
            m_progress = 0;
        emit busyChanged();
        emit changed();
    });
    connect(&m_runner, &UploadTestRunner::progress, this, [this](qint64 sent, qint64 total) {
        m_progress = total > 0 ? double(sent) / double(total) : -1;
        emit changed();
    });
    connect(&m_runner, &UploadTestRunner::previewReady, this, [this](const QString& path) {
        m_preview = path;
        log(tr("Preview ready; no network upload was performed."));
    });
    connect(&m_runner, &UploadTestRunner::completed, this, [this](const UploadResult& result) {
        m_url = result.responseInfo.responseUrl;
        m_status = result.responseInfo.statusCode ? result.responseInfo.statusCode : 200;
        m_headers.setValue(result.responseInfo.headers.toVariantMap());
        setResponseBody(result.responseInfo.responseText);
        if (result.ok)
            m_progress = 1;
        log(ConfigJson::text(result.toJson()));
        emit captureCompleted();
    });
}
void TestController::log(const QString& text)
{
    if (!m_log.isEmpty())
        m_log += QLatin1Char('\n');
    m_log += text.left(128 * 1024);
    m_log = m_log.right(512 * 1024).section(QLatin1Char('\n'), -2000);
    emit changed();
}
bool TestController::valid()
{
    const auto errors = m_draft->problems();
    if (!errors.isEmpty()) {
        log(errors.join(QLatin1Char('\n')));
        return false;
    }
    return true;
}
void TestController::reset()
{
    if (busy())
        cancel();
    m_headers.setValue(QVariantMap { });
    m_status = 200;
    m_log.clear();
    m_preview.clear();
    m_progress = 0;
    m_url = m_draft->stringValue({ QStringLiteral("request"), QStringLiteral("url") });
    setResponseBody({ });
    emit changed();
}
void TestController::validate()
{
    if (!valid())
        return;
    ParsedTargetConfig config;
    TargetConfigParser::parse(m_draft->document(), &config);
    QStringList messages { tr("Configuration valid. No upload or command was executed.") };
    const auto missing = CredentialStore::missingEnvironment(config.request);
    if (!missing.isEmpty())
        messages.append(tr("Missing environment values in this process: %1").arg(missing.join(QStringLiteral(", "))));
    const auto keys = CredentialStore::walletKeys(config.request);
    if (!keys.isEmpty())
        messages.append(tr("Wallet references: %1. Use Check credentials to verify wallet access.")
                .arg(keys.join(QStringLiteral(", "))));
    for (const auto& rule : config.preUpload.rules)
        for (const auto& command : rule.commands)
            if (!command.argv.isEmpty() && QStandardPaths::findExecutable(command.argv.first()).isEmpty())
                messages.append(tr("Program not found: %1").arg(command.argv.first()));
    if (config.request.url.contains(QStringLiteral("example.invalid")))
        messages.append(tr("Replace the example endpoint before uploading."));
    log(messages.join(QLatin1Char('\n')));
}
void TestController::start(bool previewOnly)
{
    if (busy() || !valid())
        return;
    m_preview.clear();
    emit changed();
    if (!previewOnly)
        log(tr("Request definition (credentials concealed):\n%1")
                .arg(
                    ConfigJson::text(TargetFileStore::portable(m_draft->document()).value(QStringLiteral("request")))));
    m_runner.start(m_draft->document(), m_file, previewOnly);
}
void TestController::parseResponse()
{
    if (!valid())
        return;
    if (!m_headers.error().isEmpty()) {
        log(m_headers.error());
        return;
    }
    if (m_body.size() > 1024 * 1024) {
        log(tr("Response fixtures are limited to 1 MiB."));
        return;
    }
    UploadResponseInfo info;
    info.responseUrl = m_url;
    info.statusCode = m_status;
    info.headers = m_headers.jsonValue().toObject();
    info.responseText = m_body;
    ParsedTargetConfig config;
    TargetConfigParser::parse(m_draft->document(), &config);
    TargetUploader uploader(config);
    log(CredentialStore::redact(
        ConfigJson::text(uploader.parseResponse(info).toJson()), CredentialStore::secretValues(config.request, { })));
}
void TestController::setResponseBody(const QString& value)
{
    m_body = value;
    rebuildTree();
    emit responseChanged();
}
void TestController::rebuildTree()
{
    m_tree.clear();
    m_tree.setHorizontalHeaderLabels({ tr("JSON value"), tr("Value") });
    if (m_body.size() > 1024 * 1024)
        return;
    QJsonValue root;
    if (!ConfigJson::parse(m_body, &root, nullptr))
        return;
    int remaining = 1000;
    std::function<void(QStandardItem*, const QString&, const QJsonValue&, const QString&, int)> add;
    add = [&](QStandardItem* parent, const QString& name, const QJsonValue& value, const QString& pointer, int depth) {
        if (--remaining < 0 || depth > 40)
            return;
        auto* item = new QStandardItem(name);
        auto* text = new QStandardItem(value.isString() ? value.toString().left(200)
                : value.isObject() || value.isArray()   ? QString { }
                                                        : ConfigJson::text(value));
        if (value.isString()) {
            item->setData(pointer, PointerRole);
            text->setData(pointer, PointerRole);
        }
        parent->appendRow({ item, text });
        if (value.isObject()) {
            const auto object = value.toObject();
            for (auto it = object.begin(); it != object.end() && remaining > 0; ++it) {
                auto token = it.key();
                token.replace(QStringLiteral("~"), QStringLiteral("~0"))
                    .replace(QStringLiteral("/"), QStringLiteral("~1"));
                add(item, it.key(), it.value(), pointer + QLatin1Char('/') + token, depth + 1);
            }
        } else if (value.isArray()) {
            const auto array = value.toArray();
            for (int i = 0; i < array.size() && remaining > 0; ++i)
                add(item, QString::number(i), array.at(i), pointer + QLatin1Char('/') + QString::number(i), depth + 1);
        }
    };
    add(m_tree.invisibleRootItem(), tr("Response"), root, { }, 0);
}
void TestController::usePointer(const QModelIndex& index, const QString& output)
{
    if (!m_draft->editable() || !index.isValid() || index.model() != &m_tree || !index.data(PointerRole).isValid())
        return;
    if (!output.isEmpty() && output != QLatin1StringView("thumbnail") && output != QLatin1StringView("deletion")
        && output != QLatin1StringView("error"))
        return;
    QStringList path { QStringLiteral("response"), output.isEmpty() ? QStringLiteral("url") : output };
    auto extractor = ConfigJson::get(m_draft->document(), path).toObject();
    extractor.insert(QStringLiteral("type"), QStringLiteral("json_pointer"));
    extractor.insert(QStringLiteral("pointer"), index.data(PointerRole).toString());
    m_draft->setField(path, extractor);
}
void TestController::openPreview()
{
    if (!m_preview.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_preview));
}
void TestController::copyDiagnostics() { QGuiApplication::clipboard()->setText(m_log); }
