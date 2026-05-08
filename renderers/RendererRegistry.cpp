#include "renderers/RendererRegistry.h"

#include <QDebug>
#include <QHash>
#include <QTimer>

#include "core/preview_state.h"
#include "renderers/summary/ArchiveRenderer.h"
#include "renderers/certificate/CertificateRenderer.h"
#include "renderers/code/CodeRenderer.h"
#include "renderers/document/DocumentRenderer.h"
#include "renderers/image/ImageRenderer.h"
#include "renderers/folder/FolderRenderer.h"
#include "renderers/markup/RenderedPageRenderer.h"
#include "renderers/media/MediaRenderer.h"
#include "renderers/pdf/PdfRenderer.h"
#include "renderers/summary/SummaryRenderer.h"
#include "renderers/text/TextRenderer.h"
#include "renderers/welcome/WelcomeRenderer.h"

RendererRegistry::RendererRegistry(PreviewState* previewState)
    : m_previewState(previewState)
{
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
    registerRenderer(std::make_unique<WelcomeRenderer>());
    registerRenderer(std::make_unique<DocumentRenderer>());
    registerRenderer(std::make_unique<ArchiveRenderer>());
    registerRenderer(std::make_unique<CertificateRenderer>());
    registerRenderer(std::make_unique<FolderRenderer>());
    registerRenderer(std::make_unique<RenderedPageRenderer>());
    registerRenderer(std::make_unique<CodeRenderer>(previewState));
    registerRenderer(std::make_unique<TextRenderer>(previewState));
    registerRenderer(std::make_unique<ImageRenderer>());
    registerRenderer(std::make_unique<MediaRenderer>());
    registerRenderer(std::make_unique<SummaryRenderer>(previewState));
}

RendererRegistry::~RendererRegistry() = default;

namespace {

QString normalizeRendererLookupKey(const QString& rendererId)
{
    QString normalized = rendererId.trimmed().toLower();
    if (normalized.endsWith(QStringLiteral("renderer"))) {
        normalized.chop(QStringLiteral("renderer").size());
    } else if (normalized.endsWith(QStringLiteral("render"))) {
        normalized.chop(QStringLiteral("render").size());
    }

    static const QHash<QString, QString> aliases = {
        { QStringLiteral("renderedpage"), QStringLiteral("rendered_page") },
        { QStringLiteral("summary"), QStringLiteral("summary") },
        { QStringLiteral("image"), QStringLiteral("image") },
        { QStringLiteral("media"), QStringLiteral("media") },
        { QStringLiteral("pdf"), QStringLiteral("pdf") },
        { QStringLiteral("text"), QStringLiteral("text") },
        { QStringLiteral("code"), QStringLiteral("code") },
        { QStringLiteral("document"), QStringLiteral("document") },
        { QStringLiteral("certificate"), QStringLiteral("certificate") },
        { QStringLiteral("archive"), QStringLiteral("archive") },
        { QStringLiteral("folder"), QStringLiteral("folder") }
    };

    return aliases.value(normalized, normalized);
}

}

QList<IPreviewRenderer*> RendererRegistry::renderers() const
{
    QList<IPreviewRenderer*> result;
    for (const std::unique_ptr<IPreviewRenderer>& renderer : m_renderers) {
        result.append(renderer.get());
    }
    return result;
}

IPreviewRenderer* RendererRegistry::rendererById(const QString& rendererId) const
{
    const QString normalizedRendererId = normalizeRendererLookupKey(rendererId);
    if (normalizedRendererId.isEmpty()) {
        return nullptr;
    }

    for (const std::unique_ptr<IPreviewRenderer>& renderer : m_renderers) {
        if (renderer && normalizeRendererLookupKey(renderer->rendererId()) == normalizedRendererId) {
            return renderer.get();
        }
    }

    return nullptr;
}

IPreviewRenderer* RendererRegistry::rendererFor(const HoveredItemInfo& info) const
{
    if (IPreviewRenderer* configuredRenderer = rendererById(info.rendererName)) {
        return configuredRenderer;
    }

    for (const std::unique_ptr<IPreviewRenderer>& renderer : m_renderers) {
        if (renderer && renderer->canHandle(info)) {
            return renderer.get();
        }
    }
    return nullptr;
}

std::unique_ptr<IPreviewRenderer> RendererRegistry::createRendererById(const QString& rendererId) const
{
    return createRendererByNormalizedId(normalizeRendererLookupKey(rendererId));
}

std::unique_ptr<IPreviewRenderer> RendererRegistry::createRendererFor(const HoveredItemInfo& info) const
{
    if (!info.rendererName.trimmed().isEmpty()) {
        if (std::unique_ptr<IPreviewRenderer> renderer = createRendererById(info.rendererName)) {
            return renderer;
        }
    }

    for (const std::unique_ptr<IPreviewRenderer>& renderer : m_renderers) {
        if (renderer && renderer->canHandle(info)) {
            return createRendererById(renderer->rendererId());
        }
    }

    return nullptr;
}

std::unique_ptr<IPreviewRenderer> RendererRegistry::createRendererByNormalizedId(const QString& normalizedRendererId) const
{
    if (normalizedRendererId == QStringLiteral("pdf")) {
        return std::make_unique<PdfRenderer>();
    }
    if (normalizedRendererId == QStringLiteral("welcome")) {
        return std::make_unique<WelcomeRenderer>();
    }
    if (normalizedRendererId == QStringLiteral("document")) {
        return std::make_unique<DocumentRenderer>();
    }
    if (normalizedRendererId == QStringLiteral("archive")) {
        return std::make_unique<ArchiveRenderer>();
    }
    if (normalizedRendererId == QStringLiteral("certificate")) {
        return std::make_unique<CertificateRenderer>();
    }
    if (normalizedRendererId == QStringLiteral("folder")) {
        return std::make_unique<FolderRenderer>();
    }
    if (normalizedRendererId == QStringLiteral("rendered_page")) {
        return std::make_unique<RenderedPageRenderer>();
    }
    if (normalizedRendererId == QStringLiteral("code")) {
        return std::make_unique<CodeRenderer>(m_previewState);
    }
    if (normalizedRendererId == QStringLiteral("text")) {
        return std::make_unique<TextRenderer>(m_previewState);
    }
    if (normalizedRendererId == QStringLiteral("image")) {
        return std::make_unique<ImageRenderer>();
    }
    if (normalizedRendererId == QStringLiteral("media")) {
        return std::make_unique<MediaRenderer>();
    }
    if (normalizedRendererId == QStringLiteral("summary")) {
        return std::make_unique<SummaryRenderer>(m_previewState);
    }

    return nullptr;
}

void RendererRegistry::warmUpHeavyRenderers() const
{
    const QStringList warmupOrder = {
        QStringLiteral("pdf"),
        QStringLiteral("media"),
        QStringLiteral("rendered_page"),
        QStringLiteral("document")
    };

    int delayMs = 0;
    for (const QString& rendererId : warmupOrder) {
        IPreviewRenderer* renderer = rendererById(rendererId);
        if (!renderer) {
            continue;
        }

        QTimer::singleShot(delayMs, [renderer, rendererId]() {
            qDebug().noquote() << QStringLiteral("[SpaceLookRender] Warmup renderer=%1").arg(rendererId);
            renderer->warmUp();
        });
        delayMs += 180;
    }
}
