#include "targettestpanel.h"
#include "configwidgets.h"
#include "targetfilestore.h"

#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

TargetTestPanel::TargetTestPanel(TargetEditor *editor, CredentialStore *credentials, QWidget *parent)
    : QWidget(parent), m_editor(editor), m_runner(new UploadTestRunner(this, credentials))
{
    auto *layout = new QVBoxLayout(this);
    auto *help = new QLabel(tr("Validate and response parsing stay local. Preview runs preprocessing commands. Upload test file sends the sample to the target's endpoint."), this); help->setWordWrap(true); layout->addWidget(help);
    auto *actions = new QHBoxLayout;
    auto button = [&](const QString &text, auto action) {
        auto *b = new QPushButton(text, this); actions->addWidget(b); connect(b, &QPushButton::clicked, this, action); return b;
    };
    auto *validate = button(tr("Validate"), [this]() { validateDraft(); }); validate->setObjectName(QStringLiteral("validateTarget"));
    auto *preview = button(tr("Preview preprocessing"), [this]() { if (validDraft()) { m_preview.clear(); m_openPreview->setEnabled(false); m_runner->start(m_editor->document(), m_file->text(), true); } });
    auto *upload = button(tr("Upload test file"), [this]() {
        if (!validDraft()) return;
        m_preview.clear(); m_openPreview->setEnabled(false);
        log(tr("Request definition (credentials concealed):\n%1").arg(ConfigJson::text(TargetFileStore::portable(m_editor->document()).value(QStringLiteral("request")))));
        m_runner->start(m_editor->document(), m_file->text(), false);
    }); upload->setObjectName(QStringLiteral("uploadTestFile"));
    auto *cancel = button(tr("Cancel"), [this]() { m_runner->cancel(); }); cancel->setEnabled(false);
    layout->addLayout(actions);
    auto *fileRow = new QHBoxLayout;
    m_file = new QLineEdit(this); m_file->setPlaceholderText(tr("Leave empty to generate a small PNG test image")); fileRow->addWidget(m_file);
    auto *browse = new QPushButton(tr("Choose file..."), this); fileRow->addWidget(browse);
    connect(browse, &QPushButton::clicked, this, [this]() { const auto path = QFileDialog::getOpenFileName(this, tr("Test file")); if (!path.isEmpty()) m_file->setText(path); });
    layout->addLayout(fileRow);
    m_progress = new QProgressBar(this); m_progress->setRange(0, 100); m_progress->setValue(0); layout->addWidget(m_progress);
    auto *results = m_results = new QTabWidget(this);
    layout->addWidget(results, 1);
    auto *responsePage = new QWidget(results);
    auto *responseLayout = new QVBoxLayout(responsePage);
    results->addTab(responsePage, tr("Response inspector"));
    auto *form = new QFormLayout;
    m_responseUrl = new QLineEdit(this); m_responseUrl->setPlaceholderText(QStringLiteral("https://example.invalid/upload"));
    m_status = new QSpinBox(this); m_status->setRange(100, 599); m_status->setValue(200);
    form->addRow(tr("Response URL"), m_responseUrl); form->addRow(tr("HTTP status"), m_status);
    m_headers = new StringTable(true, this); form->addRow(tr("Response headers"), m_headers); responseLayout->addLayout(form);
    auto *splitter = new QSplitter(this);
    m_response = new QPlainTextEdit(this); m_response->setObjectName(QStringLiteral("testResponse"));
    m_response->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_response->setPlaceholderText(tr("Paste a response body here, or capture one with Upload test file."));
    m_tree = new QTreeWidget(this); m_tree->setHeaderLabels({tr("JSON value"), tr("Value")});
    m_tree->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    splitter->addWidget(m_response); splitter->addWidget(m_tree); splitter->setMinimumHeight(160); responseLayout->addWidget(splitter, 1);
    connect(m_response, &QPlainTextEdit::textChanged, this, &TargetTestPanel::rebuildTree);
    auto *parserRow = new QHBoxLayout;
    auto *parse = new QPushButton(tr("Test response parsing"), this); parse->setObjectName(QStringLiteral("testResponseParsing"));
    parserRow->addWidget(parse); connect(parse, &QPushButton::clicked, this, &TargetTestPanel::parseResponse);
    m_output = new QComboBox(this); m_output->addItem(tr("Shared URL"), QString{}); m_output->addItem(tr("Thumbnail"), QStringLiteral("thumbnail"));
    m_output->addItem(tr("Deletion"), QStringLiteral("deletion")); m_output->addItem(tr("Error"), QStringLiteral("error")); parserRow->addWidget(m_output);
    auto *use = new QPushButton(tr("Use selected JSON value"), this); parserRow->addWidget(use);
    connect(use, &QPushButton::clicked, this, [this]() {
        auto *item = m_tree->currentItem(); if (!m_editor->isEditable()) { log(tr("Customize the preset before changing its response extractor.")); return; }
        if (!item || !item->data(0, Qt::UserRole).isValid() || !m_editor->hasObject()) return;
        QStringList path{QStringLiteral("response")}; if (!m_output->currentData().toString().isEmpty()) path.append(m_output->currentData().toString());
        auto type = path; type.append(QStringLiteral("type")); auto pointer = path; pointer.append(QStringLiteral("pointer"));
        m_editor->setField(type, QStringLiteral("json_pointer")); m_editor->setField(pointer, item->data(0, Qt::UserRole).toString());
        m_editor->setBytes(m_editor->bytes());
    });
    responseLayout->addLayout(parserRow);
    m_log = new QPlainTextEdit(this); m_log->setReadOnly(true); m_log->setObjectName(QStringLiteral("testLog"));
    m_log->setMaximumBlockCount(2000); m_log->setMinimumHeight(100); results->addTab(m_log, tr("Diagnostics"));
    connect(m_log, &QPlainTextEdit::textChanged, this, [this, results]() { results->setCurrentWidget(m_log); });
    auto *bottom = new QHBoxLayout;
    m_openPreview = new QPushButton(tr("Open prepared file"), this); m_openPreview->setEnabled(false); bottom->addWidget(m_openPreview);
    connect(m_openPreview, &QPushButton::clicked, this, [this]() { if (!m_preview.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(m_preview)); });
    auto *copy = new QPushButton(tr("Copy diagnostics"), this); bottom->addWidget(copy);
    connect(copy, &QPushButton::clicked, this, [this]() { QGuiApplication::clipboard()->setText(m_log->toPlainText()); });
    auto *clear = new QPushButton(tr("Clear"), this); bottom->addWidget(clear); connect(clear, &QPushButton::clicked, m_log, &QPlainTextEdit::clear);
    bottom->addStretch(); layout->addLayout(bottom);
    connect(m_runner, &UploadTestRunner::message, this, &TargetTestPanel::log);
    connect(m_runner, &UploadTestRunner::progress, this, [this](qint64 sent, qint64 total) { m_progress->setRange(0, total > 0 ? 100 : 0); if (total > 0) m_progress->setValue(int(100.0 * sent / total)); });
    connect(m_runner, &UploadTestRunner::busyChanged, this, [this, upload, preview, cancel, browse](bool busy) {
        upload->setEnabled(!busy); preview->setEnabled(!busy); cancel->setEnabled(busy); browse->setEnabled(!busy); m_file->setEnabled(!busy);
        if (busy) { m_progress->setRange(0, 0); } else m_progress->setRange(0, 100);
        emit busyChanged(busy);
    });
    connect(m_runner, &UploadTestRunner::previewReady, this, [this](const QString &path) { m_preview = path; m_openPreview->setEnabled(true); log(tr("Preview ready; no network upload was performed.")); });
    connect(m_runner, &UploadTestRunner::completed, this, [this](const UploadResult &result) {
        m_responseUrl->setText(result.responseInfo.responseUrl); m_status->setValue(result.responseInfo.statusCode ? result.responseInfo.statusCode : 200);
        m_headers->setValue(result.responseInfo.headers); m_response->setPlainText(result.responseInfo.responseText);
        log(ConfigJson::text(result.toJson()));
        if (result.ok) m_progress->setValue(100);
    });
    connect(m_runner, &UploadTestRunner::completed, this, [results, responsePage]() { results->setCurrentWidget(responsePage); });
    m_file->setObjectName(QStringLiteral("testFile"));
}

