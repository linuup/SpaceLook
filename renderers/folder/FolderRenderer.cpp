#include "renderers/folder/FolderRenderer.h"

#include <QDebug>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QProcess>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

#include <Windows.h>
#include <ShObjIdl.h>

#include "renderers/FileTypeIconResolver.h"
#include "renderers/OpenWithButton.h"
#include "renderers/PreviewHeaderBar.h"
#include "renderers/PreviewStateVisuals.h"
#include "renderers/SelectableTitleLabel.h"
#include "widgets/SpaceLookWindow.h"

namespace {

constexpr int kItemPathRole = Qt::UserRole + 1;
constexpr int kPlaceholderRole = Qt::UserRole + 2;
constexpr int kLoadingRole = Qt::UserRole + 3;
constexpr int kLoadedRole = Qt::UserRole + 4;
constexpr int kRenameOldPathRole = Qt::UserRole + 5;
constexpr int kRenameOldNameRole = Qt::UserRole + 6;
constexpr int kEntrySizeRole = Qt::UserRole + 7;
constexpr int kEntryModifiedRole = Qt::UserRole + 8;
constexpr int kExpansionPathRole = Qt::UserRole + 9;

QString normalizedFolderPath(const QString& path)
{
    QString normalized = QDir::cleanPath(path.trimmed());
    normalized.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return normalized;
}

QIcon iconForFolderEntry(const QString& entryPath, bool isDirectory)
{
    HoveredItemInfo info;
    info.filePath = entryPath;
    info.isDirectory = isDirectory;
    info.typeKey = isDirectory ? QStringLiteral("folder") : QStringLiteral("file");
    return FileTypeIconResolver::iconForInfo(info);
}

bool openItemWithDefaultApp(const QString& path)
{
    const QString nativePath = QDir::toNativeSeparators(path);
    const HINSTANCE result = ShellExecuteW(nullptr,
                                           L"open",
                                           reinterpret_cast<LPCWSTR>(nativePath.utf16()),
                                           nullptr,
                                           nullptr,
                                           SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(result) > 32;
}

bool openItemInExplorer(const QString& path)
{
    const QString trimmedPath = path.trimmed();
    if (trimmedPath.isEmpty()) {
        return false;
    }

    const QFileInfo fileInfo(trimmedPath);
    if (fileInfo.isDir()) {
        return QProcess::startDetached(QStringLiteral("explorer.exe"),
                                       { QDir::toNativeSeparators(trimmedPath) });
    }

    return QProcess::startDetached(QStringLiteral("explorer.exe"),
                                   { QStringLiteral("/select,"), QDir::toNativeSeparators(trimmedPath) });
}

QString resolveFolderShortcutTarget(const QString& shortcutPath)
{
    if (!shortcutPath.endsWith(QStringLiteral(".lnk"), Qt::CaseInsensitive)) {
        return QString();
    }

    const HRESULT coHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool shouldUninitializeCom = SUCCEEDED(coHr);
    IShellLinkW* shellLink = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&shellLink));
    if (FAILED(hr) || !shellLink) {
        if (shouldUninitializeCom) {
            CoUninitialize();
        }
        return QString();
    }

    IPersistFile* persistFile = nullptr;
    hr = shellLink->QueryInterface(IID_PPV_ARGS(&persistFile));
    if (FAILED(hr) || !persistFile) {
        shellLink->Release();
        if (shouldUninitializeCom) {
            CoUninitialize();
        }
        return QString();
    }

    const std::wstring nativePath = QDir::toNativeSeparators(shortcutPath).toStdWString();
    hr = persistFile->Load(nativePath.c_str(), STGM_READ | STGM_SHARE_DENY_NONE);
    QString result;
    if (SUCCEEDED(hr)) {
        wchar_t resolvedPath[MAX_PATH] = {};
        WIN32_FIND_DATAW findData{};
        if (SUCCEEDED(shellLink->GetPath(resolvedPath, MAX_PATH, &findData, SLGP_RAWPATH)) && resolvedPath[0] != L'\0') {
            const QFileInfo targetInfo(QDir::cleanPath(QString::fromWCharArray(resolvedPath)));
            if (targetInfo.exists() && targetInfo.isDir()) {
                result = normalizedFolderPath(targetInfo.absoluteFilePath());
            }
        }
    }

    persistFile->Release();
    shellLink->Release();
    if (shouldUninitializeCom) {
        CoUninitialize();
    }
    return result;
}

QTreeWidgetItem* findChildItemByPath(QTreeWidgetItem* item, const QString& folderPath)
{
    if (!item) {
        return nullptr;
    }

    if (item->data(0, kItemPathRole).toString() == folderPath) {
        return item;
    }

    for (int index = 0; index < item->childCount(); ++index) {
        if (QTreeWidgetItem* match = findChildItemByPath(item->child(index), folderPath)) {
            return match;
        }
    }

    return nullptr;
}

QTreeWidgetItem* findTreeItemByPath(QTreeWidget* treeWidget, const QString& folderPath)
{
    if (!treeWidget || folderPath.trimmed().isEmpty()) {
        return nullptr;
    }

    for (int index = 0; index < treeWidget->topLevelItemCount(); ++index) {
        if (QTreeWidgetItem* match = findChildItemByPath(treeWidget->topLevelItem(index), folderPath)) {
            return match;
        }
    }

    return nullptr;
}

bool folderItemHasLoadedChildren(const QTreeWidgetItem* item)
{
    return item && item->data(0, kLoadedRole).toBool();
}

bool folderItemIsLoadingChildren(const QTreeWidgetItem* item)
{
    return item && item->data(0, kLoadingRole).toBool();
}

bool treeItemHasOnlyPlaceholderChild(const QTreeWidgetItem* item)
{
    return item && item->childCount() == 1 && item->child(0)->data(0, kPlaceholderRole).toBool();
}

void ensureFolderLoadingPlaceholder(QTreeWidgetItem* item)
{
    if (!item || folderItemHasLoadedChildren(item) || folderItemIsLoadingChildren(item)) {
        return;
    }

    item->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
    if (item->childCount() > 0 && !treeItemHasOnlyPlaceholderChild(item)) {
        return;
    }

    if (treeItemHasOnlyPlaceholderChild(item)) {
        return;
    }

    auto* placeholder = new QTreeWidgetItem();
    placeholder->setText(0, QCoreApplication::translate("SpaceLook", "Loading..."));
    placeholder->setData(0, Qt::UserRole, false);
    placeholder->setData(0, kItemPathRole, QString());
    placeholder->setData(0, kPlaceholderRole, true);
    item->addChild(placeholder);
}

