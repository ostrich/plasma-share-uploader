#include "configwidgets.h"

#include <QComboBox>
#include <QEvent>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QResizeEvent>
#include <QSet>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

ElidedLabel::ElidedLabel(QWidget *parent, Qt::TextElideMode mode) : QLabel(parent), m_mode(mode)
{
    setTextFormat(Qt::PlainText);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
}

void ElidedLabel::setText(const QString &text)
{
    m_fullText = text;
    setToolTip(text);
    updateText();
}

QSize ElidedLabel::sizeHint() const { return {0, fontMetrics().height()}; }
QSize ElidedLabel::minimumSizeHint() const { return sizeHint(); }
void ElidedLabel::updateText() { QLabel::setText(fontMetrics().elidedText(m_fullText.simplified(), m_mode, contentsRect().width())); }
void ElidedLabel::resizeEvent(QResizeEvent *event) { QLabel::resizeEvent(event); updateText(); }
void ElidedLabel::changeEvent(QEvent *event)
{
    QLabel::changeEvent(event);
    if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange) { updateGeometry(); updateText(); }
}

QString ConfigJson::text(const QJsonValue &value)
{
    if (value.isObject()) return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Indented));
    if (value.isArray()) return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Indented));
    const auto wrapped = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(wrapped.mid(1, wrapped.size() - 2));
}

bool ConfigJson::parse(const QString &text, QJsonValue *value, QString *error)
{
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(QByteArray("[") + text.toUtf8() + QByteArray("]"), &parseError);
    if (parseError.error != QJsonParseError::NoError || document.array().size() != 1) {
        if (error) *error = QStringLiteral("Enter one JSON value: %1").arg(parseError.errorString());
        return false;
    }
    if (value) *value = document.array().first();
    return true;
}

QJsonValue ConfigJson::set(QJsonValue root, const QStringList &path, const QJsonValue &value)
{
    if (path.isEmpty()) return value;
    auto object = root.toObject();
    if (path.size() == 1 && value.isUndefined()) object.remove(path.first());
    else object.insert(path.first(), set(object.value(path.first()), path.mid(1), value));
    return object;
}

StringTable::StringTable(bool map, QWidget *parent) : QWidget(parent), m_table(new QTableWidget(this)), m_map(map)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_table->setColumnCount(map ? 2 : 1);
    m_table->setHorizontalHeaderLabels(map ? QStringList{tr("Name"), tr("Value")} : QStringList{tr("Value")});
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setMinimumHeight(80);
    m_table->setMaximumHeight(110);
    layout->addWidget(m_table);
    auto *buttons = new QHBoxLayout;
    auto add = [&](const QString &text, auto callback) {
        auto *button = new QPushButton(text, this);
        buttons->addWidget(button);
        connect(button, &QPushButton::clicked, this, callback);
    };
    add(tr("Add"), [this]() {
        m_loading = true;
        const auto row = m_table->rowCount();
        m_table->insertRow(row);
        for (int c = 0; c < m_table->columnCount(); ++c) m_table->setItem(row, c, new QTableWidgetItem);
        m_loading = false;
        m_table->setCurrentCell(row, 0);
        m_table->editItem(m_table->item(row, 0));
        emit changed();
    });
    add(tr("Remove"), [this]() { if (m_table->currentRow() >= 0) { m_loading = true; m_table->removeRow(m_table->currentRow()); m_loading = false; emit changed(); } });
    if (!map) {
        add(tr("Up"), [this]() { moveRow(-1); });
        add(tr("Down"), [this]() { moveRow(1); });
    }
    buttons->addStretch();
    layout->addLayout(buttons);
    connect(m_table, &QTableWidget::itemChanged, this, [this]() { if (!m_loading) emit changed(); });
}

void StringTable::setValue(const QJsonValue &value)
{
    m_loading = true;
    m_table->setRowCount(0);
    auto append = [this](const QString &name, const QJsonValue &entry) {
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        if (m_map) m_table->setItem(row, 0, new QTableWidgetItem(name));
        auto *item = new QTableWidgetItem(entry.isString() ? entry.toString() : ConfigJson::text(entry));
        m_table->setItem(row, m_map ? 1 : 0, item);
    };
    if (m_map) {
        const auto object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it) append(it.key(), it.value());
    } else for (const auto &entry : value.toArray()) append({}, entry);
    m_loading = false;
}

