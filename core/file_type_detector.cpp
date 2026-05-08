#include "core/file_type_detector.h"
#include "core/file_suffix_utils.h"
#include "core/PreviewFileReader.h"
#include "core/render_type_registry.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDebug>
#include <QStringList>
#include <QStandardPaths>
#include <QSet>
#include <QUrl>

#include <Windows.h>
#include <UIAutomation.h>
#include <ShlDisp.h>
#include <exdisp.h>
#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shlwapi.h>

namespace {

bool elementLooksLikeFileItem(IUIAutomationElement* element);

void logLookupStep(const QString& message)
{
    qDebug().noquote() << QStringLiteral("[SpaceLookLookup] %1").arg(message);
}

QString bstrToQString(BSTR value)
{
    if (!value) {
        return QString();
    }

    const QString text = QString::fromWCharArray(value);
    SysFreeString(value);
    return text;
}

QString classNameForWindow(HWND hwnd)
{
    if (!hwnd) {
        return QString();
    }

    wchar_t buffer[256] = {};
    const int length = GetClassNameW(hwnd, buffer, static_cast<int>(std::size(buffer)));
    if (length <= 0) {
        return QString();
    }

    return QString::fromWCharArray(buffer, length);
}

QString windowTextForWindow(HWND hwnd)
{
    if (!hwnd) {
        return QString();
    }

    wchar_t buffer[256] = {};
    const int length = GetWindowTextW(hwnd, buffer, static_cast<int>(std::size(buffer)));
    if (length <= 0) {
        return QString();
    }

    return QString::fromWCharArray(buffer, length);
}

bool looksLikeMpegTransportStream(const QFileInfo& fileInfo)
{
    if (fileInfo.suffix().compare(QStringLiteral("ts"), Qt::CaseInsensitive) != 0) {
        return false;
    }

    QByteArray sample;
    if (!PreviewFileReader::readPrefix(fileInfo.absoluteFilePath(), 1024, &sample)) {
        return false;
    }

    if (sample.size() < 376) {
        return false;
    }

    auto hasSyncPattern = [&sample](int offset, int packetSize) {
        for (int index = offset; index < sample.size(); index += packetSize) {
            if (sample.at(index) != char(0x47)) {
                return false;
            }
        }
        return sample.size() > offset + packetSize;
    };

    return hasSyncPattern(0, 188) || hasSyncPattern(4, 192);
}

QString executableNameForWindow(HWND hwnd)
{
    if (!hwnd) {
        return QString();
    }

    DWORD processId = 0;
    GetWindowThreadProcessId(hwnd, &processId);
    if (!processId) {
        return QString();
    }

    HANDLE processHandle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!processHandle) {
        return QString();
    }

    wchar_t buffer[MAX_PATH] = {};
    DWORD bufferLength = static_cast<DWORD>(std::size(buffer));
    QString executableName;
    if (QueryFullProcessImageNameW(processHandle, 0, buffer, &bufferLength)) {
        executableName = QFileInfo(QString::fromWCharArray(buffer, static_cast<int>(bufferLength))).fileName();
    }

    CloseHandle(processHandle);
    return executableName;
}

bool windowBelongsToLinDesk(HWND hwnd)
{
    const QString executableName = executableNameForWindow(hwnd);
    return executableName.compare(QStringLiteral("LinDesk.exe"), Qt::CaseInsensitive) == 0;
}

bool pointInsideWindow(HWND hwnd, const POINT& point)
{
    if (!hwnd || !IsWindowVisible(hwnd)) {
        return false;
    }

    RECT rect{};
    if (!GetWindowRect(hwnd, &rect)) {
        return false;
    }

    return point.x >= rect.left &&
        point.x < rect.right &&
        point.y >= rect.top &&
        point.y < rect.bottom;
}

bool isSupportedDesktopRootClass(const QString& className)
{
    return className == QStringLiteral("WorkerW") ||
        className == QStringLiteral("Progman") ||
        className == QStringLiteral("CabinetWClass");
}

HWND findUnderlyingSupportedRootWindow(const POINT& point, QString* debugSummary = nullptr)
{
    QStringList scannedWindows;
    for (HWND hwnd = GetTopWindow(nullptr); hwnd; hwnd = GetWindow(hwnd, GW_HWNDNEXT)) {
        if (!pointInsideWindow(hwnd, point)) {
            continue;
        }

        const QString className = classNameForWindow(hwnd);
        const QString title = windowTextForWindow(hwnd);
        scannedWindows.append(QStringLiteral("%1(\"%2\")").arg(className, title));

        if (isSupportedDesktopRootClass(className)) {
            if (debugSummary) {
                *debugSummary = scannedWindows.join(QStringLiteral(" -> "));
            }
            return hwnd;
        }
    }

    if (debugSummary) {
        *debugSummary = scannedWindows.join(QStringLiteral(" -> "));
    }
    return nullptr;
}

HWND rootWindowFromElement(IUIAutomation* automation, IUIAutomationElement* element)
{
    if (!automation || !element) {
        return nullptr;
    }

    IUIAutomationTreeWalker* walker = nullptr;
    if (FAILED(automation->get_ControlViewWalker(&walker)) || !walker) {
        return nullptr;
    }

    HWND foundHwnd = nullptr;
    IUIAutomationElement* current = element;
    current->AddRef();

    while (current) {
        UIA_HWND nativeHwnd = nullptr;
        if (SUCCEEDED(current->get_CurrentNativeWindowHandle(&nativeHwnd)) && nativeHwnd) {
            foundHwnd = GetAncestor(reinterpret_cast<HWND>(nativeHwnd), GA_ROOT);
            if (foundHwnd) {
                current->Release();
                walker->Release();
                return foundHwnd;
            }
        }

        IUIAutomationElement* parent = nullptr;
        if (FAILED(walker->GetParentElement(current, &parent))) {
            current->Release();
            break;
        }

        current->Release();
        current = parent;
    }

    walker->Release();
    return foundHwnd;
}

bool pointInsideRect(const RECT& rect, const POINT& point)
{
    return point.x >= rect.left &&
        point.x < rect.right &&
        point.y >= rect.top &&
        point.y < rect.bottom;
}

