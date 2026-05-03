#include "renderers/PreviewHost.h"

#include <QHBoxLayout>
#include <QDebug>
#include <QStackedWidget>

#include "core/preview_state.h"
#include "renderers/RendererRegistry.h"

PreviewHost::PreviewHost(PreviewState* previewState, QWidget* parent)
    : QWidget(parent)
    , m_stack(new QStackedWidget(this))
    , m_previewState(previewState)
    , m_registry(new RendererRegistry(previewState))
{
    setAttribute(Qt::WA_StyledBackground, true);
    setObjectName(QStringLiteral("SpaceLookPreviewHost"));
    m_stack->setObjectName(QStringLiteral("SpaceLookPreviewStack"));

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 0, 12, 12);
    layout->addWidget(m_stack);

    setStyleSheet(
        "#SpaceLookPreviewHost {"
        "  background: transparent;"
        "}"
        "#SpaceLookPreviewStack {"
        "  background: transparent;"
        "}"
    );

    const QList<IPreviewRenderer*> allRenderers = m_registry->renderers();
    for (IPreviewRenderer* renderer : allRenderers) {
        if (renderer && renderer->widget()) {
            m_stack->addWidget(renderer->widget());
        }
    }
}

PreviewHost::~PreviewHost()
{
    delete m_registry;
}

void PreviewHost::showPreview(const HoveredItemInfo& info)
{
    IPreviewRenderer* nextRenderer = m_registry->rendererFor(info);
    if (!nextRenderer) {
        qDebug().noquote() << "[SpaceLookRender] No renderer matched typeKey:" << info.typeKey;
        return;
    }

    qDebug().noquote() << QStringLiteral("[SpaceLookRender] Renderer selected: %1 for typeKey=%2")
        .arg(nextRenderer->rendererId(), info.typeKey);

    if (m_activeRenderer) {
        qDebug().noquote() << QStringLiteral("[SpaceLookRender] Unloading active renderer before reload: %1")
            .arg(m_activeRenderer->rendererId());
        m_activeRenderer->unload();
    }

    nextRenderer->load(info);
    m_activeRenderer = nextRenderer;

    if (QWidget* rendererWidget = nextRenderer->widget()) {
        m_stack->setCurrentWidget(rendererWidget);
        rendererWidget->setFocus(Qt::OtherFocusReason);
        qDebug().noquote() << QStringLiteral("[SpaceLookRender] Switched stacked widget to renderer: %1")
            .arg(nextRenderer->rendererId());
    }
}

void PreviewHost::stopPreview()
{
    if (!m_activeRenderer) {
        return;
    }

    qDebug().noquote() << QStringLiteral("[SpaceLookRender] Stopping active renderer: %1")
        .arg(m_activeRenderer->rendererId());
    m_activeRenderer->unload();
    m_activeRenderer = nullptr;
}

QWidget* PreviewHost::activeRendererWidget() const
{
    return m_activeRenderer ? m_activeRenderer->widget() : nullptr;
}
