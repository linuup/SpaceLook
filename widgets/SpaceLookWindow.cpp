#include "widgets/SpaceLookWindow.h"

#include "core/preview_state.h"
#include "settings/render_type_settings.h"
#include "settings/spacelook_ui_settings.h"
#include "renderers/OpenWithButton.h"
#include "renderers/PreviewHost.h"
#include "widgets/PreviewCapsuleMenu.h"

#include <QAbstractButton>
#include <QClipboard>
#include <QCursor>
#include <QDesktopServices>
#include <QDir>
#include <QDebug>
#include <QApplication>
#include <QBoxLayout>
#include <QHBoxLayout>
#include <QGuiApplication>
#include <QFileInfo>
#include <QIcon>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPainterPath>
#include <QPoint>
#include <QProcess>
#include <QRegion>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlError>
#include <QMenu>
#include <QQuickView>
#include <QQuickItem>
#include <QScreen>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QToolTip>
#include <QUrl>
#include <QVariantMap>
#include <QVBoxLayout>
#include <QWindow>

#include <Windows.h>
#include <shellapi.h>
#include <uiautomation.h>
#include <windowsx.h>

namespace {

HHOOK g_spaceKeyboardHook = nullptr;
SpaceLookWindow* g_spaceHookWindow = nullptr;
bool g_spaceKeyPressed = false;
constexpr int kTooltipDurationMs = 2000;
constexpr int kResizeGripMargin = 10;
constexpr qreal kWindowCornerRadius = 22.0;

QIcon SPACELOOKAppIcon()
{
    QIcon icon(QStringLiteral(":/icons/icon.ico"));
    if (!icon.isNull()) {
        return icon;
    }

    return QIcon(QStringLiteral(":/icons/icon.png"));
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

QRect centeredSettingsGeometry(const QSize& requestedSize)
{
    QScreen* screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }

    const QRect availableGeometry = screen
        ? screen->availableGeometry()
        : QRect(80, 80, 1200, 800);
    const QSize safeSize(qMin(requestedSize.width(), availableGeometry.width()),
                         qMin(requestedSize.height(), availableGeometry.height()));
    const QPoint topLeft(
        availableGeometry.x() + (availableGeometry.width() - safeSize.width()) / 2,
        availableGeometry.y() + (availableGeometry.height() - safeSize.height()) / 2);
    return QRect(topLeft, safeSize);
}

bool windowIntersectsAnyScreen(const QRect& geometry)
{
    const QList<QScreen*> screens = QGuiApplication::screens();
    for (QScreen* screen : screens) {
        if (screen && screen->availableGeometry().intersects(geometry)) {
            return true;
        }
    }

    return false;
}

void showSettingsWindowVisible(SettingsQuickView* settingsWindow)
{
    if (!settingsWindow) {
        return;
    }

    const QSize desiredSize = settingsWindow->size().isValid()
        ? settingsWindow->size()
        : QSize(820, 620);
    if (!windowIntersectsAnyScreen(QRect(settingsWindow->position(), desiredSize))) {
        settingsWindow->setGeometry(centeredSettingsGeometry(desiredSize));
    }

    if (settingsWindow->visibility() == QWindow::Minimized) {
        settingsWindow->showNormal();
    } else {
        settingsWindow->show();
    }
    settingsWindow->raise();
    settingsWindow->requestActivate();

    QTimer::singleShot(0, settingsWindow, [settingsWindow]() {
        if (!settingsWindow) {
            return;
        }
        if (settingsWindow->visibility() == QWindow::Minimized) {
            settingsWindow->showNormal();
        }
        settingsWindow->raise();
        settingsWindow->requestActivate();
    });
}

QString bstrToQString(BSTR value)
{
    return value ? QString::fromWCharArray(value).trimmed() : QString();
}

QString windowClassName(HWND window)
{
    wchar_t className[256] = {};
    if (!window || GetClassNameW(window, className, static_cast<int>(std::size(className))) <= 0) {
        return QString();
    }

    return QString::fromWCharArray(className).toLower();
}

QString windowProcessName(HWND window)
{
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    if (processId == 0) {
        return QString();
    }

    HANDLE processHandle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!processHandle) {
        return QString();
    }

