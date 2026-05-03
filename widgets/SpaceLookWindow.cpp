#include "widgets/SpaceLookWindow.h"

#include "core/preview_state.h"
#include "settings/spacelook_ui_settings.h"
#include "renderers/OpenWithButton.h"
#include "renderers/PreviewHost.h"

#include <QClipboard>
#include <QCursor>
#include <QDesktopServices>
#include <QDir>
#include <QDebug>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QGuiApplication>
#include <QFileInfo>
#include <QIcon>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPoint>
#include <QProcess>
#include <QQmlContext>
#include <QQmlEngine>
#include <QMenu>
#include <QQuickView>
#include <QQuickItem>
#include <QScreen>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QToolTip>
#include <QUrl>
#include <QVariantMap>
#include <QVBoxLayout>
#include <QWindow>

#include <Windows.h>
#include <shellapi.h>
#include <uiautomation.h>

namespace {

HHOOK g_spaceKeyboardHook = nullptr;
SpaceLookWindow* g_spaceHookWindow = nullptr;
bool g_spaceKeyPressed = false;
constexpr int kTooltipDurationMs = 2000;

QIcon SPACELOOKAppIcon()
{
    return QIcon(QStringLiteral(":/icons/app.png"));
}

QQuickItem* deepestChildAt(QQuickItem* rootItem, const QPointF& rootLocalPos)
{
    if (!rootItem) {
        return nullptr;
    }

    QQuickItem* currentItem = rootItem->childAt(rootLocalPos.x(), rootLocalPos.y());
    if (!currentItem) {
        return nullptr;
    }

    const QPointF scenePos = rootItem->mapToScene(rootLocalPos);
    while (currentItem) {
        const QPointF itemLocalPos = currentItem->mapFromScene(scenePos);
        QQuickItem* nextItem = currentItem->childAt(itemLocalPos.x(), itemLocalPos.y());
        if (!nextItem || nextItem == currentItem) {
            break;
        }
        currentItem = nextItem;
    }

    return currentItem;
}

class SettingsQuickView : public QQuickView
{
public:
    explicit SettingsQuickView(QWindow* parent = nullptr)
        : QQuickView(parent)
    {
    }

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        if (event && event->button() == Qt::LeftButton && shouldStartWindowDrag(event->pos())) {
            m_dragging = true;
            m_dragOffset = event->globalPos() - position();
            event->accept();
            return;
        }

        QQuickView::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (m_dragging && event) {
            setPosition(event->globalPos() - m_dragOffset);
            event->accept();
            return;
        }

        QQuickView::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        m_dragging = false;
        QQuickView::mouseReleaseEvent(event);
    }

private:
    bool shouldStartWindowDrag(const QPoint& localPos) const
    {
        if (localPos.y() > 120) {
            return false;
        }

        QQuickItem* rootItem = qobject_cast<QQuickItem*>(rootObject());
        if (!rootItem) {
            return true;
        }

        QQuickItem* item = deepestChildAt(rootItem, QPointF(localPos));
        while (item) {
            if (item->property("settingsNoDrag").toBool()) {
                return false;
            }
            item = item->parentItem();
        }

        return true;
    }

    bool m_dragging = false;
    QPoint m_dragOffset;
};

SettingsQuickView* g_settingsWindow = nullptr;

QString bstrToQString(BSTR value)
{
    return value ? QString::fromWCharArray(value).trimmed() : QString();
}

QUrl viewerSourceUrl(const QString& fileName)
{
    const QFileInfo sourceFile(QString::fromUtf8(__FILE__));
    const QString localPath = QDir::cleanPath(sourceFile.dir().absoluteFilePath(
        QStringLiteral("../viewers/%1").arg(fileName)));
    if (QFileInfo::exists(localPath)) {
        return QUrl::fromLocalFile(localPath);
    }

    return QUrl(QStringLiteral("qrc:/SPACELOOK/viewers/%1").arg(fileName));
}

Qt::WindowFlags taskbarAwareFlags(Qt::WindowFlags baseFlags, bool showTaskbar)
{
    Qt::WindowFlags flags = baseFlags & ~Qt::WindowType_Mask;
    flags |= showTaskbar ? Qt::Window : Qt::Tool;
    return flags;
}

