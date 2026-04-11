#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QTcpServer>
#include <QUrl>

class QTcpSocket;

struct CapturedHttpRequest {
    QByteArray method;
    QByteArray path;
    QHash<QByteArray, QByteArray> headers;
    QByteArray body;
};

struct QueuedHttpResponse {
    int statusCode = 200;
    QByteArray reasonPhrase = "OK";
    QByteArray contentType = "text/plain";
    QHash<QByteArray, QByteArray> headers;
    QByteArray body;
};

class HttpCaptureServer final : public QTcpServer
{
    Q_OBJECT
public:
    explicit HttpCaptureServer(QObject *parent = nullptr);

    bool start();
    QUrl url(const QString &path = QStringLiteral("/")) const;
    void enqueueResponse(const QueuedHttpResponse &response);
    const QList<CapturedHttpRequest> &requests() const;

signals:
    void requestCaptured();

private:
    struct ConnectionState {
        QByteArray buffer;
    };

    void handleNewConnection();
    void handleReadyRead(QTcpSocket *socket);
    void finalizeRequest(QTcpSocket *socket, const QByteArray &headerBlock, const QByteArray &body);

    QList<CapturedHttpRequest> m_requests;
    QList<QueuedHttpResponse> m_responses;
    QHash<QTcpSocket *, ConnectionState> m_connections;
};
