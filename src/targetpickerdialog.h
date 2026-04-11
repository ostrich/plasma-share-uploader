#pragma once

#include "targetdefinition.h"

#include <QDialog>

class TargetPickerDialog final : public QDialog
{
    Q_OBJECT
public:
    explicit TargetPickerDialog(const QList<TargetDefinition> &targets,
                                const QStringList &loadErrors = {},
                                const QString &systemTargetsPath = {},
                                const QString &userTargetsPath = {},
                                QWidget *parent = nullptr);

    TargetDefinition selectedTarget() const;

private:
    TargetDefinition m_selectedTarget;
};
