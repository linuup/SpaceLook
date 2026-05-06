#include "platform/spacelook_ipc_server.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>

namespace {

const char kSpaceLookServerName[] = "LinDesk.SpaceLook";

}

SpaceLookIpcServer::SpaceLookIpcServer(QObject* parent)
    : QObject(parent)
    , m_server(new QLocalServer(this))
{
    connect(m_server, &QLocalServer::newConnection, this, &SpaceLookIpcServer::handleNewConnection);
}

bool SpaceLookIpcServer::sendPreviewRequest(const QString& filePath, int timeoutMs)
{
    const QString path = filePath.trimmed();
    if (path.isEmpty()) {
        return false;
    }

    QLocalSocket socket;
    socket.connectToServer(QString::fromLatin1(kSpaceLookServerName));
    if (!socket.waitForConnected(timeoutMs)) {
        qWarning().noquote() << QStringLiteral("[SpaceLook IPC] failed to connect to running instance: error=\"%1\"")
            .arg(socket.errorString());
        return false;
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("cmd"), QStringLiteral("preview"));
    payload.insert(QStringLiteral("path"), path);

    QByteArray message = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    message.append('\n');

    if (socket.write(message) != message.size()) {
        qWarning().noquote() << QStringLiteral("[SpaceLook IPC] failed to write preview request");
        return false;
    }

    if (!socket.waitForBytesWritten(timeoutMs)) {
        qWarning().noquote() << QStringLiteral("[SpaceLook IPC] preview request write timed out: error=\"%1\"")
            .arg(socket.errorString());
        return false;
    }

    socket.disconnectFromServer();
    return true;
}

bool SpaceLookIpcServer::startListening()
{
    if (m_server->isListening()) {
        return true;
    }

    const QString serverName = QString::fromLatin1(kSpaceLookServerName);
    if (m_server->listen(serverName)) {
        qDebug().noquote() << QStringLiteral("[SpaceLook IPC] listening for preview requests on \"%1\"").arg(serverName);
        return true;
    }

    QLocalServer::removeServer(serverName);
    if (m_server->listen(serverName)) {
        qDebug().noquote() << QStringLiteral("[SpaceLook IPC] listening for preview requests on \"%1\" after cleanup").arg(serverName);
        return true;
    }

    qWarning().noquote() << QStringLiteral("[SpaceLook IPC] failed to start listener: error=\"%1\"")
        .arg(m_server->errorString());
    return false;
}

void SpaceLookIpcServer::handleNewConnection()
{
    while (QLocalSocket* socket = m_server->nextPendingConnection()) {
        connect(socket, &QLocalSocket::readyRead, this, [this, socket]() {
            handleReadyRead(socket);
        });
        connect(socket, &QLocalSocket::disconnected, this, [this, socket]() {
            handleSocketDisconnected(socket);
        });
    }
}

void SpaceLookIpcServer::handleReadyRead(QLocalSocket* socket)
{
    if (!socket) {
        return;
    }

    while (socket->canReadLine()) {
        const QByteArray rawLine = socket->readLine().trimmed();
        if (rawLine.isEmpty()) {
            continue;
        }

        const QJsonDocument jsonDocument = QJsonDocument::fromJson(rawLine);
        if (!jsonDocument.isObject()) {
            qWarning().noquote() << QStringLiteral("[SpaceLook IPC] ignored invalid request payload=%1")
                .arg(QString::fromUtf8(rawLine));
            continue;
        }

        const QJsonObject payload = jsonDocument.object();
        const QString command = payload.value(QStringLiteral("cmd")).toString().trimmed();
        const QString path = payload.value(QStringLiteral("path")).toString().trimmed();

        if (command != QStringLiteral("preview")) {
            qWarning().noquote() << QStringLiteral("[SpaceLook IPC] ignored unsupported command: cmd=\"%1\" payload=%2")
                .arg(command, QString::fromUtf8(rawLine));
            continue;
        }

        if (path.isEmpty()) {
            qWarning().noquote() << QStringLiteral("[SpaceLook IPC] ignored preview request with empty path");
            continue;
        }

        qDebug().noquote() << QStringLiteral("[SpaceLook IPC] received preview request: path=\"%1\"").arg(path);
        emit previewRequested(path);
    }
}

void SpaceLookIpcServer::handleSocketDisconnected(QLocalSocket* socket)
{
    if (!socket) {
        return;
    }

    socket->deleteLater();
}
