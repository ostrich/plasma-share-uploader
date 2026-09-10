#include "targetmanagerwindow.h"
#include "configwidgets.h"
#include "credentialpanel.h"
#include "targeteditor.h"
#include "targettestpanel.h"
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
constexpr int hasErrorsRole = Qt::UserRole;

class TargetListDelegate final : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
protected:
    void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override
    {
        QStyledItemDelegate::initStyleOption(option, index);
        if (!index.data(hasErrorsRole).toBool()) return;
        // Some themes recolor the error background on selection while leaving
        // its white cross unchanged. Keep the normal error artwork in both modes.
        const auto pixmap = option->icon.pixmap(option->decorationSize,
            option->widget ? option->widget->devicePixelRatioF() : 1.0, QIcon::Normal);
        option->icon = QIcon(pixmap);
        option->icon.addPixmap(pixmap, QIcon::Selected);
    }
};

QString kindName(const TargetFileStore::Entry &entry)
{
    using Kind = TargetFileStore::Kind;
    if (entry.kind == Kind::Preset) return QObject::tr("Available preset");
    if (entry.kind == Kind::Template) return QObject::tr("Setup template");
    if (entry.kind == Kind::Disabled) return QObject::tr("Disabled");
    return entry.linked ? QObject::tr("Enabled · linked target") : QObject::tr("Enabled · custom");
}
}