QJsonValue StringTable::value() const
{
    QJsonObject object;
    QJsonArray array;
    for (int row = 0; row < m_table->rowCount(); ++row) {
        if (m_map) object.insert(m_table->item(row, 0)->text(), m_table->item(row, 1)->text());
        else array.append(m_table->item(row, 0)->text());
    }
    return m_map ? QJsonValue(object) : QJsonValue(array);
}

QString StringTable::error() const
{
    if (!m_map) return {};
    QSet<QString> names;
    for (int row = 0; row < m_table->rowCount(); ++row) {
        const auto name = m_table->item(row, 0)->text();
        if (name.isEmpty()) return tr("Field names cannot be empty.");
        if (names.contains(name)) return tr("Duplicate field name: %1").arg(name);
        names.insert(name);
    }
    return {};
}

void StringTable::moveRow(int delta)
{
    const auto row = m_table->currentRow();
    if (row < 0 || row + delta < 0 || row + delta >= m_table->rowCount()) return;
    m_loading = true;
    auto *item = m_table->takeItem(row, 0);
    m_table->setItem(row, 0, m_table->takeItem(row + delta, 0));
    m_table->setItem(row + delta, 0, item);
    m_table->setCurrentCell(row + delta, 0);
    m_loading = false;
    emit changed();
}

ResponseEditor::ResponseEditor(bool optional, QWidget *parent) : QWidget(parent)
{
    auto *layout = new QFormLayout(this);
    m_type = new QComboBox(this);
    if (optional) m_type->addItem(tr("Not configured"), QString{});
    const QList<QPair<QString, QString>> types{{tr("Plain URL"), QStringLiteral("text_url")},
        {tr("JSON pointer"), QStringLiteral("json_pointer")}, {tr("Regular expression"), QStringLiteral("regex")},
        {tr("Response header"), QStringLiteral("header")}, {tr("Redirect URL"), QStringLiteral("redirect_url")},
        {tr("XML path"), QStringLiteral("xml_xpath")}};
    for (const auto &[title, type] : types) m_type->addItem(title, type);
    layout->addRow(tr("Extract using"), m_type);
    auto field = [&](const QString &title, const QString &key) {
        auto *edit = new QLineEdit(this);
        edit->setObjectName(key);
        layout->addRow(title, edit);
        connect(edit, &QLineEdit::textEdited, this, [this, edit, key]() {
            if (!m_loading) { m_value.insert(key, edit->text()); emit changed(); }
        });
        return edit;
    };
    m_pointer = field(tr("JSON pointer"), QStringLiteral("pointer"));
    m_pointer->setPlaceholderText(QStringLiteral("/files/0/url"));
    m_pattern = field(tr("Pattern"), QStringLiteral("pattern"));
    m_header = field(tr("Header name"), QStringLiteral("name"));
    m_xpath = field(tr("XML path"), QStringLiteral("xpath"));
    m_group = new QSpinBox(this);
    m_group->setRange(0, 100000);
    layout->addRow(tr("Capture group (0 = whole match)"), m_group);
    connect(m_group, &QSpinBox::valueChanged, this, [this](int value) {
        if (!m_loading) { m_value.insert(QStringLiteral("group"), value); emit changed(); }
    });
    connect(m_type, &QComboBox::currentIndexChanged, this, [this]() {
        if (!m_loading) { m_value.insert(QStringLiteral("type"), m_type->currentData().toString()); visibility(); emit changed(); }
    });
    setValue(optional ? QJsonValue(QJsonValue::Undefined) : QJsonValue(QJsonObject{{QStringLiteral("type"), QStringLiteral("text_url")}}));
}

void ResponseEditor::visibility()
{
    const auto type = m_type->currentData().toString();
    auto *form = qobject_cast<QFormLayout *>(layout());
    form->setRowVisible(m_pointer, type == QLatin1StringView("json_pointer"));
    form->setRowVisible(m_pattern, type == QLatin1StringView("regex"));
    form->setRowVisible(m_group, type == QLatin1StringView("regex"));
    form->setRowVisible(m_header, type == QLatin1StringView("header"));
    form->setRowVisible(m_xpath, type == QLatin1StringView("xml_xpath"));
}

void ResponseEditor::setValue(const QJsonValue &value)
{
    m_loading = true;
    m_value = value.toObject();
    m_type->setCurrentIndex(m_type->findData(m_value.value(QStringLiteral("type")).toString()));
    m_pointer->setText(m_value.value(QStringLiteral("pointer")).toString());
    m_pattern->setText(m_value.value(QStringLiteral("pattern")).toString());
    m_header->setText(m_value.value(QStringLiteral("name")).toString());
    m_xpath->setText(m_value.value(QStringLiteral("xpath")).toString());
    m_group->setValue(m_value.value(QStringLiteral("group")).toInt(1));
    visibility();
    m_loading = false;
}

