#include "targeticonprovider.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QStandardPaths>
#include <QUrl>

namespace {
constexpr int kButtonIconExtent = 36;

QString defaultSystemIconsPath()
{
    return QStringLiteral(PLASMA_SHARE_UPLOADER_SYSTEM_ICONS_PATH);
}

QString defaultUserIconsPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/plasma-share-uploader/icons");
}

QString defaultCacheIconsPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)
        + QStringLiteral("/plasma-share-uploader/icons");
}

QString localIconPath(const QString &iconName, const QString &userIconsPath, const QString &systemIconsPath)
{
    if (iconName.isEmpty()) {
        return {};
    }

    QFileInfo info(iconName);
    if (info.isAbsolute() && info.exists() && info.isFile()) {
        return info.absoluteFilePath();
    }

    const QStringList candidatePaths{
        QDir(userIconsPath).filePath(iconName),
        QDir(systemIconsPath).filePath(iconName),
    };
    for (const QString &path : candidatePaths) {
        QFileInfo candidate(path);
        if (candidate.exists() && candidate.isFile()) {
            return candidate.absoluteFilePath();
        }
    }

    return {};
}

bool isRemoteIconUrl(const QString &iconName)
{
    const QUrl url(iconName);
    return url.isValid() && (url.scheme() == QLatin1StringView("http") || url.scheme() == QLatin1StringView("https"));
}

QUrl faviconUrlForTarget(const TargetDefinition &target)
{
    const QUrl requestUrl(target.target.request.url);
    if (!requestUrl.isValid() || requestUrl.scheme().isEmpty() || requestUrl.host().isEmpty()) {
        return {};
    }

    QUrl faviconUrl;
    faviconUrl.setScheme(requestUrl.scheme());
    faviconUrl.setHost(requestUrl.host());
    faviconUrl.setPort(requestUrl.port());
    faviconUrl.setPath(QStringLiteral("/favicon.ico"));
    return faviconUrl;
}

QString cacheKeyForUrl(const QUrl &url)
{
    return QString::fromLatin1(QCryptographicHash::hash(url.toString(QUrl::FullyEncoded).toUtf8(),
                                                        QCryptographicHash::Sha256)
                                   .toHex());
}

QString cacheSuffixForUrl(const QUrl &url)
{
    const QString path = url.path();
    const int dot = path.lastIndexOf(QLatin1Char('.'));
    if (dot >= 0 && dot + 1 < path.size()) {
        return path.mid(dot + 1).toLower();
    }
    return QStringLiteral("ico");
}

QPixmap loadLabelPixmap(const QString &path)
{
    QPixmap pixmap(path);
    return pixmap;
}
}

TargetIconProvider::TargetIconProvider(QObject *parent,
                                       QString systemIconsPath,
                                       QString userIconsPath,
                                       QString cacheIconsPath)
    : QObject(parent)
    , m_systemIconsPath(std::move(systemIconsPath))
    , m_userIconsPath(std::move(userIconsPath))
    , m_cacheIconsPath(std::move(cacheIconsPath))
{
}

void TargetIconProvider::requestIcon(const TargetDefinition &target, QObject *context, Callback callback)
{
    if (!context) return;
    const QString iconName = target.icon();
    const QString local = localIconPath(iconName, userIconsPath(), systemIconsPath());
    if (!local.isEmpty() && !loadLabelPixmap(local).isNull()) { callback(QUrl::fromLocalFile(local).toString()); return; }
    const bool remote = isRemoteIconUrl(iconName);
    const QUrl url = remote ? QUrl(iconName) : faviconUrlForTarget(target);
    QString fallback = !remote && QIcon::hasThemeIcon(iconName) ? iconName : QStringLiteral("image-x-generic");
    callback(fallback);
    if (!url.isValid() || url.isEmpty()) return;
    const QString key = cacheKeyForUrl(url);
    const QString cached = cacheFilePath(key, cacheSuffixForUrl(url));
    if (!loadLabelPixmap(cached).isNull()) { callback(QUrl::fromLocalFile(cached).toString()); return; }
    fetchRemoteIcon(url, key, context, std::move(callback));
}

QString TargetIconProvider::systemIconsPath() const
{
    if (!m_systemIconsPath.isEmpty()) {
        return m_systemIconsPath;
    }

#ifdef PLASMA_SHARE_UPLOADER_DEV_ICONS_PATH
    const QString devPath = QStringLiteral(PLASMA_SHARE_UPLOADER_DEV_ICONS_PATH);
    if (QDir(devPath).exists()) {
        return devPath;
    }
#endif

    return defaultSystemIconsPath();
}

QString TargetIconProvider::userIconsPath() const
{
    return m_userIconsPath.isEmpty() ? defaultUserIconsPath() : m_userIconsPath;
}

QString TargetIconProvider::cacheIconsPath() const
{
    return m_cacheIconsPath.isEmpty() ? defaultCacheIconsPath() : m_cacheIconsPath;
}

void TargetIconProvider::fetchRemoteIcon(const QUrl &url, const QString &cacheKey, QObject *context, Callback callback)
{
    if (!url.isValid() || !context) {
        return;
    }

    m_pending[cacheKey].append({context, std::move(callback)});
    if (m_pending[cacheKey].size() > 1) {
        return;
    }

    QNetworkRequest request(url);
    request.setTransferTimeout(10000);
    QNetworkReply *reply = m_network.get(request);
    // Icons are small; bound downloads even if a server sends HTML or an endless body.
    connect(reply, &QNetworkReply::readyRead, this, [reply]() { if (reply->bytesAvailable() > 4 * 1024 * 1024) reply->abort(); });
    const QString cachePath = cacheFilePath(cacheKey, cacheSuffixForUrl(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, cachePath, cacheKey]() {
        handleRemoteIconReply(reply, cachePath, cacheKey);
    });
}

void TargetIconProvider::handleRemoteIconReply(QNetworkReply *reply, const QString &cachePath, const QString &cacheKey)
{
    const QByteArray bytes = reply->readAll();
    const bool ok = reply->error() == QNetworkReply::NoError && !bytes.isEmpty();
    reply->deleteLater();

    if (ok) {
        QDir().mkpath(QFileInfo(cachePath).absolutePath());
        QFile file(cachePath);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(bytes);
            file.close();
            applyCachedIcon(cachePath, cacheKey);
            return;
        }
    }

    m_pending.remove(cacheKey);
}

void TargetIconProvider::applyCachedIcon(const QString &cachePath, const QString &cacheKey)
{
    QPixmap pixmap(cachePath);
    if (pixmap.isNull()) {
        m_pending.remove(cacheKey);
        return;
    }
    const auto pending = m_pending.take(cacheKey);
    for (const auto &item : pending) if (item.context) item.callback(QUrl::fromLocalFile(cachePath).toString());
}

QString TargetIconProvider::cacheFilePath(const QString &cacheKey, const QString &suffix) const
{
    return QDir(cacheIconsPath()).filePath(QStringLiteral("%1.%2").arg(cacheKey, suffix));
}