TargetManagerWindow::TargetManagerWindow(QString bundled, QString active, QWidget *parent, CredentialStore *credentials)
    : QMainWindow(parent), m_store(std::move(bundled), std::move(active))
{
    setWindowTitle(tr("Upload Targets")); setWindowIcon(QIcon::fromTheme(QStringLiteral("document-send"))); resize(1180, 860);
    auto *splitter = new QSplitter(this); setCentralWidget(splitter);
    auto *left = new QWidget(splitter); auto *leftLayout = new QVBoxLayout(left);
    m_search = new QLineEdit(this); m_search->setPlaceholderText(tr("Search targets")); leftLayout->addWidget(m_search);
    m_filter = new QComboBox(this); m_filter->addItems({tr("Configured targets"), tr("Enabled"), tr("Disabled"), tr("Available presets and templates"), tr("Needs attention"), tr("All targets")}); leftLayout->addWidget(m_filter);
    m_list = new QListWidget(this); m_list->setObjectName(QStringLiteral("targetList")); m_list->setMinimumWidth(230); leftLayout->addWidget(m_list, 1);
    m_list->setItemDelegate(new TargetListDelegate(m_list));
    auto *add = new QToolButton(this); add->setText(tr("Add Target...")); add->setPopupMode(QToolButton::InstantPopup);
    auto *menu = new QMenu(add);
    menu->addAction(tr("New custom target"), this, &TargetManagerWindow::newTarget)->setObjectName(QStringLiteral("newTarget"));
    menu->addAction(tr("Packaged presets and templates"), this, [this]() { m_filter->setCurrentIndex(3); });
    menu->addAction(tr("Import JSON..."), this, &TargetManagerWindow::importTarget)->setObjectName(QStringLiteral("importTarget"));
    add->setMenu(menu); leftLayout->addWidget(add);
    auto *reload = new QPushButton(tr("Reload"), this); leftLayout->addWidget(reload);
    connect(reload, &QPushButton::clicked, this, [this]() { if (leaveDraft()) refresh(m_entry.path); });
    auto *folder = new QPushButton(tr("Open target folder"), this); leftLayout->addWidget(folder);
    connect(folder, &QPushButton::clicked, this, [this]() { QDesktopServices::openUrl(QUrl::fromLocalFile(m_store.activePath())); });

    auto *right = new QWidget(splitter); auto *rightLayout = new QVBoxLayout(right);
    auto *heading = new QVBoxLayout; heading->setSpacing(2);
    m_title = new ElidedLabel(this); auto font = m_title->font(); font.setPointSize(font.pointSize() + 4); font.setBold(true); m_title->setFont(font); heading->addWidget(m_title);
    m_state = new ElidedLabel(this); heading->addWidget(m_state);
    m_path = new ElidedLabel(this, Qt::ElideMiddle); m_path->setObjectName(QStringLiteral("targetPath")); heading->addWidget(m_path);
    rightLayout->addLayout(heading);
    auto *actions = new QHBoxLayout;
    auto button = [&](const QString &title, auto callback) { auto *b = new QPushButton(title, this); actions->addWidget(b); connect(b, &QPushButton::clicked, this, callback); return b; };
    m_toggle = button(tr("Enable"), [this]() { enableDisable(); }); m_toggle->setObjectName(QStringLiteral("toggleTarget"));
    m_customize = button(tr("Customize"), [this]() { customize(); });
    m_duplicate = button(tr("Duplicate"), [this]() { copyTarget(); });
    m_duplicate->setObjectName(QStringLiteral("duplicateTarget"));
    m_export = button(tr("Export..."), [this]() { exportTarget(); });
    m_export->setObjectName(QStringLiteral("exportTarget"));
    m_restore = button(tr("Restore preset..."), [this]() { restorePreset(); });
    m_delete = button(tr("Delete..."), [this]() { removeTarget(); }); actions->addStretch(); rightLayout->addLayout(actions);
    m_editor = new TargetEditor(this); rightLayout->addWidget(m_editor, 1);
    auto *store = credentials ? credentials : new CredentialStore(this);
    m_credentials = new CredentialPanel(m_editor, store, this);
    m_editor->tabs()->insertTab(2, m_credentials, tr("Credentials"));
    m_test = new TargetTestPanel(m_editor, store, this);
    auto *testScroll = new QScrollArea(this); testScroll->setFrameShape(QFrame::NoFrame); testScroll->setWidgetResizable(true); testScroll->setWidget(m_test);
    m_editor->tabs()->addTab(testScroll, tr("Test"));
    auto *bottom = new QHBoxLayout;
    m_open = new QPushButton(tr("Open JSON externally"), this); bottom->addWidget(m_open);
    connect(m_open, &QPushButton::clicked, this, [this]() {
        if (m_entry.editable() && !m_entry.linked && QFileInfo::exists(m_entry.path)) QDesktopServices::openUrl(QUrl::fromLocalFile(m_entry.path));
    });
    bottom->addStretch(); m_save = new QPushButton(tr("Save"), this); m_save->setObjectName(QStringLiteral("saveTarget")); bottom->addWidget(m_save);
    connect(m_save, &QPushButton::clicked, this, [this]() { save(); });
    rightLayout->addLayout(bottom);
    splitter->setStretchFactor(0, 0); splitter->setStretchFactor(1, 1); splitter->setSizes({270, 910});
    connect(m_editor, &TargetEditor::changed, this, [this]() { if (!m_loading) { m_dirty = true; updateActions(); } });
    connect(m_test, &TargetTestPanel::busyChanged, this, [this]() { if (!m_loading) updateActions(); });
    connect(m_list, &QListWidget::currentRowChanged, this, &TargetManagerWindow::selectRow);
    connect(m_search, &QLineEdit::textChanged, this, &TargetManagerWindow::filter);
    connect(m_filter, &QComboBox::currentIndexChanged, this, &TargetManagerWindow::filter);
    m_watcher = new QFileSystemWatcher(this);
    auto changed = [this]() {
        if (!m_entry.path.isEmpty()) {
            m_diskChanged = m_entry.revision != TargetFileStore::revision(m_entry.path);
            updateActions();
        }
    };
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, changed);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, changed);
    refresh();
}

void TargetManagerWindow::showError(const QString &message)
{
    if (!message.isEmpty()) QMessageBox::warning(this, tr("Upload Targets"), message);
}

