#include "core/preview_manager.h"

#include "platform/lindesk_hover_client.h"
#include "core/preview_state.h"
#include "widgets/SpaceLookWindow.h"

#include <QCoreApplication>
#include <QDebug>
#include <QCursor>

#include <Windows.h>
#include <uiautomation.h>

namespace {

QString previewIdentityFor(const HoveredItemInfo& info)
{
    if (!info.filePath.trimmed().isEmpty()) {
        return info.filePath.trimmed().toLower();
    }
    if (!info.resolvedPath.trimmed().isEmpty()) {
        return info.resolvedPath.trimmed().toLower();
    }
    return QStringLiteral("%1|%2|%3|%4")
        .arg(info.sourceKind.trimmed().toLower(),
             info.itemKind.trimmed().toLower(),
             info.title.trimmed().toLower(),
             info.windowClassName.trimmed().toLower());
}

bool isSamePreviewTarget(const HoveredItemInfo& left, const HoveredItemInfo& right)
{
    if (!left.valid || !right.valid) {
        return false;
    }

    return previewIdentityFor(left) == previewIdentityFor(right);
}

bool hasConcretePreviewTarget(const HoveredItemInfo& info)
{
    return !info.filePath.trimmed().isEmpty() || !info.resolvedPath.trimmed().isEmpty();
}

QString bstrToQString(BSTR value)
{
    return value ? QString::fromWCharArray(value).trimmed() : QString();
}

}

PreviewManager::PreviewManager(QObject* parent)
    : QObject(parent)
    , m_previewState(new PreviewState(this))
{
}

PreviewManager::~PreviewManager() = default;

void PreviewManager::showInitialPreview()
{
    const QStringList arguments = QCoreApplication::arguments();
    if (arguments.size() > 1) {
        openPreviewForPath(arguments.at(1));
        return;
    }

    SpaceLookWindow* window = ensureWindow();
    if (window) {
        window->hidePreview();
    }
}

void PreviewManager::openPreviewForPath(const QString& filePath)
{
    const HoveredItemInfo previewInfo = m_fileTypeDetector.inspectPath(filePath);
    if (m_window && m_window->isPreviewVisible() && isSamePreviewTarget(m_lastShownItem, previewInfo)) {
        qDebug().noquote() << QStringLiteral("[SpaceLookRender] IPC request matched active preview, hiding window: \"%1\"")
            .arg(previewIdentityFor(previewInfo));
        hideActivePreview();
        return;
    }

    showHoveredItem(previewInfo);
}

void PreviewManager::showSettingsWindow()
{
    SpaceLookWindow* window = ensureWindow();
    if (!window) {
        return;
    }

    window->showSettingsWindow();
}

SpaceLookWindow* PreviewManager::ensureWindow()
{
    if (!m_window) {
        m_window = new SpaceLookWindow(m_previewState);
        connect(m_window, &SpaceLookWindow::spaceHotkeyPressed, this, &PreviewManager::handleSpaceHotkey);
    }
    return m_window;
}

