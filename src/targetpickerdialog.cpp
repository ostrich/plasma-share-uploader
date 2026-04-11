#include "targetpickerdialog.h"

#include <QCommandLinkButton>
#include <QDialogButtonBox>
#include <QIcon>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>

TargetPickerDialog::TargetPickerDialog(const QList<TargetDefinition> &targets, QWidget *parent)
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

    for (const TargetDefinition &target : targets) {
        auto *button = new QCommandLinkButton(target.displayName(), target.description(), content);
        button->setIcon(QIcon::fromTheme(target.icon()));
        connect(button, &QCommandLinkButton::clicked, this, [this, target]() {
            m_selectedTarget = target;
            accept();
        });
        contentLayout->addWidget(button);
    }
    contentLayout->addStretch();

    scrollArea->setWidget(content);
    layout->addWidget(scrollArea);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

TargetDefinition TargetPickerDialog::selectedTarget() const
{
    return m_selectedTarget;
}
