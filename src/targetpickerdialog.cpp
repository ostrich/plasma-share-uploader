#include "targetpickerdialog.h"

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
                                       const QStringList &loadErrors,
                                       const QString &systemTargetsPath,
                                       const QString &userTargetsPath,
                                       QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Upload..."));
    resize(460, 360);

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
        button->setIcon(QIcon::fromTheme(target.icon()));
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
    if (!loadErrors.isEmpty()) {
        const int errorCount = loadErrors.size();
        const QString details = QStringLiteral("System targets: %1\nUser targets: %2\n\n%3")
                                    .arg(systemTargetsPath, userTargetsPath, loadErrors.join(QLatin1Char('\n')));
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
            messageBox.setInformativeText(QStringLiteral("Fix the invalid target files or remove them."));
            messageBox.setDetailedText(details);
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
