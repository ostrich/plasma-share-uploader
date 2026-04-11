#include "targeticonprovider.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QStandardPaths>
#include <QUrl>

namespace {
constexpr int kButtonIconExtent = 36;

QString defaultDevIconsPath()
{
    return QStringLiteral(PLASMA_SHARE_UPLOADER_DEV_ICONS_PATH);
}

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

void TargetIconProvider::applyIcon(QLabel *label, const TargetDefinition &target)
{
    if (!label) {
        return;
    }

    label->setFixedSize(kButtonIconExtent, kButtonIconExtent);
    label->setAlignment(Qt::AlignCenter);

    const QString iconName = target.icon();
    const QString resolvedLocalPath = localIconPath(iconName, userIconsPath(), systemIconsPath());
    if (!resolvedLocalPath.isEmpty()) {
        const QPixmap pixmap = loadLabelPixmap(resolvedLocalPath);
        if (!pixmap.isNull()) {
            setLabelPixmap(label, pixmap);
            return;
        }
    }

    if (isRemoteIconUrl(iconName)) {
        const QUrl url(iconName);
        const QString cacheKey = cacheKeyForUrl(url);
        const QString cachePath = cacheFilePath(cacheKey, cacheSuffixForUrl(url));
        if (QFileInfo::exists(cachePath)) {
            const QPixmap pixmap = loadLabelPixmap(cachePath);
            if (!pixmap.isNull()) {
                setLabelPixmap(label, pixmap);
                return;
            }
        }

        setLabelPixmap(label, QIcon::fromTheme(QStringLiteral("image-x-generic")).pixmap(kButtonIconExtent, kButtonIconExtent));
        fetchRemoteIcon(url, cacheKey, label);
        return;
    }

    const QUrl faviconUrl = faviconUrlForTarget(target);
    if (faviconUrl.isValid()) {
        const QString cacheKey = cacheKeyForUrl(faviconUrl);
        const QString cachePath = cacheFilePath(cacheKey, cacheSuffixForUrl(faviconUrl));
        if (QFileInfo::exists(cachePath)) {
            const QPixmap pixmap = loadLabelPixmap(cachePath);
            if (!pixmap.isNull()) {
                setLabelPixmap(label, pixmap);
                return;
            }
        }

        const QIcon fallbackIcon = !iconName.isEmpty() ? QIcon::fromTheme(iconName) : QIcon::fromTheme(QStringLiteral("image-x-generic"));
        setLabelPixmap(label,
                       (fallbackIcon.isNull() ? QIcon::fromTheme(QStringLiteral("image-x-generic")) : fallbackIcon)
                           .pixmap(kButtonIconExtent, kButtonIconExtent));
        fetchRemoteIcon(faviconUrl, cacheKey, label);
        return;
    }

    const QIcon themeIcon = !iconName.isEmpty() ? QIcon::fromTheme(iconName) : QIcon::fromTheme(QStringLiteral("image-x-generic"));
    setLabelPixmap(label,
                   (themeIcon.isNull() ? QIcon::fromTheme(QStringLiteral("image-x-generic")) : themeIcon)
                       .pixmap(kButtonIconExtent, kButtonIconExtent));
}

QString TargetIconProvider::systemIconsPath() const
{
    if (!m_systemIconsPath.isEmpty()) {
        return m_systemIconsPath;
    }

    const QString devPath = defaultDevIconsPath();
    if (QDir(devPath).exists()) {
        return devPath;
    }

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

void TargetIconProvider::fetchRemoteIcon(const QUrl &url, const QString &cacheKey, QLabel *label)
{
    if (!url.isValid() || !label) {
        return;
    }

    m_pendingLabels[cacheKey].append(QPointer<QLabel>(label));
    if (m_pendingLabels[cacheKey].size() > 1) {
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

    m_pendingLabels.remove(cacheKey);
}

void TargetIconProvider::applyCachedIcon(const QString &cachePath, const QString &cacheKey)
{
    QPixmap pixmap(cachePath);
    if (pixmap.isNull()) {
        m_pendingLabels.remove(cacheKey);
        return;
    }
    const QList<QPointer<QLabel>> labels = m_pendingLabels.take(cacheKey);
    for (const QPointer<QLabel> &label : labels) {
        if (label) {
            setLabelPixmap(label, pixmap);
        }
    }
}

QString TargetIconProvider::cacheFilePath(const QString &cacheKey, const QString &suffix) const
{
    return QDir(cacheIconsPath()).filePath(QStringLiteral("%1.%2").arg(cacheKey, suffix));
}

void TargetIconProvider::setLabelPixmap(QLabel *label, const QPixmap &pixmap) const
{
    if (!label || pixmap.isNull()) {
        return;
    }
    label->setPixmap(normalizedPixmap(pixmap));
}

QPixmap TargetIconProvider::normalizedPixmap(const QPixmap &pixmap) const
{
    const QSize targetSize(kButtonIconExtent, kButtonIconExtent);
    const QPixmap scaled = pixmap.scaled(targetSize, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);

    QPixmap normalized(targetSize);
    normalized.fill(Qt::transparent);
    QPainter painter(&normalized);
    const int x = (scaled.width() - targetSize.width()) / 2;
    const int y = (scaled.height() - targetSize.height()) / 2;
    painter.drawPixmap(0, 0, scaled, x, y, targetSize.width(), targetSize.height());
    painter.end();
    return normalized;
}
