#pragma once

#include "targetdefinition.h"

#include <QHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <functional>

class QNetworkReply;

class TargetIconProvider final : public QObject
{
    Q_OBJECT
public:
    explicit TargetIconProvider(QObject *parent = nullptr,
                                QString systemIconsPath = {},
                                QString userIconsPath = {},
                                QString cacheIconsPath = {});

    using Callback = std::function<void(const QString &source)>;
    void requestIcon(const TargetDefinition &target, QObject *context, Callback callback);

    QString systemIconsPath() const;
    QString userIconsPath() const;
    QString cacheIconsPath() const;

private:
    void fetchRemoteIcon(const QUrl &url, const QString &cacheKey, QObject *context, Callback callback);
    void handleRemoteIconReply(QNetworkReply *reply, const QString &cachePath, const QString &cacheKey);
    void applyCachedIcon(const QString &cachePath, const QString &cacheKey);
    QString cacheFilePath(const QString &cacheKey, const QString &suffix) const;
    struct Pending { QPointer<QObject> context; Callback callback; };

    QString m_systemIconsPath;
    QString m_userIconsPath;
    QString m_cacheIconsPath;
    QNetworkAccessManager m_network;
    QHash<QString, QList<Pending>> m_pending;
};
