#include "renderers/PreviewHost.h"

#include <QHBoxLayout>
#include <QDebug>
#include <QFileInfo>
#include <QLabel>
#include <QProgressBar>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "core/preview_state.h"
#include "renderers/PreviewStateVisuals.h"
#include "renderers/RendererRegistry.h"
#include "widgets/SpaceLookWindow.h"

PreviewHost::PreviewHost(PreviewState* previewState, QWidget* parent)
    : QWidget(parent)
    , m_stack(new QStackedWidget(this))
    , m_previewState(previewState)
    , m_registry(new RendererRegistry(previewState))
{
    setAttribute(Qt::WA_StyledBackground, true);
    setObjectName(QStringLiteral("SpaceLookPreviewHost"));
    m_stack->setObjectName(QStringLiteral("SpaceLookPreviewStack"));
    m_stack->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);

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
    createLoadingOverlay();

    const QList<IPreviewRenderer*> allRenderers = m_registry->renderers();
    for (IPreviewRenderer* renderer : allRenderers) {
        if (renderer && renderer->widget()) {
            renderer->setLoadingStateCallback([this, renderer](bool loading) {
                if (m_activeRenderer != renderer) {
                    return;
                }

                if (loading) {
                    const HoveredItemInfo info = m_previewState ? m_previewState->hoveredItem() : HoveredItemInfo();
                    showLoadingOverlay(info, renderer->rendererId());
                    return;
                }

                hideLoadingOverlay();
            });
            renderer->setSummaryFallbackCallback([this, renderer](const HoveredItemInfo& info, const QString& reason) {
                if (m_activeRenderer != renderer) {
                    return;
                }

                showSummaryFallback(info, reason);
            });
            renderer->widget()->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
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
    IPreviewRenderer* nextRenderer = nullptr;
    const QString suffix = QFileInfo(info.filePath).suffix().trimmed().toLower();
    const bool supportsRendererOverride = suffix == QStringLiteral("json")
        || suffix == QStringLiteral("xml")
        || suffix == QStringLiteral("yaml")
        || suffix == QStringLiteral("yml");
    if (m_previewState && !supportsRendererOverride) {
        m_previewState->clearRendererOverride();
    }
    if (m_previewState && supportsRendererOverride && !m_previewState->rendererOverride().trimmed().isEmpty()) {
        nextRenderer = m_registry->rendererById(m_previewState->rendererOverride());
    }
    if (!nextRenderer) {
        nextRenderer = m_registry->rendererFor(info);
    }
    if (!nextRenderer) {
        qDebug().noquote() << "[SpaceLookRender] No renderer matched typeKey:" << info.typeKey;
        return;
    }

    qDebug().noquote() << QStringLiteral("[SpaceLookRender] Renderer selected: %1 for typeKey=%2")
        .arg(nextRenderer->rendererId(), info.typeKey);

    const PreviewLoadGuard::Token loadToken = m_previewLoadGuard.begin(info.filePath);

    if (m_activeRenderer) {
        qDebug().noquote() << QStringLiteral("[SpaceLookRender] Unloading active renderer before reload: %1")
            .arg(m_activeRenderer->rendererId());
        m_activeRenderer->unload();
    }

    m_activeRenderer = nextRenderer;

    if (QWidget* rendererWidget = nextRenderer->widget()) {
        const QList<IPreviewRenderer*> allRenderers = m_registry->renderers();
        for (IPreviewRenderer* renderer : allRenderers) {
            if (renderer && renderer->widget()) {
                renderer->widget()->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
            }
        }
        rendererWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        m_stack->setCurrentWidget(rendererWidget);
        rendererWidget->setFocus(Qt::OtherFocusReason);
        m_stack->updateGeometry();
        updateGeometry();
        qDebug().noquote() << QStringLiteral("[SpaceLookRender] Switched stacked widget to renderer: %1")
            .arg(nextRenderer->rendererId());
    }

    showLoadingOverlay(info, nextRenderer->rendererId());

    if (nextRenderer->rendererId() == QStringLiteral("summary")) {
        nextRenderer->load(info);
        if (m_previewLoadGuard.isCurrent(loadToken, info.filePath) && m_activeRenderer == nextRenderer) {
            hideLoadingOverlay();
        }
        return;
    }

    QTimer::singleShot(0, this, [this, nextRenderer, info, loadToken]() {
        if (!m_previewLoadGuard.isCurrent(loadToken, info.filePath) || m_activeRenderer != nextRenderer) {
            return;
        }
        nextRenderer->load(info);
        if (!nextRenderer->reportsLoadingState() &&
            m_previewLoadGuard.isCurrent(loadToken, info.filePath) &&
            m_activeRenderer == nextRenderer) {
            hideLoadingOverlay();
        }
    });
}

void PreviewHost::stopPreview()
{
    m_previewLoadGuard.cancel();
    if (!m_activeRenderer) {
        return;
    }

    qDebug().noquote() << QStringLiteral("[SpaceLookRender] Stopping active renderer: %1")
        .arg(m_activeRenderer->rendererId());
    m_activeRenderer->unload();
    m_activeRenderer = nullptr;
    hideLoadingOverlay();
}

QWidget* PreviewHost::activeRendererWidget() const
{
    return m_activeRenderer ? m_activeRenderer->widget() : nullptr;
}

QString PreviewHost::activeRendererId() const
{
    return m_activeRenderer ? m_activeRenderer->rendererId() : QString();
}

void PreviewHost::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateLoadingOverlayGeometry();
}

