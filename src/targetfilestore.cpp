#include "targetfilestore.h"
#include "targetconfigparser.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QRegularExpression>
#include <QTemporaryFile>
#include <QUrl>
#include <QUrlQuery>
#include <cerrno>
#include <cstdio>
#include <cstring>

namespace {
QString absolute(const QString &path) { return QDir::cleanPath(QFileInfo(path).absoluteFilePath()); }
bool exists(const QString &path) { const QFileInfo f(path); return f.exists() || f.isSymLink(); }
QString lockPath(const QString &active) { return QFileInfo(active).absolutePath() + QStringLiteral("/.target-manager.lock"); }
const QRegularExpression sensitive(QStringLiteral("authorization|cookie|password|passwd|secret|token|api.?key|userhash|^key$"), QRegularExpression::CaseInsensitiveOption);
bool reference(const QString &text) { return text.contains(QStringLiteral("${ENV:")) || text.contains(QStringLiteral("${WALLET:")); }

QJsonValue exportValue(const QJsonValue &value, const QString &path, const QString &id, QStringList *redacted)
{
    if (value.isObject()) {
        auto object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it) {
            const QString fieldPath = path + QLatin1Char('/') + it.key();
            if (sensitive.match(it.key()).hasMatch() && it.value().isString() && !it.value().toString().isEmpty() && !reference(it.value().toString())) {
                QString name = (id + QLatin1Char('_') + it.key()).toUpper();
                name.replace(QRegularExpression(QStringLiteral("[^A-Z0-9_]")), QStringLiteral("_"));
                if (name.isEmpty() || name.front().isDigit()) name.prepend(QStringLiteral("UPLOAD_"));
                it.value() = QStringLiteral("${ENV:%1}").arg(name);
                if (redacted) redacted->append(fieldPath);
            } else it.value() = exportValue(it.value(), fieldPath, id, redacted);
        }
        return object;
    }
    if (value.isArray()) {
        QJsonArray array;
        for (const auto &entry : value.toArray()) array.append(exportValue(entry, path, id, redacted));
        return array;
    }
    return value;
}
}

TargetFileStore::TargetFileStore(QString bundled, QString active) : m_registry(std::move(bundled), std::move(active)) {}
QString TargetFileStore::activePath() const { return absolute(m_registry.activeTargetsPath()); }
QString TargetFileStore::bundledPath() const { return absolute(m_registry.bundledTargetsPath()); }

QByteArray TargetFileStore::revision(const QString &path)
{
    if (!exists(path)) return {};
    QFile file(path);
    QByteArray bytes;
    if (file.open(QIODevice::ReadOnly)) bytes = file.readAll();
    const auto link = QFileInfo(path).symLinkTarget().toUtf8();
    return QCryptographicHash::hash(link + '\0' + bytes, QCryptographicHash::Sha256);
}

QStringList TargetFileStore::validate(const QByteArray &bytes)
{
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError) return {QStringLiteral("JSON at byte %1: %2").arg(error.offset).arg(error.errorString())};
    if (!document.isObject()) return {QStringLiteral("A target must be a JSON object.")};
    ParsedTargetConfig parsed;
    QList<TargetDiagnostic> diagnostics;
    TargetConfigParser::parse(document.object(), &parsed, &diagnostics);
    QStringList problems;
    for (const auto &d : diagnostics) problems.append(d.jsonPath + QStringLiteral(": ") + d.message);
    return problems;
}

TargetFileStore::Entry TargetFileStore::read(const QString &path, Kind kind)
{
    Entry entry;
    entry.path = absolute(path);
    entry.kind = kind;
    const QFileInfo info(path);
    const auto link = info.symLinkTarget().toUtf8();
    entry.linked = info.isSymLink();
    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
        entry.bytes = file.readAll();
        entry.object = QJsonDocument::fromJson(entry.bytes).object();
        entry.problems = validate(entry.bytes);
    } else entry.problems.append(QStringLiteral("Cannot read file: %1").arg(file.errorString()));
    // The edit revision must describe the bytes actually loaded, not a second
    // read that could already contain a concurrent external edit.
    if (info.exists() || entry.linked)
        entry.revision = QCryptographicHash::hash(link + '\0' + entry.bytes, QCryptographicHash::Sha256);
    return entry;
}

