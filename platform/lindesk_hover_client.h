#pragma once

#include <QByteArray>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QString>

class LinDeskHoverClient
{
public:
    enum class RequestResult
    {
        Success,
        NoHoveredItem,
        Unavailable
    };

    static QString requestHoveredPath(RequestResult* result = nullptr, QString* errorMessage = nullptr)
    {
        constexpr int kLinDeskHoverTimeoutMs = 300;
        const char kLinDeskHoveredItemServerName[] = "LinDesk.HoveredItem";

        if (result) {
            *result = RequestResult::Unavailable;
        }
        if (errorMessage) {
            errorMessage->clear();
        }

        QLocalSocket socket;
        socket.connectToServer(QString::fromLatin1(kLinDeskHoveredItemServerName));
        if (!socket.waitForConnected(kLinDeskHoverTimeoutMs)) {
            const QString message = socket.errorString();
            qWarning().noquote() << QStringLiteral("[SpaceLook Hover IPC] failed to connect: error=\"%1\"").arg(message);
            if (errorMessage) {
                *errorMessage = message;
            }
            return QString();
        }

        QJsonObject payload;
        payload.insert(QStringLiteral("cmd"), QStringLiteral("get_hovered_item"));

        QByteArray bytes = QJsonDocument(payload).toJson(QJsonDocument::Compact);
        bytes.append('\n');

        if (socket.write(bytes) != bytes.size()) {
            const QString message = QStringLiteral("Failed to queue hover request");
            qWarning().noquote() << QStringLiteral("[SpaceLook Hover IPC] %1").arg(message);
            if (errorMessage) {
                *errorMessage = message;
            }
            return QString();
        }

        if (!socket.waitForBytesWritten(kLinDeskHoverTimeoutMs)) {
            const QString message = socket.errorString();
            qWarning().noquote() << QStringLiteral("[SpaceLook Hover IPC] write failed: error=\"%1\"").arg(message);
            if (errorMessage) {
                *errorMessage = message;
            }
            return QString();
        }

        if (!socket.waitForReadyRead(kLinDeskHoverTimeoutMs)) {
            const QString message = socket.errorString();
            qWarning().noquote() << QStringLiteral("[SpaceLook Hover IPC] read timed out: error=\"%1\"").arg(message);
            if (errorMessage) {
                *errorMessage = message;
            }
            return QString();
        }

        const QByteArray rawLine = socket.readLine().trimmed();
        qDebug().noquote() << QStringLiteral("[SpaceLook Hover IPC] received response payload=%1")
            .arg(QString::fromUtf8(rawLine));
        const QJsonDocument jsonDocument = QJsonDocument::fromJson(rawLine);
        if (!jsonDocument.isObject()) {
            const QString message = QStringLiteral("Invalid hover response payload");
            qWarning().noquote() << QStringLiteral("[SpaceLook Hover IPC] %1").arg(message);
            if (errorMessage) {
                *errorMessage = message;
            }
            return QString();
        }

        const QJsonObject response = jsonDocument.object();
        if (!response.value(QStringLiteral("ok")).toBool()) {
            const QString message = response.value(QStringLiteral("error")).toString().trimmed();
            qWarning().noquote() << QStringLiteral("[SpaceLook Hover IPC] hover request returned no item: error=\"%1\"")
                .arg(message);
            if (result) {
                *result = message.compare(QStringLiteral("No hovered item is available"), Qt::CaseInsensitive) == 0
                    ? RequestResult::NoHoveredItem
                    : RequestResult::Unavailable;
            }
            if (errorMessage) {
                *errorMessage = message;
            }
            return QString();
        }

        const QString path = response.value(QStringLiteral("path")).toString().trimmed();
        if (path.isEmpty()) {
            const QString message = QStringLiteral("Hover response path is empty");
            qWarning().noquote() << QStringLiteral("[SpaceLook Hover IPC] %1").arg(message);
            if (errorMessage) {
                *errorMessage = message;
            }
            return QString();
        }

        if (result) {
            *result = RequestResult::Success;
        }
        if (errorMessage) {
            errorMessage->clear();
        }
        return path;
    }
};
