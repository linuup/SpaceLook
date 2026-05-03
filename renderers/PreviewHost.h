#pragma once

#include <QWidget>

#include "core/hovered_item_info.h"

class QStackedWidget;
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

private:
    QStackedWidget* m_stack = nullptr;
    PreviewState* m_previewState = nullptr;
    RendererRegistry* m_registry = nullptr;
    IPreviewRenderer* m_activeRenderer = nullptr;
};
