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

QString readRegistryDefaultValue(HKEY rootKey, const QString& subKey, REGSAM viewAccess)
{
    HKEY key = nullptr;
    const std::wstring nativeSubKey = subKey.toStdWString();
    const LONG result = RegOpenKeyExW(rootKey, nativeSubKey.c_str(), 0, KEY_READ | viewAccess, &key);
    if (result != ERROR_SUCCESS) {
        return QString();
    }

    DWORD type = 0;
    DWORD size = 0;
    LONG queryResult = RegQueryValueExW(key, nullptr, nullptr, &type, nullptr, &size);
    if (queryResult != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || size < sizeof(wchar_t)) {
        RegCloseKey(key);
        return QString();
    }

    QVector<wchar_t> buffer(static_cast<int>(size / sizeof(wchar_t)) + 1);
    queryResult = RegQueryValueExW(key, nullptr, nullptr, &type, reinterpret_cast<LPBYTE>(buffer.data()), &size);
    RegCloseKey(key);
    if (queryResult != ERROR_SUCCESS) {
        return QString();
    }

    buffer.last() = L'\0';
    return QString::fromWCharArray(buffer.data()).trimmed();
}

bool handlerHasServerInView(const QString& handlerGuid, REGSAM viewAccess)
{
    const QString clsidKey = QStringLiteral("CLSID\\%1").arg(handlerGuid);
    return !readRegistryDefaultValue(HKEY_CLASSES_ROOT, clsidKey + QStringLiteral("\\LocalServer32"), viewAccess).isEmpty() ||
        !readRegistryDefaultValue(HKEY_CLASSES_ROOT, clsidKey + QStringLiteral("\\InprocServer32"), viewAccess).isEmpty();
}

bool handlerRegisteredIn32BitView(const QString& handlerGuid)
{
    return handlerHasServerInView(handlerGuid, KEY_WOW64_32KEY);
}

bool handlerRegisteredIn64BitView(const QString& handlerGuid)
{
    return handlerHasServerInView(handlerGuid, KEY_WOW64_64KEY);
}

DWORD previewHandlerActivationContext(const QString& handlerGuid)
{
    DWORD context = CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER;
    const bool has64BitRegistration = handlerRegisteredIn64BitView(handlerGuid);
    const bool has32BitRegistration = handlerRegisteredIn32BitView(handlerGuid);

#if defined(_WIN64)
    if (!has64BitRegistration && has32BitRegistration) {
        context = CLSCTX_LOCAL_SERVER | CLSCTX_ACTIVATE_32_BIT_SERVER;
    }
#else
    if (!has32BitRegistration && has64BitRegistration) {
        context = CLSCTX_LOCAL_SERVER | CLSCTX_ACTIVATE_64_BIT_SERVER;
    }
#endif

    return context;
}

QString processArchitectureLabel()
{
#if defined(_WIN64)
    return QStringLiteral("x64");
#else
    return QStringLiteral("x86");
#endif
}

