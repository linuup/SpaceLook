#include "renderers/markup/WebView2HtmlView.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objbase.h>

#include <WebView2.h>
#include <wrl.h>

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QResizeEvent>
#include <QShowEvent>
#include <QStandardPaths>
#include <QTimer>

using Microsoft::WRL::Callback;

namespace {

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
    return message.isEmpty()
        ? QStringLiteral("HRESULT 0x%1").arg(static_cast<qulonglong>(static_cast<unsigned long>(hr)), 8, 16, QLatin1Char('0'))
        : message;
}

QString webViewUserDataFolder()
{
    QString root = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (root.trimmed().isEmpty()) {
        root = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("WebView2Data"));
    }

    const QString folder = QDir(root).filePath(QStringLiteral("WebView2"));
    QDir().mkpath(folder);
    return QDir::toNativeSeparators(folder);
}

template <typename T>
void releaseComObject(T*& object)
{
    if (!object) {
        return;
    }

    object->Release();
    object = nullptr;
}

}

WebView2HtmlView::WebView2HtmlView(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_NativeWindow, true);
    setAttribute(Qt::WA_DontCreateNativeAncestors, true);
    setAttribute(Qt::WA_StyledBackground, true);
    setObjectName(QStringLiteral("WebView2HtmlView"));
    setStyleSheet(QStringLiteral(
        "#WebView2HtmlView {"
        "  background: #ffffff;"
        "  border: 1px solid #d0d7de;"
        "  border-radius: 12px;"
        "}"));
    winId();
}

WebView2HtmlView::~WebView2HtmlView()
{
    clearDocument();
    if (m_comInitialized) {
        CoUninitialize();
        m_comInitialized = false;
    }
}

bool WebView2HtmlView::setDocumentHtml(const QString& html, const QString& filePath, QString* errorMessage)
{
    const PreviewLoadGuard::Token loadToken = m_loadGuard.begin(filePath);
    m_pendingHtml = html;
    m_pendingFilePath = filePath;
    setLastError(QString());

    if (!ensureRuntimeAvailable(errorMessage)) {
        return false;
    }

    if (m_ready && m_webView) {
        navigatePendingHtml(loadToken);
        return true;
    }

    return ensureInitializing(errorMessage);
}

void WebView2HtmlView::clearDocument()
{
    m_loadGuard.cancel();
    m_pendingHtml.clear();
    m_pendingFilePath.clear();
    if (m_webView) {
        m_webView->NavigateToString(L"<html><body></body></html>");
    }
    if (m_controller) {
        m_controller->put_IsVisible(FALSE);
        m_controller->Close();
    }
    releaseComObject(m_webView);
    releaseComObject(m_controller);
    releaseComObject(m_environment);
    m_ready = false;
    m_initializing = false;
}

bool WebView2HtmlView::warmUp(QString* errorMessage)
{
    if (!ensureRuntimeAvailable(errorMessage)) {
        return false;
    }
    winId();
    if (m_ready && m_webView) {
        return true;
    }
    if (m_pendingHtml.isEmpty()) {
        m_pendingHtml = QStringLiteral("<!doctype html><html><body></body></html>");
        m_pendingFilePath = QStringLiteral("spacelook:warmup");
    }
    return ensureInitializing(errorMessage);
}

QString WebView2HtmlView::lastError() const
{
    return m_lastError;
}

QString WebView2HtmlView::runtimeDownloadUrl()
{
    return QStringLiteral("https://developer.microsoft.com/microsoft-edge/webview2/");
}

void WebView2HtmlView::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateControllerBounds();
}

void WebView2HtmlView::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    QTimer::singleShot(0, this, [this]() {
        updateControllerBounds();
    });
}

bool WebView2HtmlView::ensureRuntimeAvailable(QString* errorMessage) const
{
    LPWSTR versionInfo = nullptr;
    const HRESULT hr = GetAvailableCoreWebView2BrowserVersionString(nullptr, &versionInfo);
    if (versionInfo) {
        CoTaskMemFree(versionInfo);
    }
    if (SUCCEEDED(hr)) {
        return true;
    }

    const QString message = QStringLiteral("Microsoft Edge WebView2 Runtime is not installed. Install WebView2 Runtime to enable the modern HTML preview engine.");
    if (errorMessage) {
        *errorMessage = message;
    }
    return false;
}

