#pragma once

#include <QList>
#include <QObject>
#include <memory>
#include <vector>

#include "renderers/IPreviewRenderer.h"

class PreviewState;

class RendererRegistry : public QObject
{
    Q_OBJECT

public:
    explicit RendererRegistry(PreviewState* previewState, QObject* parent = nullptr);
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
