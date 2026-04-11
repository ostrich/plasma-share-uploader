#include "targetpickerdialog.h"

#include <QCommandLinkButton>
#include <QDialogButtonBox>
#include <QIcon>
#include <QLabel>
#include <QPlainTextEdit>
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

    if (!loadErrors.isEmpty()) {
        auto *warning = new QLabel(
            QStringLiteral("Some targets could not be loaded. Fix your config or remove invalid entries."), this);
        warning->setWordWrap(true);
        layout->addWidget(warning);

        QString details = QStringLiteral("System targets: %1\nUser targets: %2\n\n%3")
                              .arg(systemTargetsPath, userTargetsPath, loadErrors.join(QLatin1Char('\n')));
        auto *detailsView = new QPlainTextEdit(this);
        detailsView->setReadOnly(true);
        detailsView->setPlainText(details);
        detailsView->setMaximumHeight(140);
        layout->addWidget(detailsView);
    }

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

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    if (firstButton) {
        firstButton->setFocus();
    }
}

TargetDefinition TargetPickerDialog::selectedTarget() const
{
    return m_selectedTarget;
}
