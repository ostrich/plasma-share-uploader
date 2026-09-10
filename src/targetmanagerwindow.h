#pragma once
#include "targetfilestore.h"
#include <QMainWindow>
class TargetEditor;
class CredentialPanel;
class TargetTestPanel;
class CredentialStore;
class QListWidget;
class QLineEdit;
class QComboBox;
class QLabel;
class QPushButton;
class QFileSystemWatcher;
class ElidedLabel;

class TargetManagerWindow final : public QMainWindow
{
    Q_OBJECT
public:
    explicit TargetManagerWindow(QString bundledPath = {}, QString activePath = {}, QWidget *parent = nullptr,
                                 CredentialStore *credentials = nullptr);
protected:
    void closeEvent(QCloseEvent *event) override;
private:
    void refresh(const QString &select = {});
    void filter();
    void selectRow(int row);
    bool leaveDraft();
    bool save();
    void newTarget();
    void copyTarget();
    void importTarget();
    void exportTarget();
    void customize();
    void enableDisable();
    void removeTarget();
    void restorePreset();
    void updateActions();
    void showError(const QString &message);
    void load(const TargetFileStore::Entry &entry);
    TargetFileStore m_store;
    QList<TargetFileStore::Entry> m_entries;
    TargetFileStore::Entry m_entry;
    TargetEditor *m_editor;
    CredentialPanel *m_credentials;
    TargetTestPanel *m_test;
    QListWidget *m_list;
    QLineEdit *m_search;
    QComboBox *m_filter;
    ElidedLabel *m_title;
    ElidedLabel *m_state;
    ElidedLabel *m_path;
    QPushButton *m_save;
    QPushButton *m_toggle;
    QPushButton *m_customize;
    QPushButton *m_duplicate;
    QPushButton *m_delete;
    QPushButton *m_export;
    QPushButton *m_restore;
    QPushButton *m_open;
    QFileSystemWatcher *m_watcher;
    bool m_dirty = false;
    bool m_customizing = false;
    bool m_loading = false;
    bool m_diskChanged = false;
    QString m_notice;
    int m_selectedRow = -1;
};
