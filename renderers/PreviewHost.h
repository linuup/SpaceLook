#pragma once

#include <QWidget>

#include "core/hovered_item_info.h"
#include "renderers/PreviewLoadGuard.h"

class QStackedWidget;
class QLabel;
class QProgressBar;
class QResizeEvent;
class PreviewState;
class RendererRegistry;
class IPreviewRenderer;

class PreviewHost : public QWidget
{
    Q_OBJECT

public:
    explicit PreviewHost(PreviewState* previewState, QWidget* parent = nullptr);
    ~PreviewHost() override;

    void showPreview(const HoveredItemInfo& info);
    void stopPreview();
    QWidget* activeRendererWidget() const;
    QString activeRendererId() const;

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void createLoadingOverlay();
    void showLoadingOverlay(const HoveredItemInfo& info, const QString& rendererId);
    void hideLoadingOverlay();
    void updateLoadingOverlayGeometry();
    void showSummaryFallback(const HoveredItemInfo& info, const QString& reason);

    QStackedWidget* m_stack = nullptr;
    QWidget* m_loadingOverlay = nullptr;
    QLabel* m_loadingTitleLabel = nullptr;
    QLabel* m_loadingMessageLabel = nullptr;
    QProgressBar* m_loadingProgress = nullptr;
    PreviewState* m_previewState = nullptr;
    RendererRegistry* m_registry = nullptr;
    IPreviewRenderer* m_activeRenderer = nullptr;
    PreviewLoadGuard m_previewLoadGuard;
};
