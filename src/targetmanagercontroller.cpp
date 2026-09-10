#include "targetmanagercontroller.h"
#include "jsonutils.h"
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>

TargetManagerController::TargetManagerController(
    QString bundled, QString active, QObject* parent, CredentialStore* store)
    : QObject(parent)
    , m_store(std::move(bundled), std::move(active))
    , m_draft(this)
    , m_targets(this)
    , m_credentialStore(store ? store : new CredentialStore(this))
    , m_credentials(&m_draft, m_credentialStore, this)
    , m_test(&m_draft, m_credentialStore, this)
    , m_watcher(this)
{
    connect(&m_draft, &TargetDraft::edited, this, [this]() {
        if (!m_loading) {
            m_dirty = true;
            emit changed();
        }
    });
    connect(&m_draft, &TargetDraft::problemsChanged, this, [this]() {
        if (!m_loading)
            emit changed();
    });
    connect(&m_test, &TestController::busyChanged, this, &TargetManagerController::changed);
    auto diskChange = [this]() {
        m_diskChanged = hasEntry() && m_entry.revision != TargetFileStore::revision(m_entry.path);
        emit changed();
    };
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, diskChange);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, diskChange);
    refresh();
}
QString TargetManagerController::title() const
{
    if (!hasEntry())
        return tr("Add an upload target");
    return m_draft.document().value(QStringLiteral("displayName")).toString(QFileInfo(m_entry.path).fileName())
        + (m_dirty ? QStringLiteral(" *") : QString { });
}
QString TargetManagerController::state() const
{
    return hasEntry() ? TargetListModel::kindName(m_entry) : tr("Choose Add Target to get started.");
}
QStringList TargetManagerController::diagnostics() const
{
    auto problems = hasEntry() ? m_draft.problems() : QStringList { };
    if (!m_dirty)
        problems += m_entry.problems;
    if (m_diskChanged)
        problems.prepend(tr("This file changed on disk. Reload before saving; unsaved changes will not overwrite it."));
    problems.removeDuplicates();
    return problems;
}
QString TargetManagerController::diagnosticText() const
{
    auto details = diagnostics();
    if (!m_notice.isEmpty())
        details.prepend(m_notice);
    return details.join(QStringLiteral("\n\n"));
}
QString TargetManagerController::status() const
{
    if (!hasEntry())
        return tr("Enable a preset, import JSON, or create a custom target.");
    const auto errors = diagnostics();
    if (!errors.isEmpty())
        return errors.size() == 1 ? tr("Needs attention: 1 issue")
                                  : tr("Needs attention: %1 issues").arg(errors.size());
    return m_notice.isEmpty() ? tr("Configuration valid. Check upload compatibility in Test.") : m_notice;
}
QVariantMap TargetManagerController::actions() const
{
    using Kind = TargetFileStore::Kind;
    const bool selected = hasEntry() && !m_test.busy();
    return { { QStringLiteral("save"), selected && m_dirty && m_entry.editable() },
        { QStringLiteral("saveText"), m_entry.kind == Kind::Disabled ? tr("Save disabled draft") : tr("Save") },
        { QStringLiteral("toggle"), selected && !m_entry.revision.isEmpty() },
        { QStringLiteral("toggleText"),
            m_entry.kind == Kind::Active         ? tr("Disable")
                : m_entry.kind == Kind::Template ? tr("Set up copy")
                                                 : tr("Enable") },
        { QStringLiteral("customize"), selected && (!m_entry.editable() || (m_entry.linked && !m_customizing)) },
        { QStringLiteral("customizeText"), m_entry.editable() ? tr("Customize") : tr("Set up copy") },
        { QStringLiteral("duplicate"), selected }, { QStringLiteral("export"), selected },
        { QStringLiteral("delete"), selected && m_entry.editable() && !m_entry.revision.isEmpty() },
        { QStringLiteral("restore"),
            selected && m_entry.kind == Kind::Active && !m_entry.linked
                && QFileInfo::exists(m_store.bundledPath() + QLatin1Char('/') + QFileInfo(m_entry.path).fileName()) },
        { QStringLiteral("open"), selected && m_entry.editable() && !m_entry.linked && !m_entry.revision.isEmpty() } };
}
void TargetManagerController::setSearch(const QString& text)
{
    if (m_search == text)
        return;
    m_search = text;
    m_targets.filter(m_search, m_filter);
    emit filterChanged();
}
void TargetManagerController::setFilterKind(int kind)
{
    if (kind < 0 || kind > 5 || m_filter == kind)
        return;
    m_filter = kind;
    m_targets.filter(m_search, m_filter);
    emit filterChanged();
}
void TargetManagerController::refresh(const QString& select)
{
    QStringList problems;
    m_entries = m_store.entries(&problems);
    m_targets.setEntries(m_entries);
    int selected = -1;
    for (int i = 0; i < m_entries.size(); ++i)
        if (m_entries.at(i).path == select) {
            selected = i;
            break;
        }
    if (selected >= 0 && m_targets.indexOfPath(select) < 0) {
        setSearch({ });
        setFilterKind(5);
    }
    if (selected < 0 && m_targets.rowCount() > 0) {
        const auto first = m_targets.data(m_targets.index(0), TargetListModel::PathRole).toString();
        for (int i = 0; i < m_entries.size(); ++i)
            if (m_entries.at(i).path == first) {
                selected = i;
                break;
            }
    }
    load(selected < 0 ? TargetFileStore::Entry { } : m_entries.at(selected));
    if (problems.isEmpty()) {
        m_message = tr("Targets reload on the next share.");
    } else {
        qsizetype affectedTargets = 0;
        for (const auto& entry : m_entries) {
            if (entry.kind == TargetFileStore::Kind::Active && !entry.problems.isEmpty())
                ++affectedTargets;
        }
        const auto issues
            = problems.size() == 1 ? tr("1 configuration issue") : tr("%1 configuration issues").arg(problems.size());
        if (affectedTargets == 0) {
            m_message = issues;
        } else {
            const auto targets = affectedTargets == 1 ? tr("1 target") : tr("%1 targets").arg(affectedTargets);
            m_message
                = tr("%1 across %2", "Configuration issue count across affected target count").arg(issues, targets);
        }
    }
    emit changed();
}
void TargetManagerController::load(const TargetFileStore::Entry& entry)
{
    m_loading = true;
    m_entry = entry;
    m_dirty = false;
    m_customizing = false;
    m_diskChanged = false;
    m_notice.clear();
    m_draft.setEditable(hasEntry() && entry.editable() && !entry.linked);
    m_draft.load(hasEntry() ? entry.bytes : QByteArray("{}"));
    m_credentials.reset();
    m_test.reset();
    if (!m_watcher.files().isEmpty())
        m_watcher.removePaths(m_watcher.files());
    if (!m_watcher.directories().isEmpty())
        m_watcher.removePaths(m_watcher.directories());
    if (hasEntry()) {
        if (QFileInfo::exists(entry.path))
            m_watcher.addPath(entry.path);
        if (QFileInfo(QFileInfo(entry.path).absolutePath()).isDir())
            m_watcher.addPath(QFileInfo(entry.path).absolutePath());
    }
    m_loading = false;
    emit changed();
    if (hasEntry() && !m_draft.hasObject())
        emit showTab(5);
}
void TargetManagerController::guard(std::function<void()> next)
{
    if (m_test.busy()) {
        emit error(tr("Cancel the running test before switching targets or closing."));
        return;
    }
    if (m_pending || m_confirmed)
        return;
    if (!m_dirty) {
        next();
        return;
    }
    m_pending = std::move(next);
    emit discardRequested();
}
void TargetManagerController::resolveDraft(const QString& choice)
{
    if (!m_pending)
        return;
    auto next = std::move(m_pending);
    m_pending = { };
    if (choice == QLatin1StringView("cancel"))
        return;
    if (choice == QLatin1StringView("save")) {
        if (!save())
            return;
    } else if (choice == QLatin1StringView("discard"))
        load(m_entry);
    else
        return;
    next();
}
void TargetManagerController::confirm(bool accepted)
{
    auto action = std::move(m_confirmed);
    m_confirmed = { };
    if (accepted && action)
        action();
}
bool TargetManagerController::save()
{
    if (m_test.busy()) {
        emit error(tr("Cancel the running test before saving."));
        return false;
    }
    if (!hasEntry() || !m_entry.editable() || !m_draft.editable())
        return false;
    const auto problems = m_draft.problems();
    if ((m_entry.kind == TargetFileStore::Kind::Active && !problems.isEmpty()) || m_draft.hasFormErrors()) {
        emit error(problems.join(QLatin1Char('\n')));
        return false;
    }
    const auto result = m_store.save(
        m_entry.path, m_draft.bytes(), m_entry.revision, m_entry.kind == TargetFileStore::Kind::Disabled);
    if (!result.isEmpty()) {
        emit error(result);
        return false;
    }
    const auto path = m_entry.path;
    m_dirty = false;
    refresh(path);
    m_message = tr("Target saved.");
    emit changed();
    return true;
}
void TargetManagerController::request(const QString& action, const QString& argument)
{
    if (action == QLatin1StringView("select")) {
        if (argument == m_entry.path)
            return;
        guard([this, argument]() {
            for (const auto& entry : m_entries)
                if (entry.path == argument) {
                    load(entry);
                    return;
                }
        });
    } else if (action == QLatin1StringView("close"))
        guard([this]() { emit closeReady(); });
    else if (action == QLatin1StringView("reload"))
        guard([this]() { refresh(m_entry.path); });
    else if (action == QLatin1StringView("new"))
        guard([this]() { newTarget(); });
    else if (action == QLatin1StringView("import"))
        guard([this]() { emit fileDialogRequested(QStringLiteral("import"), { }); });
    else if (action == QLatin1StringView("folder"))
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_store.activePath()));
    else if (action == QLatin1StringView("presets"))
        setFilterKind(3);
    else if (!actions().value(action).toBool())
        return;
    else if (action == QLatin1StringView("save"))
        save();
    else if (action == QLatin1StringView("customize"))
        customize();
    else if (action == QLatin1StringView("duplicate"))
        guard([this]() { duplicate(); });
    else if (action == QLatin1StringView("toggle"))
        guard([this]() { toggle(); });
    else if (action == QLatin1StringView("open"))
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_entry.path));
    else if (action == QLatin1StringView("export"))
        reviewExport();
    else if (action == QLatin1StringView("delete") || action == QLatin1StringView("restore"))
        guard([this, action]() {
            const auto entry = m_entry;
            m_confirmed = [this, entry, action]() {
                const auto result
                    = action == QLatin1StringView("delete") ? m_store.remove(entry) : m_store.restorePreset(entry);
                if (!result.isEmpty())
                    emit error(result);
                else
                    refresh(action == QLatin1StringView("restore") ? entry.path : QString { });
            };
            emit confirmationRequested(
                action == QLatin1StringView("delete") ? tr("Delete target") : tr("Restore packaged preset"),
                action == QLatin1StringView("delete") ? tr("Delete this target entry? Use Disable to retain a custom "
                                                           "target. Stored credentials are retained.")
                                                      : tr("Replace this custom definition with a link to the packaged "
                                                           "preset? Its custom settings will be discarded."));
        });
}
void TargetManagerController::newTarget()
{
    const auto id = m_store.uniqueId(QStringLiteral("new_target"));
    TargetFileStore::Entry entry;
    entry.kind = TargetFileStore::Kind::Disabled;
    entry.path = m_store.activePath() + QStringLiteral("/disabled/") + id + QStringLiteral(".json");
    entry.object = { { QStringLiteral("schemaVersion"), 1 }, { QStringLiteral("id"), id },
        { QStringLiteral("displayName"), tr("New target") },
        { QStringLiteral("description"), tr("Custom upload target") },
        { QStringLiteral("icon"), QStringLiteral("document-send") },
        { QStringLiteral("request"),
            QJsonObject { { QStringLiteral("url"), QStringLiteral("https://example.invalid/upload") },
                { QStringLiteral("method"), QStringLiteral("POST") },
                { QStringLiteral("body"),
                    QJsonObject { { QStringLiteral("type"), QStringLiteral("multipart") },
                        { QStringLiteral("fileField"), QStringLiteral("file") },
                        { QStringLiteral("fields"), QJsonObject { } } } } } },
        { QStringLiteral("response"),
            QJsonObject {
                { QStringLiteral("url"), QJsonObject { { QStringLiteral("type"), QStringLiteral("text_url") } } } } } };
    entry.bytes = QJsonDocument(entry.object).toJson();
    load(entry);
    m_dirty = true;
    emit changed();
    emit showTab(0);
}
void TargetManagerController::duplicate()
{
    if (!m_draft.hasObject())
        return;
    auto object = m_draft.document();
    const auto id = m_store.uniqueId(object.value(QStringLiteral("id")).toString());
    object.insert(QStringLiteral("id"), id);
    object.insert(
        QStringLiteral("displayName"), object.value(QStringLiteral("displayName")).toString() + tr(" (copy)"));
    TargetFileStore::Entry entry;
    entry.kind = TargetFileStore::Kind::Disabled;
    entry.path = m_store.activePath() + QStringLiteral("/disabled/") + id + QStringLiteral(".json");
    entry.object = object;
    entry.bytes = QJsonDocument(object).toJson();
    load(entry);
    m_dirty = true;
    m_message = tr("Independent disabled draft. Credential references remain shared until changed.");
    emit changed();
}
void TargetManagerController::customize()
{
    if (!m_entry.editable()) {
        guard([this]() { duplicate(); });
        return;
    }
    if (!m_entry.linked)
        return;
    m_customizing = true;
    m_dirty = true;
    m_draft.setEditable(true);
    m_notice
        = tr("Save will replace this link with an independent custom copy. The packaged file will stay untouched.");
    emit changed();
}
void TargetManagerController::toggle()
{
    QString result;
    if (m_entry.kind == TargetFileStore::Kind::Preset)
        result = m_store.enablePreset(m_entry.path);
    else if (m_entry.kind == TargetFileStore::Kind::Template) {
        duplicate();
        return;
    } else
        result = m_store.setEnabled(m_entry, m_entry.kind != TargetFileStore::Kind::Active);
    if (!result.isEmpty())
        emit error(result);
    else
        refresh();
}
void TargetManagerController::importFile(const QUrl& url)
{
    if (!url.isLocalFile() || m_test.busy())
        return;
    QFile file(url.toLocalFile());
    if (!file.open(QIODevice::ReadOnly) || file.size() > 4 * 1024 * 1024) {
        emit error(tr("Cannot read target, or it exceeds 4 MiB."));
        return;
    }
    TargetFileStore::Entry entry;
    entry.kind = TargetFileStore::Kind::Disabled;
    entry.bytes = file.readAll();
    entry.object = QJsonDocument::fromJson(entry.bytes).object();
    const auto id
        = m_store.uniqueId(entry.object.value(QStringLiteral("id")).toString(QFileInfo(file).completeBaseName()));
    if (!entry.object.isEmpty()) {
        entry.object.insert(QStringLiteral("id"), id);
        entry.bytes = QJsonDocument(entry.object).toJson();
    }
    entry.path = m_store.activePath() + QStringLiteral("/disabled/") + id + QStringLiteral(".json");
    load(entry);
    m_dirty = true;
    m_notice = tr("Imported as a disabled draft. Review the JSON, credentials, and preprocessing commands before "
                  "enabling or testing it.");
    emit changed();
    emit showTab(5);
}
void TargetManagerController::reviewExport()
{
    if (!m_draft.hasObject()) {
        emit error(tr("Repair the JSON object before exporting."));
        return;
    }
    if (m_draft.hasFormErrors()) {
        emit error(m_draft.problems().join(QLatin1Char('\n')));
        return;
    }
    QStringList replaced;
    const auto object = TargetFileStore::portable(m_draft.document(), &replaced);
    emit exportReviewRequested(
        ConfigJson::text(object), replaced.isEmpty() ? tr("none") : replaced.join(QStringLiteral(", ")));
}
void TargetManagerController::acceptExport(const QString& text)
{
    const auto document = QJsonDocument::fromJson(text.toUtf8());
    if (!document.isObject()) {
        emit error(tr("The export must contain a JSON object."));
        return;
    }
    m_export = text.toUtf8();
    auto folder = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (!QFileInfo(folder).isDir())
        folder = QDir::homePath();
    const auto name = QFileInfo(document.object().value(QStringLiteral("id")).toString()).fileName();
    emit fileDialogRequested(QStringLiteral("export"),
        QUrl::fromLocalFile(QDir(folder).filePath(name + QStringLiteral(".json"))).toString());
}
void TargetManagerController::exportFile(const QUrl& url)
{
    if (!url.isLocalFile() || m_export.isEmpty())
        return;
    const auto path = url.toLocalFile();
    const auto absolute = QFileInfo(path).absoluteFilePath();
    if (QFileInfo(path).isSymLink() || absolute.startsWith(m_store.activePath() + QLatin1Char('/'))
        || absolute.startsWith(m_store.bundledPath() + QLatin1Char('/'))) {
        emit error(tr("Export to a separate regular file. Use Save to update a configured target."));
        return;
    }
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly) || output.write(m_export) != m_export.size() || !output.commit())
        emit error(tr("Could not write export."));
    else {
        m_export.clear();
        m_message = tr("Target exported.");
        emit changed();
    }
}