IUIAutomationElement* deepestElementAtPointInRoot(IUIAutomation* automation, HWND rootWindow, const POINT& point)
{
    if (!automation || !rootWindow) {
        return nullptr;
    }

    IUIAutomationElement* current = nullptr;
    if (FAILED(automation->ElementFromHandle(rootWindow, &current)) || !current) {
        return nullptr;
    }

    IUIAutomationTreeWalker* walker = nullptr;
    if (FAILED(automation->get_ControlViewWalker(&walker)) || !walker) {
        current->Release();
        return nullptr;
    }

    bool advanced = true;
    while (advanced) {
        advanced = false;
        IUIAutomationElement* bestChild = nullptr;
        LONG bestArea = LONG_MAX;

        IUIAutomationElement* child = nullptr;
        if (SUCCEEDED(walker->GetFirstChildElement(current, &child)) && child) {
            while (child) {
                RECT childRect{};
                if (SUCCEEDED(child->get_CurrentBoundingRectangle(&childRect)) &&
                    pointInsideRect(childRect, point)) {
                    const LONG width = childRect.right - childRect.left;
                    const LONG height = childRect.bottom - childRect.top;
                    const LONG area = width * height;
                    if (!bestChild || area <= bestArea) {
                        if (bestChild) {
                            bestChild->Release();
                        }
                        bestChild = child;
                        bestChild->AddRef();
                        bestArea = area;
                    }
                }

                IUIAutomationElement* nextSibling = nullptr;
                if (FAILED(walker->GetNextSiblingElement(child, &nextSibling))) {
                    child->Release();
                    break;
                }
                child->Release();
                child = nextSibling;
            }
        }

        if (bestChild) {
            current->Release();
            current = bestChild;
            advanced = true;
        }
    }

    walker->Release();
    return current;
}

void findBestItemElementAtPointRecursive(IUIAutomationTreeWalker* walker,
                                         IUIAutomationElement* element,
                                         const POINT& point,
                                         IUIAutomationElement** bestElement,
                                         LONG* bestArea)
{
    if (!walker || !element || !bestElement || !bestArea) {
        return;
    }

    RECT rect{};
    if (SUCCEEDED(element->get_CurrentBoundingRectangle(&rect)) &&
        pointInsideRect(rect, point)) {
        BSTR nameValue = nullptr;
        QString controlName;
        if (SUCCEEDED(element->get_CurrentName(&nameValue))) {
            controlName = bstrToQString(nameValue).trimmed();
        }

        if (!controlName.isEmpty() && elementLooksLikeFileItem(element)) {
            const LONG width = rect.right - rect.left;
            const LONG height = rect.bottom - rect.top;
            const LONG area = width * height;
            if (!*bestElement || area <= *bestArea) {
                if (*bestElement) {
                    (*bestElement)->Release();
                }
                *bestElement = element;
                (*bestElement)->AddRef();
                *bestArea = area;
                logLookupStep(QStringLiteral("Best item candidate updated: name=\"%1\" area=%2")
                    .arg(controlName)
                    .arg(area));
            }
        }
    }

    IUIAutomationElement* child = nullptr;
    if (FAILED(walker->GetFirstChildElement(element, &child)) || !child) {
        return;
    }

    while (child) {
        findBestItemElementAtPointRecursive(walker, child, point, bestElement, bestArea);

        IUIAutomationElement* nextSibling = nullptr;
        if (FAILED(walker->GetNextSiblingElement(child, &nextSibling))) {
            child->Release();
            break;
        }
        child->Release();
        child = nextSibling;
    }
}

IUIAutomationElement* bestItemElementAtPointInRoot(IUIAutomation* automation, HWND rootWindow, const POINT& point)
{
    if (!automation || !rootWindow) {
        return nullptr;
    }

    IUIAutomationElement* rootElement = nullptr;
    if (FAILED(automation->ElementFromHandle(rootWindow, &rootElement)) || !rootElement) {
        return nullptr;
    }

    IUIAutomationTreeWalker* walker = nullptr;
    if (FAILED(automation->get_ControlViewWalker(&walker)) || !walker) {
        rootElement->Release();
        return nullptr;
    }

    IUIAutomationElement* bestElement = nullptr;
    LONG bestArea = LONG_MAX;
    findBestItemElementAtPointRecursive(walker, rootElement, point, &bestElement, &bestArea);

    walker->Release();
    rootElement->Release();
    return bestElement;
}

QString pathFromLocationUrl(const QString& locationUrl)
{
    if (locationUrl.trimmed().isEmpty()) {
        return QString();
    }

    const QUrl url(locationUrl);
    const QString localFile = url.toLocalFile();
    if (!localFile.isEmpty()) {
        return QDir::cleanPath(localFile);
    }

    return QString();
}

IWebBrowserApp* explorerBrowserForWindow(HWND hwnd)
{
    IShellWindows* shellWindows = nullptr;
    const HRESULT hr = CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&shellWindows));
    if (FAILED(hr) || !shellWindows) {
        return nullptr;
    }

    long count = 0;
    shellWindows->get_Count(&count);

    for (long index = 0; index < count; ++index) {
        VARIANT variantIndex;
        VariantInit(&variantIndex);
        variantIndex.vt = VT_I4;
        variantIndex.lVal = index;

        IDispatch* dispatch = nullptr;
        const HRESULT itemHr = shellWindows->Item(variantIndex, &dispatch);
        VariantClear(&variantIndex);
        if (FAILED(itemHr) || !dispatch) {
            continue;
        }

        IWebBrowserApp* browser = nullptr;
        const HRESULT browserHr = dispatch->QueryInterface(IID_PPV_ARGS(&browser));
        dispatch->Release();
        if (FAILED(browserHr) || !browser) {
            continue;
        }

        SHANDLE_PTR browserHwnd = 0;
        browser->get_HWND(&browserHwnd);
        if (reinterpret_cast<HWND>(browserHwnd) == hwnd) {
            shellWindows->Release();
            return browser;
        }

        browser->Release();
    }

    shellWindows->Release();
    return nullptr;
}

QString folderPathForExplorerWindow(HWND hwnd)
{
    IWebBrowserApp* browser = explorerBrowserForWindow(hwnd);
    if (!browser) {
        return QString();
    }

    QString folderPath;
    BSTR locationUrl = nullptr;
    if (SUCCEEDED(browser->get_LocationURL(&locationUrl))) {
        folderPath = pathFromLocationUrl(QString::fromWCharArray(locationUrl ? locationUrl : L""));
        if (locationUrl) {
            SysFreeString(locationUrl);
        }
    }

    browser->Release();
    return folderPath;
}

QString desktopFolderPath()
{
    return QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::DesktopLocation));
}

bool isShellParsingPath(const QString& filePath)
{
    const QString trimmedPath = filePath.trimmed();
    return trimmedPath.startsWith(QStringLiteral("::")) ||
        trimmedPath.startsWith(QStringLiteral("shell:"), Qt::CaseInsensitive) ||
        (trimmedPath.startsWith(QLatin1Char('{')) && trimmedPath.endsWith(QLatin1Char('}')));
}

QString normalizeShellParsingPath(const QString& filePath)
{
    const QString trimmedPath = filePath.trimmed();
    if (trimmedPath.startsWith(QStringLiteral("::")) ||
        trimmedPath.startsWith(QStringLiteral("shell:"), Qt::CaseInsensitive)) {
        return trimmedPath;
    }
    if (trimmedPath.startsWith(QLatin1Char('{')) && trimmedPath.endsWith(QLatin1Char('}'))) {
        return QStringLiteral("::") + trimmedPath;
    }
    return trimmedPath;
}

