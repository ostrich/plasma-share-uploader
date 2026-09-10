#include "pickercontroller.h"
#include <QFileInfo>
#include <QMap>
#include <QProcess>
#include <QRegularExpression>

PickerController::PickerController(
    const QList<TargetDefinition>& targets, const QList<TargetDiagnostic>& diagnostics, QObject* parent)
    : QAbstractListModel(parent)
    , m_targets(targets)
    , m_iconProvider(this)
    , m_diagnosticCount(int(diagnostics.size()))
{
    QMap<QString, QStringList> files;
    for (const auto& diagnostic : diagnostics) {
        QString message = diagnostic.message.trimmed();
        message.remove(QRegularExpression(QStringLiteral("^Target '[^']+'\\s+")));
        auto path = diagnostic.jsonPath;
        if (path.startsWith(QLatin1Char('/')))
            path.remove(0, 1);
        path.replace(QLatin1Char('/'), QLatin1Char('.'));
        if (!path.isEmpty()) {
            if (message.startsWith(path + QLatin1Char(' ')))
                message.remove(0, path.size() + 1);
            message.prepend(path + QStringLiteral(": "));
        }
        const auto name = QFileInfo(diagnostic.filePath).fileName();
        files[name.isEmpty() ? tr("(unknown file)") : name].append(message);
    }
    QStringList lines;
    for (auto it = files.cbegin(); it != files.cend(); ++it) {
        lines.append(it.key());
        for (const auto& message : it.value())
            lines.append(QStringLiteral("  • ") + message);
        lines.append(QString { });
    }
    m_diagnostics = lines.join(QLatin1Char('\n')).trimmed();
    m_icons.fill(QStringLiteral("image-x-generic"), targets.size());
    for (int i = 0; i < targets.size(); ++i)
        m_iconProvider.requestIcon(targets.at(i), this, [this, i](const QString& source) {
            m_icons[i] = source;
            emit dataChanged(index(i), index(i), { IconRole });
        });
}
QVariant PickerController::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_targets.size())
        return { };
    const auto& target = m_targets.at(index.row());
    if (role == NameRole)
        return target.displayName();
    if (role == DescriptionRole)
        return target.description();
    if (role == IconRole)
        return m_icons.at(index.row());
    return { };
}
QHash<int, QByteArray> PickerController::roleNames() const
{
    return { { NameRole, "targetName" }, { DescriptionRole, "targetDescription" }, { IconRole, "iconSource" } };
}
void PickerController::choose(int index)
{
    if (m_done || index < 0 || index >= m_targets.size())
        return;
    m_done = true;
    m_selected = index;
    emit finished(true);
}
void PickerController::reject()
{
    if (!m_done) {
        m_done = true;
        emit finished(false);
    }
}
void PickerController::reload()
{
    if (!m_done) {
        m_done = true;
        emit reloadRequested();
    }
}
void PickerController::configure()
{
    QString executable = QStringLiteral(PLASMA_SHARE_UPLOADER_CONFIG_PATH);
#ifdef PLASMA_SHARE_UPLOADER_DEV_CONFIG_PATH
    executable = QStringLiteral(PLASMA_SHARE_UPLOADER_DEV_CONFIG_PATH);
#endif
    if (!QProcess::startDetached(executable, { })) {
        m_message = tr("Could not start %1").arg(executable);
        emit messageChanged();
    }
}
