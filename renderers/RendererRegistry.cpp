#include "renderers/RendererRegistry.h"

#include <QDebug>

#include "core/preview_state.h"
#include "renderers/summary/ArchiveRenderer.h"
#include "renderers/code/CodeRenderer.h"
#include "renderers/document/DocumentRenderer.h"
#include "renderers/image/ImageRenderer.h"
#include "renderers/media/MediaRenderer.h"
#include "renderers/pdf/PdfRenderer.h"
#include "renderers/summary/SummaryRenderer.h"
#include "renderers/text/TextRenderer.h"

RendererRegistry::RendererRegistry(PreviewState* previewState)
{
    Q_UNUSED(previewState);
    auto registerRenderer = [this](std::unique_ptr<IPreviewRenderer> renderer) {
        if (!renderer) {
            qDebug().noquote() << QStringLiteral("[SpaceLookRender] Startup renderer registration failed because renderer is null");
            return;
        }

        const QString rendererName = renderer->rendererId();
        const bool widgetReady = renderer->widget() != nullptr;
        qDebug().noquote() << QStringLiteral("[SpaceLookRender] Startup renderer=\"%1\" widgetReady=%2")
            .arg(rendererName, widgetReady ? QStringLiteral("true") : QStringLiteral("false"));
        m_renderers.push_back(std::move(renderer));
    };

    registerRenderer(std::make_unique<PdfRenderer>());
    registerRenderer(std::make_unique<DocumentRenderer>());
    registerRenderer(std::make_unique<ArchiveRenderer>());
    registerRenderer(std::make_unique<CodeRenderer>());
    registerRenderer(std::make_unique<TextRenderer>());
    registerRenderer(std::make_unique<ImageRenderer>());
    registerRenderer(std::make_unique<MediaRenderer>());
    registerRenderer(std::make_unique<SummaryRenderer>(previewState));
}

RendererRegistry::~RendererRegistry() = default;

QList<IPreviewRenderer*> RendererRegistry::renderers() const
{
    QList<IPreviewRenderer*> result;
    for (const std::unique_ptr<IPreviewRenderer>& renderer : m_renderers) {
        result.append(renderer.get());
    }
    return result;
}

IPreviewRenderer* RendererRegistry::rendererFor(const HoveredItemInfo& info) const
{
    for (const std::unique_ptr<IPreviewRenderer>& renderer : m_renderers) {
        if (renderer && renderer->canHandle(info)) {
            return renderer.get();
        }
    }
    return nullptr;
}
