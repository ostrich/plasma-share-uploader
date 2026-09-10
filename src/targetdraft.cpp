#include "targetdraft.h"
#include "jsonutils.h"
#include "targetfilestore.h"
#include <QJsonDocument>

QStringList TargetDraft::problems() const { return TargetFileStore::validate(m_bytes) + m_errors.values(); }
void TargetDraft::load(const QByteArray& bytes)
{
    m_bytes = bytes;
    const auto doc = QJsonDocument::fromJson(bytes);
    m_hasObject = doc.isObject();
    m_document = doc.object();
    m_errors.clear();
    m_bodyDraft = ConfigJson::text(
        ConfigJson::get(m_document, { QStringLiteral("request"), QStringLiteral("body"), QStringLiteral("value") }));
    ++m_revision;
    emit reset();
    emit documentChanged();
    emit jsonBodyChanged();
    emit problemsChanged();
}
void TargetDraft::setRawText(const QString& text)
{
    if (!m_editable || text.toUtf8() == m_bytes)
        return;
    load(text.toUtf8());
    emit edited();
}
void TargetDraft::setEditable(bool value)
{
    if (m_editable == value)
        return;
    m_editable = value;
    emit editableChanged();
}
QVariant TargetDraft::value(const QStringList& path) const { return ConfigJson::get(m_document, path).toVariant(); }
QString TargetDraft::stringValue(const QStringList& path, const QString& fallback) const
{
    return ConfigJson::get(m_document, path).toString(fallback);
}
void TargetDraft::setValue(const QStringList& path, const QVariant& value)
{
    setField(path, ConfigJson::fromVariant(value));
}
void TargetDraft::removeField(const QStringList& path) { setField(path, QJsonValue(QJsonValue::Undefined)); }
void TargetDraft::setField(const QStringList& path, const QJsonValue& value)
{
    if (!m_editable || !m_hasObject || path.isEmpty())
        return;
    const auto updated = ConfigJson::set(m_document, path, value).toObject();
    if (updated == m_document)
        return;
    m_document = updated;
    m_bytes = QJsonDocument(m_document).toJson();
    ++m_revision;
    const QStringList bodyPath { QStringLiteral("request"), QStringLiteral("body"), QStringLiteral("value") };
    const auto common = qMin(path.size(), bodyPath.size());
    if (!m_editingBody && path.mid(0, common) == bodyPath.mid(0, common)) {
        m_bodyDraft = ConfigJson::text(ConfigJson::get(m_document, bodyPath));
        m_errors.remove(QStringLiteral("jsonBody"));
        emit jsonBodyChanged();
    }
    emit fieldChanged(path);
    emit documentChanged();
    emit problemsChanged();
    emit edited();
}
void TargetDraft::setBodyType(const QString& type)
{
    if (type != QLatin1StringView("multipart") && type != QLatin1StringView("raw")
        && type != QLatin1StringView("form_urlencoded") && type != QLatin1StringView("json"))
        return;
    auto body = ConfigJson::get(m_document, { QStringLiteral("request"), QStringLiteral("body") }).toObject();
    body.insert(QStringLiteral("type"), type);
    if (type == QLatin1StringView("multipart") && !body.contains(QStringLiteral("fileField")))
        body.insert(QStringLiteral("fileField"), QStringLiteral("file"));
    if ((type == QLatin1StringView("multipart") || type == QLatin1StringView("form_urlencoded"))
        && !body.contains(QStringLiteral("fields")))
        body.insert(QStringLiteral("fields"), QJsonObject { });
    if (type == QLatin1StringView("json") && !body.contains(QStringLiteral("value")))
        body.insert(QStringLiteral("value"), QJsonObject { });
    setField({ QStringLiteral("request"), QStringLiteral("body") }, body);
}
void TargetDraft::setExtractorType(const QStringList& path, const QString& type)
{
    if (type.isEmpty()) {
        removeField(path);
        return;
    }
    auto object = ConfigJson::get(m_document, path).toObject();
    object.insert(QStringLiteral("type"), type);
    if (type == QLatin1StringView("json_pointer") && !object.contains(QStringLiteral("pointer")))
        object.insert(QStringLiteral("pointer"), QString());
    setField(path, object);
}
QString TargetDraft::jsonBody() const { return m_bodyDraft; }
void TargetDraft::setJsonBody(const QString& text)
{
    if (!m_editable || !m_hasObject || text == m_bodyDraft)
        return;
    QJsonValue value;
    QString error;
    m_bodyDraft = text;
    if (ConfigJson::parse(text, &value, &error)) {
        setFormError(QStringLiteral("jsonBody"), { });
        m_editingBody = true;
        setField({ QStringLiteral("request"), QStringLiteral("body"), QStringLiteral("value") }, value);
        m_editingBody = false;
    } else
        setFormError(QStringLiteral("jsonBody"), tr("JSON body: %1").arg(error));
    ++m_revision;
    emit jsonBodyChanged();
    emit edited();
}
void TargetDraft::setFormError(const QString& key, const QString& message)
{
    if (m_errors.value(key) == message)
        return;
    if (message.isEmpty())
        m_errors.remove(key);
    else
        m_errors.insert(key, message);
    ++m_revision;
    emit problemsChanged();
    emit edited();
}