void TargetManagerWindow::refresh(const QString &select)
{
    QStringList problems;
    m_entries = m_store.entries(&problems);
    QSignalBlocker blocker(m_list); m_list->clear(); m_selectedRow = -1;
    int selected = -1;
    for (int i = 0; i < m_entries.size(); ++i) {
        const auto &entry = m_entries.at(i);
        const auto name = entry.object.value(QStringLiteral("displayName")).toString(QFileInfo(entry.path).fileName());
        const auto host = QUrl(entry.object.value(QStringLiteral("request")).toObject().value(QStringLiteral("url")).toString()).host();
        auto *item = new QListWidgetItem(name + QLatin1Char('\n') + kindName(entry) + (host.isEmpty() ? QString{} : QStringLiteral(" · ") + host), m_list);
        item->setIcon(QIcon::fromTheme(entry.problems.isEmpty() ? QStringLiteral("document-send") : QStringLiteral("dialog-error")));
        item->setData(hasErrorsRole, !entry.problems.isEmpty());
        item->setToolTip(entry.path + QLatin1Char('\n') + entry.problems.join(QLatin1Char('\n')));
        item->setSizeHint(QSize(220, 58));
        if (entry.path == select) selected = i;
    }
    filter();
    if (selected < 0) for (int i = 0; i < m_list->count(); ++i) if (!m_list->item(i)->isHidden()) { selected = i; break; }
    if (selected >= 0 && m_list->item(selected)->isHidden()) { m_filter->setCurrentIndex(5); filter(); }
    m_list->setCurrentRow(selected); m_selectedRow = selected;
    if (selected >= 0) load(m_entries.at(selected));
    else {
        m_entry = {}; m_entry.path.clear(); m_editor->setBytes(QByteArray("{}")); m_editor->setEnabled(false);
        m_dirty = false; m_diskChanged = false; m_notice.clear(); updateActions();
        m_title->setText(tr("Add an upload target")); m_state->setText(tr("Choose Add Target to get started."));
        m_editor->setContextDiagnostics({}, tr("Enable a preset, import JSON, or create a custom target."));
    }
    statusBar()->showMessage(problems.isEmpty() ? tr("Targets reload on the next share.") : tr("%1 configuration diagnostics; affected targets are marked.").arg(problems.size()));
}

void TargetManagerWindow::filter()
{
    using Kind = TargetFileStore::Kind;
    for (int i = 0; i < m_entries.size(); ++i) {
        const auto &entry = m_entries.at(i); bool visible = true;
        switch (m_filter->currentIndex()) {
        case 0: visible = entry.editable(); break;
        case 1: visible = entry.kind == Kind::Active; break;
        case 2: visible = entry.kind == Kind::Disabled; break;
        case 3: visible = !entry.editable(); break;
        case 4: visible = !entry.problems.isEmpty(); break;
        }
        visible = visible && (m_list->item(i)->text().contains(m_search->text(), Qt::CaseInsensitive) || entry.path.contains(m_search->text(), Qt::CaseInsensitive));
        m_list->item(i)->setHidden(!visible);
    }
}

void TargetManagerWindow::selectRow(int row)
{
    if (m_loading || row == m_selectedRow) return;
    const auto path = row >= 0 && row < m_entries.size() ? m_entries.at(row).path : QString{};
    if (!leaveDraft()) { QSignalBlocker blocker(m_list); m_list->setCurrentRow(m_selectedRow); return; }
    // Saving a draft rebuilds and may reorder the list. Select by stable path.
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).path != path) continue;
        QSignalBlocker blocker(m_list); m_list->setCurrentRow(i); m_selectedRow = i;
        load(m_entries.at(i)); break;
    }
}

