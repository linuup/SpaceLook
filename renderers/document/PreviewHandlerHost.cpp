#include "renderers/document/PreviewHandlerHost.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objbase.h>
#include <shobjidl.h>

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QResizeEvent>
#include <QVector>

namespace {

constexpr wchar_t kPreviewHandlerShellExKey[] = L"shellex\\{8895b1c6-b41f-4c1c-a562-0d564250836f}";

QString hresultMessage(HRESULT hr)
{
    wchar_t* messageBuffer = nullptr;
    const DWORD size = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        static_cast<DWORD>(hr),
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&messageBuffer),
        0,
        nullptr);

    QString message = size > 0 && messageBuffer
        ? QString::fromWCharArray(messageBuffer).trimmed()
        : QStringLiteral("HRESULT 0x%1").arg(static_cast<qulonglong>(static_cast<unsigned long>(hr)), 8, 16, QLatin1Char('0'));

    if (messageBuffer) {
        LocalFree(messageBuffer);
    }

    if (message.isEmpty()) {
        message = QStringLiteral("HRESULT 0x%1").arg(static_cast<qulonglong>(static_cast<unsigned long>(hr)), 8, 16, QLatin1Char('0'));
    }
    return message;
}

QString readRegistryDefaultValue(const QString& subKey)
{
    HKEY key = nullptr;
    const std::wstring nativeSubKey = subKey.toStdWString();
    LONG result = RegOpenKeyExW(HKEY_CLASSES_ROOT, nativeSubKey.c_str(), 0, KEY_READ, &key);
    if (result != ERROR_SUCCESS) {
        return QString();
    }

    DWORD type = 0;
    DWORD size = 0;
    result = RegQueryValueExW(key, nullptr, nullptr, &type, nullptr, &size);
    if (result != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || size < sizeof(wchar_t)) {
        RegCloseKey(key);
        return QString();
    }

    QVector<wchar_t> buffer(static_cast<int>(size / sizeof(wchar_t)) + 1);
    result = RegQueryValueExW(key, nullptr, nullptr, &type, reinterpret_cast<LPBYTE>(buffer.data()), &size);
    RegCloseKey(key);
    if (result != ERROR_SUCCESS) {
        return QString();
    }

    buffer.last() = L'\0';
    return QString::fromWCharArray(buffer.data()).trimmed();
}

QString previewHandlerGuidForExtension(const QString& extension)
{
    QString normalizedExtension = extension.trimmed().toLower();
    if (normalizedExtension.isEmpty()) {
        return QString();
    }
    if (!normalizedExtension.startsWith(QLatin1Char('.'))) {
        normalizedExtension.prepend(QLatin1Char('.'));
    }

    const QString directKey = normalizedExtension + QLatin1Char('\\') + QString::fromWCharArray(kPreviewHandlerShellExKey);
    QString handlerGuid = readRegistryDefaultValue(directKey);
    if (!handlerGuid.isEmpty()) {
        return handlerGuid;
    }

    const QString progId = readRegistryDefaultValue(normalizedExtension);
    if (!progId.isEmpty()) {
        const QString progIdKey = progId + QLatin1Char('\\') + QString::fromWCharArray(kPreviewHandlerShellExKey);
        handlerGuid = readRegistryDefaultValue(progIdKey);
        if (!handlerGuid.isEmpty()) {
            return handlerGuid;
        }
    }

    const QString systemAssociationKey = QStringLiteral("SystemFileAssociations\\%1\\%2")
        .arg(normalizedExtension, QString::fromWCharArray(kPreviewHandlerShellExKey));
    return readRegistryDefaultValue(systemAssociationKey);
}

}

PreviewHandlerHost::PreviewHandlerHost(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_NativeWindow, true);
    setAttribute(Qt::WA_DontCreateNativeAncestors, true);
    setAttribute(Qt::WA_StyledBackground, true);
    setObjectName(QStringLiteral("PreviewHandlerHost"));
    setStyleSheet(QStringLiteral(
        "#PreviewHandlerHost {"
        "  background: #ffffff;"
        "  border: 1px solid #d3dee9;"
        "  border-radius: 18px;"
        "}"));
    winId();
}

PreviewHandlerHost::~PreviewHandlerHost()
{
    unload();
    if (m_comInitialized) {
        CoUninitialize();
        m_comInitialized = false;
    }
}

