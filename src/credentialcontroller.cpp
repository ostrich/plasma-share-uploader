#include "credentialcontroller.h"
#include "targetconfigparser.h"

namespace {
QStringList bindingPath(int mode, int location, const QString& field)
{
    if (mode < 0 || mode > 2 || location < 0 || location > 4 || (mode == 0 && field.isEmpty()))
        return { };
    QStringList path { QStringLiteral("request") };
    if (mode != 0 || location == 0)
        path.append(QStringLiteral("headers"));
    else if (location == 1)
        path.append(QStringLiteral("query"));
    else if (location == 2)
        path.append({ QStringLiteral("body"), QStringLiteral("fields") });
    else if (location == 3)
        path.append({ QStringLiteral("body"), QStringLiteral("fields") });
    else
        path.append({ QStringLiteral("body"), QStringLiteral("value") });
    path.append(mode == 0 ? field : QStringLiteral("Authorization"));
    return path;
}
}

CredentialController::CredentialController(TargetDraft* draft, CredentialStore* store, QObject* parent)
    : QObject(parent)
    , m_draft(draft)
    , m_store(store)
{
    connect(draft, &TargetDraft::documentChanged, this, &CredentialController::changed);
}
QStringList CredentialController::references() const
{
    ParsedTargetConfig parsed;
    TargetConfigParser::parse(m_draft->document(), &parsed);
    return CredentialStore::walletKeys(parsed.request);
}
QString CredentialController::suggestedKey() const
{
    const auto keys = references();
    return keys.isEmpty() ? m_draft->document().value(QStringLiteral("id")).toString() + QStringLiteral(".api")
                          : keys.first();
}
void CredentialController::reset()
{
    ++m_generation;
    m_message.clear();
    emit resetRequested();
    emit changed();
}
void CredentialController::check()
{
    if (m_busy)
        return;
    ParsedTargetConfig parsed;
    if (!m_draft->problems().isEmpty() || !TargetConfigParser::parse(m_draft->document(), &parsed)) {
        m_message = tr("Fix configuration errors first.");
        emit changed();
        return;
    }
    const auto missing = CredentialStore::missingEnvironment(parsed.request);
    if (!missing.isEmpty()) {
        m_message = tr("Missing environment values: %1").arg(missing.join(QStringLiteral(", ")));
        emit changed();
        return;
    }
    const auto revision = m_draft->revision(), generation = m_generation;
    m_busy = true;
    emit changed();
    m_store->read(CredentialStore::walletKeys(parsed.request), this,
        [this, revision, generation](CredentialStore::Values, const QString& error) {
            m_busy = false;
            if (generation == m_generation)
                m_message = revision != m_draft->revision() ? tr("The draft changed; check its credentials again.")
                    : error.isEmpty() ? tr("Referenced credentials are available. No upload performed.")
                                      : error;
            emit changed();
        });
}
void CredentialController::bind(int mode, int location, const QString& field, const QString& key, const QString& secret,
    const QString& username, bool saveSecret)
{
    if (m_busy || !m_draft->editable() || !m_draft->hasObject())
        return;
    if (m_draft->hasFormErrors()) {
        m_message = tr("Repair incomplete table or JSON body edits before binding credentials.");
        emit changed();
        return;
    }
    if (!CredentialStore::validKey(key) || (mode == 0 && field.isEmpty()) || mode < 0 || mode > 2 || location < 0
        || location > 4) {
        m_message = tr("Enter a valid credential name and request field.");
        emit changed();
        return;
    }
    const auto request = m_draft->document().value(QStringLiteral("request")).toObject();
    const auto body = request.value(QStringLiteral("body")).toObject();
    const auto type = body.value(QStringLiteral("type")).toString();
    if (mode == 0
        && ((location == 2 && type != QLatin1StringView("multipart"))
            || (location == 3 && type != QLatin1StringView("form_urlencoded"))
            || (location == 4
                && (type != QLatin1StringView("json") || !body.value(QStringLiteral("value")).isObject())))) {
        m_message = tr("Choose a placement matching the request body format. JSON placement requires an object body.");
        emit changed();
        return;
    }
    if (saveSecret && (secret.isEmpty() || (mode == 2 && username.contains(QLatin1Char(':'))))) {
        m_message = tr("Enter a nonempty secret. A basic-auth username cannot contain a colon.");
        emit changed();
        return;
    }
    const auto path = bindingPath(mode, location, field);
    QString reference = QStringLiteral("${WALLET:%1}").arg(key);
    QString value = secret;
    if (mode == 1)
        reference.prepend(QStringLiteral("Bearer "));
    if (mode == 2) {
        reference.prepend(QStringLiteral("Basic "));
        value = QString::fromLatin1((username + QLatin1Char(':') + secret).toUtf8().toBase64());
    }
    const auto revision = m_draft->revision(), generation = m_generation;
    m_busy = true;
    emit changed();
    auto apply = [this, revision, generation, path, reference](const QString& error) {
        m_busy = false;
        if (generation != m_generation) {
            emit changed();
            return;
        }
        if (!error.isEmpty()) {
            m_message = error;
            emit changed();
            return;
        }
        emit clearSecret();
        if (revision != m_draft->revision())
            m_message = tr(
                "Credential ready, but the draft changed. Use existing credential to bind it to the intended target.");
        else {
            m_draft->setField(path, reference);
            m_message = tr("Credential reference added to the draft. Save the target to apply it.");
        }
        emit changed();
    };
    if (saveSecret)
        m_store->write(key, value, this, apply);
    else
        m_store->read({ key }, this, [apply](CredentialStore::Values, const QString& error) { apply(error); });
}
void CredentialController::remove(const QString& key)
{
    if (m_busy || !m_draft->editable() || !CredentialStore::validKey(key))
        return;
    const auto generation = m_generation;
    m_busy = true;
    emit changed();
    m_store->remove(key, this, [this, generation](const QString& error) {
        m_busy = false;
        if (generation == m_generation)
            m_message = error.isEmpty() ? tr("Credential deleted. References in targets are retained.") : error;
        emit changed();
    });
}
void CredentialController::removeBinding(int mode, int location, const QString& field)
{
    if (m_busy || !m_draft->editable() || !m_draft->hasObject())
        return;
    if (m_draft->hasFormErrors()) {
        m_message = tr("Repair incomplete table or JSON body edits before removing a request field.");
        emit changed();
        return;
    }
    const auto path = bindingPath(mode, location, field);
    if (path.isEmpty())
        return;
    m_draft->removeField(path);
    m_message = tr("Request field removed from the draft. The stored credential is retained.");
    emit changed();
}
