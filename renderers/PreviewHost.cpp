#include "renderers/PreviewHost.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>

#include "core/preview_state.h"
#include "renderers/PreviewStateVisuals.h"
#include "renderers/RendererRegistry.h"
#include "renderers/folder/FolderRenderer.h"
#include "widgets/SpaceLookWindow.h"

namespace {

constexpr int kMaxPreviewStackDepth = 5;

}

PreviewHost::PreviewHost(PreviewState* previewState, QWidget* parent)
    : QWidget(parent)
    , m_routeBar(new QWidget(this))
    , m_routeLabel(new QLabel(m_routeBar))
    , m_stack(new QStackedWidget(this))
    , m_previewState(previewState)
    , m_registry(new RendererRegistry(previewState, this))
{
    setAttribute(Qt::WA_StyledBackground, true);
    setObjectName(QStringLiteral("SpaceLookPreviewHost"));
    m_routeBar->setObjectName(QStringLiteral("SpaceLookPreviewRouteBar"));
    m_routeLabel->setObjectName(QStringLiteral("SpaceLookPreviewRouteLabel"));
    m_stack->setObjectName(QStringLiteral("SpaceLookPreviewStack"));
    m_stack->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 0, 12, 12);
    layout->setSpacing(8);
    layout->addWidget(m_routeBar, 0);
    layout->addWidget(m_stack, 1);

    auto* routeLayout = new QHBoxLayout(m_routeBar);
    routeLayout->setContentsMargins(12, 6, 12, 6);
    routeLayout->setSpacing(8);
    routeLayout->addWidget(m_routeLabel, 1);
    m_routeLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_routeLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_routeLabel->setWordWrap(false);
    m_routeBar->hide();

    setStyleSheet(
        "#SpaceLookPreviewHost {"
        "  background: transparent;"
        "}"
        "#SpaceLookPreviewRouteBar {"
        "  background: rgba(238, 244, 252, 0.92);"
        "  border: 1px solid rgba(196, 210, 226, 0.9);"
        "  border-radius: 12px;"
        "}"
        "#SpaceLookPreviewRouteLabel {"
        "  color: #38526c;"
        "  font-family: 'Segoe UI Variable Text', 'Segoe UI';"
        "  font-size: 12px;"
        "}"
        "#SpaceLookPreviewStack {"
        "  background: transparent;"
        "}"
    );
    createLoadingOverlay();

    QTimer::singleShot(700, this, [this]() {
        if (m_registry) {
            m_registry->warmUpHeavyRenderers();
        }
    });
}

PreviewHost::~PreviewHost()
{
    clearPreviewStack();
}

void PreviewHost::showPreview(const HoveredItemInfo& info)
{
    clearPreviewStack();
    pushPreview(info);
}

bool PreviewHost::pushPreviewForPath(const QString& filePath)
{
    const QString trimmedPath = filePath.trimmed();
    if (trimmedPath.isEmpty()) {
        return false;
    }

    const HoveredItemInfo info = m_fileTypeDetector.inspectPath(
        trimmedPath,
        QStringLiteral("Folder Preview"),
        QStringLiteral("SpaceLook"));
    if (!info.valid || info.filePath.trimmed().isEmpty()) {
        return false;
    }

    pushPreview(info);
    return true;
}

bool PreviewHost::popPreview()
{
    if (m_previewStack.size() <= 1) {
        return false;
    }

    removeStackEntry(static_cast<int>(m_previewStack.size()) - 1);
    showStackEntry(static_cast<int>(m_previewStack.size()) - 1);
    hideLoadingOverlay();
    return true;
}

int PreviewHost::previewStackDepth() const
{
    return static_cast<int>(m_previewStack.size());
}

void PreviewHost::stopPreview()
{
    m_previewLoadGuard.cancel();
    clearPreviewStack();
    hideLoadingOverlay();
}

bool PreviewHost::previewHoveredFolderItem()
{
    auto* folderRenderer = dynamic_cast<FolderRenderer*>(activeRenderer());
    return folderRenderer && folderRenderer->previewHoveredOrCurrentItem();
}

QWidget* PreviewHost::activeRendererWidget() const
{
    IPreviewRenderer* renderer = activeRenderer();
    return renderer ? renderer->widget() : nullptr;
}

QString PreviewHost::activeRendererId() const
{
    IPreviewRenderer* renderer = activeRenderer();
    return renderer ? renderer->rendererId() : QString();
}

void PreviewHost::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateLoadingOverlayGeometry();
}

