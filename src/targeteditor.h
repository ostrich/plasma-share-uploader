#pragma once

#include <QJsonObject>
#include <QMap>
#include <QWidget>
#include <functional>

class QTabWidget;
class QPlainTextEdit;
class QLabel;
class QFormLayout;
class QLineEdit;
class StringTable;
class ElidedLabel;
class QPushButton;

class TargetEditor final : public QWidget
{
    Q_OBJECT
public:
    explicit TargetEditor(QWidget *parent = nullptr);
    void setBytes(const QByteArray &bytes);
    QByteArray bytes() const;
    QJsonObject document() const { return m_document; }
    QStringList problems() const;
    bool hasObject() const;
    void setField(const QStringList &path, const QJsonValue &value);
    QTabWidget *tabs() const { return m_tabs; }
    void showJson();
    void setEditable(bool editable);
    void setContextDiagnostics(const QStringList &problems, const QString &notice = {});
    bool isEditable() const { return m_editable; }
signals:
    void changed();
private:
    QWidget *page(const QString &title, QFormLayout **layout);
    QJsonValue field(const QStringList &path) const;
    QLineEdit *line(QFormLayout *form, const QString &label, const QStringList &path);
    StringTable *table(QFormLayout *form, const QString &label, const QStringList &path, bool map);
    void refreshForms();
    void updateStatus();
    QJsonObject m_document;
    QTabWidget *m_tabs;
    QPlainTextEdit *m_raw;
    ElidedLabel *m_status;
    QPushButton *m_details;
    QStringList m_contextProblems;
    QString m_notice;
    QString m_diagnosticText;
    QWidget *m_jsonPage;
    QList<QWidget *> m_formPages;
    QList<std::function<void()>> m_refresh;
    QMap<QWidget *, QString> m_formErrors;
    bool m_loading = false;
    bool m_editable = true;
};
