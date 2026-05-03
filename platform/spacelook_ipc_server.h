#pragma once

#include <QObject>
#include <QString>

class QLocalServer;
class QLocalSocket;

class SpaceLookIpcServer : public QObject
{
    Q_OBJECT

public:
    explicit SpaceLookIpcServer(QObject* parent = nullptr);
    bool startListening();

signals:
    void previewRequested(const QString& filePath);

private:
    void handleNewConnection();
    void handleReadyRead(QLocalSocket* socket);
    void handleSocketDisconnected(QLocalSocket* socket);

    QLocalServer* m_server = nullptr;
};
