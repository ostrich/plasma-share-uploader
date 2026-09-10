#include "credentialstore.h"

#include <KWallet>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <algorithm>

namespace {
const QString folder = QStringLiteral("plasma-share-uploader");

void openWallet(QObject* context, std::function<void(KWallet::Wallet*, QString)> callback)
{
    auto* wallet = KWallet::Wallet::openWallet(KWallet::Wallet::NetworkWallet(), 0, KWallet::Wallet::Asynchronous);
    if (!wallet) {
        QTimer::singleShot(0, context,
            [callback]() { callback(nullptr, QStringLiteral("KWallet is unavailable or access was cancelled.")); });
        return;
    }
    wallet->setParent(context);
    QObject::connect(wallet, &KWallet::Wallet::walletOpened, context, [wallet, callback](bool ok) {
        const bool ready
            = ok && (wallet->hasFolder(folder) || wallet->createFolder(folder)) && wallet->setFolder(folder);
        callback(ready ? wallet : nullptr,
            ready ? QString { } : QStringLiteral("Could not open the uploader's KWallet folder."));
        wallet->deleteLater();
    });
}

void strings(const QJsonValue& value, QStringList& out)
{
    if (value.isString())
        out.append(value.toString());
    else if (value.isArray())
        for (const auto& item : value.toArray())
            strings(item, out);
    else if (value.isObject()) {
        const auto object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it)
            strings(it.value(), out);
    }
}
}

bool CredentialStore::validKey(const QString& key)
{
    return QRegularExpression(QStringLiteral("\\A[A-Za-z0-9_.-]+\\z")).match(key).hasMatch();
}

void CredentialStore::read(const QStringList& keys, QObject* context, ReadCallback callback)
{
    if (keys.isEmpty()) {
        QTimer::singleShot(0, context, [callback]() { callback({ }, { }); });
        return;
    }
    openWallet(context, [keys, callback](KWallet::Wallet* wallet, const QString& error) {
        if (!wallet) {
            callback({ }, error);
            return;
        }
        Values values;
        for (const auto& key : keys) {
            QString value;
            if (!validKey(key) || !wallet->hasEntry(key) || wallet->readPassword(key, value) != 0 || value.isEmpty()) {
                callback({ },
                    QStringLiteral("Credential '%1' is missing or empty. Configure it in the target manager.")
                        .arg(key));
                return;
            }
            values.insert(key, value);
        }
        callback(values, { });
    });
}

void CredentialStore::write(const QString& key, const QString& value, QObject* context, WriteCallback callback)
{
    if (!validKey(key) || value.isEmpty()) {
        QTimer::singleShot(0, context, [callback]() {
            callback(QStringLiteral(
                "Enter a credential name (letters, digits, dot, dash, underscore) and a nonempty secret."));
        });
        return;
    }
    openWallet(context, [key, value, callback](KWallet::Wallet* wallet, const QString& error) {
        callback(!wallet                                 ? error
                : wallet->writePassword(key, value) == 0 ? QString { }
                                                         : QStringLiteral("Could not save credential to KWallet."));
    });
}

void CredentialStore::remove(const QString& key, QObject* context, WriteCallback callback)
{
    openWallet(context, [key, callback](KWallet::Wallet* wallet, const QString& error) {
        callback(!wallet                        ? error
                : wallet->removeEntry(key) == 0 ? QString { }
                                                : QStringLiteral("Could not remove credential from KWallet."));
    });
}

QStringList CredentialStore::requestStrings(const ParsedRequestConfig& request)
{
    QStringList out { request.url };
    out.append(request.headers.values());
    out.append(request.query.values());
    switch (request.type) {
    case RequestBodyType::Multipart:
        out.append(request.multipartFields.values());
        break;
    case RequestBodyType::FormUrlencoded:
        out.append(request.formFields.values());
        break;
    case RequestBodyType::Json:
        strings(request.jsonValue, out);
        break;
    case RequestBodyType::Raw:
        break;
    }
    return out;
}