FolderRenderer::FolderLoadResult loadFolderEntries(const QString& filePath,
                                                   const QString& folderPath,
                                                   const PreviewCancellationToken& cancelToken,
                                                   bool recursive = false)
{
    Q_UNUSED(folderPath)

    FolderRenderer::FolderLoadResult result;
    if (previewCancellationRequested(cancelToken)) {
        result.statusMessage = QCoreApplication::translate("SpaceLook", "Folder preview was canceled.");
        return result;
    }

    const QString trimmedPath = QDir::cleanPath(filePath.trimmed());
    if (trimmedPath.isEmpty()) {
        result.statusMessage = QCoreApplication::translate("SpaceLook", "Folder path is unavailable.");
        return result;
    }

    QFileInfo rootInfo(trimmedPath);
    if (!rootInfo.exists() || !rootInfo.isDir()) {
        qDebug().noquote() << QStringLiteral("[SpaceLookRender] FolderRenderer cannot open folder path=\"%1\" exists=%2 dir=%3")
            .arg(trimmedPath)
            .arg(rootInfo.exists())
            .arg(rootInfo.isDir());
        result.statusMessage = QCoreApplication::translate("SpaceLook", "The folder could not be opened.");
        return result;
    }

    QVector<QString> pendingFolders;
    pendingFolders.append(rootInfo.absoluteFilePath());

    while (!pendingFolders.isEmpty()) {
        if (previewCancellationRequested(cancelToken)) {
            result.entries.clear();
            result.statusMessage = QCoreApplication::translate("SpaceLook", "Folder preview was canceled.");
            return result;
        }

        const QString currentFolder = pendingFolders.takeFirst();
        const QDir currentDir(currentFolder);
        const QFileInfoList entries = currentDir.entryInfoList(
            QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
            QDir::Name | QDir::IgnoreCase);

        for (const QFileInfo& entryInfo : entries) {
            if (previewCancellationRequested(cancelToken)) {
                result.entries.clear();
                result.statusMessage = QCoreApplication::translate("SpaceLook", "Folder preview was canceled.");
                return result;
            }

            FolderRenderer::FolderEntry entry;
            entry.name = entryInfo.fileName();
            entry.path = normalizedFolderPath(entryInfo.absoluteFilePath());
            entry.expansionPath = entry.path;
            const QString shortcutTargetPath = resolveFolderShortcutTarget(entry.path);
            entry.isDirectory = entryInfo.isDir() || !shortcutTargetPath.isEmpty();
            if (!shortcutTargetPath.isEmpty()) {
                entry.expansionPath = shortcutTargetPath;
                const QString targetName = QFileInfo(shortcutTargetPath).fileName();
                if (!targetName.trimmed().isEmpty()) {
                    entry.name = targetName;
                }
            }
            entry.sizeBytes = entry.isDirectory ? 0 : entryInfo.size();
            entry.lastModified = entryInfo.lastModified();
            result.entries.append(entry);

            if (recursive && entryInfo.isDir()) {
                pendingFolders.append(entry.path);
            }
        }

        if (!recursive) {
            break;
        }
    }

    result.success = true;
    return result;
}

}

FolderRenderer::FolderRenderer(QWidget* parent)
    : QWidget(parent)
    , m_headerRow(new QWidget(this))
    , m_iconLabel(new QLabel(this))
    , m_titleLabel(new SelectableTitleLabel(this))
    , m_metaLabel(new QLabel(this))
    , m_pathRow(new QWidget(this))
    , m_pathTitleLabel(new QLabel(this))
    , m_pathValueLabel(new QLabel(this))
    , m_openWithButton(new OpenWithButton(this))
    , m_statusLabel(new QLabel(this))
    , m_treeWidget(new QTreeWidget(this))
{
    setAttribute(Qt::WA_StyledBackground, true);
    setObjectName(QStringLiteral("FolderRendererRoot"));
    m_headerRow->setObjectName(QStringLiteral("FolderHeaderRow"));
    m_iconLabel->setObjectName(QStringLiteral("FolderTypeIcon"));
    m_titleLabel->setObjectName(QStringLiteral("FolderTitle"));
    m_metaLabel->setObjectName(QStringLiteral("FolderMeta"));
    m_pathRow->setObjectName(QStringLiteral("FolderPathRow"));
    m_pathTitleLabel->setObjectName(QStringLiteral("FolderPathTitle"));
    m_pathValueLabel->setObjectName(QStringLiteral("FolderPathValue"));
    m_openWithButton->setObjectName(QStringLiteral("FolderOpenWithButton"));
    m_statusLabel->setObjectName(QStringLiteral("FolderStatus"));
    m_searchPanel = new QWidget(this);
    m_searchPanel->setObjectName(QStringLiteral("FolderSearchPanel"));
    m_nameSearchEdit = new QLineEdit(m_searchPanel);
    m_nameSearchEdit->setObjectName(QStringLiteral("FolderNameSearchEdit"));
    m_clearFiltersButton = new QPushButton(m_searchPanel);
    m_clearFiltersButton->setObjectName(QStringLiteral("FolderClearFiltersButton"));
    m_treeWidget->setObjectName(QStringLiteral("FolderTree"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 0, 12, 12);
    layout->setSpacing(12);
    layout->addWidget(m_headerRow);
    layout->addWidget(m_statusLabel);
    layout->addWidget(m_searchPanel);
    layout->addWidget(m_treeWidget, 1);

    auto* headerLayout = new QHBoxLayout(m_headerRow);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(12);
    auto* titleBlock = new PreviewHeaderBar(m_iconLabel, m_titleLabel, m_pathRow, m_openWithButton, m_headerRow);
    headerLayout->addWidget(titleBlock->contentWidget(), 1);

    auto* pathLayout = new QHBoxLayout(m_pathRow);
    pathLayout->setContentsMargins(0, 0, 0, 0);
    pathLayout->setSpacing(8);
    pathLayout->addWidget(m_pathValueLabel, 1);

    m_iconLabel->setFixedSize(72, 72);
    m_iconLabel->setScaledContents(false);
    m_iconLabel->setAlignment(Qt::AlignCenter);
    m_titleLabel->setWordWrap(true);
    m_pathTitleLabel->hide();
    m_pathValueLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_pathValueLabel->setWordWrap(true);
    m_pathValueLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_pathRow->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    PreviewStateVisuals::prepareStatusLabel(m_statusLabel);
    m_statusLabel->hide();

    auto* searchLayout = new QHBoxLayout(m_searchPanel);
    searchLayout->setContentsMargins(12, 10, 12, 10);
    searchLayout->setSpacing(8);
    m_nameSearchEdit->setPlaceholderText(QCoreApplication::translate("SpaceLook", "Search file name"));
    m_clearFiltersButton->setText(QCoreApplication::translate("SpaceLook", "Clear"));
    m_clearFiltersButton->setMinimumWidth(82);
    m_nameSearchEdit->installEventFilter(this);
    searchLayout->addWidget(m_nameSearchEdit, 2);
    searchLayout->addWidget(m_clearFiltersButton);
    setSearchPanelVisible(false);

    m_treeWidget->setColumnCount(1);
    m_treeWidget->setHeaderHidden(true);
    m_treeWidget->header()->setStretchLastSection(true);
    m_treeWidget->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_treeWidget->setRootIsDecorated(true);
    m_treeWidget->setUniformRowHeights(true);
    m_treeWidget->setAlternatingRowColors(false);
    m_treeWidget->setAnimated(true);
    m_treeWidget->setIconSize(QSize(22, 22));
    m_treeWidget->setIndentation(24);
    m_treeWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_treeWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    m_treeWidget->installEventFilter(this);
    m_treeWidget->viewport()->installEventFilter(this);
    connect(m_treeWidget, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem* item) {
        if (!item || !item->data(0, Qt::UserRole).toBool()) {
            return;
        }

        if (isFolderItemLoaded(item) || isFolderItemLoading(item)) {
            return;
        }

        const QString folderPath = folderPathForItem(item);
        if (folderPath.trimmed().isEmpty()) {
            return;
        }
        const QString expansionPath = expansionPathForItem(item);

        const PreviewLoadGuard::Token loadToken = m_loadGuard.observe(folderPath);
        QTimer::singleShot(0, this, [this, folderPath, expansionPath, loadToken]() {
            if (!m_loadGuard.isCurrentGeneration(loadToken)) {
                return;
            }
            QTreeWidgetItem* currentItem = folderItemForPath(folderPath);
            if (!currentItem || !currentItem->data(0, Qt::UserRole).toBool()) {
                return;
            }
            if (isFolderItemLoaded(currentItem) || isFolderItemLoading(currentItem)) {
                return;
            }
            loadFolderChildren(folderPath, expansionPath, currentItem);
        });
    });
    connect(m_treeWidget, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int) {
        if (!item) {
            return;
        }

        openTreeItem(item);
    });
    connect(m_treeWidget, &QTreeWidget::customContextMenuRequested, this, &FolderRenderer::showItemContextMenu);
    connect(m_treeWidget, &QTreeWidget::itemChanged, this, &FolderRenderer::handleInlineRename);
    connect(m_nameSearchEdit, &QLineEdit::textChanged, this, [this]() {
        scheduleSearchRefresh();
    });
    connect(m_clearFiltersButton, &QPushButton::clicked, this, [this]() {
        clearSearchFilters();
    });

    m_openWithButton->setStatusCallback([this](const QString& message) {
        showStatusMessage(message);
    });
    m_openWithButton->setLaunchSuccessCallback([this]() {
        if (SpaceLookWindow* previewWindow = qobject_cast<SpaceLookWindow*>(window())) {
            previewWindow->hidePreview();
        }
    });
    connect(titleBlock->closeButton(), &QToolButton::clicked, this, [this]() {
        if (SpaceLookWindow* previewWindow = qobject_cast<SpaceLookWindow*>(window())) {
            previewWindow->hidePreview();
        }
    });
    connect(m_titleLabel, &SelectableTitleLabel::copyFeedbackRequested, this, [this](const QString& message) {
        showStatusMessage(message);
    });

    applyChrome();
}