void PreviewHost::createLoadingOverlay()
{
    m_loadingOverlay = new QWidget(this);
    m_loadingOverlay->setObjectName(QStringLiteral("UnifiedPreviewLoadingOverlay"));
    m_loadingOverlay->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_loadingOverlay->hide();

    auto* overlayLayout = new QVBoxLayout(m_loadingOverlay);
    overlayLayout->setContentsMargins(0, 0, 0, 0);
    overlayLayout->setSpacing(0);
    overlayLayout->addStretch(1);

    auto* card = new QWidget(m_loadingOverlay);
    card->setObjectName(QStringLiteral("UnifiedPreviewLoadingCard"));
    card->setFixedWidth(340);

    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(22, 20, 22, 20);
    cardLayout->setSpacing(12);

    m_loadingTitleLabel = new QLabel(card);
    m_loadingTitleLabel->setObjectName(QStringLiteral("UnifiedPreviewLoadingTitle"));
    m_loadingTitleLabel->setAlignment(Qt::AlignCenter);

    m_loadingMessageLabel = new QLabel(card);
    m_loadingMessageLabel->setObjectName(QStringLiteral("UnifiedPreviewLoadingMessage"));
    m_loadingMessageLabel->setAlignment(Qt::AlignCenter);
    m_loadingMessageLabel->setWordWrap(true);

    m_loadingProgress = new QProgressBar(card);
    m_loadingProgress->setObjectName(QStringLiteral("UnifiedPreviewLoadingProgress"));
    m_loadingProgress->setRange(0, 0);
    m_loadingProgress->setTextVisible(false);
    m_loadingProgress->setFixedHeight(4);

    cardLayout->addWidget(m_loadingTitleLabel);
    cardLayout->addWidget(m_loadingMessageLabel);
    cardLayout->addWidget(m_loadingProgress);

    overlayLayout->addWidget(card, 0, Qt::AlignHCenter);
    overlayLayout->addStretch(2);

    m_loadingOverlay->setStyleSheet(
        "#UnifiedPreviewLoadingOverlay {"
        "  background: rgba(244, 248, 252, 0.72);"
        "}"
    );
    PreviewStateVisuals::prepareStateCard(
        card,
        m_loadingTitleLabel,
        m_loadingMessageLabel,
        m_loadingProgress,
        PreviewStateVisuals::Kind::Loading);
    updateLoadingOverlayGeometry();
}