QJsonValue ResponseEditor::value() const
{
    return m_type->currentData().toString().isEmpty() ? QJsonValue(QJsonValue::Undefined) : QJsonValue(m_value);
}

PreUploadEditor::PreUploadEditor(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    auto *help = new QLabel(tr("The first matching MIME rule runs. Commands use separate arguments, without a shell. Originals remain unchanged."), this);
    help->setWordWrap(true);
    layout->addWidget(help);
    m_rules = new QListWidget(this);
    m_rules->setMaximumHeight(115);
    layout->addWidget(m_rules);
    auto *ruleButtons = new QHBoxLayout;
    auto button = [&](QHBoxLayout *row, const QString &text, auto callback) {
        auto *b = new QPushButton(text, this); row->addWidget(b); connect(b, &QPushButton::clicked, this, callback);
    };
    button(ruleButtons, tr("Add rule"), [this]() {
        m_value.append(QJsonObject{{QStringLiteral("mime"), QJsonArray{QStringLiteral("image/jpeg"), QStringLiteral("image/tiff")}},
            {QStringLiteral("fileHandling"), QStringLiteral("inplace_copy")},
            {QStringLiteral("commands"), QJsonArray{QJsonObject{{QStringLiteral("argv"), QJsonArray{QStringLiteral("exiv2"), QStringLiteral("rm"), QStringLiteral("${FILE}")}}}}}});
        refreshRules(m_value.size() - 1); emit changed();
    });
    button(ruleButtons, tr("Remove rule"), [this]() { const int row = m_rules->currentRow(); if (row >= 0) { m_value.removeAt(row); refreshRules(qMin(row, int(m_value.size()) - 1)); emit changed(); } });
    for (int delta : {-1, 1}) button(ruleButtons, delta < 0 ? tr("Up") : tr("Down"), [this, delta]() {
        int row = m_rules->currentRow(); if (row < 0 || row + delta < 0 || row + delta >= m_value.size()) return;
        const auto item = m_value.at(row); m_value[row] = m_value.at(row + delta); m_value[row + delta] = item;
        refreshRules(row + delta); emit changed();
    });
    ruleButtons->addStretch(); layout->addLayout(ruleButtons);
    m_details = new QWidget(this);
    auto *details = new QFormLayout(m_details);
    m_mimes = new StringTable(false, this);
    details->addRow(tr("MIME patterns"), m_mimes);
    m_handling = new QComboBox(this);
    m_handling->addItem(tr("Modify a temporary copy"), QStringLiteral("inplace_copy"));
    m_handling->addItem(tr("Write a separate output file"), QStringLiteral("output_file"));
    details->addRow(tr("File handling"), m_handling);
    m_timeout = new QSpinBox(this); m_timeout->setRange(1, 2147483647); m_timeout->setSuffix(tr(" ms"));
    details->addRow(tr("Timeout per command"), m_timeout);
    m_commands = new QListWidget(this); m_commands->setMaximumHeight(100);
    details->addRow(tr("Commands, in order"), m_commands);
    auto *commandButtons = new QHBoxLayout;
    button(commandButtons, tr("Add command"), [this]() {
        const int row = m_rules->currentRow(); if (row < 0) return;
        auto commands = m_value.at(row).toObject().value(QStringLiteral("commands")).toArray();
        commands.append(QJsonObject{{QStringLiteral("argv"), QJsonArray{QStringLiteral("program"), QStringLiteral("${FILE}")}}});
        changeCommands(commands, commands.size() - 1);
    });
    button(commandButtons, tr("Remove command"), [this]() {
        const int row = m_rules->currentRow(), command = m_commands->currentRow(); if (row < 0 || command < 0) return;
        auto commands = m_value.at(row).toObject().value(QStringLiteral("commands")).toArray();
        commands.removeAt(command); changeCommands(commands, qMin(command, int(commands.size()) - 1));
    });
    for (int delta : {-1, 1}) button(commandButtons, delta < 0 ? tr("Up") : tr("Down"), [this, delta]() {
        int row = m_rules->currentRow(), command = m_commands->currentRow(); if (row < 0 || command < 0) return;
        auto commands = m_value.at(row).toObject().value(QStringLiteral("commands")).toArray();
        if (command + delta < 0 || command + delta >= commands.size()) return;
        const auto item = commands.at(command); commands[command] = commands.at(command + delta); commands[command + delta] = item;
        changeCommands(commands, command + delta);
    });
    details->addRow(commandButtons);
    m_arguments = new StringTable(false, this);
    details->addRow(tr("Program, then each argument"), m_arguments);
    auto *tokens = new QLabel(tr("Use ${FILE} for the input; output-file rules also require ${OUT_FILE}."), this); tokens->setWordWrap(true); details->addRow(tokens);
    layout->addWidget(m_details);
    connect(m_rules, &QListWidget::currentRowChanged, this, [this]() { showRule(); });
    connect(m_commands, &QListWidget::currentRowChanged, this, [this]() { showCommand(); });
    connect(m_mimes, &StringTable::changed, this, [this]() { changeRule(QStringLiteral("mime"), m_mimes->value()); });
    connect(m_handling, &QComboBox::currentIndexChanged, this, [this]() { changeRule(QStringLiteral("fileHandling"), m_handling->currentData().toString()); });
    connect(m_timeout, &QSpinBox::valueChanged, this, [this](int v) { changeRule(QStringLiteral("timeoutMs"), v); });
    connect(m_arguments, &StringTable::changed, this, [this]() {
        const int row = m_rules->currentRow(), command = m_commands->currentRow();
        if (m_loading || row < 0 || command < 0) return;
        auto rule = m_value.at(row).toObject(); auto commands = rule.value(QStringLiteral("commands")).toArray();
        auto object = commands.at(command).toObject(); object.insert(QStringLiteral("argv"), m_arguments->value()); commands[command] = object;
        rule.insert(QStringLiteral("commands"), commands); m_value[row] = rule;
        const auto args = m_arguments->value().toArray();
        m_commands->item(command)->setText(args.isEmpty() ? tr("Incomplete command") : args.first().toString());
        emit changed();
    });
    setValue({});
}