bool shouldIgnoreGlobalSpaceHotkey()
{
    const HWND focusedWindow = GetForegroundWindow();
    if (!focusedWindow) {
        return false;
    }

    const HRESULT initHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool shouldUninitialize = SUCCEEDED(initHr);
    if (FAILED(initHr) && initHr != RPC_E_CHANGED_MODE) {
        return false;
    }

    IUIAutomation* automation = nullptr;
    const HRESULT automationHr = CoCreateInstance(
        CLSID_CUIAutomation,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&automation));
    if (FAILED(automationHr) || !automation) {
        if (shouldUninitialize) {
            CoUninitialize();
        }
        return false;
    }

    IUIAutomationElement* focusedElement = nullptr;
    const HRESULT focusHr = automation->GetFocusedElement(&focusedElement);
    if (FAILED(focusHr) || !focusedElement) {
        automation->Release();
        if (shouldUninitialize) {
            CoUninitialize();
        }
        return false;
    }

    CONTROLTYPEID controlType = 0;
    focusedElement->get_CurrentControlType(&controlType);

    BOOL isEnabled = FALSE;
    focusedElement->get_CurrentIsEnabled(&isEnabled);

    BOOL isReadOnly = TRUE;
    IUIAutomationValuePattern* valuePattern = nullptr;
    if (SUCCEEDED(focusedElement->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&valuePattern))) && valuePattern) {
        valuePattern->get_CurrentIsReadOnly(&isReadOnly);
        valuePattern->Release();
    }

    BOOL supportsTextPattern = FALSE;
    IUIAutomationTextPattern* textPattern = nullptr;
    if (SUCCEEDED(focusedElement->GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(&textPattern))) && textPattern) {
        supportsTextPattern = TRUE;
        textPattern->Release();
    }

    BSTR localizedTypeValue = nullptr;
    QString localizedType;
    if (SUCCEEDED(focusedElement->get_CurrentLocalizedControlType(&localizedTypeValue))) {
        localizedType = bstrToQString(localizedTypeValue).toLower();
        SysFreeString(localizedTypeValue);
    }

    BSTR classNameValue = nullptr;
    QString className;
    if (SUCCEEDED(focusedElement->get_CurrentClassName(&classNameValue))) {
        className = bstrToQString(classNameValue).toLower();
        SysFreeString(classNameValue);
    }

    focusedElement->Release();
    automation->Release();
    if (shouldUninitialize) {
        CoUninitialize();
    }

    if (!isEnabled) {
        return false;
    }

    const bool controlLooksEditable =
        controlType == UIA_EditControlTypeId ||
        controlType == UIA_DocumentControlTypeId ||
        localizedType.contains(QStringLiteral("edit")) ||
        localizedType.contains(QStringLiteral("document")) ||
        className.contains(QStringLiteral("edit")) ||
        className.contains(QStringLiteral("textbox")) ||
        className.contains(QStringLiteral("search"));

    if (!controlLooksEditable) {
        return false;
    }

    if (supportsTextPattern) {
        return true;
    }

    return !isReadOnly;
}

LRESULT CALLBACK SPACELOOKKeyboardProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code < 0) {
        return CallNextHookEx(g_spaceKeyboardHook, code, wParam, lParam);
    }

    const KBDLLHOOKSTRUCT* keyboardInfo = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
    if (!keyboardInfo) {
        return CallNextHookEx(g_spaceKeyboardHook, code, wParam, lParam);
    }

    if (keyboardInfo->vkCode != VK_SPACE || !g_spaceHookWindow) {
        return CallNextHookEx(g_spaceKeyboardHook, code, wParam, lParam);
    }

    const bool isKeyDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
    const bool isKeyUp = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);

    if (shouldIgnoreGlobalSpaceHotkey()) {
        if (isKeyUp) {
            g_spaceKeyPressed = false;
        }
        return CallNextHookEx(g_spaceKeyboardHook, code, wParam, lParam);
    }

    if (isKeyDown) {
        if (g_spaceKeyPressed) {
            return 1;
        }

        g_spaceKeyPressed = true;
        QMetaObject::invokeMethod(g_spaceHookWindow, [window = g_spaceHookWindow]() {
            if (window) {
                window->handleGlobalSpacePressed();
            }
        }, Qt::QueuedConnection);
        return 1;
    }

    if (isKeyUp) {
        g_spaceKeyPressed = false;
        return 1;
    }

    return CallNextHookEx(g_spaceKeyboardHook, code, wParam, lParam);
}

}

