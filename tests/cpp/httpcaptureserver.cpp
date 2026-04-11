#include "httpcaptureserver.h"

#include <QHostAddress>
#include <QTcpSocket>
#include <QUrl>

namespace {
QHash<QByteArray, QByteArray> parseHeaders(const QList<QByteArray> &lines)
{
    QHash<QByteArray, QByteArray> headers;
    for (const QByteArray &line : lines) {
        const int colonIndex = line.indexOf(':');
        if (colonIndex <= 0) {
            continue;
        }
        const QByteArray name = line.left(colonIndex).trimmed().toLower();
        const QByteArray value = line.mid(colonIndex + 1).trimmed();
        headers.insert(name, value);
    }
    return headers;
}
}

HttpCaptureServer::HttpCaptureServer(QObject *parent)
    : QTcpServer(parent)
{
    connect(this, &QTcpServer::newConnection, this, &HttpCaptureServer::handleNewConnection);
}

bool HttpCaptureServer::start()
{
    return listen(QHostAddress::LocalHost, 0);
}

QUrl HttpCaptureServer::url(const QString &path) const
{
    QUrl url;
    url.setScheme(QStringLiteral("http"));
    url.setHost(QStringLiteral("127.0.0.1"));
    url.setPort(serverPort());
    url.setPath(path);
    return url;
}

void HttpCaptureServer::enqueueResponse(const QueuedHttpResponse &response)
{
    m_responses.append(response);
}

const QList<CapturedHttpRequest> &HttpCaptureServer::requests() const
{
    return m_requests;
}

void HttpCaptureServer::handleNewConnection()
{
    while (QTcpSocket *socket = nextPendingConnection()) {
        m_connections.insert(socket, ConnectionState{});
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
            handleReadyRead(socket);
        });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
            m_connections.remove(socket);
            socket->deleteLater();
        });
    }
}

void HttpCaptureServer::handleReadyRead(QTcpSocket *socket)
{
    if (!m_connections.contains(socket)) {
        return;
    }

    ConnectionState &state = m_connections[socket];
    state.buffer.append(socket->readAll());

    const int headerEnd = state.buffer.indexOf("\r\n\r\n");
    if (headerEnd < 0) {
        return;
    }

    const QByteArray headerBlock = state.buffer.left(headerEnd);
    const QList<QByteArray> lines = headerBlock.split('\n');
    if (lines.isEmpty()) {
        return;
    }

    const QHash<QByteArray, QByteArray> headers = parseHeaders(lines.mid(1));
    const int contentLength = headers.value("content-length").toInt();
    const int totalSize = headerEnd + 4 + contentLength;
    if (state.buffer.size() < totalSize) {
        return;
    }

    const QByteArray body = state.buffer.mid(headerEnd + 4, contentLength);
    finalizeRequest(socket, headerBlock, body);
}

void HttpCaptureServer::finalizeRequest(QTcpSocket *socket, const QByteArray &headerBlock, const QByteArray &body)
{
    const QList<QByteArray> lines = headerBlock.split('\n');
    const QList<QByteArray> startLineParts = lines.first().trimmed().split(' ');

    CapturedHttpRequest request;
    if (startLineParts.size() >= 2) {
        request.method = startLineParts.at(0);
        request.path = startLineParts.at(1);
    }
    request.headers = parseHeaders(lines.mid(1));
    request.body = body;
    m_requests.append(request);

    QueuedHttpResponse response;
    if (!m_responses.isEmpty()) {
        response = m_responses.takeFirst();
    }

    QByteArray wireResponse;
    wireResponse.append("HTTP/1.1 ");
    wireResponse.append(QByteArray::number(response.statusCode));
    wireResponse.append(' ');
    wireResponse.append(response.reasonPhrase);
    wireResponse.append("\r\n");
    wireResponse.append("Content-Type: ");
    wireResponse.append(response.contentType);
    wireResponse.append("\r\n");
    wireResponse.append("Content-Length: ");
    wireResponse.append(QByteArray::number(response.body.size()));
    wireResponse.append("\r\n");
    wireResponse.append("Connection: close\r\n\r\n");
    wireResponse.append(response.body);

    socket->write(wireResponse);
    socket->disconnectFromHost();
    emit requestCaptured();
}