void PreUploadEditor::refreshRules(int selected)
{
    { QSignalBlocker blocker(m_rules); m_rules->clear();
      for (const auto &rule : m_value) {
          QStringList mimes; for (const auto &v : rule.toObject().value(QStringLiteral("mime")).toArray()) mimes.append(v.toString());
          m_rules->addItem(mimes.isEmpty() ? tr("Incomplete rule") : mimes.join(QStringLiteral(", ")));
      }
      m_rules->setCurrentRow(selected);
    }
    showRule();
}

void PreUploadEditor::setValue(const QJsonArray &value) { m_value = value; refreshRules(value.isEmpty() ? -1 : 0); }

void PreUploadEditor::showRule()
{
    m_loading = true;
    const int row = m_rules->currentRow();
    m_details->setEnabled(row >= 0);
    const auto rule = row < 0 ? QJsonObject{} : m_value.at(row).toObject();
    m_mimes->setValue(rule.value(QStringLiteral("mime")));
    m_handling->setCurrentIndex(m_handling->findData(rule.value(QStringLiteral("fileHandling")).toString()));
    m_timeout->setValue(rule.value(QStringLiteral("timeoutMs")).toInt(30000));
    m_commands->clear();
    for (const auto &command : rule.value(QStringLiteral("commands")).toArray()) {
        const auto args = command.toObject().value(QStringLiteral("argv")).toArray();
        m_commands->addItem(args.isEmpty() ? tr("Incomplete command") : args.first().toString());
    }
    m_commands->setCurrentRow(m_commands->count() ? 0 : -1);
    m_loading = false; showCommand();
}

void PreUploadEditor::showCommand()
{
    if (m_loading) return;
    const int row = m_rules->currentRow(), command = m_commands->currentRow();
    m_arguments->setEnabled(row >= 0 && command >= 0);
    const auto commands = row < 0 ? QJsonArray{} : m_value.at(row).toObject().value(QStringLiteral("commands")).toArray();
    m_arguments->setValue(command < 0 ? QJsonArray{} : commands.at(command).toObject().value(QStringLiteral("argv")).toArray());
}

void PreUploadEditor::changeRule(const QString &key, const QJsonValue &value)
{
    const int row = m_rules->currentRow(); if (m_loading || row < 0) return;
    auto rule = m_value.at(row).toObject(); rule.insert(key, value); m_value[row] = rule;
    if (key == QLatin1StringView("mime")) {
        QStringList mimes; for (const auto &v : value.toArray()) mimes.append(v.toString());
        m_rules->item(row)->setText(mimes.join(QStringLiteral(", ")));
    }
    emit changed();
}

void PreUploadEditor::changeCommands(const QJsonArray &commands, int selected)
{
    changeRule(QStringLiteral("commands"), commands);
    showRule(); m_commands->setCurrentRow(selected);
}