QStringList CredentialStore::walletKeys(const ParsedRequestConfig& request)
{
    QStringList keys;
    const QRegularExpression pattern(QStringLiteral(R"(\$\{WALLET:([A-Za-z0-9_.-]+)\})"));
    for (const auto& text : requestStrings(request)) {
        auto matches = pattern.globalMatch(text);
        while (matches.hasNext())
            keys.append(matches.next().captured(1));
    }
    keys.removeDuplicates();
    return keys;
}

QStringList CredentialStore::missingEnvironment(const ParsedRequestConfig& request)
{
    QStringList missing;
    const QRegularExpression pattern(QStringLiteral(R"(\$\{ENV:([A-Za-z_][A-Za-z0-9_]*)\})"));
    for (const auto& text : requestStrings(request)) {
        auto matches = pattern.globalMatch(text);
        while (matches.hasNext()) {
            const auto key = matches.next().captured(1);
            if (qgetenv(key.toUtf8().constData()).isEmpty())
                missing.append(key);
        }
    }
    missing.removeDuplicates();
    return missing;
}

QStringList CredentialStore::secretValues(const ParsedRequestConfig& request, const Values& walletValues)
{
    QStringList values = walletValues.values();
    const QRegularExpression pattern(QStringLiteral(R"(\$\{ENV:([A-Za-z_][A-Za-z0-9_]*)\})"));
    for (const auto& text : requestStrings(request)) {
        auto matches = pattern.globalMatch(text);
        while (matches.hasNext())
            values.append(QString::fromLocal8Bit(qgetenv(matches.next().captured(1).toUtf8().constData())));
    }
    const QRegularExpression sensitive(
        QStringLiteral("authorization|cookie|password|passwd|secret|token|api.?key|userhash|^key$"),
        QRegularExpression::CaseInsensitiveOption);
    auto addSecret = [&values](const QString& value) {
        values.append(value);
        if (value.startsWith(QLatin1StringView("Bearer "), Qt::CaseInsensitive)
            || value.startsWith(QLatin1StringView("Basic "), Qt::CaseInsensitive))
            values.append(value.mid(value.indexOf(QLatin1Char(' ')) + 1));
    };
    for (const auto& map : { request.headers, request.query, request.multipartFields, request.formFields })
        for (auto it = map.begin(); it != map.end(); ++it)
            if (sensitive.match(it.key()).hasMatch())
                addSecret(it.value());
    std::function<void(QJsonValue)> collect = [&](QJsonValue value) {
        if (value.isArray())
            for (const auto& v : value.toArray())
                collect(v);
        if (value.isObject()) {
            const auto object = value.toObject();
            for (auto it = object.begin(); it != object.end(); ++it) {
                if (sensitive.match(it.key()).hasMatch() && it.value().isString())
                    addSecret(it.value().toString());
                else
                    collect(it.value());
            }
        }
    };
    collect(request.jsonValue);
    const QUrl url(request.url);
    if (!url.password().isEmpty())
        addSecret(url.password());
    for (const auto& item : QUrlQuery(url).queryItems(QUrl::FullyDecoded))
        if (sensitive.match(item.first).hasMatch())
            addSecret(item.second);
    values.removeAll(QString { });
    values.removeDuplicates();
    return values;
}

QString CredentialStore::redact(QString text, const QStringList& secrets)
{
    QStringList variants;
    for (const auto& secret : secrets) {
        if (secret.isEmpty())
            continue;
        variants.append(secret);
        variants.append(QString::fromLatin1(QUrl::toPercentEncoding(secret)));
        auto json = QJsonDocument(QJsonArray { secret }).toJson(QJsonDocument::Compact);
        variants.append(QString::fromUtf8(json.mid(2, json.size() - 4)));
    }
    std::sort(variants.begin(), variants.end(), [](const QString& a, const QString& b) { return a.size() > b.size(); });
    for (const auto& value : variants)
        if (!value.isEmpty())
            text.replace(value, QStringLiteral("[redacted]"));
    return text;
}