void TargetManagerWindow::load(const TargetFileStore::Entry &entry)
{
    m_loading = true; m_entry = entry; m_dirty = false; m_customizing = false;
    m_diskChanged = false; m_notice.clear();
    if (entry.revision.isEmpty()) {
        QSignalBlocker blocker(m_list); m_list->setCurrentRow(-1); m_selectedRow = -1;
    }
    m_editor->setEnabled(true); m_editor->setEditable(entry.editable() && !entry.linked); m_editor->setBytes(entry.bytes);
    m_credentials->setEditable(m_editor->isEditable()); m_credentials->reset(); m_test->reset();
    if (!m_watcher->files().isEmpty()) m_watcher->removePaths(m_watcher->files());
    if (!m_watcher->directories().isEmpty()) m_watcher->removePaths(m_watcher->directories());
    if (QFileInfo::exists(entry.path)) m_watcher->addPath(entry.path);
    if (QFileInfo(QFileInfo(entry.path).absolutePath()).isDir()) m_watcher->addPath(QFileInfo(entry.path).absolutePath());
    m_loading = false; updateActions();
}

bool TargetManagerWindow::leaveDraft()
{
    if (m_test->busy()) { showError(tr("Cancel the running test before switching targets or closing.")); return false; }
    if (!m_dirty) return true;
    const auto answer = QMessageBox::question(this, tr("Unsaved changes"), tr("Save changes to this target?"), QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Cancel) return false;
    if (answer == QMessageBox::Save) return save();
    const auto original = m_entry;
    load(original);
    return true;
}

bool TargetManagerWindow::save()
{
    if (m_test->busy()) { showError(tr("Cancel the running test before saving.")); return false; }
    if (!m_entry.editable() || m_entry.path.isEmpty()) return false;
    const auto problems = m_editor->problems();
    // Invalid JSON may be kept as a disabled draft. Invalid form intermediates
    // cannot be represented in JSON (e.g. duplicate table keys), so never save them.
    const auto rawProblems = TargetFileStore::validate(m_editor->bytes());
    if ((m_entry.kind == TargetFileStore::Kind::Active && !problems.isEmpty()) || problems != rawProblems) { showError(problems.join(QLatin1Char('\n'))); return false; }
    const auto error = m_store.save(m_entry.path, m_editor->bytes(), m_entry.revision, m_entry.kind == TargetFileStore::Kind::Disabled);
    if (!error.isEmpty()) { showError(error); return false; }
    m_dirty = false; refresh(m_entry.path); statusBar()->showMessage(tr("Target saved.")); return true;
}

void TargetManagerWindow::newTarget()
{
    if (!leaveDraft()) return;
    const auto id = m_store.uniqueId(QStringLiteral("new_target"));
    TargetFileStore::Entry entry;
    entry.kind = TargetFileStore::Kind::Disabled; entry.path = m_store.activePath() + QStringLiteral("/disabled/") + id + QStringLiteral(".json");
    entry.object = QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("displayName"), tr("New target")},
        {QStringLiteral("description"), tr("Custom upload target")}, {QStringLiteral("icon"), QStringLiteral("document-send")},
        {QStringLiteral("request"), QJsonObject{{QStringLiteral("url"), QStringLiteral("https://example.invalid/upload")}, {QStringLiteral("method"), QStringLiteral("POST")},
            {QStringLiteral("multipart"), QJsonObject{{QStringLiteral("fileField"), QStringLiteral("file")}, {QStringLiteral("fields"), QJsonObject{}}}}}},
        {QStringLiteral("response"), QJsonObject{{QStringLiteral("type"), QStringLiteral("text_url")}}}};
    entry.bytes = QJsonDocument(entry.object).toJson(); load(entry); m_dirty = true; updateActions();
    m_editor->tabs()->setCurrentIndex(0);
}