QList<TargetFileStore::Entry> TargetFileStore::entries(QStringList *problems) const
{
    const auto result = m_registry.loadTargets(); // Shares first-run semantics with the plugin.
    if (problems) for (const auto &d : result.diagnostics) problems->append(d.filePath + QStringLiteral(": ") + d.message);
    QList<Entry> list;
    const QList<QPair<QString, Kind>> dirs{{activePath(), Kind::Active},
        {activePath() + QStringLiteral("/disabled"), Kind::Disabled}, {bundledPath(), Kind::Preset},
        {bundledPath() + QStringLiteral("/examples"), Kind::Template}};
    for (const auto &[path, kind] : dirs) {
        for (const auto &file : QDir(path).entryInfoList({QStringLiteral("*.json")}, QDir::Files | QDir::System, QDir::Name))
            if (!file.isDir()) list.append(read(file.absoluteFilePath(), kind));
    }
    // Match the runtime's winner and ignored duplicates. Invalid definitions do
    // not reserve an ID, and the first valid definition remains usable.
    for (auto &entry : list) if (entry.kind == Kind::Active) {
        for (const auto &diagnostic : result.diagnostics)
            if (diagnostic.code == QLatin1StringView("target.duplicate_id") && absolute(diagnostic.filePath) == entry.path)
                entry.problems.append(diagnostic.message);
    }
    return list;
}

bool TargetFileStore::owned(const QString &path) const
{
    const auto parent = absolute(QFileInfo(path).absolutePath());
    return QFileInfo(path).suffix() == QLatin1StringView("json") &&
        (parent == activePath() || (parent == activePath() + QStringLiteral("/disabled") && !QFileInfo(parent).isSymLink()));
}

QString TargetFileStore::conflict(const QJsonObject &object, const QString &exceptPath) const
{
    const QString id = object.value(QStringLiteral("id")).toString();
    if (id.isEmpty()) return {};
    for (const auto &file : QDir(activePath()).entryInfoList({QStringLiteral("*.json")}, QDir::Files | QDir::System)) {
        if (absolute(file.absoluteFilePath()) == absolute(exceptPath)) continue;
        const auto other = read(file.absoluteFilePath(), Kind::Active);
        if (other.problems.isEmpty() && other.object.value(QStringLiteral("id")).toString() == id)
            return QStringLiteral("ID '%1' is already active in %2.").arg(id, file.fileName());
    }
    return {};
}

QString TargetFileStore::uniqueId(QString base) const
{
    base = base.toLower().replace(QRegularExpression(QStringLiteral("[^a-z0-9_-]")), QStringLiteral("_"));
    if (base.isEmpty() || !base.front().isLetterOrNumber()) base.prepend(QStringLiteral("target"));
    QString candidate = base;
    int index = 2;
    for (;;) {
        bool used = exists(activePath() + QLatin1Char('/') + candidate + QStringLiteral(".json")) ||
            exists(activePath() + QStringLiteral("/disabled/") + candidate + QStringLiteral(".json"));
        for (const auto &entry : entries()) if (entry.editable() && entry.object.value(QStringLiteral("id")).toString() == candidate) used = true;
        if (!used) return candidate;
        candidate = base + QLatin1Char('_') + QString::number(index++);
    }
}

QString TargetFileStore::save(const QString &path, const QByteArray &bytes, const QByteArray &expected, bool allowInvalid) const
{
    if (!owned(path)) return QStringLiteral("Choose a JSON file in the active or disabled target directory.");
    if (!allowInvalid || QFileInfo(path).absolutePath() == activePath()) {
        const auto problems = validate(bytes);
        if (!problems.isEmpty()) return problems.join(QLatin1Char('\n'));
    }
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return QStringLiteral("Cannot create target directory.");
    QLockFile lock(lockPath(activePath()));
    if (!lock.tryLock(0)) return QStringLiteral("Another manager is changing targets. Try again.");
    if (revision(path) != expected) return QStringLiteral("The target changed outside this editor. Reload it before saving.");
    if (QFileInfo(path).absolutePath() == activePath()) {
        const auto error = conflict(QJsonDocument::fromJson(bytes).object(), path);
        if (!error.isEmpty()) return error;
    }
    QTemporaryFile pending(QFileInfo(path).absolutePath() + QStringLiteral("/.target-XXXXXX"));
    if (!pending.open() || pending.write(bytes) != bytes.size() || !pending.flush()) return QStringLiteral("Could not prepare target file for saving.");
    pending.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    pending.close();
    // POSIX rename atomically replaces the directory entry, including a symlink.
    if (::rename(QFile::encodeName(pending.fileName()).constData(), QFile::encodeName(path).constData()) != 0)
        return QStringLiteral("Could not save target: %1").arg(QString::fromLocal8Bit(std::strerror(errno)));
    pending.setAutoRemove(false);
    return {};
}

QString TargetFileStore::enablePreset(const QString &path) const
{
    if (absolute(QFileInfo(path).absolutePath()) != bundledPath()) return QStringLiteral("Only packaged presets can be enabled as links.");
    const auto preset = read(path, Kind::Preset);
    if (!preset.problems.isEmpty()) return preset.problems.join(QLatin1Char('\n'));
    m_registry.loadTargets();
    QLockFile lock(lockPath(activePath()));
    if (!lock.tryLock(0)) return QStringLiteral("Another manager is changing targets.");
    const auto error = conflict(preset.object, {});
    if (!error.isEmpty()) return error;
    const auto destination = activePath() + QLatin1Char('/') + QFileInfo(path).fileName();
    if (exists(destination)) return QStringLiteral("A target already occupies %1.").arg(destination);
    return QFile::link(absolute(path), destination) ? QString{} : QStringLiteral("Could not enable preset.");
}

