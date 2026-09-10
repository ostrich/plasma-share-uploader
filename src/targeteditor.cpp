#include "targeteditor.h"
#include "configwidgets.h"
#include "credentialstore.h"
#include "targetconfigparser.h"
#include "targetfilestore.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QVBoxLayout>

TargetEditor::TargetEditor(QWidget *parent) : QWidget(parent), m_tabs(new QTabWidget(this)),
    m_raw(new QPlainTextEdit(this)), m_status(new ElidedLabel(this))
{
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_tabs);
    auto *statusRow = new QHBoxLayout;
    m_status->setObjectName(QStringLiteral("targetStatus"));
    statusRow->addWidget(m_status, 1);
    m_details = new QPushButton(tr("Details..."), this);
    m_details->setObjectName(QStringLiteral("targetDiagnostics"));
    statusRow->addWidget(m_details);
    layout->addLayout(statusRow);
    connect(m_details, &QPushButton::clicked, this, [this]() {
        QDialog dialog(this); dialog.setWindowTitle(tr("Target diagnostics")); dialog.resize(720, 420);
        auto *layout = new QVBoxLayout(&dialog);
        auto *text = new QPlainTextEdit(m_diagnosticText, &dialog); text->setReadOnly(true);
        text->setObjectName(QStringLiteral("targetDiagnosticText")); layout->addWidget(text);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog); layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        dialog.exec();
    });
    QFormLayout *general;
    page(tr("General"), &general);
    line(general, tr("Name"), {QStringLiteral("displayName")});
    line(general, tr("Description"), {QStringLiteral("description")});
    line(general, tr("Icon name or URL"), {QStringLiteral("icon")});
    auto *id = line(general, tr("Unique ID"), {QStringLiteral("id")});
    id->setToolTip(tr("Lowercase letters, digits, underscores and dashes; starts with a letter or digit."));
    table(general, tr("Constraints"), {QStringLiteral("constraints")}, false);
    auto *constraintHelp = new QLabel(tr("For example: mimeType:image/* . Every selected file must match."), this); constraintHelp->setWordWrap(true);
    general->addRow(constraintHelp);
    table(general, tr("File extensions"), {QStringLiteral("extensions")}, false);
    table(general, tr("Plugin types (compatibility field)"), {QStringLiteral("pluginTypes")}, false);

    QFormLayout *request;
    page(tr("Request"), &request);
    line(request, tr("Upload endpoint"), {QStringLiteral("request"), QStringLiteral("url")});
    auto *method = new QComboBox(this); method->addItems({QStringLiteral("POST"), QStringLiteral("PUT")});
    request->addRow(tr("Method"), method);
    m_refresh.append([this, method]() { method->setCurrentText(field({QStringLiteral("request"), QStringLiteral("method")}).toString(QStringLiteral("POST"))); });
    connect(method, &QComboBox::currentTextChanged, this, [this](const QString &value) { if (!m_loading) setField({QStringLiteral("request"), QStringLiteral("method")}, value); });
    auto *type = new QComboBox(this);
    type->addItem(tr("Multipart form"), QStringLiteral("multipart"));
    type->addItem(tr("Raw file"), QStringLiteral("raw"));
    type->addItem(tr("URL encoded form"), QStringLiteral("form_urlencoded"));
    type->addItem(tr("JSON"), QStringLiteral("json"));
    request->addRow(tr("Body format"), type);
    auto *fileField = line(request, tr("File field name"), {QStringLiteral("request"), QStringLiteral("multipart"), QStringLiteral("fileField")});
    auto *multipart = table(request, tr("Multipart fields"), {QStringLiteral("request"), QStringLiteral("multipart"), QStringLiteral("fields")}, true);
    auto *contentType = line(request, tr("Raw content type"), {QStringLiteral("request"), QStringLiteral("contentType")});
    auto *formFields = table(request, tr("Form fields"), {QStringLiteral("request"), QStringLiteral("formUrlencoded"), QStringLiteral("fields")}, true);
    auto *jsonBody = new QPlainTextEdit(this); jsonBody->setObjectName(QStringLiteral("jsonBody"));
    jsonBody->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    jsonBody->setMinimumHeight(150); request->addRow(tr("JSON body (any JSON value)"), jsonBody);
    m_refresh.append([this, jsonBody]() { jsonBody->setPlainText(ConfigJson::text(field({QStringLiteral("request"), QStringLiteral("json"), QStringLiteral("fields")}))); });
    connect(jsonBody, &QPlainTextEdit::textChanged, this, [this, jsonBody]() {
        if (m_loading) return;
        QJsonValue value; QString error;
        if (ConfigJson::parse(jsonBody->toPlainText(), &value, &error)) {
            m_formErrors.remove(jsonBody); setField({QStringLiteral("request"), QStringLiteral("json"), QStringLiteral("fields")}, value);
        } else { m_formErrors[jsonBody] = tr("JSON body: %1").arg(error); updateStatus(); emit changed(); }
    });
    auto visibility = [type, request, fileField, multipart, contentType, formFields, jsonBody]() {
        const auto value = type->currentData().toString();
        request->setRowVisible(fileField, value == QLatin1StringView("multipart"));
        request->setRowVisible(multipart, value == QLatin1StringView("multipart"));
        request->setRowVisible(contentType, value == QLatin1StringView("raw"));
        request->setRowVisible(formFields, value == QLatin1StringView("form_urlencoded"));
        request->setRowVisible(jsonBody, value == QLatin1StringView("json"));
    };
    m_refresh.append([this, type, visibility]() { type->setCurrentIndex(type->findData(field({QStringLiteral("request"), QStringLiteral("type")}).toString(QStringLiteral("multipart")))); visibility(); });
    connect(type, &QComboBox::currentIndexChanged, this, [this, type, visibility]() {
        visibility(); if (m_loading) return;
        const auto selected = type->currentData().toString();
        setField({QStringLiteral("request"), QStringLiteral("type")}, selected);
        if (selected == QLatin1StringView("multipart") && !field({QStringLiteral("request"), QStringLiteral("multipart")}).isObject())
            setField({QStringLiteral("request"), QStringLiteral("multipart")}, QJsonObject{{QStringLiteral("fileField"), QStringLiteral("file")}, {QStringLiteral("fields"), QJsonObject{}}});
        if (selected == QLatin1StringView("form_urlencoded") && !field({QStringLiteral("request"), QStringLiteral("formUrlencoded")}).isObject())
            setField({QStringLiteral("request"), QStringLiteral("formUrlencoded")}, QJsonObject{{QStringLiteral("fields"), QJsonObject{}}});
        if (selected == QLatin1StringView("json") && !field({QStringLiteral("request"), QStringLiteral("json")}).isObject())
            setField({QStringLiteral("request"), QStringLiteral("json")}, QJsonObject{{QStringLiteral("fields"), QJsonObject{}}});
        refreshForms();
    });
    table(request, tr("Headers"), {QStringLiteral("request"), QStringLiteral("headers")}, true);
    table(request, tr("Query parameters"), {QStringLiteral("request"), QStringLiteral("query")}, true);
    auto *help = new QLabel(tr("Values support ${FILENAME}, ${ENV:NAME}, and ${WALLET:credential-name}. Multipart uploads require POST. Use Credentials to store secrets outside the JSON."), this);
    help->setWordWrap(true); request->addRow(help);

    QFormLayout *response;
    page(tr("Response"), &response);
    const QList<QPair<QString, QString>> outputs{{tr("Shared URL"), QString{}}, {tr("Thumbnail URL"), QStringLiteral("thumbnail")},
        {tr("Deletion URL"), QStringLiteral("deletion")}, {tr("Error message"), QStringLiteral("error")}};
    for (const auto &[title, name] : outputs) {
        auto *group = new QGroupBox(title, this); auto *groupLayout = new QVBoxLayout(group);
        auto *editor = new ResponseEditor(!name.isEmpty(), group); groupLayout->addWidget(editor); response->addRow(group);
        QStringList path{QStringLiteral("response")}; if (!name.isEmpty()) path.append(name);
        m_refresh.append([this, editor, path]() { editor->setValue(field(path)); });
        connect(editor, &ResponseEditor::changed, this, [this, editor, path, name]() {
            if (m_loading) return;
            if (name.isEmpty()) {
                // The success extractor shares its object with the optional outputs.
                auto value = editor->value().toObject(); const auto current = field(path).toObject();
                for (const auto &key : {QStringLiteral("thumbnail"), QStringLiteral("deletion"), QStringLiteral("error")}) {
                    if (current.contains(key)) value.insert(key, current.value(key)); else value.remove(key);
                }
                setField(path, value);
            } else setField(path, editor->value());
        });
    }
    QFormLayout *preprocessing;
    page(tr("Before Upload"), &preprocessing);
    auto *pre = new PreUploadEditor(this); preprocessing->addRow(pre);
    m_refresh.append([this, pre]() { pre->setValue(field({QStringLiteral("preUpload")}).toArray()); });
    connect(pre, &PreUploadEditor::changed, this, [this, pre]() { if (!m_loading) setField({QStringLiteral("preUpload")}, pre->value()); });

    m_jsonPage = new QWidget(this); auto *rawLayout = new QVBoxLayout(m_jsonPage);
    auto *rawHelp = new QLabel(tr("Advanced JSON edits update the forms. Unknown fields are preserved. Save explicitly to apply changes."), this); rawHelp->setWordWrap(true);
    rawLayout->addWidget(rawHelp); rawLayout->addWidget(m_raw);
    m_raw->setObjectName(QStringLiteral("targetJson")); m_raw->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_tabs->addTab(m_jsonPage, tr("JSON"));
    connect(m_raw, &QPlainTextEdit::textChanged, this, [this]() {
        if (m_loading) return;
        const auto doc = QJsonDocument::fromJson(bytes());
        if (doc.isObject()) { m_document = doc.object(); m_formErrors.clear(); refreshForms(); }
        for (auto *page : m_formPages) page->setEnabled(doc.isObject() && m_editable);
        updateStatus(); emit changed();
    });
}

