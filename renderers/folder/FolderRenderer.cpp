#include "renderers/folder/FolderRenderer.h"

#include <QDebug>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHash>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

#include <Windows.h>

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
    placeholder->setText(0, QStringLiteral("Loading..."));
    placeholder->setData(0, Qt::UserRole, false);
    placeholder->setData(0, kItemPathRole, QString());
    placeholder->setData(0, kPlaceholderRole, true);
    item->addChild(placeholder);
}

FolderRenderer::FolderLoadResult loadFolderEntries(const QString& filePath,
                                                   const QString& folderPath,
                                                   const PreviewCancellationToken& cancelToken)
{
    Q_UNUSED(folderPath)

    FolderRenderer::FolderLoadResult result;
    if (previewCancellationRequested(cancelToken)) {
        result.statusMessage = QStringLiteral("Folder preview was canceled.");
        return result;
    }

    const QString trimmedPath = QDir::cleanPath(filePath.trimmed());
    if (trimmedPath.isEmpty()) {
        result.statusMessage = QStringLiteral("Folder path is unavailable.");
        return result;
    }

    QFileInfo rootInfo(trimmedPath);
    if (!rootInfo.exists() || !rootInfo.isDir()) {
        qDebug().noquote() << QStringLiteral("[SpaceLookRender] FolderRenderer cannot open folder path=\"%1\" exists=%2 dir=%3")
            .arg(trimmedPath)
            .arg(rootInfo.exists())
            .arg(rootInfo.isDir());
        result.statusMessage = QStringLiteral("The folder could not be opened.");
        return result;
    }

    const QDir rootDir(rootInfo.absoluteFilePath());
    const QFileInfoList entries = rootDir.entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
        QDir::Name | QDir::IgnoreCase);

    for (const QFileInfo& entryInfo : entries) {
        if (previewCancellationRequested(cancelToken)) {
            result.entries.clear();
            result.statusMessage = QStringLiteral("Folder preview was canceled.");
            return result;
        }

        FolderRenderer::FolderEntry entry;
        entry.name = entryInfo.fileName();
        entry.path = normalizedFolderPath(entryInfo.absoluteFilePath());
        entry.isDirectory = entryInfo.isDir();
        result.entries.append(entry);
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
    m_treeWidget->setObjectName(QStringLiteral("FolderTree"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 0, 12, 12);
    layout->setSpacing(12);
    layout->addWidget(m_headerRow);
    layout->addWidget(m_statusLabel);
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

    m_treeWidget->setColumnCount(1);
    m_treeWidget->setHeaderHidden(true);
    m_treeWidget->header()->setStretchLastSection(true);
    m_treeWidget->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_treeWidget->setRootIsDecorated(true);
    m_treeWidget->setUniformRowHeights(true);
    m_treeWidget->setAlternatingRowColors(false);
    m_treeWidget->setAnimated(true);
    m_treeWidget->setIndentation(20);
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

        QTimer::singleShot(0, this, [this, folderPath]() {
            QTreeWidgetItem* currentItem = folderItemForPath(folderPath);
            if (!currentItem || !currentItem->data(0, Qt::UserRole).toBool()) {
                return;
            }
            if (isFolderItemLoaded(currentItem) || isFolderItemLoading(currentItem)) {
                return;
            }
            loadFolderChildren(folderPath, currentItem);
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

    m_titleLabel->setText(info.title.isEmpty() ? QStringLiteral("Folder Preview") : info.title);
    m_titleLabel->setCopyText(m_titleLabel->text());
    m_iconLabel->setPixmap(FileTypeIconResolver::pixmapForInfo(info, m_iconLabel->contentsRect().size()));
    m_pathValueLabel->setText(info.filePath.trimmed().isEmpty() ? QStringLiteral("(Unavailable)") : info.filePath);
    m_openWithButton->setTargetContext(info.filePath, info.typeKey);
    cancelInlineRename();
    m_treeWidget->clear();
    m_rootEntryCount = -1;
    m_folderCountStatus.clear();
    ++m_statusMessageSerial;
    PreviewStateVisuals::showStatus(m_statusLabel, QStringLiteral("Loading folder contents..."), PreviewStateVisuals::Kind::Loading);

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

        populateTree(nullptr, result.entries);
        updateFolderCountStatus(result.entries.size());

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
        "  padding: 8px;"
        "  outline: none;"
        "}"
        "#FolderTree::item {"
        "  min-height: 28px;"
        "}"
        "#FolderTree::item:selected {"
        "  background: rgba(126, 188, 255, 0.28);"
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
        "  font-family: 'Segoe UI Rounded';"
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
    );

    QFont titleFont;
    titleFont.setFamily(QStringLiteral("Segoe UI Rounded"));
    titleFont.setPixelSize(20);
    titleFont.setWeight(QFont::Bold);
    m_titleLabel->setFont(titleFont);

    QFont metaFont;
    metaFont.setFamily(QStringLiteral("Segoe UI Rounded"));
    metaFont.setPixelSize(13);
    m_metaLabel->setFont(metaFont);
    m_pathTitleLabel->setFont(metaFont);
    m_pathValueLabel->setFont(metaFont);
    m_statusLabel->setFont(metaFont);

    QFont treeFont;
    treeFont.setFamily(QStringLiteral("Segoe UI Rounded"));
    treeFont.setPixelSize(13);
    m_treeWidget->setFont(treeFont);
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
        ? QStringLiteral("The folder is empty.")
        : QStringLiteral("Loaded %1 entries. Expand folders to load children.").arg(entryCount);
    PreviewStateVisuals::showStatus(
        m_statusLabel,
        m_folderCountStatus,
        entryCount == 0 ? PreviewStateVisuals::Kind::Empty : PreviewStateVisuals::Kind::Success);
}

void FolderRenderer::populateTree(QTreeWidgetItem* parentItem, const QVector<FolderEntry>& entries)
{
    for (const FolderEntry& entry : entries) {
        if (entry.name.trimmed().isEmpty()) {
            continue;
        }

        if (entry.isDirectory) {
            QTreeWidgetItem* folderItem = ensureFolderItem(entry.path, parentItem, entry.name);
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
                                                  const QString& folderName)
{
    if (QTreeWidgetItem* existingItem = findTreeItemByPath(m_treeWidget, folderPath)) {
        existingItem->setText(0, folderName);
        existingItem->setIcon(0, iconForFolderEntry(folderPath, true));
        existingItem->setData(0, Qt::UserRole, true);
        existingItem->setData(0, kItemPathRole, folderPath);
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

void FolderRenderer::loadFolderChildren(const QString& folderPath, QTreeWidgetItem* parentItem)
{
    if (!parentItem || folderPath.trimmed().isEmpty()) {
        return;
    }

    setFolderItemLoadState(parentItem, true, false);
    clearTreeItemChildren(parentItem);
    parentItem->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
    auto* placeholder = new QTreeWidgetItem();
    placeholder->setText(0, QStringLiteral("Loading..."));
    placeholder->setData(0, Qt::UserRole, false);
    placeholder->setData(0, kItemPathRole, QString());
    placeholder->setData(0, kPlaceholderRole, true);
    parentItem->addChild(placeholder);

    const PreviewLoadGuard::Token loadToken = m_loadGuard.observe(folderPath);
    PreviewCancellationToken cancelToken = m_cancelToken;
    if (!cancelToken) {
        cancelToken = makePreviewCancellationToken();
        m_cancelToken = cancelToken;
    }
    auto* watcher = new QFutureWatcher<FolderLoadResult>(this);
    connect(watcher, &QFutureWatcher<FolderLoadResult>::finished, this, [this, watcher, loadToken, folderPath, cancelToken]() {
        watcher->deleteLater();

        if (previewCancellationRequested(cancelToken) ||
            !m_loadGuard.isCurrentGeneration(loadToken) ||
            !isVisible()) {
            return;
        }

        const FolderLoadResult result = watcher->result();
        QTreeWidgetItem* currentParentItem = folderItemForPath(folderPath);
        if (!currentParentItem) {
            return;
        }
        if (folderPathForItem(currentParentItem) != folderPath) {
            return;
        }

        clearTreeItemChildren(currentParentItem);
        if (!result.success) {
            auto* errorItem = new QTreeWidgetItem();
            errorItem->setText(0, result.statusMessage.isEmpty() ? QStringLiteral("Failed to load folder.") : result.statusMessage);
            errorItem->setData(0, Qt::UserRole, false);
            errorItem->setData(0, kItemPathRole, QString());
            currentParentItem->addChild(errorItem);
            setFolderItemLoadState(currentParentItem, false, false);
            currentParentItem->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
            currentParentItem->setExpanded(true);
            return;
        }

        populateTree(currentParentItem, result.entries);
        setFolderItemLoadState(currentParentItem, false, true);
        currentParentItem->setChildIndicatorPolicy(
            result.entries.isEmpty() ? QTreeWidgetItem::DontShowIndicatorWhenChildless : QTreeWidgetItem::ShowIndicator);
        currentParentItem->setExpanded(true);
    });

    watcher->setFuture(QtConcurrent::run([folderPath, cancelToken]() {
        return loadFolderEntries(folderPath, folderPath, cancelToken);
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
    QAction* openAction = menu.addAction(QStringLiteral("Open"));
    QAction* explorerAction = menu.addAction(QStringLiteral("Open in Explorer"));
    menu.addSeparator();
    QAction* renameAction = menu.addAction(QStringLiteral("Rename"));
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
        showStatusMessage(QStringLiteral("Failed to open selected item."));
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
        showStatusMessage(QStringLiteral("Failed to open selected location."));
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
        showStatusMessage(QStringLiteral("Please wait until the folder finishes loading."));
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
        showStatusMessage(QStringLiteral("The selected item no longer exists."));
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

    auto* okButton = new QPushButton(QStringLiteral("OK"), editorWidget);
    okButton->setObjectName(QStringLiteral("FolderRenameButton"));
    okButton->setCursor(Qt::PointingHandCursor);
    editorLayout->addWidget(okButton, 0);

    auto* cancelButton = new QPushButton(QStringLiteral("Cancel"), editorWidget);
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
        restoreOldName(QStringLiteral("Name cannot be empty."));
        return;
    }
    if (newName == currentName) {
        item->setData(0, kRenameOldPathRole, QString());
        item->setData(0, kRenameOldNameRole, QString());
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        return;
    }
    if (newName.contains(QLatin1Char('/')) || newName.contains(QLatin1Char('\\'))) {
        restoreOldName(QStringLiteral("Name cannot contain path separators."));
        return;
    }

    const QFileInfo oldInfo(oldPath);
    if (!oldInfo.exists()) {
        restoreOldName(QStringLiteral("The selected item no longer exists."));
        return;
    }

    QDir parentDir = oldInfo.dir();
    const QString newPath = normalizedFolderPath(parentDir.filePath(newName));
    if (QFileInfo::exists(newPath)) {
        restoreOldName(QStringLiteral("An item with that name already exists."));
        return;
    }

    if (!parentDir.rename(currentName, newName)) {
        restoreOldName(QStringLiteral("Failed to rename selected item."));
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
    showStatusMessage(QStringLiteral("Renamed to %1").arg(newName));
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
