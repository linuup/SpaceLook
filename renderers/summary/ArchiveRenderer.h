#pragma once

#include <QVector>
#include <QWidget>

#include "core/hovered_item_info.h"
#include "renderers/IPreviewRenderer.h"

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
    ArchiveLoadResult loadArchiveEntries(const QString& filePath) const;

    HoveredItemInfo m_info;
    quint64 m_loadRequestId = 0;
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