SpaceLookWindow::SpaceLookWindow(PreviewState* previewState, QWidget* parent)
    : QWidget(parent)
    , m_previewState(previewState)
    , m_surface(new QWidget(this))
    , m_previewHost(new PreviewHost(previewState, this))
{
    setWindowTitle(QStringLiteral("Space Look"));
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    resize(980, 680);
    setMinimumSize(820, 560);
    setMouseTracking(true);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->addWidget(m_surface);

    m_surface->setObjectName(QStringLiteral("SpaceLookSurface"));

    auto* surfaceLayout = new QVBoxLayout(m_surface);
    surfaceLayout->setContentsMargins(1, 1, 1, 1);
    surfaceLayout->setSpacing(0);
    surfaceLayout->addWidget(m_previewHost, 1);

    auto* shadowEffect = new QGraphicsDropShadowEffect(this);
    shadowEffect->setBlurRadius(36.0);
    shadowEffect->setOffset(0.0, 18.0);
    shadowEffect->setColor(QColor(18, 32, 52, 70));
    m_surface->setGraphicsEffect(shadowEffect);

    applyWindowChromeStyle();
    applyTaskbarVisibility();
    applyPerformanceMode();
    ensureTrayIcon();
    updateTrayVisibility();
    connect(&SpaceLookUiSettings::instance(), &SpaceLookUiSettings::showTaskbarChanged, this, [this]() {
        applyTaskbarVisibility();
    });
    connect(&SpaceLookUiSettings::instance(), &SpaceLookUiSettings::performanceModeChanged, this, [this]() {
        applyPerformanceMode();
    });
    connect(&SpaceLookUiSettings::instance(), &SpaceLookUiSettings::showSystemTrayChanged, this, [this]() {
        updateTrayVisibility();
    });

    installSpaceHook();
}

SpaceLookWindow::~SpaceLookWindow()
{
    uninstallSpaceHook();
}

void SpaceLookWindow::showPreview(const HoveredItemInfo& info)
{
    if (m_previewHost) {
        m_previewHost->showPreview(info);
    }
    if (m_expandedPreview && supportsExpandedPreview(info)) {
        applyExpandedPreviewSize(info);
    } else {
        m_expandedPreview = false;
        applyPreferredSizeForPreview(info);
    }
    show();
    raise();
    activateWindow();
}

void SpaceLookWindow::hidePreview()
{
    if (m_previewHost) {
        m_previewHost->stopPreview();
    }
    hide();
}

bool SpaceLookWindow::isPreviewVisible() const
{
    return isVisible();
}

bool SpaceLookWindow::containsGlobalPoint(const QPoint& globalPos) const
{
    return isVisible() && frameGeometry().contains(globalPos);
}

void SpaceLookWindow::handleGlobalSpacePressed()
{
    emit spaceHotkeyPressed();
}

void SpaceLookWindow::toggleAlwaysOnTop()
{
    m_alwaysOnTop = !m_alwaysOnTop;
    HWND hwnd = reinterpret_cast<HWND>(winId());
    if (hwnd) {
        const UINT flags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED;
        SetWindowPos(hwnd,
                     m_alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST,
                     0,
                     0,
                     0,
                     0,
                     flags);
    }
    raise();
    activateWindow();
    QToolTip::showText(QCursor::pos(),
                       m_alwaysOnTop ? QStringLiteral("Always on top enabled")
                                     : QStringLiteral("Always on top disabled"),
                       this,
                       QRect(),
                       kTooltipDurationMs);
}

bool SpaceLookWindow::isAlwaysOnTop() const
{
    return m_alwaysOnTop;
}

