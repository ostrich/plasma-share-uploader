#pragma once

#include "targetdefinition.h"

#include <QHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>

class QLabel;
class QNetworkReply;

class TargetIconProvider final : public QObject
{
    Q_OBJECT
public:
    explicit TargetIconProvider(QObject *parent = nullptr,
                                QString systemIconsPath = {},
                                QString userIconsPath = {},
                                QString cacheIconsPath = {});

    void applyIcon(QLabel *label, const TargetDefinition &target);

    QString systemIconsPath() const;
    QString userIconsPath() const;
    QString cacheIconsPath() const;

private:
    void fetchRemoteIcon(const QUrl &url, const QString &cacheKey, QLabel *label);
    void handleRemoteIconReply(QNetworkReply *reply, const QString &cachePath, const QString &cacheKey);
    void applyCachedIcon(const QString &cachePath, const QString &cacheKey);
    QString cacheFilePath(const QString &cacheKey, const QString &suffix) const;
    void setLabelPixmap(QLabel *label, const QPixmap &pixmap) const;
    QPixmap normalizedPixmap(const QPixmap &pixmap) const;

    QString m_systemIconsPath;
    QString m_userIconsPath;
    QString m_cacheIconsPath;
    QNetworkAccessManager m_network;
    QHash<QString, QList<QPointer<QLabel>>> m_pendingLabels;
};
