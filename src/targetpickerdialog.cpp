#include "targetpickerdialog.h"

#include "targeticonprovider.h"

#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMessageBox>
#include <QPalette>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>

namespace {
constexpr int kIconEdgePadding = 9;
constexpr int kIconTextGap = 12;
constexpr int kButtonVerticalPadding = 8;
constexpr int kIconExtent = 36;

class TargetPickerButton final : public QPushButton
{
public:
    explicit TargetPickerButton(const TargetDefinition &target, QWidget *parent = nullptr)
        : QPushButton(parent)
    {
        setCursor(Qt::PointingHandCursor);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
        setStyleSheet(QStringLiteral(
            "QPushButton { text-align: left; padding: 0; }"
            "QPushButton:focus { outline: none; }"));

        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(kIconEdgePadding, kButtonVerticalPadding, kIconEdgePadding, kButtonVerticalPadding);
        layout->setSpacing(kIconTextGap);

        m_iconLabel = new QLabel(this);
        m_iconLabel->setFixedSize(kIconExtent, kIconExtent);
        m_iconLabel->setAlignment(Qt::AlignCenter);
        layout->addWidget(m_iconLabel, 0, Qt::AlignTop);

        auto *textLayout = new QVBoxLayout();
        textLayout->setContentsMargins(0, 0, 0, 0);
        textLayout->setSpacing(2);

        auto *titleLabel = new QLabel(target.displayName(), this);
        QFont titleFont = titleLabel->font();
        titleFont.setBold(true);
        titleLabel->setFont(titleFont);
        titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        textLayout->addWidget(titleLabel);

        auto *descriptionLabel = new QLabel(target.description(), this);
        descriptionLabel->setWordWrap(true);
        descriptionLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        QPalette descriptionPalette = descriptionLabel->palette();
        descriptionPalette.setColor(QPalette::WindowText,
                                    titleLabel->palette().color(QPalette::Disabled, QPalette::WindowText));
        descriptionLabel->setPalette(descriptionPalette);
        textLayout->addWidget(descriptionLabel);

        layout->addLayout(textLayout, 1);
    }

    QLabel *iconLabel() const
    {
        return m_iconLabel;
    }

private:
    QLabel *m_iconLabel = nullptr;
};
}

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

    QPushButton *firstButton = nullptr;
    for (const TargetDefinition &target : targets) {
        auto *button = new TargetPickerButton(target, content);
        m_iconProvider->applyIcon(button->iconLabel(), target);
        connect(button, &QPushButton::clicked, this, [this, target]() {
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
            messageBox.setIconPixmap(QIcon::fromTheme(QStringLiteral("dialog-error")).pixmap(24, 24));
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
