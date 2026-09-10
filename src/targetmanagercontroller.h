#pragma once
#include "credentialcontroller.h"
#include "targetlistmodel.h"
#include "testcontroller.h"
#include <QFileSystemWatcher>
#include <functional>

class TargetManagerController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the configuration application")
    Q_PROPERTY(TargetDraft* draft READ draft CONSTANT)
    Q_PROPERTY(TargetListModel* targets READ targets CONSTANT)
    Q_PROPERTY(CredentialController* credentials READ credentials CONSTANT)
    Q_PROPERTY(TestController* test READ test CONSTANT)
    Q_PROPERTY(QString selectedPath READ selectedPath NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(bool dirty READ dirty NOTIFY changed)
    Q_PROPERTY(bool hasEntry READ hasEntry NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString diagnosticText READ diagnosticText NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(QVariantMap actions READ actions NOTIFY changed)
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY filterChanged)
    Q_PROPERTY(int filterKind READ filterKind WRITE setFilterKind NOTIFY filterChanged)
public:
    explicit TargetManagerController(
        QString bundled = { }, QString active = { }, QObject* parent = nullptr, CredentialStore* store = nullptr);
    TargetDraft* draft() { return &m_draft; }
    TargetListModel* targets() { return &m_targets; }
    CredentialController* credentials() { return &m_credentials; }
    TestController* test() { return &m_test; }
    QString selectedPath() const { return m_entry.path; }
    QString title() const;
    QString state() const;
    bool dirty() const { return m_dirty; }
    bool hasEntry() const { return !m_entry.path.isEmpty(); }
    QString status() const;
    QString diagnosticText() const;
    QString message() const { return m_message; }
    QVariantMap actions() const;
    QString search() const { return m_search; }
    void setSearch(const QString& text);
    int filterKind() const { return m_filter; }
    void setFilterKind(int kind);
    Q_INVOKABLE void request(const QString& action, const QString& argument = { });
    Q_INVOKABLE void resolveDraft(const QString& choice);
    Q_INVOKABLE void confirm(bool accepted);
    Q_INVOKABLE bool save();
    Q_INVOKABLE void importFile(const QUrl& url);
    Q_INVOKABLE void acceptExport(const QString& text);
    Q_INVOKABLE void exportFile(const QUrl& url);
signals:
    void changed();
    void filterChanged();
    void error(const QString& text);
    void discardRequested();
    void confirmationRequested(const QString& title, const QString& text);
    void fileDialogRequested(const QString& kind, const QString& name);
    void exportReviewRequested(const QString& text, const QString& replaced);
    void showTab(int index);
    void closeReady();

private:
    void guard(std::function<void()> next);
    void refresh(const QString& select = { });
    void load(const TargetFileStore::Entry& entry);
    void newTarget();
    void duplicate();
    void toggle();
    void customize();
    void reviewExport();
    QStringList diagnostics() const;
    TargetFileStore m_store;
    TargetDraft m_draft;
    TargetListModel m_targets;
    CredentialStore* m_credentialStore;
    CredentialController m_credentials;
    TestController m_test;
    QFileSystemWatcher m_watcher;
    QList<TargetFileStore::Entry> m_entries;
    TargetFileStore::Entry m_entry;
    QString m_search, m_notice, m_message;
    QByteArray m_export;
    int m_filter = 0;
    bool m_dirty = false, m_loading = false, m_customizing = false, m_diskChanged = false;
    std::function<void()> m_pending, m_confirmed;
};
