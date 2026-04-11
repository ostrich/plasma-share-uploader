#pragma once

#include "targetdiagnostic.h"
#include "targetdefinition.h"

#include <QDialog>

class TargetIconProvider;

class TargetPickerDialog final : public QDialog
{
    Q_OBJECT
public:
    explicit TargetPickerDialog(const QList<TargetDefinition> &targets,
                                const QList<TargetDiagnostic> &diagnostics = {},
                                const QString &systemTargetsPath = {},
                                const QString &userTargetsPath = {},
                                QWidget *parent = nullptr);

    TargetDefinition selectedTarget() const;

private:
    TargetDefinition m_selectedTarget;
    TargetIconProvider *m_iconProvider = nullptr;
};