bool isUsersFilesShellFolderPath(const QString& shellPath)
{
    return shellPath.contains(QStringLiteral("{59031a47-3f72-44a7-89c5-5595fe6b30ee}"), Qt::CaseInsensitive);
}

QString usersFilesFolderPathForShellPath(const QString& shellPath)
{
    if (!isUsersFilesShellFolderPath(shellPath)) {
        return QString();
    }

    const QString homePath = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    if (homePath.trimmed().isEmpty()) {
        return QString();
    }

    const QFileInfo homeInfo(homePath);
    if (!homeInfo.exists() || !homeInfo.isDir()) {
        return QString();
    }

    return QDir::cleanPath(homeInfo.absoluteFilePath());
}

QString resolveShortcutTarget(const QString& shortcutPath)
{
    if (!shortcutPath.endsWith(QStringLiteral(".lnk"), Qt::CaseInsensitive)) {
        return QString();
    }

    IShellLinkW* shellLink = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&shellLink));
    if (FAILED(hr) || !shellLink) {
        return QString();
    }

    IPersistFile* persistFile = nullptr;
    hr = shellLink->QueryInterface(IID_PPV_ARGS(&persistFile));
    if (FAILED(hr) || !persistFile) {
        shellLink->Release();
        return QString();
    }

    const std::wstring nativePath = QDir::toNativeSeparators(shortcutPath).toStdWString();
    hr = persistFile->Load(nativePath.c_str(), STGM_READ | STGM_SHARE_DENY_NONE);
    if (FAILED(hr)) {
        persistFile->Release();
        shellLink->Release();
        return QString();
    }

    wchar_t resolvedPath[MAX_PATH] = {};
    WIN32_FIND_DATAW findData{};
    QString result;
    if (SUCCEEDED(shellLink->GetPath(resolvedPath, MAX_PATH, &findData, SLGP_RAWPATH)) && resolvedPath[0] != L'\0') {
        result = QDir::cleanPath(QString::fromWCharArray(resolvedPath));
    }

    persistFile->Release();
    shellLink->Release();
    return result;
}

QString exactOrFriendlyPathMatch(const QString& folderPath, const QString& name)
{
    if (folderPath.isEmpty() || name.trimmed().isEmpty()) {
        logLookupStep(QStringLiteral("Skip filesystem match because folder or name is empty"));
        return QString();
    }

    const QDir folder(folderPath);
    const QString directPath = folder.absoluteFilePath(name);
    logLookupStep(QStringLiteral("Try direct path match: folder=\"%1\" name=\"%2\" path=\"%3\"")
        .arg(folderPath, name, directPath));
    if (QFileInfo::exists(directPath)) {
        logLookupStep(QStringLiteral("Direct path match success: %1").arg(QDir::cleanPath(directPath)));
        return QDir::cleanPath(directPath);
    }

    const QFileInfoList entries = folder.entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
        QDir::Name | QDir::IgnoreCase);

    for (const QFileInfo& entry : entries) {
        if (entry.fileName().compare(name, Qt::CaseInsensitive) == 0 ||
            entry.completeBaseName().compare(name, Qt::CaseInsensitive) == 0) {
            logLookupStep(QStringLiteral("Friendly match success: candidate=\"%1\" matched=\"%2\"")
                .arg(name, QDir::cleanPath(entry.absoluteFilePath())));
            return QDir::cleanPath(entry.absoluteFilePath());
        }
    }

    logLookupStep(QStringLiteral("Filesystem match failed for candidate: %1").arg(name));
    return QString();
}

bool elementLooksLikeFileItem(IUIAutomationElement* element)
{
    if (!element) {
        return false;
    }

    CONTROLTYPEID controlType = 0;
    element->get_CurrentControlType(&controlType);
    if (controlType == UIA_ListItemControlTypeId ||
        controlType == UIA_DataItemControlTypeId ||
        controlType == UIA_TreeItemControlTypeId ||
        controlType == UIA_MenuItemControlTypeId) {
        return true;
    }

    BSTR localizedTypeValue = nullptr;
    QString localizedType;
    if (SUCCEEDED(element->get_CurrentLocalizedControlType(&localizedTypeValue))) {
        localizedType = bstrToQString(localizedTypeValue).trimmed();
    }
    if (localizedType.contains(QStringLiteral("item"), Qt::CaseInsensitive)) {
        return true;
    }

    BSTR classNameValue = nullptr;
    QString className;
    if (SUCCEEDED(element->get_CurrentClassName(&classNameValue))) {
        className = bstrToQString(classNameValue).trimmed();
    }
    return className.contains(QStringLiteral("item"), Qt::CaseInsensitive);
}

QString itemNameFromElementChain(IUIAutomation* automation, IUIAutomationElement* element)
{
    if (!automation || !element) {
        return QString();
    }

    IUIAutomationTreeWalker* walker = nullptr;
    if (FAILED(automation->get_ControlViewWalker(&walker)) || !walker) {
        return QString();
    }

    IUIAutomationElement* current = element;
    current->AddRef();
    QString matchedName;

    while (current) {
        BSTR nameValue = nullptr;
        QString controlName;
        if (SUCCEEDED(current->get_CurrentName(&nameValue))) {
            controlName = bstrToQString(nameValue).trimmed();
        }

        logLookupStep(QStringLiteral("Inspect item name candidate: \"%1\"").arg(controlName));
        if (!controlName.isEmpty() && elementLooksLikeFileItem(current)) {
            matchedName = controlName;
            logLookupStep(QStringLiteral("Accepted item name candidate: \"%1\"").arg(matchedName));
            current->Release();
            walker->Release();
            return matchedName;
        }

        IUIAutomationElement* parent = nullptr;
        if (FAILED(walker->GetParentElement(current, &parent))) {
            current->Release();
            break;
        }

        current->Release();
        current = parent;
    }

    walker->Release();
    return matchedName;
}

void appendUniqueLookupText(QStringList* values, const QString& candidate)
{
    if (!values) {
        return;
    }

    const QString normalized = candidate.trimmed();
    if (normalized.isEmpty()) {
        return;
    }

    for (const QString& existing : *values) {
        if (existing.compare(normalized, Qt::CaseInsensitive) == 0) {
            return;
        }
    }

    values->append(normalized);
}

IUIAutomationElement* fileItemElementFromChain(IUIAutomation* automation, IUIAutomationElement* element)
{
    if (!automation || !element) {
        return nullptr;
    }

    IUIAutomationTreeWalker* walker = nullptr;
    if (FAILED(automation->get_ControlViewWalker(&walker)) || !walker) {
        return nullptr;
    }

    IUIAutomationElement* current = element;
    current->AddRef();

    while (current) {
        BSTR nameValue = nullptr;
        QString controlName;
        if (SUCCEEDED(current->get_CurrentName(&nameValue))) {
            controlName = bstrToQString(nameValue).trimmed();
        }

        if (!controlName.isEmpty() && elementLooksLikeFileItem(current)) {
            walker->Release();
            return current;
        }

        IUIAutomationElement* parent = nullptr;
        if (FAILED(walker->GetParentElement(current, &parent))) {
            current->Release();
            break;
        }

        current->Release();
        current = parent;
    }

    walker->Release();
    return nullptr;
}

