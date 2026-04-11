#include "targeticonprovider.h"

#include <QAbstractButton>
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

bool loadButtonIcon(QAbstractButton *button, const QString &path)
{
    QPixmap pixmap(path);
    if (pixmap.isNull()) {
        return false;
    }
    button->setIcon(QIcon(pixmap));
    return true;
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

void TargetIconProvider::applyIcon(QAbstractButton *button, const TargetDefinition &target)
{
    if (!button) {
        return;
    }

    button->setIconSize(QSize(24, 24));

    const QString iconName = target.icon();
    const QString resolvedLocalPath = localIconPath(iconName, userIconsPath(), systemIconsPath());
    if (!resolvedLocalPath.isEmpty() && loadButtonIcon(button, resolvedLocalPath)) {
        return;
    }

    if (isRemoteIconUrl(iconName)) {
        const QUrl url(iconName);
        const QString cacheKey = cacheKeyForUrl(url);
        const QString cachePath = cacheFilePath(cacheKey, cacheSuffixForUrl(url));
        if (QFileInfo::exists(cachePath) && loadButtonIcon(button, cachePath)) {
            return;
        }

        button->setIcon(QIcon::fromTheme(QStringLiteral("image-x-generic")));
        fetchRemoteIcon(url, cacheKey, button);
        return;
    }

    const QUrl faviconUrl = faviconUrlForTarget(target);
    if (faviconUrl.isValid()) {
        const QString cacheKey = cacheKeyForUrl(faviconUrl);
        const QString cachePath = cacheFilePath(cacheKey, cacheSuffixForUrl(faviconUrl));
        if (QFileInfo::exists(cachePath) && loadButtonIcon(button, cachePath)) {
            return;
        }

        const QIcon fallbackIcon = !iconName.isEmpty() ? QIcon::fromTheme(iconName) : QIcon::fromTheme(QStringLiteral("image-x-generic"));
        button->setIcon(fallbackIcon.isNull() ? QIcon::fromTheme(QStringLiteral("image-x-generic")) : fallbackIcon);
        fetchRemoteIcon(faviconUrl, cacheKey, button);
        return;
    }

    const QIcon themeIcon = !iconName.isEmpty() ? QIcon::fromTheme(iconName) : QIcon::fromTheme(QStringLiteral("image-x-generic"));
    button->setIcon(themeIcon.isNull() ? QIcon::fromTheme(QStringLiteral("image-x-generic")) : themeIcon);
}

QString TargetIconProvider::systemIconsPath() const
{
    return m_systemIconsPath.isEmpty() ? defaultSystemIconsPath() : m_systemIconsPath;
}

QString TargetIconProvider::userIconsPath() const
{
    return m_userIconsPath.isEmpty() ? defaultUserIconsPath() : m_userIconsPath;
}

QString TargetIconProvider::cacheIconsPath() const
{
    return m_cacheIconsPath.isEmpty() ? defaultCacheIconsPath() : m_cacheIconsPath;
}

void TargetIconProvider::fetchRemoteIcon(const QUrl &url, const QString &cacheKey, QAbstractButton *button)
{
    if (!url.isValid() || !button) {
        return;
    }

    m_pendingButtons[cacheKey].append(QPointer<QAbstractButton>(button));
    if (m_pendingButtons[cacheKey].size() > 1) {
        return;
    }

    QNetworkRequest request(url);
    request.setTransferTimeout(10000);
    QNetworkReply *reply = m_network.get(request);
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

    m_pendingButtons.remove(cacheKey);
}

void TargetIconProvider::applyCachedIcon(const QString &cachePath, const QString &cacheKey)
{
    QPixmap pixmap(cachePath);
    if (pixmap.isNull()) {
        m_pendingButtons.remove(cacheKey);
        return;
    }

    const QIcon icon(pixmap);
    const QList<QPointer<QAbstractButton>> buttons = m_pendingButtons.take(cacheKey);
    for (const QPointer<QAbstractButton> &button : buttons) {
        if (button) {
            button->setIcon(icon);
        }
    }
}

QString TargetIconProvider::cacheFilePath(const QString &cacheKey, const QString &suffix) const
{
    return QDir(cacheIconsPath()).filePath(QStringLiteral("%1.%2").arg(cacheKey, suffix));
}