void PreviewHost::pushPreview(const HoveredItemInfo& info)
{
    if (!m_registry) {
        return;
    }

    if (m_previewStack.size() >= kMaxPreviewStackDepth) {
        removeStackEntry(1);
    }

    std::unique_ptr<IPreviewRenderer> renderer = m_registry->createRendererFor(info);
    if (!renderer) {
        qDebug().noquote() << "[SpaceLookRender] No renderer matched typeKey:" << info.typeKey;
        return;
    }

    auto entry = std::make_unique<PreviewStackEntry>();
    entry->info = info;
    entry->renderer = std::move(renderer);
    configureRenderer(entry->renderer.get());

    QWidget* rendererWidget = entry->renderer->widget();
    if (!rendererWidget) {
        return;
    }

    rendererWidget->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    m_stack->addWidget(rendererWidget);
    m_previewStack.push_back(std::move(entry));
    showStackEntry(static_cast<int>(m_previewStack.size()) - 1);

    IPreviewRenderer* currentRenderer = activeRenderer();
    if (!currentRenderer) {
        return;
    }

    qDebug().noquote() << QStringLiteral("[SpaceLookRender] Preview stack push depth=%1 renderer=%2 typeKey=%3 path=\"%4\"")
        .arg(m_previewStack.size())
        .arg(currentRenderer->rendererId(), info.typeKey, info.filePath);

    const PreviewLoadGuard::Token loadToken = m_previewStack.at(m_activeIndex)->loadGuard.begin(info.filePath);
    showLoadingOverlay(info, currentRenderer->rendererId());

    if (currentRenderer->rendererId() == QStringLiteral("summary")) {
        currentRenderer->load(info);
        if (m_activeIndex >= 0 &&
            m_activeIndex < static_cast<int>(m_previewStack.size()) &&
            m_previewStack.at(m_activeIndex)->loadGuard.isCurrent(loadToken, info.filePath) &&
            activeRenderer() == currentRenderer) {
            hideLoadingOverlay();
        }
        return;
    }

    QTimer::singleShot(0, this, [this, currentRenderer, info, loadToken]() {
        if (m_activeIndex < 0 ||
            m_activeIndex >= static_cast<int>(m_previewStack.size()) ||
            !m_previewStack.at(m_activeIndex)->loadGuard.isCurrent(loadToken, info.filePath) ||
            activeRenderer() != currentRenderer) {
            return;
        }

        currentRenderer->load(info);
        if (m_activeIndex < 0 ||
            m_activeIndex >= static_cast<int>(m_previewStack.size()) ||
            !m_previewStack.at(m_activeIndex)->loadGuard.isCurrent(loadToken, info.filePath) ||
            activeRenderer() != currentRenderer) {
            return;
        }

        if (!currentRenderer->reportsLoadingState()) {
            hideLoadingOverlay();
        }
    });
}

void PreviewHost::showStackEntry(int index)
{
    if (index < 0 || index >= static_cast<int>(m_previewStack.size())) {
        return;
    }

    m_activeIndex = index;
    for (int entryIndex = 0; entryIndex < static_cast<int>(m_previewStack.size()); ++entryIndex) {
        QWidget* widget = m_previewStack.at(entryIndex)->renderer
            ? m_previewStack.at(entryIndex)->renderer->widget()
            : nullptr;
        if (widget) {
            widget->setSizePolicy(entryIndex == index ? QSizePolicy::Expanding : QSizePolicy::Ignored,
                                  entryIndex == index ? QSizePolicy::Expanding : QSizePolicy::Ignored);
        }
    }

    if (QWidget* widget = activeRendererWidget()) {
        m_stack->setCurrentWidget(widget);
        widget->setFocus(Qt::OtherFocusReason);
    }
    m_stack->updateGeometry();
    updateGeometry();
    updateRouteBar();
}

void PreviewHost::clearPreviewStack()
{
    while (!m_previewStack.empty()) {
        removeStackEntry(static_cast<int>(m_previewStack.size()) - 1);
    }
    m_activeIndex = -1;
    updateRouteBar();
}

void PreviewHost::removeStackEntry(int index)
{
    if (index < 0 || index >= static_cast<int>(m_previewStack.size())) {
        return;
    }

    std::unique_ptr<PreviewStackEntry> entry = std::move(m_previewStack.at(index));
    m_previewStack.erase(m_previewStack.begin() + index);

    if (entry && entry->renderer) {
        qDebug().noquote() << QStringLiteral("[SpaceLookRender] Preview stack remove renderer=%1 path=\"%2\"")
            .arg(entry->renderer->rendererId(), entry->info.filePath);
        entry->loadGuard.cancel();
        entry->renderer->unload();
        if (QWidget* widget = entry->renderer->widget()) {
            m_stack->removeWidget(widget);
        }
    }

    if (m_activeIndex >= static_cast<int>(m_previewStack.size())) {
        m_activeIndex = static_cast<int>(m_previewStack.size()) - 1;
    }
    updateRouteBar();
}