void PreviewHost::showLoadingOverlay(const HoveredItemInfo& info, const QString& rendererId)
{
    if (!m_loadingOverlay) {
        return;
    }

    const QString title = info.fileName.trimmed().isEmpty()
        ? QStringLiteral("Preparing preview")
        : QStringLiteral("Preparing %1").arg(info.fileName.trimmed());
    const QString rendererLabel = rendererId.trimmed().isEmpty()
        ? QStringLiteral("preview")
        : rendererId.trimmed();

    if (m_loadingTitleLabel) {
        m_loadingTitleLabel->setText(title);
    }
    if (m_loadingMessageLabel) {
        m_loadingMessageLabel->setText(QStringLiteral("Loading with %1...").arg(rendererLabel));
    }
    PreviewStateVisuals::prepareStateCard(
        m_loadingTitleLabel ? m_loadingTitleLabel->parentWidget() : nullptr,
        m_loadingTitleLabel,
        m_loadingMessageLabel,
        m_loadingProgress,
        PreviewStateVisuals::Kind::Loading);

    updateLoadingOverlayGeometry();
    m_loadingOverlay->show();
    m_loadingOverlay->raise();
}

void PreviewHost::hideLoadingOverlay()
{
    if (m_loadingOverlay) {
        m_loadingOverlay->hide();
    }
}

void PreviewHost::updateLoadingOverlayGeometry()
{
    if (!m_loadingOverlay || !m_stack) {
        return;
    }

    m_loadingOverlay->setGeometry(m_stack->geometry());
}

void PreviewHost::showSummaryFallback(const HoveredItemInfo& info, const QString& reason)
{
    IPreviewRenderer* summaryRenderer = m_registry ? m_registry->rendererById(QStringLiteral("summary")) : nullptr;
    if (!summaryRenderer) {
        qDebug().noquote() << QStringLiteral("[SpaceLookRender] Summary fallback unavailable for path=\"%1\" reason=\"%2\"")
            .arg(info.filePath, reason);
        hideLoadingOverlay();
        return;
    }

    qDebug().noquote() << QStringLiteral("[SpaceLookRender] Falling back to SummaryRenderer for path=\"%1\" reason=\"%2\"")
        .arg(info.filePath, reason);

    HoveredItemInfo fallbackInfo = info;
    const QString trimmedReason = reason.trimmed();
    fallbackInfo.statusMessage = trimmedReason.isEmpty()
        ? QStringLiteral("Failed to render preview with the selected renderer. Showing file summary instead.")
        : QStringLiteral("Failed to render preview with the selected renderer. Showing file summary instead. %1").arg(trimmedReason);

    const PreviewLoadGuard::Token loadToken = m_previewLoadGuard.begin(fallbackInfo.filePath);

    if (m_activeRenderer && m_activeRenderer != summaryRenderer) {
        m_activeRenderer->unload();
    }

    m_activeRenderer = summaryRenderer;

    if (QWidget* rendererWidget = summaryRenderer->widget()) {
        const QList<IPreviewRenderer*> allRenderers = m_registry->renderers();
        for (IPreviewRenderer* renderer : allRenderers) {
            if (renderer && renderer->widget()) {
                renderer->widget()->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
            }
        }
        rendererWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        m_stack->setCurrentWidget(rendererWidget);
        rendererWidget->setFocus(Qt::OtherFocusReason);
        m_stack->updateGeometry();
        updateGeometry();
    }
    if (SpaceLookWindow* previewWindow = qobject_cast<SpaceLookWindow*>(window())) {
        previewWindow->applySummaryPreviewSize(fallbackInfo);
    }

    showLoadingOverlay(fallbackInfo, summaryRenderer->rendererId());
    summaryRenderer->load(fallbackInfo);
    if (m_previewLoadGuard.isCurrent(loadToken, fallbackInfo.filePath) && m_activeRenderer == summaryRenderer) {
        hideLoadingOverlay();
    }
}
