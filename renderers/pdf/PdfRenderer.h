#pragma once

#include <functional>

#include <QPixmap>
#include <QSet>
#include <QVector>
#include <QWidget>

#include "core/hovered_item_info.h"
#include "renderers/pdf/PdfDocument.h"
#include "renderers/IPreviewRenderer.h"
#include "renderers/PreviewLoadGuard.h"

class QLabel;
class QLineEdit;
class QListWidget;
class OpenWithButton;
class PreviewHandlerHost;
class PdfViewWidget;
class SelectableTitleLabel;
class QSpinBox;
class QTimer;
class QToolButton;
class QWidget;

class PdfRenderer : public QWidget, public IPreviewRenderer
{
    Q_OBJECT

public:
    explicit PdfRenderer(QWidget* parent = nullptr);

    QString rendererId() const override;
    bool canHandle(const HoveredItemInfo& info) const override;
    QWidget* widget() override;
    void load(const HoveredItemInfo& info) override;
    void unload() override;
    void warmUp() override;
    void setSummaryFallbackCallback(std::function<void(const HoveredItemInfo&, const QString&)> callback) override;

private:
    void applyChrome();
    void showStatusMessage(const QString& message);
    void rebuildThumbnails();
    void updatePageInfo(int pageIndex, int pageCount);
    QPixmap createThumbnailPlaceholder(int pageIndex) const;
    QPixmap renderThumbnailPixmap(int pageIndex) const;
    void scheduleThumbnailPage(int pageIndex);
    void scheduleVisibleThumbnails();
    void renderNextThumbnail();
    bool isCurrentPdfLoad() const;
    void findSearchMatch(bool backwards);
    void rebuildSearchMatches();
    void updateSearchSummary();
    void resetSearch();
    void showSearchRow();
    void hideSearchRow(bool clearQuery);

    HoveredItemInfo m_info;
    PdfDocument m_document;
    PreviewLoadGuard m_loadGuard;
    PreviewLoadGuard::Token m_loadToken;
    std::function<void(const HoveredItemInfo&, const QString&)> m_summaryFallbackCallback;
    QWidget* m_headerRow = nullptr;
    QLabel* m_iconLabel = nullptr;
    SelectableTitleLabel* m_titleLabel = nullptr;
    QLabel* m_metaLabel = nullptr;
    QWidget* m_pathRow = nullptr;
    QLabel* m_pathTitleLabel = nullptr;
    QLabel* m_pathValueLabel = nullptr;
    OpenWithButton* m_openWithButton = nullptr;
    QLabel* m_statusLabel = nullptr;
    QWidget* m_searchRow = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QToolButton* m_searchPreviousButton = nullptr;
    QToolButton* m_searchNextButton = nullptr;
    QLabel* m_searchCountLabel = nullptr;
    QWidget* m_contentRow = nullptr;
    QWidget* m_thumbnailPanel = nullptr;
    QWidget* m_pageInfoRow = nullptr;
    QLabel* m_pageInfoLabel = nullptr;
    QSpinBox* m_pageInput = nullptr;
    QLabel* m_pageTotalLabel = nullptr;
    QListWidget* m_thumbnailList = nullptr;
    PdfViewWidget* m_pdfView = nullptr;
    PreviewHandlerHost* m_previewHandlerHost = nullptr;
    QTimer* m_thumbnailRenderTimer = nullptr;
    QVector<int> m_pendingThumbnailPages;
    QSet<int> m_renderedThumbnailPages;
    QVector<int> m_searchPageMatches;
    QString m_lastSearchQuery;
    int m_searchMatchIndex = -1;
};
