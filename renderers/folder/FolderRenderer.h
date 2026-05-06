#pragma once

#include <QVector>
#include <QWidget>

#include "core/hovered_item_info.h"
#include "renderers/IPreviewRenderer.h"
#include "renderers/PreviewCancellationToken.h"
#include "renderers/PreviewLoadGuard.h"

class QLabel;
class QLineEdit;
class OpenWithButton;
class QPoint;
class QEvent;
class QTreeWidget;
class QTreeWidgetItem;
class SelectableTitleLabel;

class FolderRenderer : public QWidget, public IPreviewRenderer
{
    Q_OBJECT

public:
    explicit FolderRenderer(QWidget* parent = nullptr);

    QString rendererId() const override;
    bool canHandle(const HoveredItemInfo& info) const override;
    QWidget* widget() override;
    void load(const HoveredItemInfo& info) override;
    void unload() override;
    bool reportsLoadingState() const override;
    void setLoadingStateCallback(std::function<void(bool)> callback) override;

    struct FolderEntry
    {
        QString name;
        QString path;
        bool isDirectory = false;
    };

    struct FolderLoadResult
    {
        QVector<FolderEntry> entries;
        QString statusMessage;
        bool success = false;
    };

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void applyChrome();
    void showStatusMessage(const QString& message);
    void updateFolderCountStatus(int entryCount);
    void populateTree(QTreeWidgetItem* parentItem, const QVector<FolderEntry>& entries);
    void clearTreeItemChildren(QTreeWidgetItem* item);
    QTreeWidgetItem* ensureFolderItem(const QString& folderPath,
                                      QTreeWidgetItem* parentItem,
                                      const QString& folderName);
    QTreeWidgetItem* folderItemForPath(const QString& folderPath) const;
    QString folderPathForItem(const QTreeWidgetItem* item) const;
    bool isFolderItemLoaded(const QTreeWidgetItem* item) const;
    bool isFolderItemLoading(const QTreeWidgetItem* item) const;
    void setFolderItemLoadState(QTreeWidgetItem* item, bool loading, bool loaded);
    void loadFolderChildren(const QString& folderPath, QTreeWidgetItem* parentItem);
    void showItemContextMenu(const QPoint& position);
    void openTreeItem(QTreeWidgetItem* item);
    void openTreeItemInExplorer(QTreeWidgetItem* item);
    void renameTreeItem(QTreeWidgetItem* item);
    void confirmInlineRename();
    void cancelInlineRename();
    void handleInlineRename(QTreeWidgetItem* item, int column);
    void updateTreeItemPathPrefix(QTreeWidgetItem* item, const QString& oldPath, const QString& newPath);
    void notifyLoadingState(bool loading);

    HoveredItemInfo m_info;
    PreviewLoadGuard m_loadGuard;
    PreviewCancellationToken m_cancelToken;
    std::function<void(bool)> m_loadingStateCallback;
    QWidget* m_headerRow = nullptr;
    QLabel* m_iconLabel = nullptr;
    SelectableTitleLabel* m_titleLabel = nullptr;
    QLabel* m_metaLabel = nullptr;
    QWidget* m_pathRow = nullptr;
    QLabel* m_pathTitleLabel = nullptr;
    QLabel* m_pathValueLabel = nullptr;
    OpenWithButton* m_openWithButton = nullptr;
    QLabel* m_statusLabel = nullptr;
    QTreeWidget* m_treeWidget = nullptr;
    QTreeWidgetItem* m_renamingItem = nullptr;
    QLineEdit* m_renameEditor = nullptr;
    QString m_folderCountStatus;
    int m_rootEntryCount = -1;
    int m_statusMessageSerial = 0;
};
