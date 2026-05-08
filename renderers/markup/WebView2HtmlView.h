#pragma once

#include <QWidget>
#include <memory>

#include "renderers/PreviewLoadGuard.h"

struct ICoreWebView2;
struct ICoreWebView2Controller;
struct ICoreWebView2Environment;

class WebView2HtmlView : public QWidget
{
    Q_OBJECT

public:
    explicit WebView2HtmlView(QWidget* parent = nullptr);
    ~WebView2HtmlView() override;

    bool setDocumentHtml(const QString& html, const QString& filePath, QString* errorMessage);
    void clearDocument();
    bool warmUp(QString* errorMessage = nullptr);
    QString lastError() const;

    static QString runtimeDownloadUrl();

signals:
    void documentLoaded();
    void unavailable(const QString& message);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    bool ensureRuntimeAvailable(QString* errorMessage) const;
    bool ensureInitializing(QString* errorMessage);
    void navigatePendingHtml(const PreviewLoadGuard::Token& token);
    void updateControllerBounds();
    void setLastError(const QString& message);

    ICoreWebView2Environment* m_environment = nullptr;
    ICoreWebView2Controller* m_controller = nullptr;
    ICoreWebView2* m_webView = nullptr;
    PreviewLoadGuard m_loadGuard;
    QString m_pendingHtml;
    QString m_pendingFilePath;
    QString m_lastError;
    std::shared_ptr<bool> m_alive;
    bool m_initializing = false;
    bool m_ready = false;
    bool m_comInitialized = false;
};
