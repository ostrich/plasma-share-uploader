#include "credentialpanel.h"
#include "targetconfigparser.h"
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

CredentialPanel::CredentialPanel(TargetEditor *editor, CredentialStore *store, QWidget *parent)
    : QWidget(parent), m_editor(editor), m_store(store)
{
    auto *layout = new QVBoxLayout(this);
    auto *help = new QLabel(tr("Anonymous targets need no credentials. Secrets are stored in KWallet; JSON contains ${WALLET:name} references. Environment references remain available in Request. Credentials are shared by name, including across duplicated targets."), this);
    help->setWordWrap(true); layout->addWidget(help);
    m_references = new QLabel(this); m_references->setWordWrap(true); layout->addWidget(m_references);
    m_controls = new QWidget(this); auto *form = new QFormLayout(m_controls);
    m_mode = new QComboBox(this); m_mode->addItems({tr("API key"), tr("Bearer token"), tr("Basic authentication")}); form->addRow(tr("Authentication"), m_mode);
    m_location = new QComboBox(this); m_location->addItems({tr("Header"), tr("Query parameter"), tr("Multipart field"), tr("URL encoded form field"), tr("JSON object field")}); form->addRow(tr("Place in"), m_location);
    m_field = new QLineEdit(QStringLiteral("Authorization"), this); form->addRow(tr("Field name"), m_field);
    m_key = new QLineEdit(this); form->addRow(tr("Wallet credential name"), m_key);
    m_username = new QLineEdit(this); form->addRow(tr("Username"), m_username);
    m_secret = new QLineEdit(this); m_secret->setEchoMode(QLineEdit::Password); m_secret->setObjectName(QStringLiteral("credentialSecret")); form->addRow(tr("Secret / password"), m_secret);
    connect(m_mode, &QComboBox::currentIndexChanged, this, [this, form]() {
        form->setRowVisible(m_username, m_mode->currentIndex() == 2);
        if (m_mode->currentIndex() > 0) { m_location->setCurrentIndex(0); m_field->setText(QStringLiteral("Authorization")); }
    });
    form->setRowVisible(m_username, false);
    auto *buttons = new QHBoxLayout;
    auto add = [&](const QString &title, auto callback) { auto *b = new QPushButton(title, this); buttons->addWidget(b); connect(b, &QPushButton::clicked, this, callback); };
    add(tr("Store secret and use"), [this]() { bind(true); });
    add(tr("Use existing credential"), [this]() { bind(false); });
    form->addRow(buttons);
    auto *removeBinding = new QPushButton(tr("Remove this request field"), this); form->addRow(removeBinding);
    connect(removeBinding, &QPushButton::clicked, this, [this]() {
        if (!m_editor->hasObject() || m_field->text().isEmpty()) return;
        m_editor->setField(bindingPath(), QJsonValue(QJsonValue::Undefined)); m_editor->setBytes(m_editor->bytes());
        m_result->setText(tr("Request field removed from the draft. The stored credential is retained."));
    });
    auto *forget = new QPushButton(tr("Forget stored credential..."), this); form->addRow(forget);
    connect(forget, &QPushButton::clicked, this, [this]() {
        const QString key = m_key->text();
        if (!CredentialStore::validKey(key)) { m_result->setText(tr("Enter a valid wallet credential name.")); return; }
        if (QMessageBox::question(this, tr("Forget credential"), tr("Remove '%1' from KWallet? Other targets referencing this name will need a replacement.").arg(key)) != QMessageBox::Yes) return;
        m_busy = true; setEditable(m_editable);
        m_store->remove(key, this, [this](const QString &error) { m_busy = false; setEditable(m_editable); m_result->setText(error.isEmpty() ? tr("Stored credential removed.") : error); });
    });
    layout->addWidget(m_controls);
    auto *check = new QPushButton(tr("Check credentials"), this); layout->addWidget(check);
    connect(check, &QPushButton::clicked, this, [this, check]() {
        ParsedTargetConfig config;
        if (!TargetConfigParser::parse(m_editor->document(), &config)) { m_result->setText(tr("Fix configuration errors first.")); return; }
        const auto missing = CredentialStore::missingEnvironment(config.request);
        if (!missing.isEmpty()) { m_result->setText(tr("Missing environment values: %1").arg(missing.join(QStringLiteral(", ")))); return; }
        const auto draft = m_editor->bytes(); check->setEnabled(false);
        m_store->read(CredentialStore::walletKeys(config.request), this, [this, check, draft](CredentialStore::Values, const QString &error) {
            check->setEnabled(true);
            m_result->setText(draft != m_editor->bytes() ? tr("The draft changed; check its credentials again.") : error.isEmpty() ? tr("Referenced credentials are available. No upload performed.") : error);
        });
    });
    m_result = new QLabel(this); m_result->setWordWrap(true); m_result->setTextInteractionFlags(Qt::TextSelectableByMouse); layout->addWidget(m_result); layout->addStretch();
    connect(editor, &TargetEditor::changed, this, [this]() {
        ParsedTargetConfig parsed; TargetConfigParser::parse(m_editor->document(), &parsed);
        const auto keys = CredentialStore::walletKeys(parsed.request);
        m_references->setText(tr("Wallet references in draft: %1").arg(keys.isEmpty() ? tr("none") : keys.join(QStringLiteral(", "))));
    });
}

