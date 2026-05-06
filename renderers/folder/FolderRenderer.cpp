#include "renderers/folder/FolderRenderer.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHash>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QProcess>
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

FolderRenderer::FolderLoadResult loadFolderEntries(const QString& filePath, const QString& folderPath)
{
    FolderRenderer::FolderLoadResult result;

    const QString trimmedPath = QDir::cleanPath(filePath.trimmed());
    if (trimmedPath.isEmpty()) {
        result.statusMessage = QStringLiteral("Folder path is unavailable.");
        return result;
    }

    QFileInfo rootInfo(trimmedPath);
    if (!rootInfo.exists() || !rootInfo.isDir()) {
        result.statusMessage = QStringLiteral("The folder could not be opened.");
        return result;
    }

    const QDir rootDir(trimmedPath);
    const QFileInfoList entries = rootDir.entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
        QDir::Name | QDir::IgnoreCase);

    for (const QFileInfo& entryInfo : entries) {
        FolderRenderer::FolderEntry entry;
        entry.name = entryInfo.fileName();
        entry.path = normalizedFolderPath(folderPath + QLatin1Char('/') + entryInfo.fileName());
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
    m_iconLabel->setScaledContents(true);
    m_titleLabel->setWordWrap(true);
    m_pathTitleLabel->hide();
    m_pathValueLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_pathValueLabel->setWordWrap(true);
    m_pathValueLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_pathRow->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_statusLabel->hide();

    m_treeWidget->setColumnCount(1);
    m_treeWidget->setHeaderLabel(QStringLiteral("Folder contents"));
    m_treeWidget->header()->setStretchLastSection(true);
    m_treeWidget->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_treeWidget->setRootIsDecorated(true);
    m_treeWidget->setUniformRowHeights(true);
    m_treeWidget->setAlternatingRowColors(false);
    m_treeWidget->setAnimated(true);
    m_treeWidget->setIndentation(20);
    m_treeWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_treeWidget->setContextMenuPolicy(Qt::CustomContextMenu);
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
    m_info = info;
    const PreviewLoadGuard::Token loadToken = m_loadGuard.begin(info.filePath);
    notifyLoadingState(true);

    qDebug().noquote() << QStringLiteral("[SpaceLookRender] FolderRenderer load path=\"%1\" typeKey=\"%2\"")
        .arg(info.filePath, info.typeKey);

    m_titleLabel->setText(info.title.isEmpty() ? QStringLiteral("Folder Preview") : info.title);
    m_titleLabel->setCopyText(m_titleLabel->text());
    const QIcon typeIcon(FileTypeIconResolver::iconForInfo(info));
    m_iconLabel->setPixmap(typeIcon.pixmap(128, 128));
    m_pathValueLabel->setText(info.filePath.trimmed().isEmpty() ? QStringLiteral("(Unavailable)") : info.filePath);
    m_openWithButton->setTargetContext(info.filePath, info.typeKey);
    m_treeWidget->clear();
    m_statusLabel->setText(QStringLiteral("Loading folder contents..."));
    m_statusLabel->show();

    auto* watcher = new QFutureWatcher<FolderLoadResult>(this);
    connect(watcher, &QFutureWatcher<FolderLoadResult>::finished, this, [this, watcher, loadToken]() {
        const FolderLoadResult result = watcher->result();
        watcher->deleteLater();

        if (!m_loadGuard.isCurrent(loadToken, m_info.filePath) || !isVisible()) {
            qDebug().noquote() << QStringLiteral("[SpaceLookRender] FolderRenderer discarded stale result path=\"%1\"")
                .arg(loadToken.path);
            return;
        }

        if (!result.success) {
            m_statusLabel->setText(result.statusMessage);
            m_statusLabel->show();
            notifyLoadingState(false);
            return;
        }

        populateTree(nullptr, result.entries);
        if (result.entries.isEmpty()) {
            m_statusLabel->setText(QStringLiteral("The folder is empty."));
        } else {
            m_statusLabel->setText(QStringLiteral("Loaded %1 entries. Expand folders to load children.")
                .arg(result.entries.size()));
        }
        m_statusLabel->show();

        qDebug().noquote() << QStringLiteral("[SpaceLookRender] FolderRenderer entries=%1 path=\"%2\"")
            .arg(result.entries.size())
            .arg(loadToken.path);
        notifyLoadingState(false);
    });

    watcher->setFuture(QtConcurrent::run([filePath = info.filePath]() {
        return loadFolderEntries(filePath, filePath);
    }));
}