QString FolderRenderer::rendererId() const
{
    return QStringLiteral("folder");
}

bool FolderRenderer::canHandle(const HoveredItemInfo& info) const
{
    return info.typeKey == QStringLiteral("folder") ||
        info.typeKey == QStringLiteral("shell_folder");
}

QWidget* FolderRenderer::widget()
{
    return this;
}

bool FolderRenderer::reportsLoadingState() const
{
    return true;
}

void FolderRenderer::setLoadingStateCallback(std::function<void(bool)> callback)
{
    m_loadingStateCallback = std::move(callback);
}

void FolderRenderer::load(const HoveredItemInfo& info)
{
    cancelPreviewTask(m_cancelToken);
    const PreviewCancellationToken cancelToken = makePreviewCancellationToken();
    m_cancelToken = cancelToken;
    m_info = info;
    const PreviewLoadGuard::Token loadToken = m_loadGuard.begin(info.filePath);
    notifyLoadingState(true);

    qDebug().noquote() << QStringLiteral("[SpaceLookRender] FolderRenderer load path=\"%1\" typeKey=\"%2\"")
        .arg(info.filePath, info.typeKey);

    m_titleLabel->setText(info.title.isEmpty() ? QCoreApplication::translate("SpaceLook", "Folder Preview") : info.title);
    m_titleLabel->setCopyText(m_titleLabel->text());
    m_iconLabel->setPixmap(FileTypeIconResolver::pixmapForInfo(info, m_iconLabel->contentsRect().size()));
    m_pathValueLabel->setText(info.filePath.trimmed().isEmpty() ? QCoreApplication::translate("SpaceLook", "(Unavailable)") : info.filePath);
    m_openWithButton->setTargetContext(info.filePath, info.typeKey);
    cancelInlineRename();
    m_treeWidget->clear();
    m_rootEntries.clear();
    m_searchEntries.clear();
    m_recursiveSearchReady = false;
    m_recursiveSearchRunning = false;
    m_rootEntryCount = -1;
    m_folderCountStatus.clear();
    ++m_statusMessageSerial;
    PreviewStateVisuals::showStatus(m_statusLabel, QCoreApplication::translate("SpaceLook", "Loading folder contents..."), PreviewStateVisuals::Kind::Loading);

    auto* watcher = new QFutureWatcher<FolderLoadResult>(this);
    connect(watcher, &QFutureWatcher<FolderLoadResult>::finished, this, [this, watcher, loadToken, cancelToken]() {
        watcher->deleteLater();

        if (previewCancellationRequested(cancelToken) ||
            !m_loadGuard.isCurrent(loadToken, m_info.filePath) ||
            !isVisible()) {
            qDebug().noquote() << QStringLiteral("[SpaceLookRender] FolderRenderer discarded stale result path=\"%1\"")
                .arg(loadToken.path);
            return;
        }

        const FolderLoadResult result = watcher->result();
        if (!result.success) {
            PreviewStateVisuals::showStatus(m_statusLabel, result.statusMessage, PreviewStateVisuals::Kind::Error);
            notifyLoadingState(false);
            return;
        }

        m_rootEntries = result.entries;
        refreshRootView();

        qDebug().noquote() << QStringLiteral("[SpaceLookRender] FolderRenderer entries=%1 path=\"%2\"")
            .arg(result.entries.size())
            .arg(loadToken.path);
        notifyLoadingState(false);
    });

    watcher->setFuture(QtConcurrent::run([filePath = info.filePath, cancelToken]() {
        return loadFolderEntries(filePath, filePath, cancelToken);
    }));
}

void FolderRenderer::unload()
{
    cancelPreviewTask(m_cancelToken);
    m_loadGuard.cancel();
    notifyLoadingState(false);
    cancelInlineRename();
    m_treeWidget->clear();
    m_rootEntries.clear();
    m_searchEntries.clear();
    m_recursiveSearchReady = false;
    m_recursiveSearchRunning = false;
    m_pathValueLabel->clear();
    m_openWithButton->setTargetContext(QString(), QString());
    PreviewStateVisuals::clearStatus(m_statusLabel);
    m_rootEntryCount = -1;
    m_folderCountStatus.clear();
    ++m_statusMessageSerial;
    m_info = HoveredItemInfo();
}