bool WebView2HtmlView::ensureInitializing(QString* errorMessage)
{
    if (m_initializing) {
        return true;
    }

    const HRESULT initHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (SUCCEEDED(initHr)) {
        m_comInitialized = true;
    } else if (initHr != RPC_E_CHANGED_MODE) {
        const QString message = QStringLiteral("Failed to initialize COM for WebView2: %1").arg(hresultMessage(initHr));
        if (errorMessage) {
            *errorMessage = message;
        }
        setLastError(message);
        return false;
    }

    m_initializing = true;
    const QString userDataFolder = webViewUserDataFolder();
    const HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr,
        reinterpret_cast<LPCWSTR>(userDataFolder.utf16()),
        nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this](HRESULT result, ICoreWebView2Environment* environment) -> HRESULT {
                if (m_pendingHtml.isEmpty()) {
                    m_initializing = false;
                    return S_OK;
                }
                if (FAILED(result) || !environment) {
                    const QString message = QStringLiteral("Failed to create WebView2 environment: %1").arg(hresultMessage(result));
                    setLastError(message);
                    m_initializing = false;
                    if (!m_pendingHtml.isEmpty()) {
                        emit unavailable(message);
                    }
                    return S_OK;
                }

                releaseComObject(m_environment);
                m_environment = environment;
                m_environment->AddRef();
                const HWND parentWindow = reinterpret_cast<HWND>(winId());
                const HRESULT controllerHr = m_environment->CreateCoreWebView2Controller(
                    parentWindow,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [this](HRESULT controllerResult, ICoreWebView2Controller* controller) -> HRESULT {
                            m_initializing = false;
                            if (m_pendingHtml.isEmpty()) {
                                if (controller) {
                                    controller->Close();
                                }
                                return S_OK;
                            }
                            if (FAILED(controllerResult) || !controller) {
                                const QString message = QStringLiteral("Failed to create WebView2 controller: %1").arg(hresultMessage(controllerResult));
                                setLastError(message);
                                if (!m_pendingHtml.isEmpty()) {
                                    emit unavailable(message);
                                }
                                return S_OK;
                            }

                            releaseComObject(m_controller);
                            releaseComObject(m_webView);
                            m_controller = controller;
                            m_controller->AddRef();
                            m_controller->get_CoreWebView2(&m_webView);
                            m_ready = m_webView != nullptr;
                            updateControllerBounds();
                            if (m_controller) {
                                m_controller->put_IsVisible(TRUE);
                            }
                            navigatePendingHtml(m_loadGuard.observe(m_pendingFilePath));
                            return S_OK;
                        }).Get());

                if (FAILED(controllerHr)) {
                    const QString message = QStringLiteral("Failed to start WebView2 controller creation: %1").arg(hresultMessage(controllerHr));
                    setLastError(message);
                    m_initializing = false;
                    if (!m_pendingHtml.isEmpty()) {
                        emit unavailable(message);
                    }
                }
                return S_OK;
            }).Get());

    if (FAILED(hr)) {
        m_initializing = false;
        const QString message = QStringLiteral("Failed to start WebView2 initialization: %1").arg(hresultMessage(hr));
        if (errorMessage) {
            *errorMessage = message;
        }
        setLastError(message);
        return false;
    }

    return true;
}

void WebView2HtmlView::navigatePendingHtml(const PreviewLoadGuard::Token& token)
{
    if (!m_webView || !m_ready || m_pendingHtml.isEmpty() || !m_loadGuard.isCurrent(token, m_pendingFilePath)) {
        return;
    }

    m_webView->NavigateToString(reinterpret_cast<LPCWSTR>(m_pendingHtml.utf16()));
    if (m_loadGuard.isCurrent(token, m_pendingFilePath)) {
        emit documentLoaded();
    }
}

void WebView2HtmlView::updateControllerBounds()
{
    if (!m_controller) {
        return;
    }

    RECT bounds = {};
    GetClientRect(reinterpret_cast<HWND>(winId()), &bounds);
    m_controller->put_Bounds(bounds);
}

void WebView2HtmlView::setLastError(const QString& message)
{
    m_lastError = message;
    if (!message.trimmed().isEmpty()) {
        qDebug().noquote() << QStringLiteral("[SpaceLookRender] WebView2 unavailable: %1").arg(message);
    }
}
