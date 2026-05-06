#pragma once

#include <QVector>
#include <QWidget>

#include "core/hovered_item_info.h"
#include "renderers/IPreviewRenderer.h"
#include "renderers/PreviewCancellationToken.h"
#include "renderers/PreviewLoadGuard.h"

class QLabel;
class OpenWithButton;
class QTreeWidget;
class QTreeWidgetItem;
class SelectableTitleLabel;

class ArchiveRenderer : public QWidget, public IPreviewRenderer
{
    Q_OBJECT

public:
    explicit ArchiveRenderer(QWidget* parent = nullptr);

    QString rendererId() const override;
    bool canHandle(const HoveredItemInfo& info) const override;
    QWidget* widget() override;
    void load(const HoveredItemInfo& info) override;
    void unload() override;
    bool reportsLoadingState() const override;
    void setLoadingStateCallback(std::function<void(bool)> callback) override;

private:
    struct ArchiveEntry
    {
        QString path;
        bool isDirectory = false;
    };

    struct ArchiveLoadResult
    {
        QVector<ArchiveEntry> entries;
        QString statusMessage;
        bool success = false;
    };

    void applyChrome();
    void showStatusMessage(const QString& message);
    void populateTree(const QVector<ArchiveEntry>& entries);
    QTreeWidgetItem* ensureFolderItem(const QString& folderPath,
                                      QTreeWidgetItem* parentItem,
                                      const QString& folderName);
    ArchiveLoadResult loadArchiveEntries(const QString& filePath, const PreviewCancellationToken& cancelToken) const;
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
};