void SpaceLookWindow::openCurrentInExplorer()
{
    const QString previewPath = currentPreviewPath();
    if (previewPath.trimmed().isEmpty()) {
        return;
    }

    if (!QProcess::startDetached(QStringLiteral("explorer.exe"),
                                 { QStringLiteral("/select,"), QDir::toNativeSeparators(previewPath) })) {
        QToolTip::showText(QCursor::pos(), QStringLiteral("Failed to open file location"), this, QRect(), kTooltipDurationMs);
        return;
    }

    QToolTip::showText(QCursor::pos(), QStringLiteral("Opened file location"), this, QRect(), kTooltipDurationMs);
}

void SpaceLookWindow::copyCurrentPath()
{
    const QString previewPath = currentPreviewPath();
    if (previewPath.trimmed().isEmpty()) {
        return;
    }

    if (QClipboard* clipboard = QGuiApplication::clipboard()) {
        clipboard->setText(previewPath);
        QToolTip::showText(QCursor::pos(), QStringLiteral("Path copied"), this, QRect(), kTooltipDurationMs);
    }
}

void SpaceLookWindow::refreshCurrentPreview()
{
    if (!m_previewState || !m_previewHost) {
        return;
    }

    const HoveredItemInfo info = m_previewState->hoveredItem();
    if (!info.valid) {
        return;
    }

    m_previewHost->showPreview(info);
    QToolTip::showText(QCursor::pos(), QStringLiteral("Preview refreshed"), this, QRect(), kTooltipDurationMs);
}

void SpaceLookWindow::openCurrentWithDefaultApp()
{
    const QString previewPath = currentPreviewPath();
    if (previewPath.trimmed().isEmpty()) {
        return;
    }

    const QString nativePath = QDir::toNativeSeparators(previewPath);
    const HINSTANCE result = ShellExecuteW(nullptr,
                                           L"open",
                                           reinterpret_cast<LPCWSTR>(nativePath.utf16()),
                                           nullptr,
                                           nullptr,
                                           SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        QToolTip::showText(QCursor::pos(), QStringLiteral("Failed to open in default app"), this, QRect(), kTooltipDurationMs);
        return;
    }

    QToolTip::showText(QCursor::pos(), QStringLiteral("Opened item"), this, QRect(), kTooltipDurationMs);
    hidePreview();
}

void SpaceLookWindow::toggleExpandedPreview()
{
    if (!m_previewState) {
        return;
    }

    const HoveredItemInfo info = m_previewState->hoveredItem();
    if (!supportsExpandedPreview(info)) {
        return;
    }

    m_expandedPreview = !m_expandedPreview;
    if (m_expandedPreview) {
        applyExpandedPreviewSize(info);
    } else {
        applyPreferredSizeForPreview(info);
    }
}

bool SpaceLookWindow::isExpandedPreview() const
{
    return m_expandedPreview;
}

bool SpaceLookWindow::supportsExpandedPreview() const
{
    if (!m_previewState) {
        return false;
    }

    return supportsExpandedPreview(m_previewState->hoveredItem());
}

void SpaceLookWindow::showOpenWithMenuAt(const QPoint& globalPos)
{
    if (!m_previewHost) {
        return;
    }

    QWidget* rendererWidget = m_previewHost->activeRendererWidget();
    if (!rendererWidget) {
        return;
    }

    const QList<OpenWithButton*> buttons = rendererWidget->findChildren<OpenWithButton*>();
    for (OpenWithButton* button : buttons) {
        if (button && button->hasAvailableHandlers()) {
            button->showMenuAtGlobalPos(globalPos);
            return;
        }
    }
}