QStringList lookupTextCandidatesFromElementContext(IUIAutomation* automation, IUIAutomationElement* element)
{
    QStringList values;
    if (!automation || !element) {
        return values;
    }

    IUIAutomationElement* itemElement = fileItemElementFromChain(automation, element);
    if (!itemElement) {
        appendUniqueLookupText(&values, itemNameFromElementChain(automation, element));
        return values;
    }

    IUIAutomationCondition* trueCondition = nullptr;
    if (SUCCEEDED(automation->CreateTrueCondition(&trueCondition)) && trueCondition) {
        IUIAutomationElementArray* array = nullptr;
        if (SUCCEEDED(itemElement->FindAll(TreeScope_Subtree, trueCondition, &array)) && array) {
            int length = 0;
            array->get_Length(&length);
            const int limit = qMin(length, 64);
            for (int index = 0; index < limit; ++index) {
                IUIAutomationElement* child = nullptr;
                if (FAILED(array->GetElement(index, &child)) || !child) {
                    continue;
                }

                BSTR nameValue = nullptr;
                if (SUCCEEDED(child->get_CurrentName(&nameValue))) {
                    appendUniqueLookupText(&values, bstrToQString(nameValue));
                }
                child->Release();
            }
            array->Release();
        }
        trueCondition->Release();
    }

    appendUniqueLookupText(&values, itemNameFromElementChain(automation, itemElement));
    itemElement->Release();
    return values;
}

QString explorerFolderItemPath(FolderItem* folderItem)
{
    if (!folderItem) {
        return QString();
    }

    BSTR pathValue = nullptr;
    QString resolvedPath;
    if (SUCCEEDED(folderItem->get_Path(&pathValue))) {
        resolvedPath = bstrToQString(pathValue).trimmed();
    }

    if (resolvedPath.isEmpty()) {
        return QString();
    }

    if (resolvedPath.startsWith(QStringLiteral("file:///"), Qt::CaseInsensitive)) {
        const QString localFile = QUrl(resolvedPath).toLocalFile();
        if (!localFile.isEmpty()) {
            return QDir::cleanPath(localFile);
        }
    }

    if (resolvedPath.contains(QLatin1Char('\\')) || resolvedPath.contains(QLatin1Char('/'))) {
        return QDir::cleanPath(resolvedPath);
    }

    return resolvedPath;
}

QString explorerShellPathForHoveredItem(HWND hwnd,
                                        const QString& preferredName,
                                        const QStringList& textCandidates)
{
    IWebBrowserApp* browser = explorerBrowserForWindow(hwnd);
    if (!browser) {
        logLookupStep(QStringLiteral("Explorer shell fallback skipped because browser lookup failed"));
        return QString();
    }

    IDispatch* document = nullptr;
    const HRESULT documentHr = browser->get_Document(&document);
    browser->Release();
    if (FAILED(documentHr) || !document) {
        logLookupStep(QStringLiteral("Explorer shell fallback skipped because document lookup failed"));
        return QString();
    }

    IShellFolderViewDual* shellView = nullptr;
    const HRESULT shellViewHr = document->QueryInterface(IID_PPV_ARGS(&shellView));
    document->Release();
    if (FAILED(shellViewHr) || !shellView) {
        logLookupStep(QStringLiteral("Explorer shell fallback skipped because shell view query failed"));
        return QString();
    }

    Folder* folder = nullptr;
    const HRESULT folderHr = shellView->get_Folder(&folder);
    shellView->Release();
    if (FAILED(folderHr) || !folder) {
        logLookupStep(QStringLiteral("Explorer shell fallback skipped because folder view did not expose items"));
        return QString();
    }

    FolderItems* items = nullptr;
    const HRESULT itemsHr = folder->Items(&items);
    folder->Release();
    if (FAILED(itemsHr) || !items) {
        logLookupStep(QStringLiteral("Explorer shell fallback skipped because folder items lookup failed"));
        return QString();
    }

    long count = 0;
    items->get_Count(&count);
    logLookupStep(QStringLiteral("Explorer shell fallback scanning %1 visible items").arg(count));

    int bestScore = 0;
    int bestScoreHits = 0;
    QString bestPath;

    for (long index = 0; index < count; ++index) {
        VARIANT variantIndex;
        VariantInit(&variantIndex);
        variantIndex.vt = VT_I4;
        variantIndex.lVal = index;

        FolderItem* folderItem = nullptr;
        const HRESULT itemHr = items->Item(variantIndex, &folderItem);
        VariantClear(&variantIndex);
        if (FAILED(itemHr) || !folderItem) {
            continue;
        }

        BSTR nameValue = nullptr;
        QString itemName;
        if (SUCCEEDED(folderItem->get_Name(&nameValue))) {
            itemName = bstrToQString(nameValue).trimmed();
        }

        const QString itemPath = explorerFolderItemPath(folderItem);
        folderItem->Release();
        if (itemPath.isEmpty()) {
            continue;
        }

        const QFileInfo fileInfo(itemPath);
        const QString fileName = fileInfo.fileName();
        const QString baseName = fileInfo.completeBaseName();
        const QString folderPath = fileInfo.absolutePath();

        int score = 0;
        if (!preferredName.isEmpty()) {
            if (fileName.compare(preferredName, Qt::CaseInsensitive) == 0 ||
                itemName.compare(preferredName, Qt::CaseInsensitive) == 0) {
                score += 100;
            } else if (baseName.compare(preferredName, Qt::CaseInsensitive) == 0) {
                score += 70;
            }
        }

        for (const QString& candidate : textCandidates) {
            if (candidate.compare(itemPath, Qt::CaseInsensitive) == 0) {
                score += 120;
            } else if (candidate.compare(folderPath, Qt::CaseInsensitive) == 0) {
                score += 90;
            } else if (candidate.compare(fileName, Qt::CaseInsensitive) == 0 ||
                       candidate.compare(itemName, Qt::CaseInsensitive) == 0) {
                score += 40;
            } else if (!candidate.isEmpty() &&
                       candidate.size() > 2 &&
                       folderPath.contains(candidate, Qt::CaseInsensitive)) {
                score += 30;
            }
        }

        if (score <= 0) {
            continue;
        }

        logLookupStep(QStringLiteral("Explorer shell candidate: score=%1 name=\"%2\" path=\"%3\"")
            .arg(score)
            .arg(itemName.isEmpty() ? fileName : itemName, itemPath));

        if (score > bestScore) {
            bestScore = score;
            bestScoreHits = 1;
            bestPath = itemPath;
        } else if (score == bestScore) {
            ++bestScoreHits;
        }
    }

    items->Release();

    if (bestScore <= 0) {
        logLookupStep(QStringLiteral("Explorer shell fallback found no matching item"));
        return QString();
    }

    if (bestScoreHits > 1 && bestScore < 120) {
        logLookupStep(QStringLiteral("Explorer shell fallback found ambiguous matches for the hovered item"));
        return QString();
    }

    logLookupStep(QStringLiteral("Explorer shell fallback success: %1").arg(bestPath));
    return bestPath;
}

