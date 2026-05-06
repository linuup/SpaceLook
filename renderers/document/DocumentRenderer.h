#pragma once

#include <functional>

#include <QtGlobal>
#include <QWidget>

#include "core/hovered_item_info.h"
#include "renderers/IPreviewRenderer.h"
#include "renderers/PreviewCancellationToken.h"
#include "renderers/PreviewLoadGuard.h"

class QLabel;
class OpenWithButton;
class PreviewHandlerHost;
class SelectableTitleLabel;
class QStackedWidget;
class QTextBrowser;
class QWidget;

class DocumentRenderer : public QWidget, public IPreviewRenderer
{
    Q_OBJECT

public:
    explicit DocumentRenderer(QWidget* parent = nullptr);

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
    void notifyLoadingState(bool loading);
    void loadWithQtParser(const HoveredItemInfo& info, const PreviewLoadGuard::Token& loadToken, const QString& handlerError);

    HoveredItemInfo m_info;
    PreviewLoadGuard m_loadGuard;
    PreviewCancellationToken m_cancelToken;
    std::function<void(bool)> m_loadingStateCallback;
    QWidget* m_headerRow = nullptr;
    QLabel* m_iconLabel = nullptr;
    SelectableTitleLabel* m_titleLabel = nullptr;
    QLabel* m_metaLabel = nullptr;
    QWidget* m_pathRow = nullptr;
    QLabel* m_pathTitleLabel = nullptr;
    QLabel* m_pathValueLabel = nullptr;
    OpenWithButton* m_openWithButton = nullptr;
    QLabel* m_statusLabel = nullptr;
    QStackedWidget* m_contentStack = nullptr;
    PreviewHandlerHost* m_previewHandlerHost = nullptr;
    QTextBrowser* m_textBrowser = nullptr;
};
