#pragma once
#include "jsonrowsmodel.h"
#include "uploadtestrunner.h"
#include <QStandardItemModel>

class TestController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the target manager")
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString file READ file WRITE setFile NOTIFY changed)
    Q_PROPERTY(QString responseUrl READ responseUrl WRITE setResponseUrl NOTIFY changed)
    Q_PROPERTY(int status READ status WRITE setStatus NOTIFY changed)
    Q_PROPERTY(QString responseBody READ responseBody WRITE setResponseBody NOTIFY responseChanged)
    Q_PROPERTY(JsonRowsModel* headers READ headers CONSTANT)
    Q_PROPERTY(QAbstractItemModel* tree READ tree CONSTANT)
    Q_PROPERTY(QString diagnostics READ diagnostics NOTIFY changed)
    Q_PROPERTY(QString preview READ preview NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
public:
    TestController(TargetDraft* draft, CredentialStore* credentials, QObject* parent = nullptr);
    bool busy() const { return m_runner.busy(); }
    QString file() const { return m_file; }
    void setFile(const QString& value)
    {
        if (!busy()) {
            m_file = value;
            emit changed();
        }
    }
    Q_INVOKABLE void setFileUrl(const QUrl& url)
    {
        if (url.isLocalFile())
            setFile(url.toLocalFile());
    }
    QString responseUrl() const { return m_url; }
    void setResponseUrl(const QString& value)
    {
        m_url = value;
        emit changed();
    }
    int status() const { return m_status; }
    void setStatus(int value)
    {
        m_status = qBound(100, value, 599);
        emit changed();
    }
    QString responseBody() const { return m_body; }
    void setResponseBody(const QString& value);
    JsonRowsModel* headers() { return &m_headers; }
    QAbstractItemModel* tree() { return &m_tree; }
    QString diagnostics() const { return m_log; }
    QString preview() const { return m_preview; }
    double progress() const { return m_progress; }
    void reset();
    Q_INVOKABLE void validate();
    Q_INVOKABLE void parseResponse();
    Q_INVOKABLE void start(bool previewOnly);
    Q_INVOKABLE void cancel() { m_runner.cancel(); }
    Q_INVOKABLE void usePointer(const QModelIndex& index, const QString& output);
    Q_INVOKABLE void openPreview();
    Q_INVOKABLE void copyDiagnostics();
    Q_INVOKABLE void clearDiagnostics()
    {
        m_log.clear();
        emit changed();
    }
signals:
    void changed();
    void responseChanged();
    void busyChanged();
    void captureCompleted();

private:
    bool valid();
    void log(const QString& text);
    void rebuildTree();
    TargetDraft* m_draft;
    UploadTestRunner m_runner;
    JsonRowsModel m_headers;
    QStandardItemModel m_tree;
    QString m_file, m_url, m_body, m_log, m_preview;
    int m_status = 200;
    double m_progress = 0;
};