QString TargetFileStore::setEnabled(const Entry &entry, bool enabled) const
{
    if (!owned(entry.path) || !entry.editable()) return QStringLiteral("Select a configured target.");
    if (enabled == (entry.kind == Kind::Active)) return {};
    const QString destination = activePath() + (enabled ? QStringLiteral("/") : QStringLiteral("/disabled/")) + QFileInfo(entry.path).fileName();
    if (!owned(destination)) return QStringLiteral("The disabled directory must be a regular directory.");
    if (!QDir().mkpath(QFileInfo(destination).absolutePath())) return QStringLiteral("Cannot create destination directory.");
    QLockFile lock(lockPath(activePath()));
    if (!lock.tryLock(0)) return QStringLiteral("Another manager is changing targets.");
    if (revision(entry.path) != entry.revision) return QStringLiteral("The target changed. Reload the list.");
    if (enabled) {
        const auto problems = validate(entry.bytes);
        if (!problems.isEmpty()) return problems.join(QLatin1Char('\n'));
        const auto error = conflict(entry.object, entry.path);
        if (!error.isEmpty()) return error;
    } else if (entry.linked && absolute(QFileInfo(QFileInfo(entry.path).symLinkTarget()).absolutePath()) == bundledPath()) {
        return QFile::remove(entry.path) ? QString{} : QStringLiteral("Could not remove preset link.");
    }
    if (exists(destination)) return QStringLiteral("Destination already exists: %1").arg(destination);
    return QFile::rename(entry.path, destination) ? QString{} : QStringLiteral("Could not move target.");
}

QString TargetFileStore::restorePreset(const Entry &entry) const
{
    if (!owned(entry.path) || entry.kind != Kind::Active) return QStringLiteral("Select an active custom target.");
    const QString preset = bundledPath() + QLatin1Char('/') + QFileInfo(entry.path).fileName();
    const auto source = read(preset, Kind::Preset);
    if (!source.problems.isEmpty()) return QStringLiteral("No valid packaged preset with this filename.");
    QLockFile lock(lockPath(activePath()));
    if (!lock.tryLock(0)) return QStringLiteral("Another manager is changing targets.");
    if (revision(entry.path) != entry.revision) return QStringLiteral("The target changed. Reload it first.");
    const auto error = conflict(source.object, entry.path);
    if (!error.isEmpty()) return error;
    QTemporaryFile temp(activePath() + QStringLiteral("/.restore-XXXXXX"));
    if (!temp.open()) return QStringLiteral("Cannot prepare preset link.");
    const QString staging = temp.fileName();
    temp.remove();
    if (!QFile::link(preset, staging)) return QStringLiteral("Cannot prepare preset link.");
    const int result = ::rename(QFile::encodeName(staging).constData(), QFile::encodeName(entry.path).constData());
    if (result != 0) QFile::remove(staging);
    return result == 0 ? QString{} : QStringLiteral("Cannot restore preset.");
}

QString TargetFileStore::remove(const Entry &entry) const
{
    if (!owned(entry.path) || !entry.editable()) return QStringLiteral("Packaged definitions cannot be deleted here.");
    QLockFile lock(lockPath(activePath()));
    if (!lock.tryLock(0)) return QStringLiteral("Another manager is changing targets.");
    if (revision(entry.path) != entry.revision) return QStringLiteral("The target changed. Reload it first.");
    return QFile::remove(entry.path) ? QString{} : QStringLiteral("Could not remove target.");
}

QJsonObject TargetFileStore::portable(const QJsonObject &object, QStringList *redacted)
{
    auto result = exportValue(object, {}, object.value(QStringLiteral("id")).toString(), redacted).toObject();
    auto request = result.value(QStringLiteral("request")).toObject();
    const auto endpoint = request.value(QStringLiteral("url")).toString();
    const QUrl url(endpoint);
    bool sensitiveUrl = !url.userInfo().isEmpty();
    for (const auto &item : QUrlQuery(url).queryItems()) if (sensitive.match(item.first).hasMatch() && !reference(item.second)) sensitiveUrl = true;
    if (sensitiveUrl) {
        request.insert(QStringLiteral("url"), QStringLiteral("${ENV:UPLOAD_ENDPOINT}"));
        result.insert(QStringLiteral("request"), request);
        if (redacted) redacted->append(QStringLiteral("/request/url"));
    }
    return result;
}