QString explorerSelectedShellPath(HWND hwnd)
{
    IWebBrowserApp* browser = explorerBrowserForWindow(hwnd);
    if (!browser) {
        logLookupStep(QStringLiteral("Explorer selection fallback skipped because browser lookup failed"));
        return QString();
    }

    IDispatch* document = nullptr;
    const HRESULT documentHr = browser->get_Document(&document);
    browser->Release();
    if (FAILED(documentHr) || !document) {
        logLookupStep(QStringLiteral("Explorer selection fallback skipped because document lookup failed"));
        return QString();
    }

    IShellFolderViewDual* shellView = nullptr;
    const HRESULT shellViewHr = document->QueryInterface(IID_PPV_ARGS(&shellView));
    document->Release();
    if (FAILED(shellViewHr) || !shellView) {
        logLookupStep(QStringLiteral("Explorer selection fallback skipped because shell view query failed"));
        return QString();
    }

    FolderItems* selectedItems = nullptr;
    const HRESULT selectedHr = shellView->SelectedItems(&selectedItems);
    shellView->Release();
    if (FAILED(selectedHr) || !selectedItems) {
        logLookupStep(QStringLiteral("Explorer selection fallback skipped because selected items lookup failed"));
        return QString();
    }

    long count = 0;
    selectedItems->get_Count(&count);
    if (count != 1) {
        selectedItems->Release();
        logLookupStep(QStringLiteral("Explorer selection fallback ignored selected count=%1").arg(count));
        return QString();
    }

    VARIANT variantIndex;
    VariantInit(&variantIndex);
    variantIndex.vt = VT_I4;
    variantIndex.lVal = 0;

    FolderItem* selectedItem = nullptr;
    const HRESULT itemHr = selectedItems->Item(variantIndex, &selectedItem);
    VariantClear(&variantIndex);
    selectedItems->Release();
    if (FAILED(itemHr) || !selectedItem) {
        logLookupStep(QStringLiteral("Explorer selection fallback selected item query failed"));
        return QString();
    }

    const QString selectedPath = explorerFolderItemPath(selectedItem);
    selectedItem->Release();
    if (selectedPath.isEmpty()) {
        logLookupStep(QStringLiteral("Explorer selection fallback selected path is empty"));
        return QString();
    }

    logLookupStep(QStringLiteral("Explorer selection fallback success: %1").arg(selectedPath));
    return selectedPath;
}

QString filePathFromElementChain(IUIAutomation* automation, IUIAutomationElement* element, const QString& folderPath)
{
    if (!automation || !element || folderPath.isEmpty()) {
        return QString();
    }

    IUIAutomationTreeWalker* walker = nullptr;
    if (FAILED(automation->get_ControlViewWalker(&walker)) || !walker) {
        return QString();
    }

    IUIAutomationElement* current = element;
    current->AddRef();
    QString matchedPath;

    while (current) {
        BSTR nameValue = nullptr;
        QString controlName;
        if (SUCCEEDED(current->get_CurrentName(&nameValue))) {
            controlName = bstrToQString(nameValue);
        }

        logLookupStep(QStringLiteral("Inspect path candidate: \"%1\"").arg(controlName.trimmed()));
        if (!controlName.trimmed().isEmpty() && elementLooksLikeFileItem(current)) {
            matchedPath = exactOrFriendlyPathMatch(folderPath, controlName);
            if (!matchedPath.isEmpty()) {
                logLookupStep(QStringLiteral("Accepted filesystem path: %1").arg(matchedPath));
                current->Release();
                walker->Release();
                return matchedPath;
            }
        }

        IUIAutomationElement* parent = nullptr;
        if (FAILED(walker->GetParentElement(current, &parent))) {
            current->Release();
            break;
        }

        current->Release();
        current = parent;
    }

    walker->Release();
    return matchedPath;
}

QString desktopShellPathForHoveredName(const QString& hoveredName)
{
    const QString normalizedName = hoveredName.trimmed();
    if (normalizedName.isEmpty()) {
        logLookupStep(QStringLiteral("Skip desktop shell fallback because hovered name is empty"));
        return QString();
    }

    logLookupStep(QStringLiteral("Try desktop shell fallback for name: \"%1\"").arg(normalizedName));

    IShellFolder* desktopFolder = nullptr;
    if (FAILED(SHGetDesktopFolder(&desktopFolder)) || !desktopFolder) {
        return QString();
    }

    LPITEMIDLIST desktopPidl = nullptr;
    if (FAILED(SHGetSpecialFolderLocation(nullptr, CSIDL_DESKTOP, &desktopPidl)) || !desktopPidl) {
        desktopFolder->Release();
        return QString();
    }

    IEnumIDList* enumerator = nullptr;
    const HRESULT enumHr = desktopFolder->EnumObjects(
        nullptr,
        SHCONTF_FOLDERS | SHCONTF_NONFOLDERS | SHCONTF_INCLUDEHIDDEN,
        &enumerator);
    if (FAILED(enumHr) || !enumerator) {
        CoTaskMemFree(desktopPidl);
        desktopFolder->Release();
        return QString();
    }

    QString matchedPath;
    LPITEMIDLIST child = nullptr;
    ULONG fetched = 0;
    while (enumerator->Next(1, &child, &fetched) == S_OK) {
        STRRET displayNameValue{};
        wchar_t displayName[MAX_PATH] = {};
        if (SUCCEEDED(desktopFolder->GetDisplayNameOf(child, SHGDN_NORMAL, &displayNameValue)) &&
            SUCCEEDED(StrRetToBufW(&displayNameValue, child, displayName, MAX_PATH))) {
            const QString candidateName = QString::fromWCharArray(displayName).trimmed();
            if (candidateName.compare(normalizedName, Qt::CaseInsensitive) == 0) {
                PIDLIST_ABSOLUTE absolutePidl = ILCombine(desktopPidl, child);
                if (absolutePidl) {
                    PWSTR parsingName = nullptr;
                    if (SUCCEEDED(SHGetNameFromIDList(absolutePidl, SIGDN_DESKTOPABSOLUTEPARSING, &parsingName)) && parsingName) {
                        matchedPath = QString::fromWCharArray(parsingName).trimmed();
                        logLookupStep(QStringLiteral("Desktop shell fallback success: %1").arg(matchedPath));
                        CoTaskMemFree(parsingName);
                    }
                    CoTaskMemFree(absolutePidl);
                }
                CoTaskMemFree(child);
                break;
            }
        }

        CoTaskMemFree(child);
        child = nullptr;
        fetched = 0;
    }

    enumerator->Release();
    CoTaskMemFree(desktopPidl);
    desktopFolder->Release();
    if (matchedPath.isEmpty()) {
        logLookupStep(QStringLiteral("Desktop shell fallback failed for name: \"%1\"").arg(normalizedName));
    }
    return matchedPath;
}

