#pragma once

#include <memory>
#include <vector>

#include <QWidget>

#include "core/file_type_detector.h"
#include "core/hovered_item_info.h"
#include "renderers/IPreviewRenderer.h"
#include "renderers/PreviewLoadGuard.h"

class QStackedWidget;
class QLabel;
class QProgressBar;
class QResizeEvent;
class PreviewState;
class RendererRegistry;

class PreviewHost : public QWidget
{
    Q_OBJECT

public:
    explicit PreviewHost(PreviewState* previewState, QWidget* parent = nullptr);
    ~PreviewHost() override;

    void showPreview(const HoveredItemInfo& info);
    void stopPreview();
    bool previewHoveredFolderItem();
    bool pushPreviewForPath(const QString& filePath);
    bool popPreview();
    int previewStackDepth() const;
    QWidget* activeRendererWidget() const;
    QString activeRendererId() const;

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    struct PreviewStackEntry
    {
        HoveredItemInfo info;
        std::unique_ptr<IPreviewRenderer> renderer;
        PreviewLoadGuard loadGuard;
    };

    void createLoadingOverlay();
    void showLoadingOverlay(const HoveredItemInfo& info, const QString& rendererId);
    void hideLoadingOverlay();
    void updateLoadingOverlayGeometry();
    void showSummaryFallback(const HoveredItemInfo& info, const QString& reason);
    void pushPreview(const HoveredItemInfo& info);
    void showStackEntry(int index);
    void clearPreviewStack();
    void removeStackEntry(int index);
    void configureRenderer(IPreviewRenderer* renderer);
    void updateRouteBar();
    QString routeTitleForInfo(const HoveredItemInfo& info) const;
    IPreviewRenderer* activeRenderer() const;

    QWidget* m_routeBar = nullptr;
    QLabel* m_routeLabel = nullptr;
    QStackedWidget* m_stack = nullptr;
    QWidget* m_loadingOverlay = nullptr;
    QLabel* m_loadingTitleLabel = nullptr;
    QLabel* m_loadingMessageLabel = nullptr;
    QProgressBar* m_loadingProgress = nullptr;
    PreviewState* m_previewState = nullptr;
    RendererRegistry* m_registry = nullptr;
    FileTypeDetector m_fileTypeDetector;
    std::vector<std::unique_ptr<PreviewStackEntry>> m_previewStack;
    int m_activeIndex = -1;
    PreviewLoadGuard m_previewLoadGuard;
};
