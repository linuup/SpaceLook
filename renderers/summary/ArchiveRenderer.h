#pragma once

#include <QVector>
#include <QWidget>

#include "core/hovered_item_info.h"
#include "renderers/IPreviewRenderer.h"
#include "renderers/PreviewCancellationToken.h"
#include "renderers/PreviewLoadGuard.h"

class QLabel;
class OpenWithButton;
class QScrollArea;
class QSplitter;
class QStackedWidget;
class QTextEdit;
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
        QString sizeText;
        QString packedSizeText;
        QString modifiedText;
        qint64 size = -1;
        bool isDirectory = false;
    };

    struct ArchiveLoadResult
    {
        QVector<ArchiveEntry> entries;
        QString statusMessage;
        bool success = false;
    };

    struct EntryPreviewResult
    {
        QString entryPath;
        QString statusMessage;
        QByteArray data;
        bool isImage = false;
        bool isText = false;
        bool success = false;
    };

    void applyChrome();
    void showStatusMessage(const QString& message);
    void populateTree(const QVector<ArchiveEntry>& entries);
    void previewArchiveEntry(QTreeWidgetItem* item);
    void clearEntryPreview(const QString& message = QString());
    QTreeWidgetItem* ensureFolderItem(const QString& folderPath,
                                      QTreeWidgetItem* parentItem,
                                      const QString& folderName);
    static ArchiveLoadResult loadArchiveEntries(const QString& filePath, const PreviewCancellationToken& cancelToken);
    static EntryPreviewResult loadEntryPreview(const QString& archivePath,
                                               const ArchiveEntry& entry,
                                               const PreviewCancellationToken& cancelToken);
    void notifyLoadingState(bool loading);

    HoveredItemInfo m_info;
    PreviewLoadGuard m_loadGuard;
    PreviewCancellationToken m_cancelToken;
    PreviewCancellationToken m_entryPreviewCancelToken;
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
    QSplitter* m_contentSplitter = nullptr;
    QTreeWidget* m_treeWidget = nullptr;
    QStackedWidget* m_previewStack = nullptr;
    QLabel* m_emptyPreviewLabel = nullptr;
    QTextEdit* m_textPreview = nullptr;
    QScrollArea* m_imageScrollArea = nullptr;
    QLabel* m_imagePreview = nullptr;
};
