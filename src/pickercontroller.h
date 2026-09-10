#pragma once
#include "targetdefinition.h"
#include "targetdiagnostic.h"
#include "targeticonprovider.h"
#include <QAbstractListModel>
#include <QtQmlIntegration/qqmlintegration.h>

class PickerController : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by a share job")
    Q_PROPERTY(QString diagnosticText READ diagnosticText CONSTANT)
    Q_PROPERTY(int diagnosticCount READ diagnosticCount CONSTANT)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
public:
    PickerController(
        const QList<TargetDefinition>& targets, const QList<TargetDiagnostic>& diagnostics, QObject* parent = nullptr);
    enum Role { NameRole = Qt::UserRole + 1, DescriptionRole, IconRole };
    int rowCount(const QModelIndex& parent = { }) const override
    {
        return parent.isValid() ? 0 : int(m_targets.size());
    }
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    QString diagnosticText() const { return m_diagnostics; }
    int diagnosticCount() const { return m_diagnosticCount; }
    QString message() const { return m_message; }
    TargetDefinition selectedTarget() const
    {
        return m_selected >= 0 ? m_targets.at(m_selected) : TargetDefinition { };
    }
    Q_INVOKABLE void choose(int index);
    Q_INVOKABLE void reject();
    Q_INVOKABLE void reload();
    Q_INVOKABLE void configure();
signals:
    void finished(bool accepted);
    void reloadRequested();
    void messageChanged();

private:
    QList<TargetDefinition> m_targets;
    QStringList m_icons;
    TargetIconProvider m_iconProvider;
    QString m_diagnostics, m_message;
    int m_diagnosticCount = 0, m_selected = -1;
    bool m_done = false;
};