bool FolderRenderer::eventFilter(QObject* watched, QEvent* event)
{
    if (event && event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if ((watched == m_treeWidget || watched == m_treeWidget->viewport() || watched == m_nameSearchEdit) &&
            keyEvent->matches(QKeySequence::Find)) {
            setSearchPanelVisible(true);
            if (m_nameSearchEdit) {
                m_nameSearchEdit->setFocus(Qt::ShortcutFocusReason);
                m_nameSearchEdit->selectAll();
            }
            return true;
        }

        if (watched == m_nameSearchEdit &&
            keyEvent->key() == Qt::Key_Escape) {
            const bool hasFilters = m_nameSearchEdit && !m_nameSearchEdit->text().trimmed().isEmpty();
            if (!hasFilters) {
                setSearchPanelVisible(false);
                if (m_treeWidget) {
                    m_treeWidget->setFocus(Qt::ShortcutFocusReason);
                }
            }
            return true;
        }

        if ((watched == m_treeWidget || watched == m_treeWidget->viewport()) &&
            keyEvent->key() == Qt::Key_Space &&
            keyEvent->modifiers() == Qt::NoModifier) {
            return previewHoveredOrCurrentItem();
        }

        if ((watched == m_treeWidget || watched == m_treeWidget->viewport()) && keyEvent->key() == Qt::Key_F2) {
            renameTreeItem(m_treeWidget ? m_treeWidget->currentItem() : nullptr);
            return true;
        }
        if (watched == m_renameEditor) {
            if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
                confirmInlineRename();
                return true;
            }
            if (keyEvent->key() == Qt::Key_Escape) {
                cancelInlineRename();
                return true;
            }
        }
    }

    return QWidget::eventFilter(watched, event);
}

void FolderRenderer::notifyLoadingState(bool loading)
{
    if (m_loadingStateCallback) {
        m_loadingStateCallback(loading);
    }
}

bool FolderRenderer::previewHoveredOrCurrentItem()
{
    if (m_nameSearchEdit && m_nameSearchEdit->hasFocus()) {
        return false;
    }
    if (m_renameEditor && m_renameEditor->hasFocus()) {
        return false;
    }

    QTreeWidgetItem* item = nullptr;
    if (m_treeWidget) {
        item = m_treeWidget->itemAt(m_treeWidget->viewport()->mapFromGlobal(QCursor::pos()));
    }

    return previewTreeItem(item);
}

void FolderRenderer::applyChrome()
{
    setStyleSheet(
        "#FolderRendererRoot {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:1,"
        "      stop:0 #fcfdff,"
        "      stop:1 #f4f8fc);"
        "  border-radius: 0px;"
        "}"
        "QLabel {"
        "  color: #18324a;"
        "}"
        "#FolderTypeIcon {"
        "  background: transparent;"
        "  border: none;"
        "  padding: 0px;"
        "}"
        "#FolderTitle {"
        "  color: #0f2740;"
        "}"
        "#FolderPathValue {"
        "  color: #445d76;"
        "}"
        "#FolderOpenWithButton QToolButton {"
        "  background: rgba(238, 244, 252, 0.96);"
        "  color: #17324b;"
        "  border: 1px solid rgba(193, 208, 224, 0.95);"
        "  padding: 3px 8px;"
        "  min-height: 24px;"
        "}"
        "#FolderOpenWithButton QToolButton:hover {"
        "  background: rgba(245, 249, 255, 1.0);"
        "}"
        "#FolderOpenWithButton QToolButton:pressed {"
        "  background: rgba(224, 234, 246, 1.0);"
        "}"
        "#FolderOpenWithButton #OpenWithPrimaryButton {"
        "  border-top-left-radius: 10px;"
        "  border-bottom-left-radius: 10px;"
        "  border-top-right-radius: 0px;"
        "  border-bottom-right-radius: 0px;"
        "  min-width: 28px;"
        "}"
        "#FolderOpenWithButton #OpenWithExpandButton {"
        "  border-left: none;"
        "  border-top-left-radius: 0px;"
        "  border-bottom-left-radius: 0px;"
        "  border-top-right-radius: 10px;"
        "  border-bottom-right-radius: 10px;"
        "  min-width: 22px;"
        "  padding-left: 5px;"
        "  padding-right: 5px;"
        "}"
        "#FolderStatus {"
        "  color: #27568b;"
        "  background: rgba(220, 235, 255, 0.92);"
        "  border: 1px solid rgba(164, 193, 229, 0.95);"
        "  border-radius: 12px;"
        "  padding: 10px 14px;"
        "}"
        "#FolderTree {"
        "  background: #f4f7fb;"
        "  border: 1px solid #ccd6e2;"
        "  border-radius: 18px;"
        "  color: #18324a;"
        "  padding: 10px 8px;"
        "  outline: none;"
        "}"
        "#FolderTree::item {"
        "  min-height: 32px;"
        "  padding: 3px 8px;"
        "  border-radius: 9px;"
        "}"
        "#FolderTree::item:hover {"
        "  background: rgba(222, 235, 250, 0.92);"
        "}"
        "#FolderTree::item:selected {"
        "  background: rgba(126, 188, 255, 0.30);"
        "  color: #08233b;"
        "}"
        "#FolderRenameWidget {"
        "  background: rgba(255, 255, 255, 0.92);"
        "  border: 1px solid rgba(132, 188, 255, 0.95);"
        "  border-radius: 12px;"
        "}"
        "#FolderRenameEdit {"
        "  background: transparent;"
        "  border: none;"
        "  color: #102a42;"
        "  selection-background-color: #cfe3ff;"
        "  selection-color: #102a42;"
        "  padding: 2px 4px;"
        "}"
        "#FolderRenameButton {"
        "  background: rgba(231, 241, 255, 0.96);"
        "  border: 1px solid rgba(184, 207, 235, 0.95);"
        "  border-radius: 9px;"
        "  color: #18324a;"
        "  padding: 2px 8px;"
        "}"
        "#FolderRenameButton:hover {"
        "  background: rgba(213, 231, 255, 1.0);"
        "}"
        "#FolderTree QHeaderView::section {"
        "  background: rgba(232, 239, 247, 0.96);"
        "  color: #35506b;"
        "  border: none;"
        "  border-bottom: 1px solid rgba(204, 214, 226, 0.95);"
        "  padding: 8px 10px;"
        "  font-family: 'Segoe UI Variable Text', 'Segoe UI';"
        "}"
        "#FolderTree QScrollBar:vertical {"
        "  background: rgba(232, 238, 245, 0.8);"
        "  border: none;"
        "  width: 8px;"
        "  margin: 10px 0 10px 0;"
        "  border-radius: 4px;"
        "}"
        "#FolderTree QScrollBar::handle:vertical {"
        "  background: #7c8fa8;"
        "  min-height: 52px;"
        "  border-radius: 4px;"
        "}"
        "#FolderTree QScrollBar::handle:vertical:hover {"
        "  background: #6d829d;"
        "}"
        "#FolderTree QScrollBar::handle:vertical:pressed {"
        "  background: #61768f;"
        "}"
        "#FolderTree QScrollBar:horizontal {"
        "  background: rgba(232, 238, 245, 0.8);"
        "  border: none;"
        "  height: 8px;"
        "  margin: 0 10px 0 10px;"
        "  border-radius: 4px;"
        "}"
        "#FolderTree QScrollBar::handle:horizontal {"
        "  background: #7c8fa8;"
        "  min-width: 52px;"
        "  border-radius: 4px;"
        "}"
        "#FolderTree QScrollBar::handle:horizontal:hover {"
        "  background: #6d829d;"
        "}"
        "#FolderTree QScrollBar::handle:horizontal:pressed {"
        "  background: #61768f;"
        "}"
        "#FolderTree QScrollBar::add-line:vertical, #FolderTree QScrollBar::sub-line:vertical,"
        "#FolderTree QScrollBar::add-line:horizontal, #FolderTree QScrollBar::sub-line:horizontal {"
        "  width: 0px;"
        "  height: 0px;"
        "}"
        "#FolderTree QScrollBar::add-page:vertical, #FolderTree QScrollBar::sub-page:vertical,"
        "#FolderTree QScrollBar::add-page:horizontal, #FolderTree QScrollBar::sub-page:horizontal {"
        "  background: transparent;"
        "}"
        "#FolderSearchPanel {"
        "  background: rgba(244, 250, 255, 0.9);"
        "  border: 1px solid rgba(196, 214, 232, 0.9);"
        "  border-radius: 14px;"
        "}"
        "#FolderSearchPanel QLineEdit {"
        "  background: rgba(255, 255, 255, 0.95);"
        "  border: 1px solid rgba(190, 210, 230, 0.95);"
        "  border-radius: 9px;"
        "  padding: 6px 10px;"
        "  color: #18324a;"
        "}"
        "#FolderSearchPanel QLineEdit:focus {"
        "  border-color: rgba(84, 148, 214, 0.95);"
        "}"
        "#FolderClearFiltersButton {"
        "  background: rgba(231, 241, 255, 0.96);"
        "  border: 1px solid rgba(184, 207, 235, 0.95);"
        "  border-radius: 9px;"
        "  color: #18324a;"
        "  padding: 4px 10px;"
        "}"
        "#FolderClearFiltersButton:hover {"
        "  background: rgba(213, 231, 255, 1.0);"
        "}"
    );

    QFont titleFont;
    titleFont.setFamilies({ QStringLiteral("Segoe UI Variable Display"), QStringLiteral("Segoe UI") });
    titleFont.setPixelSize(21);
    titleFont.setWeight(QFont::DemiBold);
    m_titleLabel->setFont(titleFont);

    QFont metaFont;
    metaFont.setFamilies({ QStringLiteral("Segoe UI Variable Text"), QStringLiteral("Segoe UI") });
    metaFont.setPixelSize(12);
    m_metaLabel->setFont(metaFont);
    m_pathTitleLabel->setFont(metaFont);
    m_pathValueLabel->setFont(metaFont);
    m_statusLabel->setFont(metaFont);

    QFont treeFont;
    treeFont.setFamilies({ QStringLiteral("Segoe UI Variable Text"), QStringLiteral("Segoe UI") });
    treeFont.setPixelSize(14);
    m_treeWidget->setFont(treeFont);
    m_nameSearchEdit->setFont(treeFont);
    m_clearFiltersButton->setFont(treeFont);
}

