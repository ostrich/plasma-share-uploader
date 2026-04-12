#include "targetpickerdialog.h"

#include "targeticonprovider.h"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMap>
#include <QPlainTextEdit>
#include <QPalette>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>

namespace {
constexpr int kIconEdgePadding = 9;
constexpr int kIconTextGap = 12;
constexpr int kButtonVerticalPadding = 8;
constexpr int kIconExtent = 36;

QString pickerDiagnosticText(const TargetDiagnostic &diagnostic)
{
    QString message = diagnostic.message.trimmed();
    static const QRegularExpression targetPrefix(QStringLiteral("^Target '[^']+'\\s+"));
    message.remove(targetPrefix);

    if (diagnostic.jsonPath.isEmpty()) {
        return message;
    }

    QString dottedPath = diagnostic.jsonPath;
    if (dottedPath.startsWith(QLatin1Char('/'))) {
        dottedPath.remove(0, 1);
    }
    dottedPath.replace(QLatin1Char('/'), QLatin1Char('.'));

    const QString pathPrefix = dottedPath + QLatin1Char(' ');
    if (message.startsWith(pathPrefix)) {
        message.remove(0, pathPrefix.size());
    }

    return QStringLiteral("%1: %2").arg(dottedPath, message);
}

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
        QMap<QString, QStringList> diagnosticsByFile;
        for (const TargetDiagnostic &diagnostic : diagnostics) {
            const QString fileName = QFileInfo(diagnostic.filePath).fileName();
            diagnosticsByFile[fileName.isEmpty() ? QStringLiteral("(unknown file)") : fileName].append(pickerDiagnosticText(diagnostic));
        }
        QStringList detailLines;
        for (auto it = diagnosticsByFile.cbegin(); it != diagnosticsByFile.cend(); ++it) {
            detailLines.append(it.key());
            for (const QString &message : it.value()) {
                detailLines.append(QStringLiteral("  - %1").arg(message));
            }
            detailLines.append(QString());
        }
        const QString details = detailLines.join(QLatin1Char('\n')).trimmed();
        const int fileCount = diagnosticsByFile.size();
        auto *errorButton = new QPushButton(
            QIcon::fromTheme(QStringLiteral("dialog-error")),
            errorCount == 1 ? QStringLiteral("1 error") : QStringLiteral("%1 errors").arg(errorCount),
            this);
        errorButton->setToolTip(QStringLiteral("Show target configuration errors"));
        connect(errorButton, &QPushButton::clicked, this, [this, details, errorCount, fileCount]() {
            auto *dialog = new QDialog(this);
            dialog->setWindowTitle(QStringLiteral("Target Configuration Errors"));
            dialog->resize(560, 360);

            auto *dialogLayout = new QVBoxLayout(dialog);
            auto *headerLayout = new QHBoxLayout();
            auto *iconLabel = new QLabel(dialog);
            iconLabel->setPixmap(QIcon::fromTheme(QStringLiteral("dialog-error")).pixmap(24, 24));
            headerLayout->addWidget(iconLabel, 0, Qt::AlignTop);

            QString summary;
            if (fileCount == 1) {
                summary = errorCount == 1
                    ? QStringLiteral("1 error in 1 file.")
                    : QStringLiteral("%1 errors in 1 file.").arg(errorCount);
            } else {
                summary = QStringLiteral("%1 errors in %2 files.").arg(errorCount).arg(fileCount);
            }
            auto *summaryLabel = new QLabel(summary, dialog);
            summaryLabel->setWordWrap(true);
            headerLayout->addWidget(summaryLabel, 1);
            dialogLayout->addLayout(headerLayout);

            auto *detailsEdit = new QPlainTextEdit(dialog);
            detailsEdit->setReadOnly(true);
            detailsEdit->setLineWrapMode(QPlainTextEdit::WidgetWidth);
            detailsEdit->setPlainText(details);
            dialogLayout->addWidget(detailsEdit, 1);

            auto *okLayout = new QHBoxLayout();
            okLayout->addStretch();
            auto *okButton = new QPushButton(QStringLiteral("OK"), dialog);
            connect(okButton, &QPushButton::clicked, dialog, &QDialog::accept);
            okLayout->addWidget(okButton);
            dialogLayout->addLayout(okLayout);

            dialog->exec();
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
