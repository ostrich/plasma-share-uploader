#pragma once
#include "targetdraft.h"
#include <QAbstractListModel>
#include <QPointer>

class JsonRowsModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(TargetDraft* draft READ draft WRITE setDraft NOTIFY sourceChanged)
    Q_PROPERTY(QStringList path READ path WRITE setPath NOTIFY sourceChanged)
    Q_PROPERTY(bool map READ map WRITE setMap NOTIFY sourceChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(QString error READ error NOTIFY valueChanged)
    Q_PROPERTY(QVariant value READ value WRITE setValue NOTIFY valueChanged)
public:
    explicit JsonRowsModel(QObject* parent = nullptr);
    enum Role { NameRole = Qt::UserRole + 1, TextRole, LabelRole };
    int rowCount(const QModelIndex& parent = { }) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return int(m_rows.size()); }
    TargetDraft* draft() const { return m_draft; }
    void setDraft(TargetDraft* draft);
    QStringList path() const { return m_path; }
    void setPath(const QStringList& path);
    bool map() const { return m_map; }
    void setMap(bool map);
    QString error() const;
    QVariant value() const { return jsonValue().toVariant(); }
    QJsonValue jsonValue() const;
    void setValue(const QVariant& value);
    Q_INVOKABLE void edit(int row, int column, const QString& text);
    Q_INVOKABLE void append(const QVariant& value = QString());
    Q_INVOKABLE void remove(int row);
    Q_INVOKABLE void move(int row, int delta);
signals:
    void sourceChanged();
    void countChanged();
    void valueChanged();

private:
    void reload();
    void assign(const QJsonValue& value);
    void publish();
    bool editable() const;
    QList<QPair<QString, QJsonValue>> m_rows;
    QPointer<TargetDraft> m_draft;
    QStringList m_path;
    bool m_map = false;
    bool m_publishing = false;
};
