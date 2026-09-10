#include "targetlistmodel.h"
#include <QFileInfo>
#include <QUrl>
QString TargetListModel::kindName(const TargetFileStore::Entry& entry)
{
    using Kind = TargetFileStore::Kind;
    if (entry.kind == Kind::Preset)
        return tr("Available preset");
    if (entry.kind == Kind::Template)
        return tr("Setup template");
    if (entry.kind == Kind::Disabled)
        return tr("Disabled");
    return entry.linked ? tr("Enabled · linked target") : tr("Enabled · custom");
}
QHash<int, QByteArray> TargetListModel::roleNames() const
{
    return { { PathRole, "targetPath" }, { NameRole, "targetName" }, { StateRole, "targetState" },
        { HostRole, "targetHost" }, { ProblemsRole, "targetProblems" } };
}
QVariant TargetListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_visible.size())
        return { };
    const auto& e = m_entries.at(m_visible.at(index.row()));
    if (role == PathRole)
        return e.path;
    if (role == NameRole)
        return e.object.value(QStringLiteral("displayName")).toString(QFileInfo(e.path).fileName());
    if (role == StateRole)
        return kindName(e);
    if (role == HostRole)
        return QUrl(e.object.value(QStringLiteral("request")).toObject().value(QStringLiteral("url")).toString())
            .host();
    if (role == ProblemsRole)
        return e.problems;
    return { };
}
void TargetListModel::setEntries(const QList<TargetFileStore::Entry>& entries)
{
    beginResetModel();
    m_entries = entries;
    rebuild();
    endResetModel();
}
void TargetListModel::filter(const QString& search, int kind)
{
    beginResetModel();
    m_search = search;
    m_filter = kind;
    rebuild();
    endResetModel();
}
void TargetListModel::rebuild()
{
    using Kind = TargetFileStore::Kind;
    m_visible.clear();
    for (int i = 0; i < m_entries.size(); ++i) {
        const auto& e = m_entries.at(i);
        bool show = true;
        switch (m_filter) {
        case 0:
            show = e.editable();
            break;
        case 1:
            show = e.kind == Kind::Active;
            break;
        case 2:
            show = e.kind == Kind::Disabled;
            break;
        case 3:
            show = !e.editable();
            break;
        case 4:
            show = !e.problems.isEmpty();
            break;
        }
        const auto searchable = e.path + QLatin1Char(' ') + kindName(e) + QLatin1Char(' ')
            + e.object.value(QStringLiteral("displayName")).toString() + QLatin1Char(' ')
            + QUrl(e.object.value(QStringLiteral("request")).toObject().value(QStringLiteral("url")).toString()).host();
        if (show && searchable.contains(m_search, Qt::CaseInsensitive))
            m_visible.append(i);
    }
}
int TargetListModel::indexOfPath(const QString& path) const
{
    for (int i = 0; i < m_visible.size(); ++i)
        if (m_entries.at(m_visible.at(i)).path == path)
            return i;
    return -1;
}