void SpaceLookWindow::showSettingsWindow()
{
    if (g_settingsWindow) {
        g_settingsWindow->show();
        g_settingsWindow->raise();
        g_settingsWindow->requestActivate();
        return;
    }

    g_settingsWindow = new SettingsQuickView();
    connect(g_settingsWindow, &QObject::destroyed, this, []() {
        g_settingsWindow = nullptr;
    });
    g_settingsWindow->rootContext()->setContextProperty(QStringLiteral("uiSettings"), &SpaceLookUiSettings::instance());
    g_settingsWindow->rootContext()->setContextProperty(QStringLiteral("settingsWindow"), this);
    g_settingsWindow->setColor(Qt::transparent);
    g_settingsWindow->setIcon(SPACELOOKAppIcon());
    g_settingsWindow->setFlags(taskbarAwareFlags(Qt::FramelessWindowHint,
                                                 SpaceLookUiSettings::instance().showTaskbar()));
    g_settingsWindow->setResizeMode(QQuickView::SizeRootObjectToView);
    g_settingsWindow->engine()->clearComponentCache();
    const QUrl settingsSource = viewerSourceUrl(QStringLiteral("SettingsPage.qml"));
    qDebug().noquote() << QStringLiteral("[SpaceLookSettings] loading source=%1").arg(settingsSource.toString());
    connect(g_settingsWindow, &QQuickView::statusChanged, g_settingsWindow, [settingsSource](QQuickView::Status status) {
        qDebug().noquote() << QStringLiteral("[SpaceLookSettings] status=%1 source=%2")
            .arg(static_cast<int>(status))
            .arg(settingsSource.toString());
    });
    g_settingsWindow->setSource(settingsSource);
    g_settingsWindow->resize(820, 620);

    g_settingsWindow->show();
    g_settingsWindow->raise();
    g_settingsWindow->requestActivate();
}

void SpaceLookWindow::requestSettingsWindowMinimize()
{
    if (g_settingsWindow) {
        qDebug().noquote() << QStringLiteral("[SpaceLookSettings] minimize requested");
        g_settingsWindow->showMinimized();
    }
}

void SpaceLookWindow::requestSettingsWindowToggleMaximize()
{
    if (!g_settingsWindow) {
        return;
    }

    qDebug().noquote() << QStringLiteral("[SpaceLookSettings] toggle maximize requested visibility=%1")
        .arg(static_cast<int>(g_settingsWindow->visibility()));
    if (g_settingsWindow->visibility() == QWindow::Maximized) {
        g_settingsWindow->showNormal();
        return;
    }

    g_settingsWindow->showMaximized();
}

void SpaceLookWindow::requestSettingsWindowClose()
{
    if (g_settingsWindow) {
        qDebug().noquote() << QStringLiteral("[SpaceLookSettings] close requested");
        g_settingsWindow->close();
    }
}

void SpaceLookWindow::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    if (!m_suppressPreviewStopOnHide && m_previewHost) {
        m_previewHost->stopPreview();
    }
}

void SpaceLookWindow::mousePressEvent(QMouseEvent* event)
{
    if (!event || event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    m_activeResizeEdges = resizeEdgesForPosition(event->pos());
    if (m_activeResizeEdges != Qt::Edges()) {
        m_resizingWindow = true;
        m_resizeStartGlobalPos = event->globalPos();
        m_resizeStartGeometry = geometry();
        event->accept();
        return;
    }

    if (m_surface->geometry().contains(event->pos())) {
        m_draggingWindow = true;
        m_dragOffset = event->globalPos() - frameGeometry().topLeft();
        event->accept();
        return;
    }

    QWidget::mousePressEvent(event);
}

void SpaceLookWindow::mouseMoveEvent(QMouseEvent* event)
{
    if (!event) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    if (m_resizingWindow) {
        QRect nextGeometry = m_resizeStartGeometry;
        const QPoint delta = event->globalPos() - m_resizeStartGlobalPos;

        if (m_activeResizeEdges.testFlag(Qt::LeftEdge)) {
            nextGeometry.setLeft(nextGeometry.left() + delta.x());
        }
        if (m_activeResizeEdges.testFlag(Qt::RightEdge)) {
            nextGeometry.setRight(nextGeometry.right() + delta.x());
        }
        if (m_activeResizeEdges.testFlag(Qt::TopEdge)) {
            nextGeometry.setTop(nextGeometry.top() + delta.y());
        }
        if (m_activeResizeEdges.testFlag(Qt::BottomEdge)) {
            nextGeometry.setBottom(nextGeometry.bottom() + delta.y());
        }

        if (nextGeometry.width() < minimumWidth()) {
            if (m_activeResizeEdges.testFlag(Qt::LeftEdge)) {
                nextGeometry.setLeft(nextGeometry.right() - minimumWidth() + 1);
            } else {
                nextGeometry.setRight(nextGeometry.left() + minimumWidth() - 1);
            }
        }
        if (nextGeometry.height() < minimumHeight()) {
            if (m_activeResizeEdges.testFlag(Qt::TopEdge)) {
                nextGeometry.setTop(nextGeometry.bottom() - minimumHeight() + 1);
            } else {
                nextGeometry.setBottom(nextGeometry.top() + minimumHeight() - 1);
            }
        }

        setGeometry(nextGeometry);
        event->accept();
        return;
    }

    if (m_draggingWindow) {
        move(event->globalPos() - m_dragOffset);
        event->accept();
        return;
    }

    updateCursorForPosition(event->pos());
    QWidget::mouseMoveEvent(event);
}

void SpaceLookWindow::mouseReleaseEvent(QMouseEvent* event)
{
    m_draggingWindow = false;
    m_resizingWindow = false;
    m_activeResizeEdges = Qt::Edges();
    updateCursorForPosition(mapFromGlobal(QCursor::pos()));
    QWidget::mouseReleaseEvent(event);
}

void SpaceLookWindow::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
}

