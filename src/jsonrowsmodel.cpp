#include "jsonrowsmodel.h"
#include "jsonutils.h"
#include <QJsonArray>
#include <QSet>

JsonRowsModel::JsonRowsModel(QObject* parent)
    : QAbstractListModel(parent)
{
}
int JsonRowsModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : count();
}
QHash<int, QByteArray> JsonRowsModel::roleNames() const
{
    return { { NameRole, "rowName" }, { TextRole, "rowText" }, { LabelRole, "rowLabel" } };
}
QVariant JsonRowsModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= count())
        return { };
    const auto& [name, value] = m_rows.at(index.row());
    if (role == NameRole)
        return name;
    if (role == TextRole)
        return value.isString() ? value.toString() : ConfigJson::text(value);
    if (role == LabelRole) {
        if (value.isObject()) {
            const auto object = value.toObject();
            if (object.contains(QStringLiteral("mime"))) {
                QStringList mimes;
                for (const auto& v : object.value(QStringLiteral("mime")).toArray())
                    mimes.append(v.toString());
                return mimes.isEmpty() ? tr("Incomplete rule") : mimes.join(QStringLiteral(", "));
            }
            const auto args = object.value(QStringLiteral("argv")).toArray();
            return args.isEmpty() ? tr("Incomplete command") : args.first().toString();
        }
        return value.isString() ? value.toString() : ConfigJson::text(value);
    }
    return { };
}
void JsonRowsModel::setDraft(TargetDraft* draft)
{
    if (m_draft == draft)
        return;
    if (m_draft)
        disconnect(m_draft, nullptr, this, nullptr);
    m_draft = draft;
    if (draft) {
        connect(draft, &TargetDraft::reset, this, &JsonRowsModel::reload);
        connect(draft, &TargetDraft::fieldChanged, this, [this](const QStringList& path) {
            if (m_publishing)
                return;
            const auto common = qMin(path.size(), m_path.size());
            if (path.mid(0, common) != m_path.mid(0, common))
                return;
            if (!m_map && path.size() > m_path.size()) {
                bool ok = false;
                const int row = path.at(m_path.size()).toInt(&ok);
                if (ok && row >= 0 && row < count()) {
                    auto rowPath = m_path;
                    rowPath.append(QString::number(row));
                    m_rows[row].second = ConfigJson::get(m_draft->document(), rowPath);
                    emit dataChanged(index(row), index(row), { TextRole, LabelRole });
                    emit valueChanged();
                    return;
                }
            }
            reload();
        });
    }
    reload();
    emit sourceChanged();
}
void JsonRowsModel::setPath(const QStringList& path)
{
    if (m_path == path)
        return;
    m_path = path;
    reload();
    emit sourceChanged();
}
void JsonRowsModel::setMap(bool map)
{
    if (m_map == map)
        return;
    m_map = map;
    reload();
    emit sourceChanged();
}
void JsonRowsModel::reload()
{
    if (m_draft)
        assign(ConfigJson::get(m_draft->document(), m_path));
}
void JsonRowsModel::assign(const QJsonValue& value)
{
    beginResetModel();
    m_rows.clear();
    if (m_map) {
        const auto object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it)
            m_rows.append({ it.key(), it.value() });
    } else
        for (const auto& v : value.toArray())
            m_rows.append({ { }, v });
    endResetModel();
    emit countChanged();
    emit valueChanged();
}
void JsonRowsModel::setValue(const QVariant& value)
{
    if (!m_draft)
        assign(ConfigJson::fromVariant(value));
}
QString JsonRowsModel::error() const
{
    if (!m_map)
        return { };
    QSet<QString> keys;
    for (const auto& [name, value] : m_rows) {
        if (name.isEmpty())
            return tr("Field names cannot be empty.");
        if (keys.contains(name))
            return tr("Duplicate field name: %1").arg(name);
        keys.insert(name);
    }
    return { };
}
QJsonValue JsonRowsModel::jsonValue() const
{
    QJsonObject object;
    QJsonArray array;
    for (const auto& [name, value] : m_rows) {
        if (m_map)
            object.insert(name, value);
        else
            array.append(value);
    }
    return m_map ? QJsonValue(object) : QJsonValue(array);
}
bool JsonRowsModel::editable() const
{
    return !m_draft || (m_draft->editable() && m_draft->hasObject());
}
void JsonRowsModel::publish()
{
    if (m_draft) {
        m_publishing = true;
        m_draft->setFormError(m_path.join(QLatin1Char('/')), error());
        if (error().isEmpty())
            m_draft->setField(m_path, jsonValue());
        m_publishing = false;
    }
    emit valueChanged();
}
void JsonRowsModel::edit(int row, int column, const QString& text)
{
    if (!editable() || row < 0 || row >= count() || column < 0 || column > (m_map ? 1 : 0))
        return;
    if (m_map && column == 0)
        m_rows[row].first = text;
    else
        m_rows[row].second = text;
    emit dataChanged(index(row), index(row), { NameRole, TextRole, LabelRole });
    publish();
}
void JsonRowsModel::append(const QVariant& value)
{
    if (!editable())
        return;
    beginInsertRows({ }, count(), count());
    m_rows.append({ { }, ConfigJson::fromVariant(value) });
    endInsertRows();
    emit countChanged();
    publish();
}
void JsonRowsModel::remove(int row)
{
    if (!editable() || row < 0 || row >= count())
        return;
    beginRemoveRows({ }, row, row);
    m_rows.removeAt(row);
    endRemoveRows();
    emit countChanged();
    publish();
}
void JsonRowsModel::move(int row, int delta)
{
    const int to = row + delta;
    if (!editable() || m_map || row < 0 || row >= count() || to < 0 || to >= count() || row == to)
        return;
    beginMoveRows({ }, row, row, { }, to > row ? to + 1 : to);
    m_rows.move(row, to);
    endMoveRows();
    publish();
}
