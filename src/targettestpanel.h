#pragma once
#include "targeteditor.h"
#include "uploadtestrunner.h"
#include <QWidget>

class QLineEdit;
class QPlainTextEdit;
class QSpinBox;
class QTreeWidget;
class QComboBox;
class QProgressBar;
class QPushButton;
class StringTable;
class QTabWidget;

class TargetTestPanel final : public QWidget
{
    Q_OBJECT
public:
    explicit TargetTestPanel(TargetEditor *editor, CredentialStore *credentials, QWidget *parent = nullptr);
    bool busy() const { return m_runner->busy(); }
    void cancel() { m_runner->cancel(); }
    void reset();
    void validateDraft();
signals:
    void busyChanged(bool busy);
private:
    bool validDraft();
    void parseResponse();
    void rebuildTree();
    void log(const QString &message);
    TargetEditor *m_editor;
    UploadTestRunner *m_runner;
    QLineEdit *m_file;
    QLineEdit *m_responseUrl;
    QPlainTextEdit *m_response;
    QPlainTextEdit *m_log;
    QSpinBox *m_status;
    StringTable *m_headers;
    QTreeWidget *m_tree;
    QComboBox *m_output;
    QProgressBar *m_progress;
    QPushButton *m_openPreview;
    QTabWidget *m_results;
    QString m_preview;
};