QString SpaceLookWindow::currentPreviewPath() const
{
    if (!m_previewState) {
        return QString();
    }

    const HoveredItemInfo info = m_previewState->hoveredItem();
    if (!info.filePath.trimmed().isEmpty()) {
        return info.filePath.trimmed();
    }

    return info.resolvedPath.trimmed();
}

void SpaceLookWindow::installSpaceHook()
{
    if (g_spaceHookWindow == this && g_spaceKeyboardHook) {
        return;
    }

    if (!g_spaceKeyboardHook) {
        g_spaceKeyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, SPACELOOKKeyboardProc, GetModuleHandleW(nullptr), 0);
    }

    g_spaceHookWindow = this;
}

void SpaceLookWindow::uninstallSpaceHook()
{
    if (g_spaceHookWindow == this) {
        g_spaceHookWindow = nullptr;
    }

    if (g_spaceKeyboardHook && !g_spaceHookWindow) {
        UnhookWindowsHookEx(g_spaceKeyboardHook);
        g_spaceKeyboardHook = nullptr;
    }
}

Qt::Edges SpaceLookWindow::resizeEdgesForPosition(const QPoint& localPos) const
{
    constexpr int margin = 10;
    Qt::Edges edges;
    if (localPos.x() <= margin) {
        edges |= Qt::LeftEdge;
    } else if (localPos.x() >= width() - margin) {
        edges |= Qt::RightEdge;
    }
    if (localPos.y() <= margin) {
        edges |= Qt::TopEdge;
    } else if (localPos.y() >= height() - margin) {
        edges |= Qt::BottomEdge;
    }
    return edges;
}

void SpaceLookWindow::updateCursorForPosition(const QPoint& localPos)
{
    const Qt::Edges edges = resizeEdgesForPosition(localPos);
    if (edges.testFlag(Qt::TopEdge) && edges.testFlag(Qt::LeftEdge)) {
        setCursor(Qt::SizeFDiagCursor);
    } else if (edges.testFlag(Qt::BottomEdge) && edges.testFlag(Qt::RightEdge)) {
        setCursor(Qt::SizeFDiagCursor);
    } else if (edges.testFlag(Qt::TopEdge) && edges.testFlag(Qt::RightEdge)) {
        setCursor(Qt::SizeBDiagCursor);
    } else if (edges.testFlag(Qt::BottomEdge) && edges.testFlag(Qt::LeftEdge)) {
        setCursor(Qt::SizeBDiagCursor);
    } else if (edges.testFlag(Qt::LeftEdge) || edges.testFlag(Qt::RightEdge)) {
        setCursor(Qt::SizeHorCursor);
    } else if (edges.testFlag(Qt::TopEdge) || edges.testFlag(Qt::BottomEdge)) {
        setCursor(Qt::SizeVerCursor);
    } else {
        unsetCursor();
    }
}

void SpaceLookWindow::applyWindowChromeStyle()
{
    setStyleSheet(QStringLiteral(
        "SpaceLookWindow {"
        "  background: transparent;"
        "}"
        "#SpaceLookSurface {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:1,"
        "      stop:0 rgba(243, 247, 252, 250),"
        "      stop:0.55 rgba(235, 241, 248, 248),"
        "      stop:1 rgba(226, 233, 242, 246));"
        "  border: 1px solid rgba(155, 171, 190, 170);"
        "  border-radius: 0px;"
        "}"
    ));
}

