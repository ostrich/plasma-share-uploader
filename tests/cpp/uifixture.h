#pragma once
#include "targetmanagercontroller.h"
#include "httpcaptureserver.h"
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <memory>
class UiFixture : public QObject {
    Q_OBJECT
    Q_PROPERTY(TargetManagerController* manager READ manager NOTIFY changed)
public:
    using QObject::QObject;
    TargetManagerController* manager() { return m_manager.get(); }
    Q_INVOKABLE QQuickItem* findItem(QQuickWindow* window, const QString& name);
    Q_INVOKABLE void reset();
    Q_INVOKABLE QString path(const QString& name) const;
    Q_INVOKABLE QString saved(const QString& name) const;
    Q_INVOKABLE void externalChange();
    Q_INVOKABLE void pressKey(QQuickItem* item, int key);
    Q_INVOKABLE void typeText(QQuickItem* item, const QString& text);
    Q_INVOKABLE bool screenshot(QQuickWindow* window, const QString& name);
    Q_INVOKABLE bool redIcon(QQuickWindow* window);
    Q_INVOKABLE void startServer();
    Q_INVOKABLE int requestCount() const;
    Q_INVOKABLE void clear();
    Q_INVOKABLE QUrl transferFile() const;
    Q_INVOKABLE QString exported() const;
signals:
    void changed();

private:
    void write(const QString& name, const QByteArray& bytes);
    std::unique_ptr<TargetManagerController> m_manager;
    std::unique_ptr<QTemporaryDir> m_files;
    std::unique_ptr<HttpCaptureServer> m_server;
};