void FolderRenderer::showStatusMessage(const QString& message)
{
    if (message.trimmed().isEmpty()) {
        updateFolderCountStatus(m_rootEntryCount);
        return;
    }

    const int statusSerial = ++m_statusMessageSerial;
    PreviewStateVisuals::showStatus(m_statusLabel, message);
    QTimer::singleShot(1400, this, [this, statusSerial]() {
        if (statusSerial != m_statusMessageSerial) {
            return;
        }
        if (!m_folderCountStatus.trimmed().isEmpty()) {
            PreviewStateVisuals::showStatus(m_statusLabel, m_folderCountStatus);
            return;
        }
        PreviewStateVisuals::clearStatus(m_statusLabel);
    });
}

void FolderRenderer::updateFolderCountStatus(int entryCount)
{
    ++m_statusMessageSerial;
    m_rootEntryCount = entryCount;
    if (entryCount < 0) {
        m_folderCountStatus.clear();
        PreviewStateVisuals::clearStatus(m_statusLabel);
        return;
    }

    m_folderCountStatus = entryCount == 0
        ? QCoreApplication::translate("SpaceLook", "The folder is empty.")
        : QCoreApplication::translate("SpaceLook", "Loaded %1 entries. Expand folders to load children.").arg(entryCount);
    PreviewStateVisuals::showStatus(
        m_statusLabel,
        m_folderCountStatus,
        entryCount == 0 ? PreviewStateVisuals::Kind::Empty : PreviewStateVisuals::Kind::Success);
}

void FolderRenderer::toggleSearchPanel()
{
    setSearchPanelVisible(!m_searchPanelVisible);
}

void FolderRenderer::setSearchPanelVisible(bool visible)
{
    m_searchPanelVisible = visible;
    if (m_searchPanel) {
        m_searchPanel->setVisible(visible);
    }
}

void FolderRenderer::clearSearchFilters()
{
    if (!m_nameSearchEdit) {
        return;
    }

    m_nameSearchEdit->clear();
    refreshRootView();
}

bool FolderRenderer::hasActiveSearchFilters() const
{
    return m_nameSearchEdit && !m_nameSearchEdit->text().trimmed().isEmpty();
}

QVector<FolderRenderer::FolderEntry> FolderRenderer::filteredRootEntries() const
{
    const QVector<FolderEntry>& sourceEntries = hasActiveSearchFilters() && m_recursiveSearchReady
        ? m_searchEntries
        : m_rootEntries;
    QVector<FolderEntry> filtered;
    filtered.reserve(sourceEntries.size());
    for (const FolderEntry& entry : sourceEntries) {
        if (entryMatchesFilters(entry)) {
            filtered.append(entry);
        }
    }
    return filtered;
}

bool FolderRenderer::entryMatchesFilters(const FolderEntry& entry) const
{
    const QString nameFilter = m_nameSearchEdit ? m_nameSearchEdit->text().trimmed() : QString();
    if (!nameFilter.isEmpty() && !entry.name.contains(nameFilter, Qt::CaseInsensitive)) {
        return false;
    }

    return true;
}

void FolderRenderer::refreshRootView()
{
    const QVector<FolderEntry> filtered = filteredRootEntries();
    m_treeWidget->clear();
    populateTree(nullptr, filtered);
    applyFiltersToTree();
    updateFolderCountStatus(filtered.size());

    if (hasActiveSearchFilters()) {
        const int sourceCount = m_recursiveSearchReady ? m_searchEntries.size() : m_rootEntries.size();
        const QString filterStatus = QCoreApplication::translate("SpaceLook", "Filtered %1 of %2 entries.")
            .arg(filtered.size())
            .arg(sourceCount);
        PreviewStateVisuals::showStatus(m_statusLabel, filterStatus, PreviewStateVisuals::Kind::Info);
        m_folderCountStatus = filterStatus;
    }
}

void FolderRenderer::scheduleSearchRefresh()
{
    if (!hasActiveSearchFilters()) {
        refreshRootView();
        return;
    }

    if (m_recursiveSearchReady) {
        refreshRootView();
        return;
    }

    startRecursiveSearch();
}