void FolderRenderer::unload()
{
    m_loadGuard.cancel();
    notifyLoadingState(false);
    m_treeWidget->clear();
    m_pathValueLabel->clear();
    m_openWithButton->setTargetContext(QString(), QString());
    m_statusLabel->clear();
    m_statusLabel->hide();
    m_info = HoveredItemInfo();
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
        "#FolderTree QHeaderView::section {"
        "  background: rgba(232, 239, 247, 0.96);"
        "  color: #35506b;"
        "  border: none;"
        "  border-bottom: 1px solid rgba(204, 214, 226, 0.95);"
        "  padding: 8px 10px;"
        "  font-family: 'Segoe UI Semibold';"
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
    titleFont.setFamily(QStringLiteral("Microsoft YaHei UI"));
    titleFont.setPixelSize(20);
    titleFont.setWeight(QFont::Bold);
    m_titleLabel->setFont(titleFont);

    QFont metaFont;
    metaFont.setFamily(QStringLiteral("Segoe UI"));
    metaFont.setPixelSize(13);
    m_metaLabel->setFont(metaFont);
    m_pathTitleLabel->setFont(metaFont);
    m_pathValueLabel->setFont(metaFont);
    m_statusLabel->setFont(metaFont);

    QFont treeFont;
    treeFont.setFamily(QStringLiteral("Segoe UI"));
    treeFont.setPixelSize(13);
    m_treeWidget->setFont(treeFont);
}

void FolderRenderer::showStatusMessage(const QString& message)
{
    if (message.trimmed().isEmpty()) {
        m_statusLabel->clear();
        m_statusLabel->hide();
        return;
    }

    m_statusLabel->setText(message);
    m_statusLabel->show();
    QTimer::singleShot(1400, m_statusLabel, [label = m_statusLabel]() {
        if (label) {
            label->clear();
            label->hide();
        }
    });
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
                folderItem->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
                if (folderItem->childCount() == 0) {
                    auto* placeholder = new QTreeWidgetItem();
                    placeholder->setText(0, QStringLiteral("Loading..."));
                    placeholder->setData(0, Qt::UserRole, false);
                    placeholder->setData(0, kPlaceholderRole, true);
                    folderItem->addChild(placeholder);
                }
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
    auto* placeholder = new QTreeWidgetItem();
    placeholder->setText(0, QStringLiteral("Loading..."));
    placeholder->setData(0, Qt::UserRole, false);
    placeholder->setData(0, kItemPathRole, QString());
    placeholder->setData(0, kPlaceholderRole, true);
    parentItem->addChild(placeholder);

    const PreviewLoadGuard::Token loadToken = m_loadGuard.observe(folderPath);
    auto* watcher = new QFutureWatcher<FolderLoadResult>(this);
    connect(watcher, &QFutureWatcher<FolderLoadResult>::finished, this, [this, watcher, loadToken, folderPath]() {
        const FolderLoadResult result = watcher->result();
        watcher->deleteLater();

        if (!m_loadGuard.isCurrentGeneration(loadToken) || !isVisible()) {
            return;
        }

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
            currentParentItem->setExpanded(false);
            return;
        }

        populateTree(currentParentItem, result.entries);
        setFolderItemLoadState(currentParentItem, false, true);
    });

    watcher->setFuture(QtConcurrent::run([folderPath]() {
        return loadFolderEntries(folderPath, folderPath);
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

    if (isFolderItemLoading(item)) {
        showStatusMessage(QStringLiteral("Please wait until the folder finishes loading."));
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
    m_treeWidget->editItem(item, 0);
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
    item->setData(0, kItemPathRole, itemPath);

    for (int index = 0; index < item->childCount(); ++index) {
        updateTreeItemPathPrefix(item->child(index), normalizedOldPath, normalizedNewPath);
    }
}
