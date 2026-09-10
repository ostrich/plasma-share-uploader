#pragma once
#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QStringList>
#include <QtQmlIntegration/qqmlintegration.h>

class TargetDraft : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString rawText READ rawText WRITE setRawText NOTIFY documentChanged)
    Q_PROPERTY(QJsonObject document READ document NOTIFY documentChanged)
    Q_PROPERTY(bool hasObject READ hasObject NOTIFY documentChanged)
    Q_PROPERTY(bool editable READ editable WRITE setEditable NOTIFY editableChanged)
    Q_PROPERTY(QStringList problems READ problems NOTIFY problemsChanged)
    Q_PROPERTY(bool formErrors READ hasFormErrors NOTIFY problemsChanged)
    Q_PROPERTY(QString jsonBody READ jsonBody WRITE setJsonBody NOTIFY jsonBodyChanged)
public:
    using QObject::QObject;
    QString rawText() const { return QString::fromUtf8(m_bytes); }
    QByteArray bytes() const { return m_bytes; }
    QJsonObject document() const { return m_document; }
    bool hasObject() const { return m_hasObject; }
    bool editable() const { return m_editable; }
    quint64 revision() const { return m_revision; }
    QStringList problems() const;
    bool hasFormErrors() const { return !m_errors.isEmpty(); }
    void load(const QByteArray& bytes);
    void setRawText(const QString& text);
    void setEditable(bool value);
    Q_INVOKABLE QVariant value(const QStringList& path) const;
    Q_INVOKABLE QString stringValue(const QStringList& path, const QString& fallback = { }) const;
    Q_INVOKABLE void setValue(const QStringList& path, const QVariant& value);
    void setField(const QStringList& path, const QJsonValue& value);
    Q_INVOKABLE void removeField(const QStringList& path);
    Q_INVOKABLE void setBodyType(const QString& type);
    Q_INVOKABLE void setExtractorType(const QStringList& path, const QString& type);
    QString jsonBody() const;
    void setJsonBody(const QString& value);
    void setFormError(const QString& key, const QString& message);
signals:
    void documentChanged();
    void jsonBodyChanged();
    void problemsChanged();
    void editableChanged();
    void edited();
    void reset();
    void fieldChanged(const QStringList& path);

private:
    QByteArray m_bytes = "{}";
    QJsonObject m_document;
    QMap<QString, QString> m_errors;
    QString m_bodyDraft;
    bool m_hasObject = true;
    bool m_editable = true;
    bool m_editingBody = false;
    quint64 m_revision = 0;
};