void FolderRenderer::startRecursiveSearch()
{
    if (m_recursiveSearchRunning || m_info.filePath.trimmed().isEmpty()) {
        return;
    }

    m_recursiveSearchRunning = true;
    PreviewStateVisuals::showStatus(
        m_statusLabel,
        QCoreApplication::translate("SpaceLook", "Searching folder contents..."),
        PreviewStateVisuals::Kind::Loading);

    const PreviewLoadGuard::Token loadToken = m_loadGuard.observe(m_info.filePath);
    PreviewCancellationToken cancelToken = m_cancelToken;
    if (!cancelToken) {
        cancelToken = makePreviewCancellationToken();
        m_cancelToken = cancelToken;
    }

    auto* watcher = new QFutureWatcher<FolderLoadResult>(this);
    connect(watcher, &QFutureWatcher<FolderLoadResult>::finished, this, [this, watcher, loadToken, cancelToken]() {
        watcher->deleteLater();
        m_recursiveSearchRunning = false;

        if (previewCancellationRequested(cancelToken) ||
            !m_loadGuard.isCurrent(loadToken, m_info.filePath) ||
            !isVisible()) {
            return;
        }

        const FolderLoadResult result = watcher->result();
        if (!result.success) {
            PreviewStateVisuals::showStatus(m_statusLabel, result.statusMessage, PreviewStateVisuals::Kind::Error);
            return;
        }

        m_searchEntries = result.entries;
        m_recursiveSearchReady = true;
        refreshRootView();
    });

    watcher->setFuture(QtConcurrent::run([filePath = m_info.filePath, cancelToken]() {
        return loadFolderEntries(filePath, filePath, cancelToken, true);
    }));
}

bool FolderRenderer::treeItemMatchesFilters(const QTreeWidgetItem* item) const
{
    if (!item) {
        return false;
    }
    if (item->data(0, kPlaceholderRole).toBool()) {
        return true;
    }

    FolderEntry entry;
    entry.name = item->text(0);
    entry.path = item->data(0, kItemPathRole).toString();
    entry.isDirectory = item->data(0, Qt::UserRole).toBool();
    entry.sizeBytes = static_cast<qint64>(item->data(0, kEntrySizeRole).toULongLong());
    entry.lastModified = item->data(0, kEntryModifiedRole).toDateTime();
    return entryMatchesFilters(entry);
}

bool FolderRenderer::applyFilterToTreeItem(QTreeWidgetItem* item)
{
    if (!item) {
        return false;
    }

    const bool matchesSelf = treeItemMatchesFilters(item);
    bool hasVisibleChild = false;
    for (int index = 0; index < item->childCount(); ++index) {
        hasVisibleChild = applyFilterToTreeItem(item->child(index)) || hasVisibleChild;
    }

    const bool visible = matchesSelf || hasVisibleChild;
    item->setHidden(!visible);
    return visible;
}

void FolderRenderer::applyFiltersToTree()
{
    if (!m_treeWidget) {
        return;
    }
    for (int index = 0; index < m_treeWidget->topLevelItemCount(); ++index) {
        applyFilterToTreeItem(m_treeWidget->topLevelItem(index));
    }
}

void FolderRenderer::populateTree(QTreeWidgetItem* parentItem, const QVector<FolderEntry>& entries)
{
    for (const FolderEntry& entry : entries) {
        if (entry.name.trimmed().isEmpty()) {
            continue;
        }

        if (entry.isDirectory) {
            QTreeWidgetItem* folderItem = ensureFolderItem(entry.path, parentItem, entry.name, entry.expansionPath);
            if (folderItem) {
                ensureFolderLoadingPlaceholder(folderItem);
            }
            continue;
        }

        auto* item = new QTreeWidgetItem();
        item->setText(0, entry.name);
        item->setIcon(0, iconForFolderEntry(entry.path, false));
        item->setData(0, Qt::UserRole, false);
        item->setData(0, kItemPathRole, entry.path);
        item->setData(0, kExpansionPathRole, entry.expansionPath);
        item->setData(0, kEntrySizeRole, static_cast<qulonglong>(entry.sizeBytes > 0 ? entry.sizeBytes : 0));
        item->setData(0, kEntryModifiedRole, entry.lastModified);
        if (parentItem) {
            parentItem->addChild(item);
        } else {
            m_treeWidget->addTopLevelItem(item);
        }
    }
}

void FolderRenderer::clearTreeItemChildren(QTreeWidgetItem* item)
{
    if (!item) {
        return;
    }

    const QList<QTreeWidgetItem*> children = item->takeChildren();
    qDeleteAll(children);
}

QTreeWidgetItem* FolderRenderer::ensureFolderItem(const QString& folderPath,
                                                  QTreeWidgetItem* parentItem,
                                                  const QString& folderName,
                                                  const QString& expansionPath)
{
    const QString normalizedExpansionPath = normalizedFolderPath(expansionPath.trimmed().isEmpty() ? folderPath : expansionPath);
    if (QTreeWidgetItem* existingItem = findTreeItemByPath(m_treeWidget, folderPath)) {
        existingItem->setText(0, folderName);
        existingItem->setIcon(0, iconForFolderEntry(folderPath, true));
        existingItem->setData(0, Qt::UserRole, true);
        existingItem->setData(0, kItemPathRole, folderPath);
        existingItem->setData(0, kExpansionPathRole, normalizedExpansionPath);
        existingItem->setData(0, kEntrySizeRole, static_cast<qulonglong>(0));
        if (!existingItem->data(0, kLoadingRole).isValid()) {
            existingItem->setData(0, kLoadingRole, false);
        }
        if (!existingItem->data(0, kLoadedRole).isValid()) {
            existingItem->setData(0, kLoadedRole, false);
        }
        ensureFolderLoadingPlaceholder(existingItem);
        return existingItem;
    }

    auto* item = new QTreeWidgetItem();
    item->setText(0, folderName);
    item->setIcon(0, iconForFolderEntry(folderPath, true));
    item->setData(0, Qt::UserRole, true);
    item->setData(0, kItemPathRole, folderPath);
    item->setData(0, kExpansionPathRole, normalizedExpansionPath);
    item->setData(0, kEntrySizeRole, static_cast<qulonglong>(0));
    item->setData(0, kLoadingRole, false);
    item->setData(0, kLoadedRole, false);
    if (parentItem) {
        parentItem->addChild(item);
    } else {
        m_treeWidget->addTopLevelItem(item);
    }
    ensureFolderLoadingPlaceholder(item);
    return item;
}

QTreeWidgetItem* FolderRenderer::folderItemForPath(const QString& folderPath) const
{
    return findTreeItemByPath(m_treeWidget, folderPath);
}

QString FolderRenderer::folderPathForItem(const QTreeWidgetItem* item) const
{
    return item ? item->data(0, kItemPathRole).toString() : QString();
}

QString FolderRenderer::expansionPathForItem(const QTreeWidgetItem* item) const
{
    if (!item) {
        return QString();
    }

    const QString expansionPath = item->data(0, kExpansionPathRole).toString();
    return expansionPath.trimmed().isEmpty() ? folderPathForItem(item) : expansionPath;
}

bool FolderRenderer::isFolderItemLoaded(const QTreeWidgetItem* item) const
{
    return item && item->data(0, kLoadedRole).toBool();
}

bool FolderRenderer::isFolderItemLoading(const QTreeWidgetItem* item) const
{
    return item && item->data(0, kLoadingRole).toBool();
}

