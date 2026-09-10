#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QWidget>

class QTableWidget;
class QComboBox;
class QLineEdit;
class QSpinBox;
class QListWidget;

namespace ConfigJson {
QString text(const QJsonValue &value);
bool parse(const QString &text, QJsonValue *value, QString *error);
QJsonValue set(QJsonValue root, const QStringList &path, const QJsonValue &value);
}

// One line whose contents never change the surrounding layout's size hints.
class ElidedLabel final : public QLabel
{
public:
    explicit ElidedLabel(QWidget *parent = nullptr, Qt::TextElideMode mode = Qt::ElideRight);
    void setText(const QString &text);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
protected:
    void resizeEvent(QResizeEvent *event) override;
    void changeEvent(QEvent *event) override;
private:
    void updateText();
    QString m_fullText;
    Qt::TextElideMode m_mode;
};

class StringTable final : public QWidget
{
    Q_OBJECT
public:
    explicit StringTable(bool map, QWidget *parent = nullptr);
    void setValue(const QJsonValue &value);
    QJsonValue value() const;
    QString error() const;
signals:
    void changed();
private:
    void moveRow(int delta);
    QTableWidget *m_table;
    bool m_map;
    bool m_loading = false;
};

class ResponseEditor final : public QWidget
{
    Q_OBJECT
public:
    explicit ResponseEditor(bool optional, QWidget *parent = nullptr);
    void setValue(const QJsonValue &value);
    QJsonValue value() const;
signals:
    void changed();
private:
    void visibility();
    QJsonObject m_value;
    QComboBox *m_type;
    QLineEdit *m_pointer;
    QLineEdit *m_pattern;
    QLineEdit *m_header;
    QLineEdit *m_xpath;
    QSpinBox *m_group;
    bool m_loading = false;
};

class PreUploadEditor final : public QWidget
{
    Q_OBJECT
public:
    explicit PreUploadEditor(QWidget *parent = nullptr);
    void setValue(const QJsonArray &value);
    QJsonArray value() const { return m_value; }
signals:
    void changed();
private:
    void refreshRules(int selected);
    void showRule();
    void showCommand();
    void changeRule(const QString &key, const QJsonValue &value);
    void changeCommands(const QJsonArray &commands, int selected);
    QJsonArray m_value;
    QListWidget *m_rules;
    QWidget *m_details;
    StringTable *m_mimes;
    QComboBox *m_handling;
    QSpinBox *m_timeout;
    QListWidget *m_commands;
    StringTable *m_arguments;
    bool m_loading = false;
};