HoveredItemInfo makeFailureInfo(const QString& statusMessage,
                                const QString& windowClassName = QString(),
                                const QString& sourceLabel = QString())
{
    HoveredItemInfo info;
    info.title = QStringLiteral("Space Look");
    info.statusMessage = statusMessage;
    info.windowClassName = windowClassName;
    info.sourceLabel = sourceLabel;
    return info;
}

} // namespace

DetectedTypeInfo FileTypeDetector::detectTypeInfo(const QString& filePath, bool shellItem) const
{
    DetectedTypeInfo info;
    const QString trimmedPath = filePath.trimmed();
    if (trimmedPath.isEmpty()) {
        info.typeKey = QStringLiteral("welcome");
        info.typeDetails = QStringLiteral("Ready to inspect an item under the mouse cursor.");
        info.rendererName = QStringLiteral("welcome");
        return info;
    }

    if (shellItem) {
        info.typeKey = QStringLiteral("shell_item");
        info.typeDetails = QStringLiteral("Windows shell namespace object.");
        info.rendererName = QStringLiteral("summary");
        return info;
    }

    const QFileInfo fileInfo(trimmedPath);
    if (fileInfo.isDir()) {
        info.typeKey = QStringLiteral("folder");
        info.typeDetails = QStringLiteral("File system folder.");
        info.rendererName = QStringLiteral("folder");
        info.isDirectory = true;
        return info;
    }

    const QString suffix = FileSuffixUtils::fullSuffix(fileInfo);
    const QStringList suffixCandidates = FileSuffixUtils::suffixCandidates(fileInfo);

    if (looksLikeMpegTransportStream(fileInfo)) {
        info.typeKey = QStringLiteral("video");
        info.typeDetails = QStringLiteral("MPEG transport stream video file.");
        info.rendererName = QStringLiteral("media");
        return info;
    }

    if (const auto detectedInfo = RenderTypeRegistry::instance().detectTypeInfoForSuffixCandidates(suffixCandidates)) {
        info = *detectedInfo;
        return info;
    }

    info.typeKey = suffix.isEmpty() ? QStringLiteral("file") : suffix;
    info.typeDetails = suffix.isEmpty()
        ? QStringLiteral("Generic file system file.")
        : QStringLiteral("File with extension .%1").arg(suffix);
    info.rendererName = QStringLiteral("summary");
    return info;
}

HoveredItemInfo FileTypeDetector::inspectPath(const QString& filePath,
                                              const QString& sourceLabel,
                                              const QString& windowClassName) const
{
    const QString trimmedPath = filePath.trimmed();
    logLookupStep(QStringLiteral("inspectPath input: \"%1\" source=\"%2\" window=\"%3\"")
        .arg(trimmedPath, sourceLabel, windowClassName));
    if (trimmedPath.isEmpty()) {
        HoveredItemInfo info;
        info.title = QStringLiteral("Space Look");
        info.typeKey = QStringLiteral("welcome");
        info.typeDetails = QStringLiteral("Ready to inspect an item under the mouse cursor.");
        info.rendererName = QStringLiteral("welcome");
        info.sourceKind = QStringLiteral("SpaceLook");
        info.statusMessage = QStringLiteral("Press Space to inspect the file system item under the mouse cursor.");
        return info;
    }

    if (isShellParsingPath(trimmedPath)) {
        const QString shellPath = normalizeShellParsingPath(trimmedPath);
        const QString fileSystemFolderPath = usersFilesFolderPathForShellPath(shellPath);
        PIDLIST_ABSOLUTE pidl = nullptr;
        const std::wstring shellSource = shellPath.toStdWString();
        if (SUCCEEDED(SHParseDisplayName(shellSource.c_str(), nullptr, &pidl, 0, nullptr)) && pidl) {
            PWSTR displayName = nullptr;
            QString title;
            if (SUCCEEDED(SHGetNameFromIDList(pidl, SIGDN_NORMALDISPLAY, &displayName)) && displayName) {
                title = QString::fromWCharArray(displayName).trimmed();
                CoTaskMemFree(displayName);
            }

            SFGAOF attributes = 0;
            bool isDirectory = !fileSystemFolderPath.isEmpty();
            IShellItem* shellItem = nullptr;
            if (SUCCEEDED(SHCreateItemFromIDList(pidl, IID_PPV_ARGS(&shellItem))) && shellItem) {
                if (SUCCEEDED(shellItem->GetAttributes(SFGAO_FOLDER | SFGAO_FILESYSTEM, &attributes))) {
                    const bool isFolderLike = (attributes & SFGAO_FOLDER) != 0;
                    const bool isFileSystemItem = (attributes & SFGAO_FILESYSTEM) != 0;
                    isDirectory = isDirectory || (isFolderLike && isFileSystemItem);
                }
                shellItem->Release();
            }

            const DetectedTypeInfo typeInfo = detectTypeInfo(shellPath, true);
            const QString displayPath = fileSystemFolderPath.isEmpty() ? shellPath : fileSystemFolderPath;

            HoveredItemInfo info;
            info.valid = true;
            info.exists = true;
            info.isDirectory = isDirectory;
            info.title = title.isEmpty() ? shellPath : title;
            info.typeKey = isDirectory ? QStringLiteral("shell_folder") : typeInfo.typeKey;
            info.typeDetails = isDirectory
                ? QStringLiteral("Windows shell namespace folder.")
                : typeInfo.typeDetails;
            info.rendererName = isDirectory ? QStringLiteral("folder") : typeInfo.rendererName;
            info.sourceKind = sourceLabel.trimmed().isEmpty() ? QStringLiteral("Unknown") : sourceLabel;
            info.filePath = displayPath;
            info.fileName = info.title;
            info.folderPath = fileSystemFolderPath.isEmpty()
                ? QStringLiteral("Desktop")
                : QFileInfo(fileSystemFolderPath).absolutePath();
            info.resolvedPath = fileSystemFolderPath;
            info.sourceLabel = sourceLabel;
            info.windowClassName = windowClassName;
            info.statusMessage = QStringLiteral("Loaded object information for a Windows shell item.");
            logLookupStep(QStringLiteral("inspectPath shell item success: title=\"%1\" path=\"%2\"")
                .arg(info.title, info.filePath));
            CoTaskMemFree(pidl);
            return info;
        }
        logLookupStep(QStringLiteral("inspectPath shell parsing failed: %1").arg(shellPath));
    }

    const QString cleanPath = QDir::cleanPath(trimmedPath);

    const QFileInfo fileInfo(cleanPath);
    const DetectedTypeInfo typeInfo = detectTypeInfo(cleanPath, false);
    const QString shortcutTargetPath = resolveShortcutTarget(cleanPath);
    const QFileInfo shortcutTargetInfo(shortcutTargetPath);
    if (!shortcutTargetPath.trimmed().isEmpty() &&
        shortcutTargetInfo.exists() &&
        shortcutTargetInfo.isDir()) {
        HoveredItemInfo info;
        info.valid = true;
        info.exists = true;
        info.isDirectory = true;
        info.title = fileInfo.fileName().isEmpty() ? shortcutTargetInfo.fileName() : fileInfo.fileName();
        info.typeKey = QStringLiteral("folder");
        info.typeDetails = QStringLiteral("Shortcut to a folder.");
        info.rendererName = QStringLiteral("folder");
        info.sourceKind = sourceLabel.trimmed().isEmpty() ? QStringLiteral("Unknown") : sourceLabel;
        info.filePath = QDir::cleanPath(shortcutTargetInfo.absoluteFilePath());
        info.fileName = shortcutTargetInfo.fileName();
        info.folderPath = shortcutTargetInfo.absolutePath();
        info.resolvedPath = info.filePath;
        info.sourceLabel = sourceLabel;
        info.windowClassName = windowClassName;
        info.statusMessage = QStringLiteral("Loaded folder information from the shortcut target.");
        logLookupStep(QStringLiteral("inspectPath shortcut folder result: link=\"%1\" target=\"%2\"")
            .arg(cleanPath, info.filePath));
        return info;
    }

    HoveredItemInfo info;
    info.valid = fileInfo.exists();
    info.exists = fileInfo.exists();
    info.isDirectory = typeInfo.isDirectory;
    info.title = fileInfo.fileName().isEmpty() ? cleanPath : fileInfo.fileName();
    info.typeKey = typeInfo.typeKey;
    info.typeDetails = typeInfo.typeDetails;
    info.rendererName = typeInfo.rendererName;
    info.sourceKind = sourceLabel.trimmed().isEmpty() ? QStringLiteral("Unknown") : sourceLabel;
    info.filePath = cleanPath;
    info.fileName = fileInfo.fileName();
    info.folderPath = fileInfo.absolutePath();
    info.resolvedPath = shortcutTargetPath;
    info.sourceLabel = sourceLabel;
    info.windowClassName = windowClassName;
    info.statusMessage = fileInfo.exists()
        ? QStringLiteral("Loaded object information from the detected path.")
        : QStringLiteral("The detected path does not exist.");
    logLookupStep(QStringLiteral("inspectPath filesystem result: exists=%1 dir=%2 path=\"%3\"")
        .arg(info.exists ? QStringLiteral("true") : QStringLiteral("false"),
             info.isDirectory ? QStringLiteral("true") : QStringLiteral("false"),
             info.filePath));
    return info;
}

