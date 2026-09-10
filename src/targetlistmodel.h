#pragma once
#include "targetfilestore.h"
#include <QAbstractListModel>
#include <QtQmlIntegration/qqmlintegration.h>

class TargetListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the target manager")
public:
    using QAbstractListModel::QAbstractListModel;
    enum Role { PathRole = Qt::UserRole + 1, NameRole, StateRole, HostRole, ProblemsRole };
    int rowCount(const QModelIndex& parent = { }) const override
    {
        return parent.isValid() ? 0 : int(m_visible.size());
    }
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void setEntries(const QList<TargetFileStore::Entry>& entries);
    void filter(const QString& search, int kind);
    Q_INVOKABLE int indexOfPath(const QString& path) const;
    Q_INVOKABLE QString pathAt(int row) const { return data(index(row), PathRole).toString(); }
    static QString kindName(const TargetFileStore::Entry& entry);

private:
    void rebuild();
    QList<TargetFileStore::Entry> m_entries;
    QList<int> m_visible;
    QString m_search;
    int m_filter = 0;
};
