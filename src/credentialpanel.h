#pragma once
#include "credentialstore.h"
#include "targeteditor.h"
#include <QWidget>
class QComboBox;
class QLineEdit;
class QLabel;

class CredentialPanel final : public QWidget
{
    Q_OBJECT
public:
    CredentialPanel(TargetEditor *editor, CredentialStore *store, QWidget *parent = nullptr);
    void reset();
    void setEditable(bool editable);
private:
    QStringList bindingPath() const;
    void bind(bool saveSecret);
    TargetEditor *m_editor;
    CredentialStore *m_store;
    QComboBox *m_mode;
    QComboBox *m_location;
    QLineEdit *m_field;
    QLineEdit *m_key;
    QLineEdit *m_secret;
    QLineEdit *m_username;
    QLabel *m_result;
    QLabel *m_references;
    QWidget *m_controls;
    bool m_editable = true;
    bool m_busy = false;
};