HoveredItemInfo FileTypeDetector::inspectItemUnderCursor() const
{
    logLookupStep(QStringLiteral("begin inspectItemUnderCursor"));
    const HRESULT initHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool shouldUninitialize = SUCCEEDED(initHr);
    if (FAILED(initHr) && initHr != RPC_E_CHANGED_MODE) {
        logLookupStep(QStringLiteral("COM initialization failed"));
        return makeFailureInfo(QStringLiteral("COM initialization failed."));
    }

    POINT cursorPos{};
    if (!GetCursorPos(&cursorPos)) {
        if (shouldUninitialize) {
            CoUninitialize();
        }
        logLookupStep(QStringLiteral("GetCursorPos failed"));
        return makeFailureInfo(QStringLiteral("Cursor position is unavailable."));
    }
    logLookupStep(QStringLiteral("Cursor position: x=%1 y=%2").arg(cursorPos.x).arg(cursorPos.y));

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
        logLookupStep(QStringLiteral("UI Automation unavailable"));
        return makeFailureInfo(QStringLiteral("UI Automation is unavailable."));
    }

    IUIAutomationElement* element = nullptr;
    const HRESULT elementHr = automation->ElementFromPoint(cursorPos, &element);
    if (FAILED(elementHr) || !element) {
        const HWND pointWindow = WindowFromPoint(cursorPos);
        HWND rootWindow = GetAncestor(pointWindow, GA_ROOT);
        QString windowClass = classNameForWindow(rootWindow);
        if (!isSupportedDesktopRootClass(windowClass)) {
            QString scannedWindows;
            const HWND fallbackRootWindow = findUnderlyingSupportedRootWindow(cursorPos, &scannedWindows);
            logLookupStep(QStringLiteral("ElementFromPoint failed, underlying window scan: %1")
                .arg(scannedWindows.isEmpty() ? QStringLiteral("(none)") : scannedWindows));
            if (fallbackRootWindow) {
                rootWindow = fallbackRootWindow;
                windowClass = classNameForWindow(rootWindow);
            }
        }

        if (isSupportedDesktopRootClass(windowClass)) {
            if (IUIAutomationElement* hoveredElement = bestItemElementAtPointInRoot(automation, rootWindow, cursorPos)) {
                element = hoveredElement;
                logLookupStep(QStringLiteral("ElementFromPoint failed, recovered hovered item from root window: %1").arg(windowClass));
            } else if (IUIAutomationElement* fallbackElement = deepestElementAtPointInRoot(automation, rootWindow, cursorPos)) {
                element = fallbackElement;
                logLookupStep(QStringLiteral("ElementFromPoint failed, recovered deepest element from root window: %1").arg(windowClass));
            }
        }

        if (element) {
            logLookupStep(QStringLiteral("Continuing lookup with recovered hover element"));
        } else {
            if (windowClass == QStringLiteral("CabinetWClass")) {
                const QString selectedPath = explorerSelectedShellPath(rootWindow);
                if (!selectedPath.isEmpty()) {
                    automation->Release();
                    if (shouldUninitialize) {
                        CoUninitialize();
                    }
                    HoveredItemInfo info = inspectPath(
                        selectedPath,
                        QStringLiteral("ExplorerSelection"),
                        windowClass);
                    info.statusMessage = QStringLiteral("Detected the selected Explorer item after cursor lookup missed the hovered UI element.");
                    logLookupStep(QStringLiteral("end inspectItemUnderCursor via Explorer selection fallback"));
                    return info;
                }
            }

            automation->Release();
            if (shouldUninitialize) {
                CoUninitialize();
            }
            logLookupStep(QStringLiteral("No UI element under cursor"));
            return makeFailureInfo(QStringLiteral("No UI element is under the mouse cursor."));
        }
    }

    BSTR hoveredNameValue = nullptr;
    QString hoveredName;
    if (SUCCEEDED(element->get_CurrentName(&hoveredNameValue))) {
        hoveredName = bstrToQString(hoveredNameValue).trimmed();
    }

    BSTR hoveredLocalizedTypeValue = nullptr;
    QString hoveredLocalizedType;
    if (SUCCEEDED(element->get_CurrentLocalizedControlType(&hoveredLocalizedTypeValue))) {
        hoveredLocalizedType = bstrToQString(hoveredLocalizedTypeValue).trimmed();
    }

    BSTR hoveredClassValue = nullptr;
    QString hoveredClassName;
    if (SUCCEEDED(element->get_CurrentClassName(&hoveredClassValue))) {
        hoveredClassName = bstrToQString(hoveredClassValue).trimmed();
    }

    const HWND rootWindow = rootWindowFromElement(automation, element);
    QString windowClass = classNameForWindow(rootWindow);
    const QString rootExecutableName = executableNameForWindow(rootWindow);
    logLookupStep(QStringLiteral("Hovered control: name=\"%1\" localizedType=\"%2\" class=\"%3\" rootWindowClass=\"%4\"")
        .arg(hoveredName, hoveredLocalizedType, hoveredClassName, windowClass));
    if (!rootExecutableName.isEmpty()) {
        logLookupStep(QStringLiteral("Hovered root process: %1").arg(rootExecutableName));
    }

    if (windowBelongsToLinDesk(rootWindow)) {
        element->Release();
        automation->Release();
        if (shouldUninitialize) {
            CoUninitialize();
        }
        logLookupStep(QStringLiteral("Cursor is over LinDesk, skip local lookup and wait for IPC preview request"));
        return makeFailureInfo(
            QStringLiteral("The hovered window belongs to LinDesk. Waiting for its preview request."),
            windowClass,
            QStringLiteral("LinDesk"));
    }

    HWND effectiveRootWindow = rootWindow;
    IUIAutomationElement* effectiveElement = element;
    effectiveElement->AddRef();

    const bool initialSupportedRoot = isSupportedDesktopRootClass(windowClass);
    if (!initialSupportedRoot) {
        QString scannedWindows;
        const HWND fallbackRootWindow = findUnderlyingSupportedRootWindow(cursorPos, &scannedWindows);
        logLookupStep(QStringLiteral("Unsupported root window class: %1").arg(windowClass));
        logLookupStep(QStringLiteral("Underlying window scan: %1").arg(scannedWindows.isEmpty() ? QStringLiteral("(none)") : scannedWindows));
        if (fallbackRootWindow) {
            const QString fallbackClass = classNameForWindow(fallbackRootWindow);
            logLookupStep(QStringLiteral("Fallback root window: %1").arg(fallbackClass));
            if (IUIAutomationElement* fallbackElement = deepestElementAtPointInRoot(automation, fallbackRootWindow, cursorPos)) {
                effectiveRootWindow = fallbackRootWindow;
                windowClass = fallbackClass;
                effectiveElement->Release();
                effectiveElement = fallbackElement;
            } else {
                logLookupStep(QStringLiteral("Fallback root was found, but no child element matched the cursor point"));
            }
        }
    }

    if (IUIAutomationElement* preciseItemElement = bestItemElementAtPointInRoot(automation, effectiveRootWindow, cursorPos)) {
        logLookupStep(QStringLiteral("Using precise item element search inside root window"));
        effectiveElement->Release();
        effectiveElement = preciseItemElement;
    } else {
        logLookupStep(QStringLiteral("Precise item element search found no item candidate inside root window"));
    }

    const bool isDesktopWindow = windowClass == QStringLiteral("WorkerW") || windowClass == QStringLiteral("Progman");
    const bool isExplorerWindow = windowClass == QStringLiteral("CabinetWClass");
    if (!isDesktopWindow && !isExplorerWindow) {
        effectiveElement->Release();
        element->Release();
        automation->Release();
        if (shouldUninitialize) {
            CoUninitialize();
        }
        return makeFailureInfo(QStringLiteral("The hovered window is outside Explorer or the desktop."), windowClass);
    }

    const QString preferredName = itemNameFromElementChain(automation, effectiveElement);
    const QStringList lookupTexts = lookupTextCandidatesFromElementContext(automation, effectiveElement);
    logLookupStep(QStringLiteral("Hovered item name: \"%1\"").arg(preferredName));
    if (!lookupTexts.isEmpty()) {
        logLookupStep(QStringLiteral("Hovered item text candidates: %1").arg(lookupTexts.join(QStringLiteral(" | "))));
    }

    const QString folderPath = isDesktopWindow ? desktopFolderPath() : folderPathForExplorerWindow(effectiveRootWindow);
    logLookupStep(QStringLiteral("Resolved base folder: \"%1\"").arg(folderPath));
    if (folderPath.isEmpty() && isDesktopWindow) {
        effectiveElement->Release();
        element->Release();
        automation->Release();
        if (shouldUninitialize) {
            CoUninitialize();
        }
        logLookupStep(QStringLiteral("Base folder resolution failed"));
        return makeFailureInfo(
            QStringLiteral("The current Explorer location does not map to a filesystem folder."),
            windowClass,
            isDesktopWindow ? QStringLiteral("Desktop") : QStringLiteral("Explorer"));
    }

    const QString filePath = folderPath.isEmpty()
        ? QString()
        : filePathFromElementChain(automation, effectiveElement, folderPath);
    QString resolvedPath = filePath;
    if (resolvedPath.isEmpty() && isDesktopWindow) {
        logLookupStep(QStringLiteral("Filesystem match missed, desktop shell fallback name: \"%1\"").arg(preferredName));
        resolvedPath = desktopShellPathForHoveredName(preferredName);
    }
    if (resolvedPath.isEmpty() && isExplorerWindow) {
        logLookupStep(QStringLiteral("Filesystem match missed, trying Explorer shell fallback"));
        resolvedPath = explorerShellPathForHoveredItem(effectiveRootWindow, preferredName, lookupTexts);
    }
    effectiveElement->Release();
    element->Release();
    automation->Release();
    if (shouldUninitialize) {
        CoUninitialize();
    }

    if (resolvedPath.isEmpty()) {
        logLookupStep(QStringLiteral("No object matched under cursor"));
        return makeFailureInfo(
            QStringLiteral("No desktop or file system item was matched under the mouse cursor."),
            windowClass,
            isDesktopWindow ? QStringLiteral("Desktop") : QStringLiteral("Explorer"));
    }

    logLookupStep(QStringLiteral("Final resolved path: %1").arg(resolvedPath));

    HoveredItemInfo info = inspectPath(
        resolvedPath,
        isDesktopWindow ? QStringLiteral("Desktop") : QStringLiteral("Explorer"),
        windowClass);
    info.statusMessage = QStringLiteral("Detected the item under the mouse cursor and analyzed its type.");
    logLookupStep(QStringLiteral("end inspectItemUnderCursor"));
    return info;
}