void SpaceLookWindow::applyTaskbarVisibility()
{
    const bool showTaskbar = SpaceLookUiSettings::instance().showTaskbar();
    const Qt::WindowFlags flags = taskbarAwareFlags(Qt::FramelessWindowHint, showTaskbar);
    setWindowFlags(flags);
    if (isVisible()) {
        show();
        raise();
    }

    if (g_settingsWindow) {
        g_settingsWindow->setFlags(taskbarAwareFlags(Qt::FramelessWindowHint, showTaskbar));
        if (g_settingsWindow->isVisible()) {
            g_settingsWindow->show();
            g_settingsWindow->raise();
        }
    }
}

void SpaceLookWindow::applyPerformanceMode()
{
    if (QGraphicsDropShadowEffect* shadowEffect = qobject_cast<QGraphicsDropShadowEffect*>(m_surface->graphicsEffect())) {
        if (SpaceLookUiSettings::instance().performanceMode()) {
            shadowEffect->setBlurRadius(18.0);
            shadowEffect->setOffset(0.0, 8.0);
            shadowEffect->setColor(QColor(18, 32, 52, 36));
            return;
        }

        shadowEffect->setBlurRadius(36.0);
        shadowEffect->setOffset(0.0, 18.0);
        shadowEffect->setColor(QColor(18, 32, 52, 70));
    }
}

void SpaceLookWindow::ensureTrayIcon()
{
    if (m_trayIcon) {
        return;
    }

    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        return;
    }

    m_trayIcon = new QSystemTrayIcon(this);
    const QIcon appIcon = SPACELOOKAppIcon();
    m_trayIcon->setIcon(appIcon.isNull() ? style()->standardIcon(QStyle::SP_ComputerIcon) : appIcon);
    m_trayIcon->setToolTip(QStringLiteral("Space Look"));

    m_trayMenu = new QMenu(this);
    QAction* showAction = m_trayMenu->addAction(QStringLiteral("Show"));
    QAction* hideAction = m_trayMenu->addAction(QStringLiteral("Hide"));
    QAction* quitAction = m_trayMenu->addAction(QStringLiteral("Exit"));

    connect(showAction, &QAction::triggered, this, [this]() {
        show();
        raise();
        activateWindow();
    });
    connect(hideAction, &QAction::triggered, this, [this]() {
        hidePreview();
    });
    connect(quitAction, &QAction::triggered, qApp, []() {
        qApp->quit();
    });
    connect(m_trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger) {
            if (isVisible()) {
                hidePreview();
            } else {
                show();
                raise();
                activateWindow();
            }
        }
    });

    m_trayIcon->setContextMenu(m_trayMenu);
}

void SpaceLookWindow::updateTrayVisibility()
{
    ensureTrayIcon();
    if (!m_trayIcon) {
        qDebug().noquote() << QStringLiteral("[SpaceLookTray] system tray unavailable");
        return;
    }

    if (SpaceLookUiSettings::instance().showSystemTray()) {
        qDebug().noquote() << QStringLiteral("[SpaceLookTray] showing tray icon");
        m_trayIcon->show();
        return;
    }

    qDebug().noquote() << QStringLiteral("[SpaceLookTray] hiding tray icon");
    m_trayIcon->hide();
}

bool SpaceLookWindow::isDocumentPreview(const HoveredItemInfo& info) const
{
    return info.typeKey == QStringLiteral("pdf") || info.typeKey == QStringLiteral("office");
}

bool SpaceLookWindow::isLargeContentPreview(const HoveredItemInfo& info) const
{
    return isDocumentPreview(info) ||
        info.typeKey == QStringLiteral("image") ||
        info.typeKey == QStringLiteral("video");
}

bool SpaceLookWindow::isMediumContentPreview(const HoveredItemInfo& info) const
{
    return info.typeKey == QStringLiteral("text") ||
        info.typeKey == QStringLiteral("code") ||
        info.typeKey == QStringLiteral("archive") ||
        info.typeKey == QStringLiteral("audio");
}