void TargetTestPanel::log(const QString &message) { m_log->appendPlainText(message.left(128 * 1024)); }
bool TargetTestPanel::validDraft()
{
    const auto errors = m_editor->problems(); if (errors.isEmpty()) return true;
    log(errors.join(QLatin1Char('\n'))); return false;
}
void TargetTestPanel::validateDraft()
{
    if (!validDraft()) return;
    ParsedTargetConfig config; TargetConfigParser::parse(m_editor->document(), &config);
    QStringList messages{tr("Configuration valid. No upload or command was executed.")};
    const auto missing = CredentialStore::missingEnvironment(config.request);
    if (!missing.isEmpty()) messages.append(tr("Missing environment values in this process: %1").arg(missing.join(QStringLiteral(", "))));
    const auto keys = CredentialStore::walletKeys(config.request);
    if (!keys.isEmpty()) messages.append(tr("Wallet references: %1. Use Check credentials to verify wallet access.").arg(keys.join(QStringLiteral(", "))));
    for (const auto &rule : config.preUpload.rules) for (const auto &command : rule.commands)
        if (!command.argv.isEmpty() && QStandardPaths::findExecutable(command.argv.first()).isEmpty()) messages.append(tr("Program not found: %1").arg(command.argv.first()));
    const auto endpoint = config.request.url;
    if (endpoint.contains(QStringLiteral("example.invalid"))) messages.append(tr("Replace the example endpoint before uploading."));
    log(messages.join(QLatin1Char('\n')));
}
void TargetTestPanel::parseResponse()
{
    if (!validDraft()) return;
    if (!m_headers->error().isEmpty()) { log(m_headers->error()); return; }
    if (m_response->toPlainText().size() > 1024 * 1024) { log(tr("Response fixtures are limited to 1 MiB.")); return; }
    UploadResponseInfo response;
    response.statusCode = m_status->value(); response.responseUrl = m_responseUrl->text(); response.headers = m_headers->value().toObject(); response.responseText = m_response->toPlainText();
    TargetUploader uploader(m_editor->document());
    ParsedTargetConfig config; TargetConfigParser::parse(m_editor->document(), &config);
    log(CredentialStore::redact(ConfigJson::text(uploader.parseResponse(response).toJson()), CredentialStore::secretValues(config.request, {})));
}

