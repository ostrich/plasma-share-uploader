#pragma once
#include "pickercontroller.h"
#include <QPointer>
#include <QQuickWindow>
#include <memory>

class QQmlEngine;

// Owns one private QML engine and window for the lifetime of a Purpose job's picker.
class TargetPickerDialog final : public QObject {
    Q_OBJECT
public:
    explicit TargetPickerDialog(const QList<TargetDefinition>& targets,
        const QList<TargetDiagnostic>& diagnostics = { }, QWindow* transientParent = nullptr,
        QObject* parent = nullptr);
    ~TargetPickerDialog() override;
    TargetDefinition selectedTarget() const { return m_controller.selectedTarget(); }
    void open();
    void hide();
    void reject() { m_controller.reject(); }
    QQuickWindow* window() const { return m_window; }
signals:
    void reloadRequested();
    void finished(bool accepted);
    void loadFailed(const QString& error);

private:
    PickerController m_controller;
    std::unique_ptr<QQmlEngine> m_engine;
    QPointer<QQuickWindow> m_window;
    QPointer<QWindow> m_transientParent;
    bool m_failed = false;
};