QStringList CredentialPanel::bindingPath() const
{
    QStringList path{QStringLiteral("request")};
    switch (m_location->currentIndex()) {
    case 0: path.append(QStringLiteral("headers")); break;
    case 1: path.append(QStringLiteral("query")); break;
    case 2: path.append({QStringLiteral("multipart"), QStringLiteral("fields")}); break;
    case 3: path.append({QStringLiteral("formUrlencoded"), QStringLiteral("fields")}); break;
    case 4: path.append({QStringLiteral("json"), QStringLiteral("fields")}); break;
    }
    path.append(m_field->text()); return path;
}

void CredentialPanel::bind(bool saveSecret)
{
    if (!m_editable || m_busy) return;
    if (!m_editor->hasObject()) { m_result->setText(tr("Repair the target JSON first.")); return; }
    const QString key = m_key->text();
    if (!CredentialStore::validKey(key) || m_field->text().isEmpty()) { m_result->setText(tr("Enter a field name and a valid credential name.")); return; }
    const auto request = m_editor->document().value(QStringLiteral("request")).toObject();
    const auto type = request.value(QStringLiteral("type")).toString(QStringLiteral("multipart"));
    const auto location = m_location->currentIndex();
    if ((location == 2 && type != QLatin1StringView("multipart")) || (location == 3 && type != QLatin1StringView("form_urlencoded")) ||
        (location == 4 && (type != QLatin1StringView("json") || !request.value(QStringLiteral("json")).toObject().value(QStringLiteral("fields")).isObject()))) {
        m_result->setText(tr("Choose a placement matching the request body format. JSON placement requires an object body.")); return;
    }
    QString value = m_secret->text();
    if (saveSecret && value.isEmpty()) { m_result->setText(tr("Enter a nonempty secret.")); return; }
    if (saveSecret && m_mode->currentIndex() == 2 && m_username->text().contains(QLatin1Char(':'))) {
        m_result->setText(tr("A basic-auth username cannot contain a colon.")); return;
    }
    QString reference = QStringLiteral("${WALLET:%1}").arg(key);
    if (m_mode->currentIndex() == 1) reference.prepend(QStringLiteral("Bearer "));
    if (m_mode->currentIndex() == 2) { reference.prepend(QStringLiteral("Basic ")); value = QString::fromLatin1((m_username->text() + QLatin1Char(':') + value).toUtf8().toBase64()); }
    const auto draft = m_editor->bytes(); const auto path = bindingPath();
    auto apply = [this, draft, path, reference](const QString &error) {
        m_busy = false; setEditable(m_editable);
        if (!error.isEmpty()) { m_result->setText(error); return; }
        m_secret->clear();
        if (draft != m_editor->bytes()) { m_result->setText(tr("Credential ready, but the draft changed. Use existing credential to bind it to the intended target.")); return; }
        m_editor->setField(path, reference); m_editor->setBytes(m_editor->bytes());
        m_result->setText(tr("Credential reference added to the draft. Save the target to apply it."));
    };
    m_busy = true; setEditable(m_editable);
    if (saveSecret) m_store->write(key, value, this, apply);
    else m_store->read({key}, this, [apply](CredentialStore::Values, const QString &error) { apply(error); });
}

void CredentialPanel::setEditable(bool editable)
{
    m_editable = editable;
    m_controls->setEnabled(editable && !m_busy);
}

void CredentialPanel::reset()
{
    m_secret->clear(); m_username->clear(); m_result->clear();
    ParsedTargetConfig parsed; TargetConfigParser::parse(m_editor->document(), &parsed);
    const auto keys = CredentialStore::walletKeys(parsed.request);
    m_references->setText(tr("Wallet references in draft: %1").arg(keys.isEmpty() ? tr("none") : keys.join(QStringLiteral(", "))));
    m_key->setText(keys.isEmpty() ? m_editor->document().value(QStringLiteral("id")).toString() + QStringLiteral(".api") : keys.first());
}
