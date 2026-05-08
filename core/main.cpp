#include <QApplication>
#include <QDebug>
#include <QIcon>
#include <Windows.h>
#include <objbase.h>

#include "core/preview_manager.h"
#include "core/render_type_registry.h"
#include "platform/spacelook_ipc_server.h"
#include "settings/app_translator.h"

int main(int argc, char* argv[])
{
    qputenv("QT_MULTIMEDIA_PREFERRED_PLUGINS", QByteArrayLiteral("windowsmediafoundation"));

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("SpaceLook"));
    app.setOrganizationName(QStringLiteral("LinDesk"));
    AppTranslator::instance().initialize();

    HANDLE singleInstanceMutex = CreateMutexW(nullptr, TRUE, L"Local\\LinDesk.SpaceLook.SingleInstance");
    const DWORD mutexError = GetLastError();
    if (singleInstanceMutex && mutexError == ERROR_ALREADY_EXISTS) {
        const QStringList arguments = app.arguments();
        if (arguments.size() > 1) {
            SpaceLookIpcServer::sendPreviewRequest(arguments.at(1));
        }
        CloseHandle(singleInstanceMutex);
        return 0;
    }
    if (!singleInstanceMutex) {
        qWarning().noquote() << QStringLiteral("[SpaceLook] failed to create single instance mutex");
    }

    QIcon appIcon(QStringLiteral(":/icons/icon.ico"));
    if (appIcon.isNull()) {
        appIcon = QIcon(QStringLiteral(":/icons/icon.png"));
    }
    app.setWindowIcon(appIcon);
    app.setQuitOnLastWindowClosed(false);

    RenderTypeRegistry::instance().load();

    const HRESULT oleInitHr = OleInitialize(nullptr);
    PreviewManager previewManager;
    SpaceLookIpcServer ipcServer;
    QObject::connect(&ipcServer, &SpaceLookIpcServer::previewRequested,
                     &previewManager, &PreviewManager::openPreviewForPath);
    ipcServer.startListening();
    previewManager.showInitialPreview();

    const int exitCode = app.exec();
    if (SUCCEEDED(oleInitHr)) {
        OleUninitialize();
    }
    if (singleInstanceMutex) {
        ReleaseMutex(singleInstanceMutex);
        CloseHandle(singleInstanceMutex);
    }
    return exitCode;
}