void PreviewHost::configureRenderer(IPreviewRenderer* renderer)
{
    if (!renderer) {
        return;
    }

    renderer->setLoadingStateCallback([this, renderer](bool loading) {
        if (activeRenderer() != renderer) {
            return;
        }

        if (loading) {
            const HoveredItemInfo info = m_activeIndex >= 0 &&
                    m_activeIndex < static_cast<int>(m_previewStack.size())
                ? m_previewStack.at(m_activeIndex)->info
                : HoveredItemInfo();
            showLoadingOverlay(info, renderer->rendererId());
            return;
        }

        hideLoadingOverlay();
    });

    renderer->setSummaryFallbackCallback([this, renderer](const HoveredItemInfo& info, const QString& reason) {
        if (activeRenderer() != renderer) {
            return;
        }

        QTimer::singleShot(0, this, [this, renderer, info, reason]() {
            if (activeRenderer() != renderer) {
                return;
            }

            showSummaryFallback(info, reason);
        });
    });

    if (auto* folderRenderer = dynamic_cast<FolderRenderer*>(renderer)) {
        connect(folderRenderer, &FolderRenderer::previewRequested, this, [this](const QString& path) {
            pushPreviewForPath(path);
        });
    }
}

void PreviewHost::updateRouteBar()
{
    if (!m_routeBar || !m_routeLabel) {
        return;
    }

    if (m_previewStack.size() <= 1) {
        m_routeBar->hide();
        m_routeLabel->clear();
        return;
    }

    QStringList routeParts;
    routeParts.reserve(static_cast<int>(m_previewStack.size()));
    for (const std::unique_ptr<PreviewStackEntry>& entry : m_previewStack) {
        if (entry) {
            routeParts.append(routeTitleForInfo(entry->info));
        }
    }

    const QString routeText = routeParts.join(QStringLiteral("  >  "));
    m_routeLabel->setText(routeText);
    m_routeLabel->setToolTip(routeText);
    m_routeBar->show();
}

QString PreviewHost::routeTitleForInfo(const HoveredItemInfo& info) const
{
    if (!info.title.trimmed().isEmpty()) {
        return info.title.trimmed();
    }
    if (!info.fileName.trimmed().isEmpty()) {
        return info.fileName.trimmed();
    }
    if (!info.filePath.trimmed().isEmpty()) {
        return QFileInfo(info.filePath).fileName();
    }
    return QCoreApplication::translate("SpaceLook", "Preview");
}

IPreviewRenderer* PreviewHost::activeRenderer() const
{
    if (m_activeIndex < 0 || m_activeIndex >= static_cast<int>(m_previewStack.size())) {
        return nullptr;
    }

    return m_previewStack.at(m_activeIndex)->renderer.get();
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
        ? QCoreApplication::translate("SpaceLook", "Preparing preview")
        : QCoreApplication::translate("SpaceLook", "Preparing %1").arg(info.fileName.trimmed());
    const QString rendererLabel = rendererId.trimmed().isEmpty()
        ? QCoreApplication::translate("SpaceLook", "preview")
        : rendererId.trimmed();

    if (m_loadingTitleLabel) {
        m_loadingTitleLabel->setText(title);
    }
    if (m_loadingMessageLabel) {
        m_loadingMessageLabel->setText(QCoreApplication::translate("SpaceLook", "Loading with %1...").arg(rendererLabel));
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
    if (!m_registry || m_activeIndex < 0 || m_activeIndex >= static_cast<int>(m_previewStack.size())) {
        hideLoadingOverlay();
        return;
    }

    std::unique_ptr<IPreviewRenderer> summaryRenderer = m_registry->createRendererById(QStringLiteral("summary"));
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
    if (trimmedReason.startsWith(QStringLiteral("Current SpaceLook process is "))) {
        fallbackInfo.statusMessage = trimmedReason;
    } else {
        fallbackInfo.statusMessage = trimmedReason.isEmpty()
            ? QCoreApplication::translate("SpaceLook", "Failed to render preview with the selected renderer. Showing file summary instead.")
            : QCoreApplication::translate("SpaceLook", "Failed to render preview with the selected renderer. Showing file summary instead. %1").arg(trimmedReason);
    }

    PreviewStackEntry* activeEntry = m_previewStack.at(m_activeIndex).get();
    if (activeEntry->renderer) {
        activeEntry->loadGuard.cancel();
        activeEntry->renderer->unload();
        if (QWidget* oldWidget = activeEntry->renderer->widget()) {
            m_stack->removeWidget(oldWidget);
        }
    }

    activeEntry->info = fallbackInfo;
    activeEntry->renderer = std::move(summaryRenderer);
    configureRenderer(activeEntry->renderer.get());

    if (QWidget* rendererWidget = activeEntry->renderer->widget()) {
        m_stack->addWidget(rendererWidget);
        showStackEntry(m_activeIndex);
    }

    if (SpaceLookWindow* previewWindow = qobject_cast<SpaceLookWindow*>(window())) {
        previewWindow->applySummaryPreviewSize(fallbackInfo);
    }

    const PreviewLoadGuard::Token loadToken = activeEntry->loadGuard.begin(fallbackInfo.filePath);
    IPreviewRenderer* renderer = activeEntry->renderer.get();
    showLoadingOverlay(fallbackInfo, renderer->rendererId());
    renderer->load(fallbackInfo);
    if (activeEntry->loadGuard.isCurrent(loadToken, fallbackInfo.filePath) && activeRenderer() == renderer) {
        hideLoadingOverlay();
    }
}
