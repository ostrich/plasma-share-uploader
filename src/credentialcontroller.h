#pragma once
#include "credentialstore.h"
#include "targetdraft.h"

class CredentialController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the target manager")
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(QStringList references READ references NOTIFY changed)
    Q_PROPERTY(QString suggestedKey READ suggestedKey NOTIFY resetRequested)
public:
    CredentialController(TargetDraft* draft, CredentialStore* store, QObject* parent = nullptr);
    bool busy() const { return m_busy; }
    QString message() const { return m_message; }
    QStringList references() const;
    QString suggestedKey() const;
    void reset();
    Q_INVOKABLE void check();
    Q_INVOKABLE void bind(int mode, int location, const QString& field, const QString& key, const QString& secret,
        const QString& username, bool saveSecret);
    Q_INVOKABLE void remove(const QString& key);
    Q_INVOKABLE void removeBinding(int mode, int location, const QString& field);
signals:
    void changed();
    void resetRequested();
    void clearSecret();

private:
    TargetDraft* m_draft;
    CredentialStore* m_store;
    QString m_message;
    bool m_busy = false;
    quint64 m_generation = 0;
};
