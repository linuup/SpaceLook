#pragma once

#include <QtGlobal>
#include <QWidget>

#include "core/hovered_item_info.h"
#include "renderers/IPreviewRenderer.h"
#include "renderers/PreviewCancellationToken.h"
#include "renderers/PreviewLoadGuard.h"

class QLabel;
class LiteHtmlView;
class OpenWithButton;
class SelectableTitleLabel;
class QStackedWidget;
class WebView2HtmlView;
class QWidget;

class RenderedPageRenderer : public QWidget, public IPreviewRenderer
{
    Q_OBJECT

public:
    explicit RenderedPageRenderer(QWidget* parent = nullptr);

    QString rendererId() const override;
    bool canHandle(const HoveredItemInfo& info) const override;
    QWidget* widget() override;
    void load(const HoveredItemInfo& info) override;
    void unload() override;
    bool reportsLoadingState() const override;
    void setLoadingStateCallback(std::function<void(bool)> callback) override;

private:
    void applyChrome();
    void showStatusMessage(const QString& message);
    QString prepareHtmlDocument(const QString& html, const QString& filePath, bool markdownSource) const;
    void notifyLoadingState(bool loading);

    HoveredItemInfo m_info;
    PreviewLoadGuard m_loadGuard;
    PreviewCancellationToken m_cancelToken;
    std::function<void(bool)> m_loadingStateCallback;
    QWidget* m_headerRow = nullptr;
    QLabel* m_iconLabel = nullptr;
    SelectableTitleLabel* m_titleLabel = nullptr;
    QWidget* m_pathRow = nullptr;
    QLabel* m_pathValueLabel = nullptr;
    OpenWithButton* m_openWithButton = nullptr;
    QLabel* m_statusLabel = nullptr;
    QStackedWidget* m_contentStack = nullptr;
    WebView2HtmlView* m_webView = nullptr;
    LiteHtmlView* m_fallbackHtmlView = nullptr;
};