void TargetTestPanel::rebuildTree()
{
    m_tree->clear(); if (m_response->toPlainText().size() > 1024 * 1024) return;
    QJsonValue root; if (!ConfigJson::parse(m_response->toPlainText(), &root, nullptr)) return;
    int remaining = 1000;
    std::function<void(QTreeWidgetItem *, const QString &, const QJsonValue &, const QString &, int)> add;
    add = [&](QTreeWidgetItem *parent, const QString &name, const QJsonValue &value, const QString &pointer, int depth) {
        if (--remaining < 0 || depth > 40) return;
        auto *item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tree);
        item->setText(0, name);
        if (value.isString()) { item->setText(1, value.toString().left(200)); item->setData(0, Qt::UserRole, pointer); }
        else if (!value.isObject() && !value.isArray()) item->setText(1, ConfigJson::text(value));
        if (value.isObject()) {
            const auto object = value.toObject();
            for (auto it = object.begin(); it != object.end() && remaining > 0; ++it) { QString token = it.key(); token.replace(QStringLiteral("~"), QStringLiteral("~0")).replace(QStringLiteral("/"), QStringLiteral("~1")); add(item, it.key(), it.value(), pointer + QLatin1Char('/') + token, depth + 1); }
        } else if (value.isArray()) {
            const auto array = value.toArray(); for (int i = 0; i < array.size() && remaining > 0; ++i) add(item, QString::number(i), array.at(i), pointer + QLatin1Char('/') + QString::number(i), depth + 1);
        }
    };
    add(nullptr, tr("Response"), root, {}, 0); m_tree->expandToDepth(2);
}

void TargetTestPanel::reset()
{
    if (busy()) m_runner->cancel();
    m_response->clear(); m_headers->setValue(QJsonObject{}); m_status->setValue(200); m_log->clear();
    m_responseUrl->setText(m_editor->document().value(QStringLiteral("request")).toObject().value(QStringLiteral("url")).toString());
    m_preview.clear(); m_openPreview->setEnabled(false); m_progress->setValue(0);
    m_results->setCurrentIndex(0);
}