bool SpaceLookWindow::isCompactSummaryPreview(const HoveredItemInfo& info) const
{
    const QString lowerFilePath = info.filePath.trimmed().toLower();
    static const QStringList compactBinarySuffixes = {
        QStringLiteral(".exe"),
        QStringLiteral(".dll"),
        QStringLiteral(".sys"),
        QStringLiteral(".ocx"),
        QStringLiteral(".cpl"),
        QStringLiteral(".com"),
        QStringLiteral(".scr"),
        QStringLiteral(".msi"),
        QStringLiteral(".msp"),
        QStringLiteral(".msu"),
        QStringLiteral(".cab"),
        QStringLiteral(".bin"),
        QStringLiteral(".dat"),
        QStringLiteral(".pak"),
        QStringLiteral(".iso"),
        QStringLiteral(".img")
    };
    for (const QString& suffix : compactBinarySuffixes) {
        if (lowerFilePath.endsWith(suffix)) {
            return true;
        }
    }

    return info.typeKey == QStringLiteral("welcome") ||
        info.typeKey == QStringLiteral("folder") ||
        info.typeKey == QStringLiteral("shortcut") ||
        info.typeKey == QStringLiteral("shell_folder") ||
        info.typeKey == QStringLiteral("file") ||
        info.typeKey == QStringLiteral("shell_item") ||
        info.itemKind == QStringLiteral("Folder") ||
        info.itemKind == QStringLiteral("Shell Folder") ||
        info.itemKind == QStringLiteral("Shortcut");
}

bool SpaceLookWindow::supportsExpandedPreview(const HoveredItemInfo& info) const
{
    return isLargeContentPreview(info) ||
        isMediumContentPreview(info);
}

void SpaceLookWindow::applyPreferredSizeForPreview(const HoveredItemInfo& info)
{
    const QPoint currentCenter = frameGeometry().center();
    QSize preferredSize = size();
    if (isLargeContentPreview(info)) {
        preferredSize = QSize(1180, 820);
    } else if (isMediumContentPreview(info)) {
        preferredSize = QSize(960, 700);
    } else if (isCompactSummaryPreview(info)) {
        preferredSize = QSize(640, 460);
    } else {
        preferredSize = QSize(760, 560);
    }

    if (width() == preferredSize.width() && height() == preferredSize.height()) {
        return;
    }

    const QSize minimumSizeForType = isCompactSummaryPreview(info)
        ? QSize(560, 400)
        : QSize(820, 560);
    setMinimumSize(minimumSizeForType);
    resize(preferredSize.expandedTo(minimumSizeForType));

    QScreen* targetScreen = QGuiApplication::screenAt(currentCenter);
    if (!targetScreen) {
        targetScreen = screen();
    }
    if (!targetScreen && !QGuiApplication::screens().isEmpty()) {
        targetScreen = QGuiApplication::primaryScreen();
    }
    if (!targetScreen) {
        return;
    }

    const QRect availableGeometry = targetScreen->availableGeometry();
    QRect nextGeometry(QPoint(0, 0), size());
    nextGeometry.moveCenter(availableGeometry.center());
    move(nextGeometry.topLeft());
}

void SpaceLookWindow::applyExpandedPreviewSize(const HoveredItemInfo& info)
{
    Q_UNUSED(info);

    QScreen* targetScreen = QGuiApplication::screenAt(frameGeometry().center());
    if (!targetScreen) {
        targetScreen = screen();
    }
    if (!targetScreen) {
        targetScreen = QGuiApplication::primaryScreen();
    }
    if (!targetScreen) {
        return;
    }

    const QRect availableGeometry = targetScreen->availableGeometry();
    const int targetWidth = qMax(960, static_cast<int>(availableGeometry.width() * 0.88));
    const int targetHeight = qMax(700, static_cast<int>(availableGeometry.height() * 0.88));
    setMinimumSize(820, 560);
    resize(targetWidth, targetHeight);

    QRect nextGeometry(QPoint(0, 0), size());
    nextGeometry.moveCenter(availableGeometry.center());
    move(nextGeometry.topLeft());
}