void FolderRenderer::setFolderItemLoadState(QTreeWidgetItem* item, bool loading, bool loaded)
{
    if (!item) {
        return;
    }

    item->setData(0, kLoadingRole, loading);
    item->setData(0, kLoadedRole, loaded);
}

void FolderRenderer::loadFolderChildren(const QString& displayPath, const QString& expansionPath, QTreeWidgetItem* parentItem)
{
    if (!parentItem || displayPath.trimmed().isEmpty() || expansionPath.trimmed().isEmpty()) {
        return;
    }

    setFolderItemLoadState(parentItem, true, false);
    clearTreeItemChildren(parentItem);
    parentItem->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
    auto* placeholder = new QTreeWidgetItem();
    placeholder->setText(0, QCoreApplication::translate("SpaceLook", "Loading..."));
    placeholder->setData(0, Qt::UserRole, false);
    placeholder->setData(0, kItemPathRole, QString());
    placeholder->setData(0, kPlaceholderRole, true);
    parentItem->addChild(placeholder);

    const PreviewLoadGuard::Token loadToken = m_loadGuard.observe(displayPath);
    PreviewCancellationToken cancelToken = m_cancelToken;
    if (!cancelToken) {
        cancelToken = makePreviewCancellationToken();
        m_cancelToken = cancelToken;
    }
    auto* watcher = new QFutureWatcher<FolderLoadResult>(this);
    connect(watcher, &QFutureWatcher<FolderLoadResult>::finished, this, [this, watcher, loadToken, displayPath, cancelToken]() {
        watcher->deleteLater();

        if (previewCancellationRequested(cancelToken) ||
            !m_loadGuard.isCurrentGeneration(loadToken) ||
            !isVisible()) {
            return;
        }

        const FolderLoadResult result = watcher->result();
        QTreeWidgetItem* currentParentItem = folderItemForPath(displayPath);
        if (!currentParentItem) {
            return;
        }
        if (folderPathForItem(currentParentItem) != displayPath) {
            return;
        }

        clearTreeItemChildren(currentParentItem);
        if (!result.success) {
            auto* errorItem = new QTreeWidgetItem();
            errorItem->setText(0, result.statusMessage.isEmpty() ? QCoreApplication::translate("SpaceLook", "Failed to load folder.") : result.statusMessage);
            errorItem->setData(0, Qt::UserRole, false);
            errorItem->setData(0, kItemPathRole, QString());
            currentParentItem->addChild(errorItem);
            setFolderItemLoadState(currentParentItem, false, false);
            currentParentItem->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
            currentParentItem->setExpanded(true);
            return;
        }

        populateTree(currentParentItem, result.entries);
        applyFiltersToTree();
        setFolderItemLoadState(currentParentItem, false, true);
        currentParentItem->setChildIndicatorPolicy(
            result.entries.isEmpty() ? QTreeWidgetItem::DontShowIndicatorWhenChildless : QTreeWidgetItem::ShowIndicator);
        currentParentItem->setExpanded(true);
    });

    watcher->setFuture(QtConcurrent::run([expansionPath, cancelToken]() {
        return loadFolderEntries(expansionPath, expansionPath, cancelToken);
    }));
}

void FolderRenderer::showItemContextMenu(const QPoint& position)
{
    if (!m_treeWidget) {
        return;
    }

    QTreeWidgetItem* item = m_treeWidget->itemAt(position);
    if (!item) {
        return;
    }
    m_treeWidget->setCurrentItem(item);

    const QString targetPath = item->data(0, kItemPathRole).toString().trimmed();
    if (targetPath.isEmpty()) {
        return;
    }

    QMenu menu(this);
    QAction* openAction = menu.addAction(QCoreApplication::translate("SpaceLook", "Open"));
    QAction* explorerAction = menu.addAction(QCoreApplication::translate("SpaceLook", "Open in Explorer"));
    menu.addSeparator();
    QAction* renameAction = menu.addAction(QCoreApplication::translate("SpaceLook", "Rename"));
    QAction* selectedAction = menu.exec(m_treeWidget->viewport()->mapToGlobal(position));
    if (selectedAction == openAction) {
        openTreeItem(item);
        return;
    }
    if (selectedAction == explorerAction) {
        openTreeItemInExplorer(item);
        return;
    }
    if (selectedAction == renameAction) {
        renameTreeItem(item);
    }
}

void FolderRenderer::openTreeItem(QTreeWidgetItem* item)
{
    if (!item) {
        return;
    }

    const QString targetPath = item->data(0, kItemPathRole).toString();
    if (targetPath.trimmed().isEmpty()) {
        return;
    }

    if (!openItemWithDefaultApp(targetPath)) {
        showStatusMessage(QCoreApplication::translate("SpaceLook", "Failed to open selected item."));
        return;
    }

    if (SpaceLookWindow* previewWindow = qobject_cast<SpaceLookWindow*>(window())) {
        previewWindow->hidePreview();
    }
}

void FolderRenderer::openTreeItemInExplorer(QTreeWidgetItem* item)
{
    if (!item) {
        return;
    }

    const QString targetPath = item->data(0, kItemPathRole).toString();
    if (targetPath.trimmed().isEmpty()) {
        return;
    }

    if (!openItemInExplorer(targetPath)) {
        showStatusMessage(QCoreApplication::translate("SpaceLook", "Failed to open selected location."));
        return;
    }
}

void FolderRenderer::renameTreeItem(QTreeWidgetItem* item)
{
    if (!item) {
        return;
    }
    if (item->data(0, kPlaceholderRole).toBool()) {
        return;
    }

    if (isFolderItemLoading(item)) {
        showStatusMessage(QCoreApplication::translate("SpaceLook", "Please wait until the folder finishes loading."));
        return;
    }
    if (m_renamingItem && m_renamingItem != item) {
        cancelInlineRename();
    }
    if (m_renamingItem == item && m_renameEditor) {
        m_renameEditor->setFocus(Qt::OtherFocusReason);
        m_renameEditor->selectAll();
        return;
    }

    const QString oldPath = normalizedFolderPath(item->data(0, kItemPathRole).toString());
    if (oldPath.trimmed().isEmpty()) {
        return;
    }

    const QFileInfo oldInfo(oldPath);
    if (!oldInfo.exists()) {
        showStatusMessage(QCoreApplication::translate("SpaceLook", "The selected item no longer exists."));
        return;
    }

    const QString currentName = oldInfo.fileName();
    {
        const QSignalBlocker blocker(m_treeWidget);
        item->setData(0, kRenameOldPathRole, oldPath);
        item->setData(0, kRenameOldNameRole, currentName);
        item->setFlags(item->flags() | Qt::ItemIsEditable);
    }
    m_treeWidget->setCurrentItem(item);

    auto* editorWidget = new QWidget(m_treeWidget);
    editorWidget->setObjectName(QStringLiteral("FolderRenameWidget"));
    auto* editorLayout = new QHBoxLayout(editorWidget);
    editorLayout->setContentsMargins(8, 2, 4, 2);
    editorLayout->setSpacing(6);

    auto* nameEdit = new QLineEdit(currentName, editorWidget);
    nameEdit->setObjectName(QStringLiteral("FolderRenameEdit"));
    nameEdit->installEventFilter(this);
    editorLayout->addWidget(nameEdit, 1);

    auto* okButton = new QPushButton(QCoreApplication::translate("SpaceLook", "OK"), editorWidget);
    okButton->setObjectName(QStringLiteral("FolderRenameButton"));
    okButton->setCursor(Qt::PointingHandCursor);
    editorLayout->addWidget(okButton, 0);

    auto* cancelButton = new QPushButton(QCoreApplication::translate("SpaceLook", "Cancel"), editorWidget);
    cancelButton->setObjectName(QStringLiteral("FolderRenameButton"));
    cancelButton->setCursor(Qt::PointingHandCursor);
    editorLayout->addWidget(cancelButton, 0);

    m_renamingItem = item;
    m_renameEditor = nameEdit;
    m_treeWidget->setItemWidget(item, 0, editorWidget);

    connect(nameEdit, &QLineEdit::returnPressed, this, &FolderRenderer::confirmInlineRename);
    connect(okButton, &QPushButton::clicked, this, &FolderRenderer::confirmInlineRename);
    connect(cancelButton, &QPushButton::clicked, this, &FolderRenderer::cancelInlineRename);

    QTimer::singleShot(0, nameEdit, [nameEdit]() {
        nameEdit->setFocus(Qt::OtherFocusReason);
        nameEdit->selectAll();
    });
}