QWidget *TargetEditor::page(const QString &title, QFormLayout **form)
{
    auto *scroll = new QScrollArea(this); scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
    auto *widget = new QWidget(scroll); *form = new QFormLayout(widget);
    (*form)->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow); (*form)->setRowWrapPolicy(QFormLayout::WrapLongRows);
    scroll->setWidget(widget); m_tabs->addTab(scroll, title); m_formPages.append(widget); return widget;
}

QJsonValue TargetEditor::field(const QStringList &path) const
{
    QJsonValue value(m_document); for (const auto &key : path) value = value.toObject().value(key); return value;
}

QLineEdit *TargetEditor::line(QFormLayout *form, const QString &label, const QStringList &path)
{
    auto *edit = new QLineEdit(this); edit->setObjectName(path.join(QLatin1Char('.'))); form->addRow(label, edit);
    m_refresh.append([this, edit, path]() { edit->setText(field(path).toString()); });
    connect(edit, &QLineEdit::textEdited, this, [this, edit, path]() { if (!m_loading) setField(path, edit->text()); }); return edit;
}

StringTable *TargetEditor::table(QFormLayout *form, const QString &label, const QStringList &path, bool map)
{
    auto *edit = new StringTable(map, this); edit->setObjectName(path.join(QLatin1Char('.'))); form->addRow(label, edit);
    m_refresh.append([this, edit, path]() { edit->setValue(field(path)); });
    connect(edit, &StringTable::changed, this, [this, edit, path, label]() {
        if (m_loading) return;
        const auto error = edit->error();
        if (error.isEmpty()) { m_formErrors.remove(edit); setField(path, edit->value()); }
        else { m_formErrors[edit] = label + QStringLiteral(": ") + error; updateStatus(); emit changed(); }
    }); return edit;
}

