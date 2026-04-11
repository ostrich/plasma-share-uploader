#include "targetpickerdialog.h"

#include "targeticonprovider.h"

#include <QCommandLinkButton>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>

TargetPickerDialog::TargetPickerDialog(const QList<TargetDefinition> &targets,
                                       const QList<TargetDiagnostic> &diagnostics,
                                       const QString &systemTargetsPath,
                                       const QString &userTargetsPath,
                                       QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Upload..."));
    resize(460, 360);
    m_iconProvider = new TargetIconProvider(this);

    auto *layout = new QVBoxLayout(this);
    auto *label = new QLabel(QStringLiteral("Choose an upload target:"), this);
    layout->addWidget(label);

    auto *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    auto *content = new QWidget(scrollArea);
    auto *contentLayout = new QVBoxLayout(content);

    QCommandLinkButton *firstButton = nullptr;
    for (const TargetDefinition &target : targets) {
        auto *button = new QCommandLinkButton(target.displayName(), target.description(), content);
        button->setStyleSheet(QStringLiteral("QCommandLinkButton { padding: 8px 12px; }"));
        m_iconProvider->applyIcon(button, target);
        connect(button, &QCommandLinkButton::clicked, this, [this, target]() {
            m_selectedTarget = target;
            accept();
        });
        contentLayout->addWidget(button);
        if (!firstButton) {
            firstButton = button;
        }
    }
    contentLayout->addStretch();

    scrollArea->setWidget(content);
    layout->addWidget(scrollArea);

    auto *buttonLayout = new QHBoxLayout();
    if (!diagnostics.isEmpty()) {
        const int errorCount = diagnostics.size();
        Q_UNUSED(systemTargetsPath)
        Q_UNUSED(userTargetsPath)
        QStringList detailLines;
        detailLines.reserve(diagnostics.size());
        for (const TargetDiagnostic &diagnostic : diagnostics) {
            detailLines.append(diagnostic.displayText());
        }
        const QString details = detailLines.join(QLatin1Char('\n'));
        auto *errorButton = new QPushButton(
            QIcon::fromTheme(QStringLiteral("dialog-error")),
            errorCount == 1 ? QStringLiteral("1 error") : QStringLiteral("%1 errors").arg(errorCount),
            this);
        errorButton->setToolTip(QStringLiteral("Show target configuration errors"));
        connect(errorButton, &QPushButton::clicked, this, [this, details, errorCount]() {
            QMessageBox messageBox(this);
            messageBox.setIcon(QMessageBox::Warning);
            messageBox.setWindowTitle(QStringLiteral("Target Configuration Errors"));
            messageBox.setText(errorCount == 1
                                   ? QStringLiteral("One target could not be loaded.")
                                   : QStringLiteral("%1 targets could not be loaded.").arg(errorCount));
            messageBox.setInformativeText(details);
            messageBox.exec();
        });
        buttonLayout->addWidget(errorButton);
    }
    buttonLayout->addStretch();

    auto *cancelButton = new QPushButton(QStringLiteral("Cancel"), this);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    buttonLayout->addWidget(cancelButton);
    layout->addLayout(buttonLayout);

    if (firstButton) {
        firstButton->setFocus();
    }
}

TargetDefinition TargetPickerDialog::selectedTarget() const
{
    return m_selectedTarget;
}
