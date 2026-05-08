#pragma once

#include <QList>
#include <memory>
#include <vector>

#include "renderers/IPreviewRenderer.h"

class PreviewState;

class RendererRegistry
{
public:
    explicit RendererRegistry(PreviewState* previewState);
    ~RendererRegistry();

    QList<IPreviewRenderer*> renderers() const;
    IPreviewRenderer* rendererById(const QString& rendererId) const;
    IPreviewRenderer* rendererFor(const HoveredItemInfo& info) const;
    std::unique_ptr<IPreviewRenderer> createRendererById(const QString& rendererId) const;
    std::unique_ptr<IPreviewRenderer> createRendererFor(const HoveredItemInfo& info) const;
    void warmUpHeavyRenderers() const;

private:
    std::unique_ptr<IPreviewRenderer> createRendererByNormalizedId(const QString& normalizedRendererId) const;
    PreviewState* m_previewState = nullptr;
    std::vector<std::unique_ptr<IPreviewRenderer>> m_renderers;
};