    wchar_t path[MAX_PATH] = {};
    DWORD pathSize = static_cast<DWORD>(std::size(path));
    const bool ok = QueryFullProcessImageNameW(processHandle, 0, path, &pathSize);
    CloseHandle(processHandle);
    if (!ok) {
        return QString();
    }

    return QFileInfo(QString::fromWCharArray(path, static_cast<int>(pathSize))).fileName().toLower();
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

Qt::WindowFlags settingsWindowFlags()
{
    return Qt::Window | Qt::FramelessWindowHint;
}

long hitTestForResizeEdges(Qt::Edges edges)
{
    if (edges.testFlag(Qt::TopEdge) && edges.testFlag(Qt::LeftEdge)) {
        return HTTOPLEFT;
    }
    if (edges.testFlag(Qt::TopEdge) && edges.testFlag(Qt::RightEdge)) {
        return HTTOPRIGHT;
    }
    if (edges.testFlag(Qt::BottomEdge) && edges.testFlag(Qt::LeftEdge)) {
        return HTBOTTOMLEFT;
    }
    if (edges.testFlag(Qt::BottomEdge) && edges.testFlag(Qt::RightEdge)) {
        return HTBOTTOMRIGHT;
    }
    if (edges.testFlag(Qt::LeftEdge)) {
        return HTLEFT;
    }
    if (edges.testFlag(Qt::RightEdge)) {
        return HTRIGHT;
    }
    if (edges.testFlag(Qt::TopEdge)) {
        return HTTOP;
    }
    if (edges.testFlag(Qt::BottomEdge)) {
        return HTBOTTOM;
    }

    return HTNOWHERE;
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

bool shouldHandleGlobalSpaceHotkey()
{
    const HWND foregroundWindow = GetForegroundWindow();
    if (!foregroundWindow) {
        return false;
    }

    if (g_spaceHookWindow && foregroundWindow == reinterpret_cast<HWND>(g_spaceHookWindow->winId())) {
        return true;
    }

    const QString className = windowClassName(foregroundWindow);
    if (className == QStringLiteral("cabinetwclass") ||
        className == QStringLiteral("explorerwclass") ||
        className == QStringLiteral("progman") ||
        className == QStringLiteral("workerw")) {
        return true;
    }

    const QString processName = windowProcessName(foregroundWindow);
    return processName.contains(QStringLiteral("lindesk"));
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

    if (!shouldHandleGlobalSpaceHotkey()) {
        g_spaceKeyPressed = false;
        return CallNextHookEx(g_spaceKeyboardHook, code, wParam, lParam);
    }

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
    , m_container(new QWidget(this))
    , m_menuRegion(new QWidget(m_container))
    , m_surface(new QWidget(m_container))
    , m_previewHost(new PreviewHost(previewState, m_surface))
    , m_menuBar(new PreviewCapsuleMenu(m_menuRegion))
{
    setWindowTitle(QStringLiteral("Space Look"));
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, false);
    setAttribute(Qt::WA_StyledBackground, true);
    resize(980, 680);
    setMinimumSize(500, 320);
    setMouseTracking(true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(0);
    layout->addWidget(m_container, 1);

    m_container->setObjectName(QStringLiteral("SpaceLookContainer"));
    m_container->setAttribute(Qt::WA_StyledBackground, true);
    m_menuRegion->setObjectName(QStringLiteral("SpaceLookMenuRegion"));
    m_menuRegion->setAttribute(Qt::WA_StyledBackground, true);
    m_menuRegion->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    m_surface->setObjectName(QStringLiteral("SpaceLookSurface"));

    m_containerLayout = new QBoxLayout(QBoxLayout::TopToBottom, m_container);
    m_containerLayout->setContentsMargins(1, 1, 1, 1);
    m_containerLayout->setSpacing(0);
    m_containerLayout->addWidget(m_menuRegion);
    m_containerLayout->addWidget(m_surface, 1);

    m_menuLayout = new QBoxLayout(QBoxLayout::TopToBottom, m_menuRegion);
    m_menuLayout->setContentsMargins(14, 8, 14, 8);
    m_menuLayout->setSpacing(0);
    m_menuLayout->addWidget(m_menuBar, 0, Qt::AlignHCenter | Qt::AlignTop);

    auto* surfaceLayout = new QVBoxLayout(m_surface);
    surfaceLayout->setContentsMargins(1, 1, 1, 1);
    surfaceLayout->setSpacing(0);
    surfaceLayout->addWidget(m_previewHost, 1);
    applyWindowChromeStyle();
    applyRoundedWindowMask();
    applyTaskbarVisibility();
    applyPerformanceMode();
    ensureTrayIcon();
    updateTrayVisibility();
    connect(&SpaceLookUiSettings::instance(), &SpaceLookUiSettings::showTaskbarChanged, this, [this]() {
        applyTaskbarVisibility();
    });
    connect(&SpaceLookUiSettings::instance(), &SpaceLookUiSettings::menuAppearanceChanged, this, [this]() {
        applyMenuPlacement();
        applyWindowChromeStyle();
    });
    connect(&SpaceLookUiSettings::instance(), &SpaceLookUiSettings::performanceModeChanged, this, [this]() {
        applyPerformanceMode();
    });
    connect(&SpaceLookUiSettings::instance(), &SpaceLookUiSettings::showSystemTrayChanged, this, [this]() {
        updateTrayVisibility();
    });

    installSpaceHook();
    applyMenuPlacement();
    applyWindowChromeStyle();
    applyRoundedWindowMask();
    QTimer::singleShot(0, this, [this]() {
        updateTrayVisibility();
    });
    QTimer::singleShot(300, this, [this]() {
        if (SpaceLookUiSettings::instance().showSystemTray()) {
            updateTrayVisibility();
        }
    });
}

SpaceLookWindow::~SpaceLookWindow()
{
    uninstallSpaceHook();
}

void SpaceLookWindow::showPreview(const HoveredItemInfo& info)
{
    const PreviewLoadGuard::Token previewToken = m_previewGuard.begin(info.filePath);
    const bool compactSummaryPreview = isCompactSummaryPreview(info);
    if (m_menuBar) {
        m_menuBar->syncToWindowState();
    }
    if (m_menuRegion) {
        m_menuRegion->show();
        m_menuRegion->raise();
    }
    if (m_menuBar) {
        m_menuBar->show();
        m_menuBar->raise();
    }
    if (compactSummaryPreview) {
        m_expandedPreview = false;
        applyPreferredSizeForPreview(info);
    } else if (m_expandedPreview && supportsExpandedPreview(info)) {
        applyExpandedPreviewSize(info);
    } else {
        m_expandedPreview = false;
        applyPreferredSizeForPreview(info);
    }

    if (m_previewHost) {
        m_previewHost->showPreview(info);
        if (compactSummaryPreview || m_previewHost->activeRendererId() == QStringLiteral("summary")) {
            applySummaryPreviewSize(info);
        }
    }

    show();
    if (m_menuRegion) {
        m_menuRegion->show();
        m_menuRegion->raise();
    }
    if (m_menuBar) {
        m_menuBar->show();
        m_menuBar->raise();
    }
    raise();
    activateWindow();

    QTimer::singleShot(0, this, [this, info, previewToken]() {
        if (!m_previewGuard.isCurrent(previewToken, info.filePath) || !isVisible() || !m_previewHost) {
            return;
        }

        if (isCompactSummaryPreview(info) || m_previewHost->activeRendererId() == QStringLiteral("summary")) {
            applySummaryPreviewSize(info);
        }
    });
}

void SpaceLookWindow::hidePreview()
{
    m_previewGuard.cancel();
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
    if (m_menuBar) {
        m_menuBar->syncToWindowState();
    }
}

void SpaceLookWindow::applySummaryPreviewSize(const HoveredItemInfo& info)
{
    HoveredItemInfo summaryInfo = info;
    summaryInfo.rendererName = QStringLiteral("summary");
    m_expandedPreview = false;
    applyPreferredSizeForPreview(summaryInfo);
    if (m_menuBar) {
        m_menuBar->syncToWindowState();
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
        showSettingsWindowVisible(g_settingsWindow);
        return;
    }

    g_settingsWindow = new SettingsQuickView();
    connect(g_settingsWindow, &QObject::destroyed, this, []() {
        g_settingsWindow = nullptr;
    });
    g_settingsWindow->rootContext()->setContextProperty(QStringLiteral("uiSettings"), &SpaceLookUiSettings::instance());
    g_settingsWindow->rootContext()->setContextProperty(QStringLiteral("renderTypeSettings"), &RenderTypeSettings::instance());
    g_settingsWindow->rootContext()->setContextProperty(QStringLiteral("settingsWindow"), this);
    g_settingsWindow->setColor(Qt::transparent);
    g_settingsWindow->setIcon(SPACELOOKAppIcon());
    g_settingsWindow->setTitle(QStringLiteral("Settings"));
    g_settingsWindow->setFlags(settingsWindowFlags());
    g_settingsWindow->setResizeMode(QQuickView::SizeRootObjectToView);
    g_settingsWindow->engine()->clearComponentCache();
    const QUrl settingsSource = viewerSourceUrl(QStringLiteral("SettingsPage.qml"));
    qDebug().noquote() << QStringLiteral("[SpaceLookSettings] loading source=%1").arg(settingsSource.toString());
    connect(g_settingsWindow, &QQuickView::statusChanged, g_settingsWindow, [settingsSource](QQuickView::Status status) {
        qDebug().noquote() << QStringLiteral("[SpaceLookSettings] status=%1 source=%2")
            .arg(static_cast<int>(status))
            .arg(settingsSource.toString());
        if (status == QQuickView::Error && g_settingsWindow) {
            const QList<QQmlError> errors = g_settingsWindow->errors();
            for (const QQmlError& error : errors) {
                qDebug().noquote() << QStringLiteral("[SpaceLookSettings] qml error=%1").arg(error.toString());
            }
        }
    });
    g_settingsWindow->setSource(settingsSource);
    g_settingsWindow->setGeometry(centeredSettingsGeometry(QSize(820, 620)));

    showSettingsWindowVisible(g_settingsWindow);
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

    if (m_menuRegion && m_menuRegion->geometry().contains(event->pos())) {
        const QPoint menuLocalPos = event->pos() - m_menuRegion->geometry().topLeft();
        const bool hitMenuBlankArea = !m_menuBar || !m_menuBar->geometry().contains(menuLocalPos);
        if (hitMenuBlankArea) {
            m_draggingWindow = true;
            m_dragOffset = event->globalPos() - frameGeometry().topLeft();
            event->accept();
            return;
        }
    }

    if (shouldStartDragFromActiveHeader(event->pos())) {
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
    applyRoundedWindowMask();
}

bool SpaceLookWindow::nativeEvent(const QByteArray& eventType, void* message, long* result)
{
    Q_UNUSED(eventType);

    if (!message || !result) {
        return QWidget::nativeEvent(eventType, message, result);
    }

    MSG* msg = static_cast<MSG*>(message);
    if (msg->message != WM_NCHITTEST) {
        return QWidget::nativeEvent(eventType, message, result);
    }

    if (!canManuallyResizeCurrentPreview()) {
        return QWidget::nativeEvent(eventType, message, result);
    }

    const QPoint globalPos(GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam));
    const Qt::Edges edges = resizeEdgesForPosition(mapFromGlobal(globalPos));
    const long hitTest = hitTestForResizeEdges(edges);
    if (hitTest == HTNOWHERE) {
        return QWidget::nativeEvent(eventType, message, result);
    }

    *result = hitTest;
    return true;
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
    if (!canManuallyResizeCurrentPreview()) {
        return Qt::Edges();
    }

    const QRect resizeRect = m_container
        ? QRect(m_container->mapTo(const_cast<SpaceLookWindow*>(this), QPoint(0, 0)), m_container->size())
        : rect();
    const QRect grabRect = resizeRect.adjusted(-kResizeGripMargin, -kResizeGripMargin,
                                               kResizeGripMargin, kResizeGripMargin);
    if (!grabRect.contains(localPos)) {
        return Qt::Edges();
    }

    Qt::Edges edges;
    if (qAbs(localPos.x() - resizeRect.left()) <= kResizeGripMargin) {
        edges |= Qt::LeftEdge;
    } else if (qAbs(localPos.x() - resizeRect.right()) <= kResizeGripMargin) {
        edges |= Qt::RightEdge;
    }
    if (qAbs(localPos.y() - resizeRect.top()) <= kResizeGripMargin) {
        edges |= Qt::TopEdge;
    } else if (qAbs(localPos.y() - resizeRect.bottom()) <= kResizeGripMargin) {
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

bool SpaceLookWindow::shouldStartDragFromActiveHeader(const QPoint& localPos) const
{
    if (!m_previewHost) {
        return false;
    }

    QWidget* rendererWidget = m_previewHost->activeRendererWidget();
    if (!rendererWidget) {
        return false;
    }

    const QList<QWidget*> directChildren = rendererWidget->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
    for (QWidget* child : directChildren) {
        if (!child || !child->objectName().endsWith(QStringLiteral("HeaderRow"))) {
            continue;
        }

        const QRect headerRect(child->mapTo(const_cast<SpaceLookWindow*>(this), QPoint(0, 0)), child->size());
        if (!headerRect.contains(localPos)) {
            continue;
        }

        QWidget* hitWidget = QApplication::widgetAt(mapToGlobal(localPos));
        while (hitWidget) {
            if (qobject_cast<QAbstractButton*>(hitWidget)) {
                return false;
            }
            if (hitWidget == child) {
                return true;
            }
            hitWidget = hitWidget->parentWidget();
        }
    }

    return false;
}

void SpaceLookWindow::applyMenuPlacement()
{
    if (!m_containerLayout || !m_menuLayout || !m_menuRegion || !m_surface || !m_menuBar) {
        return;
    }

    const int placement = SpaceLookUiSettings::instance().menuPlacement();
    const bool menuOnBottom = placement == 1;
    const bool menuOnLeft = placement == 2;
    const bool menuOnRight = placement == 3;
    const bool verticalSection = menuOnLeft || menuOnRight;

    m_containerLayout->setDirection(verticalSection ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom);
    m_menuLayout->setDirection(verticalSection ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom);

    if (verticalSection) {
        m_menuRegion->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Expanding);
        m_menuRegion->setMinimumWidth(58);
        m_menuRegion->setMinimumHeight(0);
        m_menuLayout->setContentsMargins(8, 14, 8, 14);
    } else {
        m_menuRegion->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
        m_menuRegion->setMinimumWidth(0);
        m_menuRegion->setMinimumHeight(58);
        m_menuLayout->setContentsMargins(14, 8, 14, 8);
    }

    m_containerLayout->removeWidget(m_menuRegion);
    m_containerLayout->removeWidget(m_surface);
    m_menuLayout->removeWidget(m_menuBar);

    if (menuOnRight || menuOnBottom) {
        m_containerLayout->addWidget(m_surface, 1);
        m_containerLayout->addWidget(m_menuRegion, 0);
    } else {
        m_containerLayout->addWidget(m_menuRegion, 0);
        m_containerLayout->addWidget(m_surface, 1);
    }

    Qt::Alignment menuAlignment = Qt::AlignHCenter | Qt::AlignTop;
    if (menuOnBottom) {
        menuAlignment = Qt::AlignHCenter | Qt::AlignBottom;
    } else if (menuOnLeft) {
        menuAlignment = Qt::AlignHCenter | Qt::AlignVCenter;
    } else if (menuOnRight) {
        menuAlignment = Qt::AlignHCenter | Qt::AlignVCenter;
    }
    m_menuLayout->addWidget(m_menuBar, 0, menuAlignment);

    m_menuRegion->updateGeometry();
    m_surface->updateGeometry();
    m_container->updateGeometry();
    m_menuRegion->show();
    m_menuBar->show();
    m_menuRegion->raise();
    m_menuBar->raise();
}

void SpaceLookWindow::applyWindowChromeStyle()
{
    const int placement = SpaceLookUiSettings::instance().menuPlacement();
    const bool menuOnBottom = placement == 1;
    const bool menuOnLeft = placement == 2;
    const bool menuOnRight = placement == 3;

    QString menuRadius;
    QString surfaceRadius;
    if (menuOnLeft) {
        menuRadius = QStringLiteral(
            "  border-top-left-radius: 22px;"
            "  border-top-right-radius: 0px;"
            "  border-bottom-left-radius: 22px;"
            "  border-bottom-right-radius: 0px;");
        surfaceRadius = QStringLiteral(
            "  border-top-left-radius: 0px;"
            "  border-top-right-radius: 22px;"
            "  border-bottom-left-radius: 0px;"
            "  border-bottom-right-radius: 22px;");
    } else if (menuOnRight) {
        menuRadius = QStringLiteral(
            "  border-top-left-radius: 0px;"
            "  border-top-right-radius: 22px;"
            "  border-bottom-left-radius: 0px;"
            "  border-bottom-right-radius: 22px;");
        surfaceRadius = QStringLiteral(
            "  border-top-left-radius: 22px;"
            "  border-top-right-radius: 0px;"
            "  border-bottom-left-radius: 22px;"
            "  border-bottom-right-radius: 0px;");
    } else if (menuOnBottom) {
        menuRadius = QStringLiteral(
            "  border-top-left-radius: 0px;"
            "  border-top-right-radius: 0px;"
            "  border-bottom-left-radius: 22px;"
            "  border-bottom-right-radius: 22px;");
        surfaceRadius = QStringLiteral(
            "  border-top-left-radius: 22px;"
            "  border-top-right-radius: 22px;"
            "  border-bottom-left-radius: 0px;"
            "  border-bottom-right-radius: 0px;");
    } else {
        menuRadius = QStringLiteral(
            "  border-top-left-radius: 22px;"
            "  border-top-right-radius: 22px;"
            "  border-bottom-left-radius: 0px;"
            "  border-bottom-right-radius: 0px;");
        surfaceRadius = QStringLiteral(
            "  border-top-left-radius: 0px;"
            "  border-top-right-radius: 0px;"
            "  border-bottom-left-radius: 22px;"
            "  border-bottom-right-radius: 22px;");
    }

    setStyleSheet(QStringLiteral(
        "SpaceLookWindow {"
        "  background: #445a70;"
        "}"
        "#SpaceLookContainer {"
        "  background: rgba(252, 253, 255, 250);"
        "  border: 1px solid rgba(68, 90, 112, 245);"
        "  border-radius: 22px;"
        "}"
        "#SpaceLookMenuRegion {"
        "  background: rgba(236, 243, 251, 245);"
        "  border: none;"
        "%1"
        "}"
        "#SpaceLookSurface {"
        "  background: rgba(244, 248, 252, 252);"
        "  border: none;"
        "%2"
        "}"
    ).arg(menuRadius, surfaceRadius));
}

void SpaceLookWindow::applyRoundedWindowMask()
{
    if (width() <= 0 || height() <= 0) {
        return;
    }

    const QRect maskRect = m_container
        ? m_container->geometry()
        : rect();
    QPainterPath roundedPath;
    roundedPath.addRoundedRect(maskRect, kWindowCornerRadius, kWindowCornerRadius);
    setMask(QRegion(roundedPath.toFillPolygon().toPolygon()));
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
}

void SpaceLookWindow::applyPerformanceMode()
{
    Q_UNUSED(this);
    Q_UNUSED(SpaceLookUiSettings::instance());
}

void SpaceLookWindow::ensureTrayIcon()
{
    if (m_trayIcon) {
        return;
    }

    if (qApp) {
        qApp->setQuitOnLastWindowClosed(false);
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
        showSettingsWindow();
    });
    connect(hideAction, &QAction::triggered, this, [this]() {
        requestSettingsWindowClose();
    });
    connect(quitAction, &QAction::triggered, qApp, []() {
        qApp->quit();
    });
    connect(m_trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger) {
            if (g_settingsWindow && g_settingsWindow->isVisible()) {
                requestSettingsWindowClose();
            } else {
                showSettingsWindow();
            }
        }
    });

    m_trayIcon->setContextMenu(m_trayMenu);
}

void SpaceLookWindow::updateTrayVisibility()
{
    ensureTrayIcon();
    if (!m_trayIcon) {
        qDebug().noquote() << QStringLiteral("[SpaceLookTray] tray icon instance unavailable");
        return;
    }

    if (SpaceLookUiSettings::instance().showSystemTray()) {
        if (!QSystemTrayIcon::isSystemTrayAvailable()) {
            qDebug().noquote() << QStringLiteral("[SpaceLookTray] system tray currently unavailable");
        }
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
        info.typeKey == QStringLiteral("markdown") ||
        info.typeKey == QStringLiteral("html") ||
        info.typeKey == QStringLiteral("folder") ||
        info.typeKey == QStringLiteral("shell_folder") ||
        info.typeKey == QStringLiteral("archive") ||
        info.typeKey == QStringLiteral("audio");
}

bool SpaceLookWindow::isWelcomePreview(const HoveredItemInfo& info) const
{
    return info.typeKey == QStringLiteral("welcome") ||
        info.rendererName == QStringLiteral("welcome");
}

bool SpaceLookWindow::isCompactSummaryPreview(const HoveredItemInfo& info) const
{
    QString rendererName = info.rendererName.trimmed().toLower();
    if (rendererName.endsWith(QStringLiteral("renderer"))) {
        rendererName.chop(QStringLiteral("renderer").size());
    } else if (rendererName.endsWith(QStringLiteral("render"))) {
        rendererName.chop(QStringLiteral("render").size());
    }

    return rendererName == QStringLiteral("summary");
}

bool SpaceLookWindow::supportsExpandedPreview(const HoveredItemInfo& info) const
{
    if (isCompactSummaryPreview(info)) {
        return false;
    }

    return isLargeContentPreview(info) ||
        isMediumContentPreview(info) ||
        isWelcomePreview(info);
}

bool SpaceLookWindow::canManuallyResizeCurrentPreview() const
{
    if (!m_previewHost) {
        return true;
    }

    const QString rendererId = m_previewHost->activeRendererId();
    if (rendererId.trimmed().isEmpty()) {
        return true;
    }

    return rendererId != QStringLiteral("summary");
}

void SpaceLookWindow::applyPreferredSizeForPreview(const HoveredItemInfo& info)
{
    const QPoint currentCenter = frameGeometry().center();
    QSize preferredSize = size();
    QSize minimumSizeForType(820, 560);
    if (isWelcomePreview(info)) {
        preferredSize = QSize(960, 700);
    } else if (isLargeContentPreview(info)) {
        preferredSize = QSize(1180, 820);
    } else if (isCompactSummaryPreview(info)) {
        preferredSize = QSize(620, 450);
        minimumSizeForType = QSize(560, 340);
    } else if (isMediumContentPreview(info)) {
        preferredSize = QSize(960, 700);
    } else {
        preferredSize = QSize(760, 560);
    }

    setMinimumSize(minimumSizeForType);
    const QSize targetSize = preferredSize.expandedTo(minimumSizeForType);

    if (size() == targetSize) {
        return;
    }

    resize(targetSize);

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