void TargetEditor::setBytes(const QByteArray &bytes)
{
    m_loading = true; m_raw->setPlainText(QString::fromUtf8(bytes)); m_loading = false;
    m_formErrors.clear(); const auto doc = QJsonDocument::fromJson(bytes);
    m_document = doc.object(); refreshForms();
    for (auto *page : m_formPages) page->setEnabled(doc.isObject() && m_editable);
    if (!doc.isObject()) showJson(); updateStatus();
}

QByteArray TargetEditor::bytes() const { return m_raw->toPlainText().toUtf8(); }
bool TargetEditor::hasObject() const { return QJsonDocument::fromJson(bytes()).isObject(); }

void TargetEditor::setField(const QStringList &path, const QJsonValue &value)
{
    if (!m_editable) return;
    m_document = ConfigJson::set(m_document, path, value).toObject();
    { QSignalBlocker blocker(m_raw); m_raw->setPlainText(ConfigJson::text(m_document)); }
    updateStatus(); emit changed();
}

void TargetEditor::refreshForms()
{
    m_loading = true; for (const auto &refresh : m_refresh) refresh(); m_loading = false;
}

QStringList TargetEditor::problems() const { auto errors = TargetFileStore::validate(bytes()); errors.append(m_formErrors.values()); return errors; }
void TargetEditor::updateStatus()
{
    auto errors = problems() + m_contextProblems;
    errors.removeDuplicates();
    QStringList details;
    if (!m_notice.isEmpty()) details.append(m_notice);
    details.append(errors);
    m_diagnosticText = details.join(QStringLiteral("\n\n"));
    m_status->setText(errors.isEmpty()
        ? (m_notice.isEmpty() ? tr("Configuration valid. Check upload compatibility in Test.") : m_notice)
        : errors.size() == 1 ? tr("Needs attention: 1 issue") : tr("Needs attention: %1 issues").arg(errors.size()));
    m_status->setToolTip(m_diagnosticText.isEmpty() ? m_status->toolTip() : m_diagnosticText);
    m_details->setEnabled(!details.isEmpty());
}

void TargetEditor::setContextDiagnostics(const QStringList &problems, const QString &notice)
{
    m_contextProblems = problems; m_notice = notice; updateStatus();
}
void TargetEditor::showJson() { m_tabs->setCurrentWidget(m_jsonPage); }

void TargetEditor::setEditable(bool editable)
{
    m_editable = editable;
    m_raw->setReadOnly(!editable);
    for (auto *page : m_formPages) page->setEnabled(editable && hasObject());
}
