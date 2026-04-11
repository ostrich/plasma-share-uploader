#pragma once

#include <QFileInfo>
#include <QString>

enum class TargetDiagnosticSeverity
{
    Error
};

struct TargetDiagnostic
{
    TargetDiagnosticSeverity severity = TargetDiagnosticSeverity::Error;
    QString filePath;
    QString jsonPath;
    QString code;
    QString message;

    QString displayText() const
    {
        if (filePath.isEmpty()) {
            return message;
        }

        const QString fileName = QFileInfo(filePath).fileName();
        if (fileName.isEmpty()) {
            return QStringLiteral("%1: %2").arg(filePath, message);
        }
        return QStringLiteral("%1: %2").arg(fileName, message);
    }
};
