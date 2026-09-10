#pragma once

#include "targetregistry.h"
#include <QJsonObject>

class TargetFileStore
{
public:
    enum class Kind { Active, Disabled, Preset, Template };
    struct Entry {
        QString path;
        Kind kind = Kind::Active;
        QByteArray bytes;
        QByteArray revision;
        QJsonObject object;
        QStringList problems;
        bool linked = false;
        bool editable() const { return kind == Kind::Active || kind == Kind::Disabled; }
    };
    explicit TargetFileStore(QString bundledPath = {}, QString activePath = {});
    QList<Entry> entries(QStringList *problems = nullptr) const;
    QString activePath() const;
    QString bundledPath() const;
    QString uniqueId(QString base) const;
    static Entry read(const QString &path, Kind kind);
    static QByteArray revision(const QString &path);
    static QStringList validate(const QByteArray &bytes);
    static QJsonObject portable(const QJsonObject &object, QStringList *redacted = nullptr);

    // Empty expectedRevision requires that the path not exist. Existing links are
    // replaced atomically, never followed for writing.
    QString save(const QString &path, const QByteArray &bytes, const QByteArray &expectedRevision,
                 bool allowInvalid = false) const;
    QString setEnabled(const Entry &entry, bool enabled) const;
    QString enablePreset(const QString &presetPath) const;
    QString restorePreset(const Entry &entry) const;
    QString remove(const Entry &entry) const;

private:
    bool owned(const QString &path) const;
    QString conflict(const QJsonObject &object, const QString &exceptPath) const;
    TargetRegistry m_registry;
};
