#include "renderers/document/PreviewHandlerHost.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objbase.h>
#include <shlwapi.h>
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

bool registryKeyExists(const QString& subKey)
{
    HKEY key = nullptr;
    const std::wstring nativeSubKey = subKey.toStdWString();
    const LONG result = RegOpenKeyExW(HKEY_CLASSES_ROOT, nativeSubKey.c_str(), 0, KEY_READ, &key);
    if (result == ERROR_SUCCESS) {
        RegCloseKey(key);
        return true;
    }
    return false;
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
    const QFileInfo fileInfo(filePath);
    const QString handlerGuid = previewHandlerGuidForExtension(QStringLiteral(".") + fileInfo.suffix());
    if (handlerGuid.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("No Windows Preview Handler is registered for this file format.");
        }
        return false;
    }

    return openFileWithHandlerGuid(filePath, handlerGuid, QStringLiteral("Windows Preview Handler"), false, errorMessage);
}

bool PreviewHandlerHost::openFileWithHandler(const QString& filePath, const QString& handlerGuid, QString* errorMessage)
{
    const QString cleanGuid = handlerGuid.trimmed();
    if (cleanGuid.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("The requested Preview Handler CLSID is empty.");
        }
        return false;
    }

    return openFileWithHandlerGuid(filePath, cleanGuid, QStringLiteral("requested Preview Handler"), true, errorMessage);
}

void PreviewHandlerHost::warmUp()
{
    QString ignoredError;
    ensureComInitialized(&ignoredError);
    winId();
}

bool PreviewHandlerHost::openFileWithHandlerGuid(const QString& filePath,
                                                 const QString& handlerGuid,
                                                 const QString& handlerLabel,
                                                 bool requireRegistration,
                                                 QString* errorMessage)
{
    const QString cleanGuid = handlerGuid.trimmed();
    if (cleanGuid.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("The %1 CLSID is empty.").arg(handlerLabel);
        }
        return false;
    }

    const QString clsidKey = QStringLiteral("CLSID\\%1").arg(cleanGuid);
    if (requireRegistration && !registryKeyExists(clsidKey)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("The %1 is not registered: %2").arg(handlerLabel, cleanGuid);
        }
        return false;
    }

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

    CLSID clsid = {};
    HRESULT hr = CLSIDFromString(reinterpret_cast<LPCOLESTR>(cleanGuid.utf16()), &clsid);
    if (FAILED(hr)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("The %1 CLSID is invalid: %2").arg(handlerLabel, cleanGuid);
        }
        return false;
    }

    IUnknown* unknown = nullptr;
    hr = CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER, IID_IUnknown, reinterpret_cast<void**>(&unknown));
    if (FAILED(hr) || !unknown) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to activate %1 %2: %3")
                .arg(handlerLabel, cleanGuid, hresultMessage(hr));
        }
        return false;
    }

    const QString nativePath = QDir::toNativeSeparators(fileInfo.absoluteFilePath());
    bool initialized = false;
    QString initializationError;

    IInitializeWithStream* streamInitializer = nullptr;
    hr = unknown->QueryInterface(IID_IInitializeWithStream, reinterpret_cast<void**>(&streamInitializer));
    if (SUCCEEDED(hr) && streamInitializer) {
        IStream* stream = nullptr;
        const HRESULT streamHr = SHCreateStreamOnFileEx(
            reinterpret_cast<LPCWSTR>(nativePath.utf16()),
            STGM_READ | STGM_SHARE_DENY_NONE,
            FILE_ATTRIBUTE_NORMAL,
            FALSE,
            nullptr,
            &stream);
        if (SUCCEEDED(streamHr) && stream) {
            hr = streamInitializer->Initialize(stream, STGM_READ | STGM_SHARE_DENY_NONE);
            stream->Release();
            initialized = SUCCEEDED(hr);
            if (!initialized) {
                initializationError = hresultMessage(hr);
            }
        } else {
            initializationError = hresultMessage(streamHr);
        }
        streamInitializer->Release();
    }

    if (!initialized) {
        IInitializeWithFile* fileInitializer = nullptr;
        hr = unknown->QueryInterface(IID_IInitializeWithFile, reinterpret_cast<void**>(&fileInitializer));
        if (SUCCEEDED(hr) && fileInitializer) {
            hr = fileInitializer->Initialize(reinterpret_cast<LPCWSTR>(nativePath.utf16()), STGM_READ | STGM_SHARE_DENY_NONE);
            fileInitializer->Release();
            initialized = SUCCEEDED(hr);
            if (!initialized) {
                initializationError = hresultMessage(hr);
            }
        }
    }

    if (!initialized) {
        unknown->Release();
        if (errorMessage) {
            *errorMessage = initializationError.isEmpty()
                ? QStringLiteral("The %1 does not support shared file initialization.").arg(handlerLabel)
                : QStringLiteral("The %1 failed to open the file: %2").arg(handlerLabel, initializationError);
        }
        return false;
    }

    hr = unknown->QueryInterface(IID_IPreviewHandler, reinterpret_cast<void**>(&m_previewHandler));
    unknown->Release();
    if (FAILED(hr) || !m_previewHandler) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("The %1 COM object is not a Preview Handler.").arg(handlerLabel);
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
            *errorMessage = QStringLiteral("The %1 failed to render the preview: %2").arg(handlerLabel, hresultMessage(hr));
        }
        return false;
    }

    m_activeHandlerGuid = cleanGuid;
    qDebug().noquote() << QStringLiteral("[SpaceLookRender] %1 loaded clsid=%2 path=\"%3\"")
        .arg(handlerLabel, cleanGuid, fileInfo.absoluteFilePath());
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