bool PreviewHandlerHost::openFile(const QString& filePath, QString* errorMessage)
{
    unload();

    if (!ensureComInitialized(errorMessage)) {
        return false;
    }

    const QFileInfo fileInfo(filePath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("The selected document file does not exist.");
        }
        return false;
    }

    const QString handlerGuid = previewHandlerGuidForExtension(QStringLiteral(".") + fileInfo.suffix());
    if (handlerGuid.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("No Windows Preview Handler is registered for this Office format.");
        }
        return false;
    }

    CLSID clsid = {};
    HRESULT hr = CLSIDFromString(reinterpret_cast<LPCOLESTR>(handlerGuid.utf16()), &clsid);
    if (FAILED(hr)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("The registered Preview Handler CLSID is invalid: %1").arg(handlerGuid);
        }
        return false;
    }

    IUnknown* unknown = nullptr;
    hr = CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER, IID_IUnknown, reinterpret_cast<void**>(&unknown));
    if (FAILED(hr) || !unknown) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to activate Windows Preview Handler %1: %2")
                .arg(handlerGuid, hresultMessage(hr));
        }
        return false;
    }

    IInitializeWithFile* fileInitializer = nullptr;
    hr = unknown->QueryInterface(IID_IInitializeWithFile, reinterpret_cast<void**>(&fileInitializer));
    if (FAILED(hr) || !fileInitializer) {
        unknown->Release();
        if (errorMessage) {
            *errorMessage = QStringLiteral("The registered Preview Handler does not support file initialization.");
        }
        return false;
    }

    const QString nativePath = QDir::toNativeSeparators(fileInfo.absoluteFilePath());
    hr = fileInitializer->Initialize(reinterpret_cast<LPCWSTR>(nativePath.utf16()), STGM_READ);
    fileInitializer->Release();
    if (FAILED(hr)) {
        unknown->Release();
        if (errorMessage) {
            *errorMessage = QStringLiteral("Windows Preview Handler failed to open the file: %1").arg(hresultMessage(hr));
        }
        return false;
    }

    hr = unknown->QueryInterface(IID_IPreviewHandler, reinterpret_cast<void**>(&m_previewHandler));
    unknown->Release();
    if (FAILED(hr) || !m_previewHandler) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("The registered COM object is not a Preview Handler.");
        }
        return false;
    }

    updateGeometry();
    RECT rect = {};
    GetClientRect(reinterpret_cast<HWND>(winId()), &rect);
    hr = m_previewHandler->SetWindow(reinterpret_cast<HWND>(winId()), &rect);
    if (SUCCEEDED(hr)) {
        hr = m_previewHandler->DoPreview();
    }

    if (FAILED(hr)) {
        unload();
        if (errorMessage) {
            *errorMessage = QStringLiteral("Windows Preview Handler failed to render the preview: %1").arg(hresultMessage(hr));
        }
        return false;
    }

    m_activeHandlerGuid = handlerGuid;
    qDebug().noquote() << QStringLiteral("[SpaceLookRender] Windows Preview Handler loaded clsid=%1 path=\"%2\"")
        .arg(handlerGuid, fileInfo.absoluteFilePath());
    return true;
}

void PreviewHandlerHost::unload()
{
    m_activeHandlerGuid.clear();
    if (!m_previewHandler) {
        return;
    }

    m_previewHandler->Unload();
    m_previewHandler->Release();
    m_previewHandler = nullptr;
}

QString PreviewHandlerHost::activeHandlerGuid() const
{
    return m_activeHandlerGuid;
}

void PreviewHandlerHost::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updatePreviewRect();
}

bool PreviewHandlerHost::ensureComInitialized(QString* errorMessage)
{
    if (m_comInitialized) {
        return true;
    }

    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (SUCCEEDED(hr)) {
        m_comInitialized = true;
        return true;
    }

    if (hr == RPC_E_CHANGED_MODE) {
        return true;
    }

    if (errorMessage) {
        *errorMessage = QStringLiteral("Failed to initialize COM for Windows Preview Handler: %1").arg(hresultMessage(hr));
    }
    return false;
}

void PreviewHandlerHost::updatePreviewRect()
{
    if (!m_previewHandler) {
        return;
    }

    updateGeometry();
    RECT rect = {};
    GetClientRect(reinterpret_cast<HWND>(winId()), &rect);
    m_previewHandler->SetRect(&rect);
}