void PreviewManager::handleSpaceHotkey()
{
    qDebug().noquote() << "[SpaceLookRender] Space hotkey triggered";

    if (shouldIgnoreSpaceHotkey()) {
        qDebug().noquote() << "[SpaceLookRender] Space hotkey ignored because focus is in an editable control";
        return;
    }

    if (m_window && m_window->isPreviewVisible() && m_window->isAlwaysOnTop()) {
        if (m_window->containsGlobalPoint(QCursor::pos())) {
            qDebug().noquote() << "[SpaceLookRender] Space pressed over pinned preview, hiding window";
            hideActivePreview();
        } else {
            qDebug().noquote() << "[SpaceLookRender] Preview is pinned, keeping current object";
        }
        return;
    }

    LinDeskHoverClient::RequestResult lindeskHoverResult = LinDeskHoverClient::RequestResult::Unavailable;
    QString lindeskHoverError;
    const QString lindeskHoveredPath = LinDeskHoverClient::requestHoveredPath(&lindeskHoverResult, &lindeskHoverError);
    HoveredItemInfo hoveredInfo;
    if (!lindeskHoveredPath.isEmpty()) {
        qDebug().noquote() << QStringLiteral("[SpaceLookRender] Using LinDesk hovered item: path=\"%1\"")
            .arg(lindeskHoveredPath);
        hoveredInfo = m_fileTypeDetector.inspectPath(
            lindeskHoveredPath,
            QStringLiteral("LinDesk"),
            QStringLiteral("LinDesk"));
    } else {
        if (lindeskHoverResult == LinDeskHoverClient::RequestResult::NoHoveredItem) {
            qDebug().noquote() << QStringLiteral("[SpaceLookRender] LinDesk reported blank hover area, suppressing preview");
            hoveredInfo = HoveredItemInfo();
        } else {
            if (!lindeskHoverError.trimmed().isEmpty()) {
                qDebug().noquote() << QStringLiteral("[SpaceLookRender] LinDesk hovered item unavailable, falling back to local lookup: error=\"%1\"")
                    .arg(lindeskHoverError);
            }
            hoveredInfo = m_fileTypeDetector.inspectItemUnderCursor();
        }
    }

    if (!hasConcretePreviewTarget(hoveredInfo)) {
        if (m_window && m_window->isPreviewVisible()) {
            qDebug().noquote() << "[SpaceLookRender] No valid hovered item, hiding active preview";
            hideActivePreview();
            return;
        }

        qDebug().noquote() << "[SpaceLookRender] No valid hovered item, preview request ignored";
        return;
    }

    if (m_window && m_window->isPreviewVisible()) {
        if (isSamePreviewTarget(m_lastShownItem, hoveredInfo)) {
            qDebug().noquote() << "[SpaceLookRender] Preview visible on same target, hiding window";
            hideActivePreview();
            return;
        }

        qDebug().noquote() << QStringLiteral("[SpaceLookRender] Preview visible, switching target from \"%1\" to \"%2\"")
            .arg(previewIdentityFor(m_lastShownItem), previewIdentityFor(hoveredInfo));
    }

    showHoveredItem(hoveredInfo);
}

void PreviewManager::showHoveredItem(const HoveredItemInfo& info)
{
    HoveredItemInfo updatedInfo = info;
    if (updatedInfo.typeLabel.trimmed().isEmpty()) {
        updatedInfo.typeLabel = m_fileTypeDetector.detectTypeLabel(updatedInfo.filePath);
    }
    if (updatedInfo.title.trimmed().isEmpty()) {
        updatedInfo.title = updatedInfo.fileName.trimmed().isEmpty()
            ? QStringLiteral("Space Look")
            : updatedInfo.fileName;
    }
    if (updatedInfo.statusMessage.trimmed().isEmpty()) {
        updatedInfo.statusMessage = updatedInfo.valid
            ? QStringLiteral("Object information loaded.")
            : QStringLiteral("No object information is available.");
    }

    qDebug().noquote() << QStringLiteral("[SpaceLookRender] showHoveredItem valid=%1 typeKey=%2 title=\"%3\" path=\"%4\"")
        .arg(updatedInfo.valid ? QStringLiteral("true") : QStringLiteral("false"),
             updatedInfo.typeKey,
             updatedInfo.title,
             updatedInfo.filePath);

    m_previewState->setHoveredItem(updatedInfo);
    m_lastShownItem = updatedInfo;

    SpaceLookWindow* window = ensureWindow();
    if (!window) {
        return;
    }

    window->showPreview(updatedInfo);
}

void PreviewManager::hideActivePreview()
{
    if (!m_window || !m_window->isPreviewVisible()) {
        return;
    }

    m_window->hidePreview();
}

bool PreviewManager::shouldIgnoreSpaceHotkey() const
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