void TargetManagerWindow::copyTarget()
{
    if (!leaveDraft() || !m_editor->hasObject()) return;
    auto object = m_editor->document();
    const auto id = m_store.uniqueId(object.value(QStringLiteral("id")).toString()); object.insert(QStringLiteral("id"), id);
    object.insert(QStringLiteral("displayName"), object.value(QStringLiteral("displayName")).toString() + tr(" (copy)"));
    TargetFileStore::Entry entry; entry.kind = TargetFileStore::Kind::Disabled;
    entry.path = m_store.activePath() + QStringLiteral("/disabled/") + id + QStringLiteral(".json"); entry.object = object; entry.bytes = QJsonDocument(object).toJson();
    load(entry); m_dirty = true; updateActions(); statusBar()->showMessage(tr("Independent disabled draft. Credential references remain shared until changed."));
}

void TargetManagerWindow::customize()
{
    if (m_test->busy()) return;
    if (!m_entry.editable()) { copyTarget(); return; }
    if (m_entry.linked) {
        m_customizing = true; m_dirty = true; m_editor->setEditable(true); m_credentials->setEditable(true);
        m_notice = tr("Save will replace this link with an independent custom copy. The packaged file will stay untouched."); updateActions();
    }
}

void TargetManagerWindow::enableDisable()
{
    if (!leaveDraft()) return;
    QString error;
    if (m_entry.kind == TargetFileStore::Kind::Preset) error = m_store.enablePreset(m_entry.path);
    else if (m_entry.kind == TargetFileStore::Kind::Template) { copyTarget(); return; }
    else error = m_store.setEnabled(m_entry, m_entry.kind != TargetFileStore::Kind::Active);
    if (!error.isEmpty()) { showError(error); return; }
    refresh();
}

void TargetManagerWindow::removeTarget()
{
    if (!leaveDraft() || !m_entry.editable()) return;
    if (QMessageBox::question(this, tr("Delete target"), tr("Delete this target entry? Use Disable to retain a custom target. Stored credentials are retained.")) != QMessageBox::Yes) return;
    const auto error = m_store.remove(m_entry); if (!error.isEmpty()) showError(error); else refresh();
}

void TargetManagerWindow::restorePreset()
{
    if (!leaveDraft()) return;
    if (QMessageBox::question(this, tr("Restore packaged preset"), tr("Replace this custom definition with a link to the packaged preset? Its custom settings will be discarded.")) != QMessageBox::Yes) return;
    const auto error = m_store.restorePreset(m_entry); if (!error.isEmpty()) showError(error); else refresh(m_entry.path);
}

void TargetManagerWindow::importTarget()
{
    if (!leaveDraft()) return;
    const auto path = QFileDialog::getOpenFileName(this, tr("Import target JSON"), {}, tr("JSON targets (*.json)")); if (path.isEmpty()) return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 4 * 1024 * 1024) { showError(tr("Cannot read target, or it exceeds 4 MiB.")); return; }
    TargetFileStore::Entry entry; entry.kind = TargetFileStore::Kind::Disabled; entry.bytes = file.readAll(); entry.object = QJsonDocument::fromJson(entry.bytes).object();
    const auto id = m_store.uniqueId(entry.object.value(QStringLiteral("id")).toString(QFileInfo(path).completeBaseName()));
    if (!entry.object.isEmpty()) { entry.object.insert(QStringLiteral("id"), id); entry.bytes = QJsonDocument(entry.object).toJson(); }
    entry.path = m_store.activePath() + QStringLiteral("/disabled/") + id + QStringLiteral(".json");
    load(entry); m_dirty = true; updateActions(); m_editor->showJson();
    m_notice = tr("Imported as a disabled draft. Review the JSON, credentials, and preprocessing commands before enabling or testing it."); updateActions();
}