QString registeredArchitectureForHandler(const QString& handlerGuid)
{
    const bool has64BitRegistration = handlerRegisteredIn64BitView(handlerGuid);
    const bool has32BitRegistration = handlerRegisteredIn32BitView(handlerGuid);

    if (has64BitRegistration && has32BitRegistration) {
        return QStringLiteral("x64 and x86");
    }
    if (has64BitRegistration) {
        return QStringLiteral("x64");
    }
    if (has32BitRegistration) {
        return QStringLiteral("x86");
    }
    return QStringLiteral("unknown");
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

bool isLikelyRegisteredForOtherProcessBitness(const QString& handlerGuid)
{
    const bool has64BitRegistration = handlerRegisteredIn64BitView(handlerGuid);
    const bool has32BitRegistration = handlerRegisteredIn32BitView(handlerGuid);
#if defined(_WIN64)
    return !has64BitRegistration && has32BitRegistration;
#else
    return has64BitRegistration && !has32BitRegistration;
#endif
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
    if (m_comInitializedHere) {
        CoUninitialize();
        m_comInitializedHere = false;
    }
}

bool PreviewHandlerHost::openFile(const QString& filePath, QString* errorMessage)
{
    const PreviewHandlerOpenResult result = openFileDetailed(filePath);
    if (errorMessage) {
        *errorMessage = result.message;
    }
    return result.success;
}

PreviewHandlerOpenResult PreviewHandlerHost::openFileDetailed(const QString& filePath)
{
    unload();
    PreviewHandlerOpenResult result;
    result.processArchitecture = processArchitectureLabel();

    QString comError;
    if (!ensureComInitialized(&comError)) {
        result.message = comError;
        return result;
    }

    const QFileInfo fileInfo(filePath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        result.message = QStringLiteral("The selected document file does not exist.");
        return result;
    }

    const QString handlerGuid = previewHandlerGuidForExtension(QStringLiteral(".") + fileInfo.suffix());
    result.handlerGuid = handlerGuid;
    result.registeredArchitecture = handlerGuid.isEmpty()
        ? QStringLiteral("unknown")
        : registeredArchitectureForHandler(handlerGuid);
    if (handlerGuid.isEmpty()) {
        result.message = QStringLiteral("No Windows Preview Handler is registered for this Office format.");
        qDebug().noquote() << QStringLiteral("[SpaceLookRender] Windows Preview Handler lookup failed path=\"%1\" processArch=%2 registeredArch=%3")
            .arg(fileInfo.absoluteFilePath(), result.processArchitecture, result.registeredArchitecture);
        return result;
    }

    CLSID clsid = {};
    HRESULT hr = CLSIDFromString(reinterpret_cast<LPCOLESTR>(handlerGuid.utf16()), &clsid);
    if (FAILED(hr)) {
        result.message = QStringLiteral("The registered Preview Handler CLSID is invalid: %1").arg(handlerGuid);
        qDebug().noquote() << QStringLiteral("[SpaceLookRender] Windows Preview Handler invalid clsid=%1 path=\"%2\" processArch=%3 registeredArch=%4")
            .arg(handlerGuid, fileInfo.absoluteFilePath(), result.processArchitecture, result.registeredArchitecture);
        return result;
    }

    IUnknown* unknown = nullptr;
    const DWORD activationContext = previewHandlerActivationContext(handlerGuid);
    hr = CoCreateInstance(clsid, nullptr, activationContext, IID_IUnknown, reinterpret_cast<void**>(&unknown));
    if (FAILED(hr) || !unknown) {
        result.architectureMismatch = hr == REGDB_E_CLASSNOTREG && isLikelyRegisteredForOtherProcessBitness(handlerGuid);
        if (result.architectureMismatch) {
            result.message = QStringLiteral("Current SpaceLook process is %1.\nInstalled Preview Handler %2 is registered as %3.")
                .arg(result.processArchitecture, handlerGuid, result.registeredArchitecture);
        } else {
            result.message = QStringLiteral("Failed to activate Windows Preview Handler %1: %2")
                .arg(handlerGuid, hresultMessage(hr));
        }
        qDebug().noquote() << QStringLiteral("[SpaceLookRender] Windows Preview Handler activation failed clsid=%1 path=\"%2\" hr=0x%3 message=\"%4\" processArch=%5 registeredArch=%6 mismatch=%7 context=0x%8")
            .arg(handlerGuid,
                 fileInfo.absoluteFilePath(),
                 QString::number(static_cast<qulonglong>(static_cast<unsigned long>(hr)), 16).rightJustified(8, QLatin1Char('0')),
                 hresultMessage(hr),
                 result.processArchitecture,
                 result.registeredArchitecture,
                 result.architectureMismatch ? QStringLiteral("true") : QStringLiteral("false"),
                 QString::number(static_cast<qulonglong>(activationContext), 16));
        return result;
    }

    IInitializeWithFile* fileInitializer = nullptr;
    hr = unknown->QueryInterface(IID_IInitializeWithFile, reinterpret_cast<void**>(&fileInitializer));
    if (FAILED(hr) || !fileInitializer) {
        unknown->Release();
        result.message = QStringLiteral("The registered Preview Handler does not support file initialization.");
        qDebug().noquote() << QStringLiteral("[SpaceLookRender] Windows Preview Handler missing IInitializeWithFile clsid=%1 path=\"%2\" hr=0x%3 message=\"%4\" processArch=%5 registeredArch=%6")
            .arg(handlerGuid,
                 fileInfo.absoluteFilePath(),
                 QString::number(static_cast<qulonglong>(static_cast<unsigned long>(hr)), 16).rightJustified(8, QLatin1Char('0')),
                 hresultMessage(hr),
                 result.processArchitecture,
                 result.registeredArchitecture);
        return result;
    }

    const QString nativePath = QDir::toNativeSeparators(fileInfo.absoluteFilePath());
    hr = fileInitializer->Initialize(reinterpret_cast<LPCWSTR>(nativePath.utf16()), 0);
    fileInitializer->Release();
    if (FAILED(hr)) {
        unknown->Release();
        result.message = QStringLiteral("Windows Preview Handler failed to open the file: %1").arg(hresultMessage(hr));
        qDebug().noquote() << QStringLiteral("[SpaceLookRender] Windows Preview Handler initialize failed clsid=%1 path=\"%2\" hr=0x%3 message=\"%4\" processArch=%5 registeredArch=%6")
            .arg(handlerGuid,
                 fileInfo.absoluteFilePath(),
                 QString::number(static_cast<qulonglong>(static_cast<unsigned long>(hr)), 16).rightJustified(8, QLatin1Char('0')),
                 hresultMessage(hr),
                 result.processArchitecture,
                 result.registeredArchitecture);
        return result;
    }

    hr = unknown->QueryInterface(IID_IPreviewHandler, reinterpret_cast<void**>(&m_previewHandler));
    unknown->Release();
    if (FAILED(hr) || !m_previewHandler) {
        result.message = QStringLiteral("The registered COM object is not a Preview Handler.");
        qDebug().noquote() << QStringLiteral("[SpaceLookRender] Windows Preview Handler missing IPreviewHandler clsid=%1 path=\"%2\" processArch=%3 registeredArch=%4")
            .arg(handlerGuid, fileInfo.absoluteFilePath(), result.processArchitecture, result.registeredArchitecture);
        return result;
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
        result.message = QStringLiteral("Windows Preview Handler failed to render the preview: %1").arg(hresultMessage(hr));
        qDebug().noquote() << QStringLiteral("[SpaceLookRender] Windows Preview Handler render failed clsid=%1 path=\"%2\" hr=0x%3 message=\"%4\" processArch=%5 registeredArch=%6")
            .arg(handlerGuid,
                 fileInfo.absoluteFilePath(),
                 QString::number(static_cast<qulonglong>(static_cast<unsigned long>(hr)), 16).rightJustified(8, QLatin1Char('0')),
                 hresultMessage(hr),
                 result.processArchitecture,
                 result.registeredArchitecture);
        return result;
    }

    m_activeHandlerGuid = handlerGuid;
    result.success = true;
    qDebug().noquote() << QStringLiteral("[SpaceLookRender] Windows Preview Handler loaded clsid=%1 path=\"%2\" processArch=%3 registeredArch=%4")
        .arg(handlerGuid, fileInfo.absoluteFilePath(), result.processArchitecture, result.registeredArchitecture);
    return result;
}

void PreviewHandlerHost::warmUp()
{
    QString ignoredError;
    ensureComInitialized(&ignoredError);
    winId();
}

void PreviewHandlerHost::unload()
{
    m_activeHandlerGuid.clear();
    if (!m_previewHandler) {
        return;
    }

    IPreviewHandler* previewHandler = m_previewHandler;
    m_previewHandler = nullptr;
    previewHandler->Unload();
    previewHandler->Release();
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
    if (m_comInitializedHere) {
        return true;
    }

    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (SUCCEEDED(hr)) {
        m_comInitializedHere = true;
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
