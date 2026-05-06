#include "renderers/PreviewHost.h"

#include <QHBoxLayout>
#include <QDebug>
#include <QFileInfo>
#include <QLabel>
#include <QProgressBar>
#include <QResizeEvent>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

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
        m_stack->setCurrentWidget(rendererWidget);
        rendererWidget->setFocus(Qt::OtherFocusReason);
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
        "#UnifiedPreviewLoadingCard {"
        "  background: rgba(255, 255, 255, 0.96);"
        "  border: 1px solid rgba(206, 218, 232, 0.96);"
        "  border-radius: 18px;"
        "}"
        "#UnifiedPreviewLoadingTitle {"
        "  color: #0f2740;"
        "  font-family: 'Microsoft YaHei UI';"
        "  font-size: 18px;"
        "  font-weight: 700;"
        "}"
        "#UnifiedPreviewLoadingMessage {"
        "  color: #526b85;"
        "  font-family: 'Segoe UI';"
        "  font-size: 13px;"
        "}"
        "#UnifiedPreviewLoadingProgress {"
        "  background: #e6edf5;"
        "  border: none;"
        "  border-radius: 2px;"
        "}"
        "#UnifiedPreviewLoadingProgress::chunk {"
        "  background: #0078d4;"
        "  border-radius: 2px;"
        "}"
    );
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