void TargetManagerWindow::exportTarget()
{
    if (!m_editor->hasObject()) { showError(tr("Repair the JSON object before exporting.")); return; }
    if (m_editor->problems() != TargetFileStore::validate(m_editor->bytes())) {
        showError(m_editor->problems().join(QLatin1Char('\n'))); return;
    }
    QStringList redacted; const auto object = TargetFileStore::portable(m_editor->document(), &redacted);
    QDialog dialog(this); dialog.setWindowTitle(tr("Review target export")); dialog.resize(760, 660);
    auto *layout = new QVBoxLayout(&dialog);
    auto *help = new QLabel(tr("Export contains a complete definition and credential references. Recognized inline secrets are replaced with environment references. Review for other private values before sharing.\nReplaced fields: %1").arg(redacted.isEmpty() ? tr("none") : redacted.join(QStringLiteral(", "))), &dialog); help->setWordWrap(true); layout->addWidget(help);
    auto *json = new QPlainTextEdit(ConfigJson::text(object), &dialog); layout->addWidget(json);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog); layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept); connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;
    if (!QJsonDocument::fromJson(json->toPlainText().toUtf8()).isObject()) { showError(tr("The export must contain a JSON object.")); return; }
    const auto path = QFileDialog::getSaveFileName(this, tr("Export target"), object.value(QStringLiteral("id")).toString() + QStringLiteral(".json"), tr("JSON targets (*.json)")); if (path.isEmpty()) return;
    const auto absolute = QFileInfo(path).absoluteFilePath();
    if (QFileInfo(path).isSymLink() || absolute.startsWith(m_store.activePath() + QLatin1Char('/')) || absolute.startsWith(m_store.bundledPath() + QLatin1Char('/'))) {
        showError(tr("Export to a separate regular file. Use Save to update a configured target.")); return;
    }
    QSaveFile output(path); const auto bytes = json->toPlainText().toUtf8();
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit()) showError(tr("Could not write export."));
    else statusBar()->showMessage(tr("Target exported."));
}

void TargetManagerWindow::updateActions()
{
    const bool hasEntry = !m_entry.path.isEmpty();
    const bool selected = hasEntry && !m_test->busy();
    m_title->setText(hasEntry ? m_editor->document().value(QStringLiteral("displayName")).toString(QFileInfo(m_entry.path).fileName()) + (m_dirty ? QStringLiteral(" *") : QString{}) : QString{});
    m_state->setText(hasEntry ? kindName(m_entry) : QString{});
    m_path->setText(m_entry.path);
    auto diagnostics = hasEntry && !m_dirty ? m_entry.problems : QStringList{};
    if (m_diskChanged) diagnostics.prepend(tr("This file changed on disk. Reload before saving; unsaved changes will not overwrite it."));
    m_editor->setContextDiagnostics(diagnostics, m_notice);
    m_save->setEnabled(selected && m_dirty && m_entry.editable());
    m_save->setText(m_entry.kind == TargetFileStore::Kind::Disabled ? tr("Save disabled draft") : tr("Save"));
    m_toggle->setEnabled(selected && !m_entry.revision.isEmpty());
    m_toggle->setText(m_entry.kind == TargetFileStore::Kind::Active ? tr("Disable") : m_entry.kind == TargetFileStore::Kind::Template ? tr("Set up copy") : tr("Enable"));
    m_customize->setEnabled(selected && ((!m_entry.editable()) || (m_entry.linked && !m_customizing)));
    m_customize->setText(m_entry.editable() ? tr("Customize") : tr("Set up copy"));
    m_duplicate->setEnabled(selected); m_export->setEnabled(selected); m_delete->setEnabled(selected && m_entry.editable() && !m_entry.revision.isEmpty());
    m_restore->setEnabled(selected && m_entry.kind == TargetFileStore::Kind::Active && !m_entry.linked && QFileInfo::exists(m_store.bundledPath() + QLatin1Char('/') + QFileInfo(m_entry.path).fileName()));
    m_open->setEnabled(selected && m_entry.editable() && !m_entry.linked && !m_entry.revision.isEmpty());
}

void TargetManagerWindow::closeEvent(QCloseEvent *event)
{
    if (leaveDraft()) event->accept(); else event->ignore();
}
