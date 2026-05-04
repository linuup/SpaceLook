#include <QApplication>
#include <QIcon>
#include <objbase.h>

#include "core/preview_manager.h"
#include "platform/spacelook_ipc_server.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("SpaceLook"));
    app.setOrganizationName(QStringLiteral("LinDesk"));
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/app.png")));
    app.setQuitOnLastWindowClosed(false);

    const HRESULT oleInitHr = OleInitialize(nullptr);
    PreviewManager previewManager;
    SpaceLookIpcServer ipcServer;
    QObject::connect(&ipcServer, &SpaceLookIpcServer::previewRequested,
                     &previewManager, &PreviewManager::openPreviewForPath);
    ipcServer.startListening();
    previewManager.showInitialPreview();
    previewManager.showSettingsWindow();

    const int exitCode = app.exec();
    if (SUCCEEDED(oleInitHr)) {
        OleUninitialize();
    }
    return exitCode;
}