void FolderRenderer::confirmInlineRename()
{
    if (!m_treeWidget || !m_renamingItem || !m_renameEditor) {
        return;
    }

    QTreeWidgetItem* item = m_renamingItem;
    const QString newName = m_renameEditor->text().trimmed();
    QWidget* editorWidget = m_treeWidget->itemWidget(item, 0);
    m_treeWidget->removeItemWidget(item, 0);
    if (editorWidget) {
        editorWidget->deleteLater();
    }
    m_renamingItem = nullptr;
    m_renameEditor = nullptr;

    {
        const QSignalBlocker blocker(m_treeWidget);
        item->setText(0, newName);
    }
    handleInlineRename(item, 0);
}

void FolderRenderer::cancelInlineRename()
{
    if (!m_treeWidget || !m_renamingItem) {
        return;
    }

    QTreeWidgetItem* item = m_renamingItem;
    const QString currentName = item->data(0, kRenameOldNameRole).toString();
    QWidget* editorWidget = m_treeWidget->itemWidget(item, 0);
    m_treeWidget->removeItemWidget(item, 0);
    if (editorWidget) {
        editorWidget->deleteLater();
    }
    {
        const QSignalBlocker blocker(m_treeWidget);
        item->setText(0, currentName);
        item->setData(0, kRenameOldPathRole, QString());
        item->setData(0, kRenameOldNameRole, QString());
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    }
    m_renamingItem = nullptr;
    m_renameEditor = nullptr;
    updateFolderCountStatus(m_rootEntryCount);
}

void FolderRenderer::handleInlineRename(QTreeWidgetItem* item, int column)
{
    if (!item || column != 0) {
        return;
    }

    const QString oldPath = normalizedFolderPath(item->data(0, kRenameOldPathRole).toString());
    if (oldPath.trimmed().isEmpty()) {
        return;
    }

    const QString currentName = item->data(0, kRenameOldNameRole).toString();
    const QString newName = item->text(0).trimmed();
    auto restoreOldName = [this, item, currentName](const QString& message) {
        const QSignalBlocker blocker(m_treeWidget);
        item->setText(0, currentName);
        item->setData(0, kRenameOldPathRole, QString());
        item->setData(0, kRenameOldNameRole, QString());
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        showStatusMessage(message);
    };

    if (newName.isEmpty()) {
        restoreOldName(QCoreApplication::translate("SpaceLook", "Name cannot be empty."));
        return;
    }
    if (newName == currentName) {
        item->setData(0, kRenameOldPathRole, QString());
        item->setData(0, kRenameOldNameRole, QString());
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        return;
    }
    if (newName.contains(QLatin1Char('/')) || newName.contains(QLatin1Char('\\'))) {
        restoreOldName(QCoreApplication::translate("SpaceLook", "Name cannot contain path separators."));
        return;
    }

    const QFileInfo oldInfo(oldPath);
    if (!oldInfo.exists()) {
        restoreOldName(QCoreApplication::translate("SpaceLook", "The selected item no longer exists."));
        return;
    }

    QDir parentDir = oldInfo.dir();
    const QString newPath = normalizedFolderPath(parentDir.filePath(newName));
    if (QFileInfo::exists(newPath)) {
        restoreOldName(QCoreApplication::translate("SpaceLook", "An item with that name already exists."));
        return;
    }

    if (!parentDir.rename(currentName, newName)) {
        restoreOldName(QCoreApplication::translate("SpaceLook", "Failed to rename selected item."));
        return;
    }

    item->setIcon(0, iconForFolderEntry(newPath, oldInfo.isDir()));
    {
        const QSignalBlocker blocker(m_treeWidget);
        item->setData(0, kRenameOldPathRole, QString());
        item->setData(0, kRenameOldNameRole, QString());
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    }
    updateTreeItemPathPrefix(item, oldPath, newPath);
    showStatusMessage(QCoreApplication::translate("SpaceLook", "Renamed to %1").arg(newName));
}

void FolderRenderer::updateTreeItemPathPrefix(QTreeWidgetItem* item, const QString& oldPath, const QString& newPath)
{
    if (!item || oldPath.trimmed().isEmpty() || newPath.trimmed().isEmpty()) {
        return;
    }

    const QString normalizedOldPath = normalizedFolderPath(oldPath);
    const QString normalizedNewPath = normalizedFolderPath(newPath);
    const QString oldChildPrefix = normalizedOldPath + QLatin1Char('/');
    QString itemPath = normalizedFolderPath(item->data(0, kItemPathRole).toString());
    if (itemPath == normalizedOldPath) {
        itemPath = normalizedNewPath;
    } else if (itemPath.startsWith(oldChildPrefix, Qt::CaseInsensitive)) {
        itemPath = normalizedNewPath + QLatin1Char('/') + itemPath.mid(oldChildPrefix.size());
    }
    {
        const QSignalBlocker blocker(m_treeWidget);
        item->setData(0, kItemPathRole, itemPath);
        if (!itemPath.trimmed().isEmpty() && !item->data(0, kPlaceholderRole).toBool()) {
            item->setText(0, QFileInfo(itemPath).fileName());
        }
    }

    for (int index = 0; index < item->childCount(); ++index) {
        updateTreeItemPathPrefix(item->child(index), normalizedOldPath, normalizedNewPath);
    }
}

bool FolderRenderer::previewTreeItem(QTreeWidgetItem* item)
{
    if (!item || item->data(0, kPlaceholderRole).toBool()) {
        return false;
    }
    if (isFolderItemLoading(item)) {
        return false;
    }

    const QString targetPath = item->data(0, kItemPathRole).toString().trimmed();
    if (targetPath.isEmpty()) {
        return false;
    }

    emit previewRequested(targetPath);
    return true;
}
