#include "targetpickerdialog.h"
#include <QQmlComponent>
#include <QQmlEngine>
#include <QtQml/QQmlExtensionPlugin>

Q_IMPORT_QML_PLUGIN(ShareUploaderPlugin)

TargetPickerDialog::TargetPickerDialog(const QList<TargetDefinition>& targets,
    const QList<TargetDiagnostic>& diagnostics, QWindow* transientParent, QObject* parent)
    : QObject(parent)
    , m_controller(targets, diagnostics, this)
    , m_transientParent(transientParent)
{
    connect(&m_controller, &PickerController::finished, this, &TargetPickerDialog::finished);
    connect(&m_controller, &PickerController::reloadRequested, this, &TargetPickerDialog::reloadRequested);
}
TargetPickerDialog::~TargetPickerDialog()
{
    delete m_window.data();
}
void TargetPickerDialog::hide()
{
    if (m_window)
        m_window->hide();
}
void TargetPickerDialog::open()
{
    if (m_engine)
        return;
    m_engine = std::make_unique<QQmlEngine>();
    QQmlComponent component(m_engine.get());
    component.loadFromModule(
        QStringLiteral("ShareUploader"), QStringLiteral("PickerWindow"), QQmlComponent::PreferSynchronous);
    QObject* root = component.createWithInitialProperties(
        { { QStringLiteral("controller"), QVariant::fromValue(&m_controller) } });
    m_window = qobject_cast<QQuickWindow*>(root);
    if (!m_window) {
        delete root;
        emit loadFailed(tr("Could not load upload target picker: %1").arg(component.errorString()));
        return;
    }
    m_window->setTransientParent(m_transientParent);
    m_window->setModality(m_transientParent ? Qt::WindowModal : Qt::ApplicationModal);
    connect(
        m_window, &QQuickWindow::sceneGraphError, this, [this](QQuickWindow::SceneGraphError, const QString& message) {
            if (m_failed)
                return;
            m_failed = true;
            hide();
            emit loadFailed(tr("Could not render upload target picker: %1").arg(message));
        });
    m_window->show();
    m_window->requestActivate();
}
