#include "renderers/pdf/PdfRenderer.h"

#include <QAbstractItemView>
#include <QCoreApplication>
#include <QDebug>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPainter>
#include <QPixmap>
#include <QScrollBar>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "renderers/pdf/PdfViewWidget.h"
#include "renderers/document/PreviewHandlerHost.h"
#include "renderers/FileTypeIconResolver.h"
#include "renderers/OpenWithButton.h"
#include "renderers/PreviewHeaderBar.h"
#include "renderers/PreviewStateVisuals.h"
#include "renderers/SelectableTitleLabel.h"
#include "widgets/SpaceLookWindow.h"

namespace {

bool isXpsPreviewFile(const QString& filePath)
{
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    return suffix == QStringLiteral("xps") || suffix == QStringLiteral("oxps");
}

}

PdfRenderer::PdfRenderer(QWidget* parent)
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
    , m_searchRow(new QWidget(this))
    , m_searchEdit(new QLineEdit(this))
    , m_searchPreviousButton(new QToolButton(this))
    , m_searchNextButton(new QToolButton(this))
    , m_searchCountLabel(new QLabel(this))
    , m_contentRow(new QWidget(this))
    , m_thumbnailPanel(new QWidget(this))
    , m_pageInfoRow(new QWidget(this))
    , m_pageInfoLabel(new QLabel(this))
    , m_pageInput(new QSpinBox(this))
    , m_pageTotalLabel(new QLabel(this))
    , m_thumbnailList(new QListWidget(this))
    , m_pdfView(new PdfViewWidget(this))
    , m_previewHandlerHost(new PreviewHandlerHost(this))
    , m_thumbnailRenderTimer(new QTimer(this))
{
    setAttribute(Qt::WA_StyledBackground, true);
    setObjectName(QStringLiteral("PdfRendererRoot"));
    m_headerRow->setObjectName(QStringLiteral("PdfHeaderRow"));
    m_iconLabel->setObjectName(QStringLiteral("PdfTypeIcon"));
    m_titleLabel->setObjectName(QStringLiteral("PdfTitle"));
    m_metaLabel->setObjectName(QStringLiteral("PdfMeta"));
    m_pathRow->setObjectName(QStringLiteral("PdfPathRow"));
    m_pathTitleLabel->setObjectName(QStringLiteral("PdfPathTitle"));
    m_pathValueLabel->setObjectName(QStringLiteral("PdfPathValue"));
    m_openWithButton->setObjectName(QStringLiteral("PdfOpenWithButton"));
    m_statusLabel->setObjectName(QStringLiteral("PdfStatus"));
    m_searchRow->setObjectName(QStringLiteral("PdfSearchRow"));
    m_searchEdit->setObjectName(QStringLiteral("PdfSearchEdit"));
    m_searchPreviousButton->setObjectName(QStringLiteral("PdfSearchPreviousButton"));
    m_searchNextButton->setObjectName(QStringLiteral("PdfSearchNextButton"));
    m_searchCountLabel->setObjectName(QStringLiteral("PdfSearchCount"));
    m_contentRow->setObjectName(QStringLiteral("PdfContentRow"));
    m_thumbnailPanel->setObjectName(QStringLiteral("PdfThumbnailPanel"));
    m_pageInfoRow->setObjectName(QStringLiteral("PdfPageInfoRow"));
    m_pageInfoLabel->setObjectName(QStringLiteral("PdfPageInfo"));
    m_pageInput->setObjectName(QStringLiteral("PdfPageInput"));
    m_pageTotalLabel->setObjectName(QStringLiteral("PdfPageTotal"));
    m_thumbnailList->setObjectName(QStringLiteral("PdfThumbnailList"));
    m_pdfView->setObjectName(QStringLiteral("PdfView"));
    m_previewHandlerHost->setObjectName(QStringLiteral("XpsPreviewHost"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 0, 12, 12);
    layout->setSpacing(14);
    layout->addWidget(m_headerRow);
    layout->addWidget(m_statusLabel);
    layout->addWidget(m_searchRow);
    layout->addWidget(m_contentRow, 1);

    auto* headerLayout = new QHBoxLayout(m_headerRow);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(12);
    auto* titleBlock = new PreviewHeaderBar(m_iconLabel, m_titleLabel, m_pathRow, m_openWithButton, m_headerRow);
    headerLayout->addWidget(titleBlock->contentWidget(), 1);

    auto* pathLayout = new QHBoxLayout(m_pathRow);
    pathLayout->setContentsMargins(0, 0, 0, 0);
    pathLayout->setSpacing(8);
    pathLayout->addWidget(m_pathValueLabel, 1);

    m_pathTitleLabel->hide();
    m_iconLabel->setFixedSize(72, 72);
    m_iconLabel->setScaledContents(false);
    m_iconLabel->setAlignment(Qt::AlignCenter);
    m_titleLabel->setWordWrap(true);
    m_pathValueLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_pathValueLabel->setWordWrap(true);
    m_pathValueLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_pathRow->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
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
    PreviewStateVisuals::prepareStatusLabel(m_statusLabel);
    m_statusLabel->hide();
    auto* searchLayout = new QHBoxLayout(m_searchRow);
    searchLayout->setContentsMargins(8, 4, 8, 4);
    searchLayout->setSpacing(6);
    m_searchEdit->setPlaceholderText(QCoreApplication::translate("SpaceLook", "Search PDF"));
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setFixedWidth(190);
    m_searchPreviousButton->setText(QStringLiteral("<"));
    m_searchNextButton->setText(QStringLiteral(">"));
    m_searchPreviousButton->setFixedSize(24, 24);
    m_searchNextButton->setFixedSize(24, 24);
    m_searchCountLabel->setMinimumWidth(46);
    searchLayout->addWidget(m_searchEdit);
    searchLayout->addWidget(m_searchPreviousButton);
    searchLayout->addWidget(m_searchNextButton);
    searchLayout->addWidget(m_searchCountLabel);
    searchLayout->addStretch(1);
    m_searchRow->hide();
    auto* contentLayout = new QHBoxLayout(m_contentRow);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(12);
    contentLayout->addWidget(m_thumbnailPanel, 0);
    contentLayout->addWidget(m_pdfView, 1);
    contentLayout->addWidget(m_previewHandlerHost, 1);
    m_previewHandlerHost->hide();

    auto* thumbnailLayout = new QVBoxLayout(m_thumbnailPanel);
    thumbnailLayout->setContentsMargins(10, 10, 10, 10);
    thumbnailLayout->setSpacing(10);
    thumbnailLayout->addWidget(m_pageInfoRow);
    thumbnailLayout->addWidget(m_thumbnailList, 1);
    auto* pageInfoLayout = new QHBoxLayout(m_pageInfoRow);
    pageInfoLayout->setContentsMargins(10, 7, 10, 7);
    pageInfoLayout->setSpacing(6);
    pageInfoLayout->addWidget(m_pageInfoLabel);
    pageInfoLayout->addWidget(m_pageInput, 1);
    pageInfoLayout->addWidget(m_pageTotalLabel);
    m_thumbnailPanel->setFixedWidth(168);
    m_pageInfoLabel->setAlignment(Qt::AlignCenter);
    m_pageInfoLabel->setText(QCoreApplication::translate("SpaceLook", "Page"));
    m_pageInput->setRange(0, 0);
    m_pageInput->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_pageInput->setAlignment(Qt::AlignCenter);
    m_pageInput->setKeyboardTracking(false);
    m_pageTotalLabel->setText(QStringLiteral("/ 0"));
    m_thumbnailList->setIconSize(QSize(112, 158));
    m_thumbnailList->setSpacing(10);
    m_thumbnailList->setViewMode(QListView::IconMode);
    m_thumbnailList->setFlow(QListView::TopToBottom);
    m_thumbnailList->setWrapping(false);
    m_thumbnailList->setMovement(QListView::Static);
    m_thumbnailList->setGridSize(QSize(138, 196));
    m_thumbnailList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_thumbnailList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_thumbnailList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_thumbnailList->setResizeMode(QListView::Adjust);
    m_thumbnailRenderTimer->setInterval(15);

    connect(m_titleLabel, &SelectableTitleLabel::copyFeedbackRequested, this, [this](const QString& message) {
        showStatusMessage(message);
    });

    connect(m_pdfView, &PdfViewWidget::zoomFactorChanged, this, [this](double factor) {
        qDebug().noquote() << QStringLiteral("[SpaceLookRender] PDF zoom factor=%1 path=\"%2\"")
            .arg(factor, 0, 'f', 2)
            .arg(m_info.filePath);
    });
    connect(m_pdfView, &PdfViewWidget::currentPageChanged, this, [this](int pageIndex, int pageCount) {
        updatePageInfo(pageIndex, pageCount);
    });
    connect(m_thumbnailList, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        if (!item) {
            return;
        }
        m_pdfView->scrollToPage(item->data(Qt::UserRole).toInt());
    });
    connect(m_pageInput, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int pageNumber) {
        if (pageNumber > 0) {
            m_pdfView->scrollToPage(pageNumber - 1);
        }
    });
    connect(m_thumbnailList->verticalScrollBar(), &QScrollBar::valueChanged, this, [this]() {
        scheduleVisibleThumbnails();
    });
    connect(m_thumbnailRenderTimer, &QTimer::timeout, this, [this]() {
        renderNextThumbnail();
    });
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this]() {
        findSearchMatch(false);
    });
    connect(m_searchEdit, &QLineEdit::returnPressed, this, [this]() {
        findSearchMatch(false);
    });
    connect(m_searchPreviousButton, &QToolButton::clicked, this, [this]() {
        findSearchMatch(true);
    });
    connect(m_searchNextButton, &QToolButton::clicked, this, [this]() {
        findSearchMatch(false);
    });
    auto* findShortcut = new QShortcut(QKeySequence::Find, this);
    findShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(findShortcut, &QShortcut::activated, this, &PdfRenderer::showSearchRow);
    auto* hideSearchShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), m_searchRow);
    hideSearchShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(hideSearchShortcut, &QShortcut::activated, this, [this]() {
        hideSearchRow(false);
    });

    applyChrome();
}

QString PdfRenderer::rendererId() const
{
    return QStringLiteral("pdf");
}

bool PdfRenderer::canHandle(const HoveredItemInfo& info) const
{
    return info.typeKey == QStringLiteral("pdf");
}

QWidget* PdfRenderer::widget()
{
    return this;
}

void PdfRenderer::warmUp()
{
    m_pdfView->winId();
    m_previewHandlerHost->winId();
}

void PdfRenderer::setSummaryFallbackCallback(std::function<void(const HoveredItemInfo&, const QString&)> callback)
{
    m_summaryFallbackCallback = std::move(callback);
}

void PdfRenderer::load(const HoveredItemInfo& info)
{
    m_info = info;
    m_loadToken = m_loadGuard.begin(info.filePath);
    qDebug().noquote() << QStringLiteral("[SpaceLookRender] PdfRenderer load path=\"%1\"").arg(info.filePath);

    m_thumbnailRenderTimer->stop();
    m_pendingThumbnailPages.clear();
    m_renderedThumbnailPages.clear();
    m_thumbnailList->clear();
    m_previewHandlerHost->unload();
    m_previewHandlerHost->hide();
    m_pdfView->clearDocument();
    m_document.unload();
    updatePageInfo(-1, 0);
    resetSearch();

    m_titleLabel->setText(info.fileName.trimmed().isEmpty() ? QCoreApplication::translate("SpaceLook", "PDF Preview") : info.fileName);
    m_titleLabel->setCopyText(info.fileName.trimmed().isEmpty() ? m_titleLabel->text() : info.fileName);
    m_iconLabel->setPixmap(FileTypeIconResolver::pixmapForInfo(info, m_iconLabel->contentsRect().size()));
    m_pathValueLabel->setText(info.filePath.trimmed().isEmpty() ? QCoreApplication::translate("SpaceLook", "(Unavailable)") : info.filePath);
    m_openWithButton->setTargetContext(info.filePath, info.typeKey);
    hideSearchRow(true);

    if (isXpsPreviewFile(info.filePath)) {
        showStatusMessage(QCoreApplication::translate("SpaceLook", "Opening XPS document through Windows Preview Handler..."));
        m_thumbnailPanel->hide();
        m_pdfView->hide();
        m_previewHandlerHost->show();

        QString errorMessage;
        if (!m_previewHandlerHost->openFile(info.filePath, &errorMessage)) {
            if (errorMessage.contains(QStringLiteral("No Windows Preview Handler"), Qt::CaseInsensitive)) {
                errorMessage = QCoreApplication::translate("SpaceLook", "No Windows XPS Preview Handler is registered. Install the Windows XPS Viewer optional feature, then try again.");
            }
            m_previewHandlerHost->hide();
            m_thumbnailPanel->hide();
            m_pdfView->hide();
            if (m_summaryFallbackCallback) {
                m_summaryFallbackCallback(info, errorMessage);
                return;
            }
            showStatusMessage(errorMessage.isEmpty()
                ? QCoreApplication::translate("SpaceLook", "Failed to open the XPS document.")
                : errorMessage);
            return;
        }

        showStatusMessage(QString());
        resetSearch();
        return;
    }

    m_previewHandlerHost->unload();
    m_previewHandlerHost->hide();
    m_thumbnailPanel->show();
    m_pdfView->show();

    showStatusMessage(QCoreApplication::translate("SpaceLook", "Opening PDF document..."));
    QString errorMessage;
    if (!m_document.load(info.filePath, &errorMessage)) {
        showStatusMessage(errorMessage.isEmpty()
            ? QCoreApplication::translate("SpaceLook", "Failed to open the PDF document.")
            : errorMessage);
        m_pdfView->clearDocument();
        rebuildThumbnails();
        resetSearch();
        return;
    }

    qDebug().noquote() << QStringLiteral("[SpaceLookRender] PdfRenderer opened pages=%1 path=\"%2\"")
        .arg(m_document.pageCount())
        .arg(info.filePath);

    showStatusMessage(QCoreApplication::translate("SpaceLook", "Rendering first PDF page..."));
    m_pdfView->setDocument(&m_document);
    rebuildThumbnails();
    updatePageInfo(0, m_document.pageCount());
    resetSearch();
    showStatusMessage(QString());
}

void PdfRenderer::unload()
{
    m_loadGuard.cancel();
    m_loadToken = PreviewLoadGuard::Token();
    m_thumbnailRenderTimer->stop();
    m_pendingThumbnailPages.clear();
    m_renderedThumbnailPages.clear();
    m_pathValueLabel->clear();
    m_openWithButton->setTargetContext(QString(), QString());
    showStatusMessage(QString());
    m_previewHandlerHost->unload();
    m_previewHandlerHost->hide();
    m_thumbnailPanel->show();
    m_pdfView->show();
    m_pdfView->clearDocument();
    m_document.unload();
    rebuildThumbnails();
    updatePageInfo(-1, 0);
    hideSearchRow(true);
    m_info = HoveredItemInfo();
}

void PdfRenderer::rebuildThumbnails()
{
    m_thumbnailRenderTimer->stop();
    m_pendingThumbnailPages.clear();
    m_renderedThumbnailPages.clear();
    m_thumbnailList->clear();
    if (!m_document.isLoaded()) {
        return;
    }

    constexpr int thumbHeight = 158;
    for (int pageIndex = 0; pageIndex < m_document.pageCount(); ++pageIndex) {
        auto* item = new QListWidgetItem(QIcon(createThumbnailPlaceholder(pageIndex)), QString::number(pageIndex + 1), m_thumbnailList);
        item->setData(Qt::UserRole, pageIndex);
        item->setTextAlignment(Qt::AlignHCenter);
        item->setSizeHint(QSize(138, thumbHeight + 38));
    }

    const PreviewLoadGuard::Token thumbnailToken = m_loadToken;
    QTimer::singleShot(0, this, [this, thumbnailToken]() {
        if (!m_loadGuard.isCurrent(thumbnailToken, m_info.filePath)) {
            return;
        }
        scheduleVisibleThumbnails();
    });
}

void PdfRenderer::updatePageInfo(int pageIndex, int pageCount)
{
    if (pageIndex < 0 || pageCount <= 0) {
        const QSignalBlocker blocker(m_pageInput);
        m_pageInput->setRange(0, 0);
        m_pageInput->setValue(0);
        m_pageTotalLabel->setText(QStringLiteral("/ 0"));
        return;
    }

    {
        const QSignalBlocker blocker(m_pageInput);
        m_pageInput->setRange(1, pageCount);
        m_pageInput->setValue(pageIndex + 1);
        m_pageTotalLabel->setText(QStringLiteral("/ %1").arg(pageCount));
    }
    if (pageIndex >= 0 && pageIndex < m_thumbnailList->count()) {
        const QSignalBlocker blocker(m_thumbnailList);
        m_thumbnailList->setCurrentRow(pageIndex);
        m_thumbnailList->scrollToItem(m_thumbnailList->item(pageIndex), QAbstractItemView::PositionAtCenter);
    }

    scheduleThumbnailPage(pageIndex);
    scheduleThumbnailPage(pageIndex - 1);
    scheduleThumbnailPage(pageIndex + 1);
    scheduleVisibleThumbnails();
}

QPixmap PdfRenderer::createThumbnailPlaceholder(int pageIndex) const
{
    constexpr int thumbWidth = 112;
    constexpr int thumbHeight = 158;
    QPixmap thumbnail(thumbWidth, thumbHeight);
    thumbnail.fill(QColor(QStringLiteral("#f3f6f9")));

    QPainter painter(&thumbnail);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRect pageRect(8, 6, thumbWidth - 16, thumbHeight - 12);
    painter.setPen(QColor(QStringLiteral("#d7e0ea")));
    painter.setBrush(QColor(QStringLiteral("#ffffff")));
    painter.drawRect(pageRect.adjusted(0, 0, -1, -1));

    painter.setPen(QColor(QStringLiteral("#9badbf")));
    painter.drawText(pageRect, Qt::AlignCenter, QCoreApplication::translate("SpaceLook", "Page %1").arg(pageIndex + 1));
    return thumbnail;
}

QPixmap PdfRenderer::renderThumbnailPixmap(int pageIndex) const
{
    constexpr int thumbWidth = 112;
    constexpr int thumbHeight = 158;
    if (!isCurrentPdfLoad() || !m_document.isLoaded() || pageIndex < 0 || pageIndex >= m_document.pageCount()) {
        return createThumbnailPlaceholder(pageIndex);
    }

    const QSizeF pageSize = m_document.pageSizePoints(pageIndex);
    const double scale = pageSize.width() > 0.0
        ? static_cast<double>(thumbWidth) / pageSize.width()
        : 0.18;

    QString errorMessage;
    const QImage renderedPage = m_document.renderPage(pageIndex, scale, Qt::white, &errorMessage);
    if (renderedPage.isNull()) {
        return createThumbnailPlaceholder(pageIndex);
    }

    QPixmap thumbnail(thumbWidth, thumbHeight);
    thumbnail.fill(QColor(QStringLiteral("#f3f6f9")));

    QPainter painter(&thumbnail);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.setPen(QColor(QStringLiteral("#d7e0ea")));
    painter.setBrush(Qt::white);

    const QSize fittedSize = renderedPage.size().scaled(thumbnail.size(), Qt::KeepAspectRatio);
    const QRect pageRect(
        (thumbWidth - fittedSize.width()) / 2,
        (thumbHeight - fittedSize.height()) / 2,
        fittedSize.width(),
        fittedSize.height());
    painter.drawRect(pageRect.adjusted(0, 0, -1, -1));
    painter.drawImage(pageRect, renderedPage);
    return thumbnail;
}

void PdfRenderer::scheduleThumbnailPage(int pageIndex)
{
    if (!isCurrentPdfLoad() ||
        !m_document.isLoaded() ||
        pageIndex < 0 ||
        pageIndex >= m_document.pageCount() ||
        m_renderedThumbnailPages.contains(pageIndex) ||
        m_pendingThumbnailPages.contains(pageIndex)) {
        return;
    }

    m_pendingThumbnailPages.append(pageIndex);
    if (!m_thumbnailRenderTimer->isActive()) {
        m_thumbnailRenderTimer->start();
    }
}

void PdfRenderer::scheduleVisibleThumbnails()
{
    if (!isCurrentPdfLoad() || !m_document.isLoaded() || m_thumbnailList->count() <= 0) {
        return;
    }

    int firstPage = m_thumbnailList->currentRow() >= 0 ? m_thumbnailList->currentRow() : 0;
    int lastPage = firstPage;
    const QModelIndex firstVisible = m_thumbnailList->indexAt(QPoint(8, 8));
    const QModelIndex lastVisible = m_thumbnailList->indexAt(QPoint(8, qMax(8, m_thumbnailList->viewport()->height() - 8)));
    if (firstVisible.isValid()) {
        firstPage = firstVisible.row();
    }
    if (lastVisible.isValid()) {
        lastPage = lastVisible.row();
    } else {
        lastPage = qMin(m_thumbnailList->count() - 1, firstPage + 4);
    }

    const int fromPage = qMax(0, qMin(firstPage, lastPage) - 2);
    const int toPage = qMin(m_thumbnailList->count() - 1, qMax(firstPage, lastPage) + 2);
    for (int pageIndex = fromPage; pageIndex <= toPage; ++pageIndex) {
        scheduleThumbnailPage(pageIndex);
    }
}

void PdfRenderer::renderNextThumbnail()
{
    if (!isCurrentPdfLoad() || !m_document.isLoaded()) {
        m_thumbnailRenderTimer->stop();
        m_pendingThumbnailPages.clear();
        return;
    }

    while (!m_pendingThumbnailPages.isEmpty()) {
        const int pageIndex = m_pendingThumbnailPages.takeFirst();
        if (m_renderedThumbnailPages.contains(pageIndex) ||
            pageIndex < 0 ||
            pageIndex >= m_thumbnailList->count()) {
            continue;
        }

        QListWidgetItem* item = m_thumbnailList->item(pageIndex);
        if (!item) {
            continue;
        }

        item->setIcon(QIcon(renderThumbnailPixmap(pageIndex)));
        if (!isCurrentPdfLoad()) {
            return;
        }
        m_renderedThumbnailPages.insert(pageIndex);
        break;
    }

    if (m_pendingThumbnailPages.isEmpty()) {
        m_thumbnailRenderTimer->stop();
    }
}

bool PdfRenderer::isCurrentPdfLoad() const
{
    return m_loadGuard.isCurrent(m_loadToken, m_info.filePath);
}

void PdfRenderer::findSearchMatch(bool backwards)
{
    rebuildSearchMatches();
    if (m_searchPageMatches.isEmpty()) {
        updateSearchSummary();
        return;
    }

    const int nextIndex = m_searchMatchIndex < 0
        ? 0
        : (backwards
            ? (m_searchMatchIndex + m_searchPageMatches.size() - 1) % m_searchPageMatches.size()
            : (m_searchMatchIndex + 1) % m_searchPageMatches.size());
    m_searchMatchIndex = nextIndex;
    m_pdfView->scrollToPage(m_searchPageMatches.at(m_searchMatchIndex));
    updateSearchSummary();
}

void PdfRenderer::rebuildSearchMatches()
{
    const QString query = m_searchEdit ? m_searchEdit->text().trimmed() : QString();
    if (query == m_lastSearchQuery && !m_searchPageMatches.isEmpty()) {
        return;
    }

    m_lastSearchQuery = query;
    m_searchPageMatches.clear();
    m_searchMatchIndex = -1;
    if (query.isEmpty() || !m_document.isLoaded()) {
        return;
    }

    for (int pageIndex = 0; pageIndex < m_document.pageCount(); ++pageIndex) {
        if (m_document.pageText(pageIndex).contains(query, Qt::CaseInsensitive)) {
            m_searchPageMatches.append(pageIndex);
        }
    }
}

void PdfRenderer::updateSearchSummary()
{
    if (!m_searchCountLabel) {
        return;
    }

    const QString query = m_searchEdit ? m_searchEdit->text() : QString();
    if (query.trimmed().isEmpty()) {
        m_searchCountLabel->clear();
        return;
    }

    rebuildSearchMatches();
    m_searchCountLabel->setText(m_searchPageMatches.isEmpty()
        ? QStringLiteral("0/0")
        : QStringLiteral("%1/%2").arg(m_searchMatchIndex >= 0 ? m_searchMatchIndex + 1 : 1).arg(m_searchPageMatches.size()));
}

void PdfRenderer::resetSearch()
{
    m_searchPageMatches.clear();
    m_lastSearchQuery.clear();
    m_searchMatchIndex = -1;
    if (m_searchEdit && !m_searchEdit->text().trimmed().isEmpty()) {
        findSearchMatch(false);
        return;
    }
    if (m_searchCountLabel) {
        m_searchCountLabel->clear();
    }
}

void PdfRenderer::showSearchRow()
{
    if (!m_searchRow) {
        return;
    }

    m_searchRow->show();
    if (m_searchEdit) {
        m_searchEdit->setFocus(Qt::ShortcutFocusReason);
        m_searchEdit->selectAll();
    }
}

void PdfRenderer::hideSearchRow(bool clearQuery)
{
    if (clearQuery && m_searchEdit) {
        m_searchEdit->clear();
    }
    m_searchPageMatches.clear();
    m_lastSearchQuery.clear();
    m_searchMatchIndex = -1;
    if (m_searchCountLabel) {
        m_searchCountLabel->clear();
    }
    if (m_searchRow) {
        m_searchRow->hide();
    }
}

void PdfRenderer::applyChrome()
{
    setStyleSheet(
        "#PdfRendererRoot {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:1,"
        "      stop:0 #fcfdff,"
        "      stop:1 #f4f8fc);"
        "  border-radius: 0px;"
        "}"
        "QLabel {"
        "  color: #18324a;"
        "}"
        "#PdfTypeIcon {"
        "  background: transparent;"
        "  border: none;"
        "  padding: 0px;"
        "}"
        "#PdfTitle {"
        "  color: #0f2740;"
        "}"
        "#PdfMeta {"
        "  color: #5c738b;"
        "}"
        "#PdfPathTitle {"
        "  color: #16324a;"
        "  font-family: 'Segoe UI Rounded';"
        "}"
        "#PdfPathValue {"
        "  color: #445d76;"
        "}"
        "#PdfOpenWithButton QToolButton {"
        "  background: rgba(238, 244, 252, 0.96);"
        "  color: #17324b;"
        "  border: 1px solid rgba(193, 208, 224, 0.95);"
        "  padding: 3px 8px;"
        "  min-height: 24px;"
        "}"
        "#PdfOpenWithButton QToolButton:hover {"
        "  background: rgba(245, 249, 255, 1.0);"
        "}"
        "#PdfOpenWithButton QToolButton:pressed {"
        "  background: rgba(224, 234, 246, 1.0);"
        "}"
        "#PdfOpenWithButton #OpenWithPrimaryButton {"
        "  border-top-left-radius: 10px;"
        "  border-bottom-left-radius: 10px;"
        "  border-top-right-radius: 0px;"
        "  border-bottom-right-radius: 0px;"
        "  min-width: 28px;"
        "}"
        "#PdfOpenWithButton #OpenWithExpandButton {"
        "  border-left: none;"
        "  border-top-left-radius: 0px;"
        "  border-bottom-left-radius: 0px;"
        "  border-top-right-radius: 10px;"
        "  border-bottom-right-radius: 10px;"
        "  min-width: 22px;"
        "  padding-left: 5px;"
        "  padding-right: 5px;"
        "}"
        "#PdfStatus {"
        "  color: #27568b;"
        "  background: rgba(220, 235, 255, 0.92);"
        "  border: 1px solid rgba(164, 193, 229, 0.95);"
        "  border-radius: 12px;"
        "  padding: 10px 14px;"
        "}"
        "#PdfSearchRow {"
        "  background: rgba(238, 244, 252, 0.96);"
        "  border: 1px solid rgba(193, 208, 224, 0.95);"
        "  border-radius: 12px;"
        "}"
        "#PdfSearchEdit {"
        "  color: #17324b;"
        "  background: transparent;"
        "  border: none;"
        "  selection-background-color: #cfe3ff;"
        "}"
        "#PdfSearchPreviousButton, #PdfSearchNextButton {"
        "  color: #17324b;"
        "  background: transparent;"
        "  border: none;"
        "  border-radius: 8px;"
        "}"
        "#PdfSearchPreviousButton:hover, #PdfSearchNextButton:hover {"
        "  background: rgba(215, 229, 246, 0.95);"
        "}"
        "#PdfSearchCount {"
        "  color: #526b85;"
        "}"
        "#PdfContentRow {"
        "  background: transparent;"
        "}"
        "#PdfThumbnailPanel {"
        "  background: #f6f9fc;"
        "  border: 1px solid #d3dee9;"
        "  border-radius: 18px;"
        "}"
        "#PdfPageInfoRow {"
        "  color: #19334d;"
        "  background: rgba(255, 255, 255, 0.82);"
        "  border: 1px solid #dbe5ef;"
        "  border-radius: 12px;"
        "}"
        "#PdfPageInfo, #PdfPageTotal {"
        "  color: #19334d;"
        "}"
        "#PdfPageInput {"
        "  color: #102d47;"
        "  background: #ffffff;"
        "  border: 1px solid #c7d6e5;"
        "  border-radius: 8px;"
        "  padding: 3px 6px;"
        "  min-height: 22px;"
        "}"
        "#PdfPageInput:focus {"
        "  border: 1px solid #0078d4;"
        "}"
        "#PdfThumbnailList {"
        "  background: transparent;"
        "  border: none;"
        "  outline: none;"
        "  color: #203a54;"
        "}"
        "#PdfThumbnailList::item {"
        "  background: transparent;"
        "  border: none;"
        "  border-radius: 0px;"
        "  padding: 7px 5px;"
        "}"
        "#PdfThumbnailList::item:hover {"
        "  background: rgba(218, 232, 246, 0.82);"
        "  border-radius: 8px;"
        "}"
        "#PdfThumbnailList::item:selected {"
        "  background: transparent;"
        "  border: none;"
        "  border-radius: 0px;"
        "  color: #0078d4;"
        "}"
        "#PdfThumbnailList::item:selected:hover {"
        "  background: rgba(218, 232, 246, 0.82);"
        "  border: none;"
        "  border-radius: 8px;"
        "  color: #0078d4;"
        "}"
        "#PdfThumbnailList QScrollBar:vertical {"
        "  background: transparent;"
        "  border: none;"
        "  width: 7px;"
        "}"
        "#PdfThumbnailList QScrollBar::handle:vertical {"
        "  background: #91a2b4;"
        "  min-height: 42px;"
        "  border-radius: 3px;"
        "}"
        "#PdfView {"
        "  background: #eef3f8;"
        "  border: none;"
        "  border-radius: 0px;"
        "}"
        "#PdfView QScrollBar:vertical {"
        "  background: rgba(232, 238, 245, 0.8);"
        "  border: none;"
        "  width: 8px;"
        "  margin: 10px 0 10px 0;"
        "  border-radius: 4px;"
        "}"
        "#PdfView QScrollBar::handle:vertical {"
        "  background: #7c8fa8;"
        "  min-height: 52px;"
        "  border-radius: 4px;"
        "}"
        "#PdfView QScrollBar::handle:vertical:hover {"
        "  background: #6d829d;"
        "}"
        "#PdfView QScrollBar::handle:vertical:pressed {"
        "  background: #61768f;"
        "}"
        "#PdfView QScrollBar:horizontal {"
        "  background: rgba(232, 238, 245, 0.8);"
        "  border: none;"
        "  height: 8px;"
        "  margin: 0 10px 0 10px;"
        "  border-radius: 4px;"
        "}"
        "#PdfView QScrollBar::handle:horizontal {"
        "  background: #7c8fa8;"
        "  min-width: 52px;"
        "  border-radius: 4px;"
        "}"
        "#PdfView QScrollBar::handle:horizontal:hover {"
        "  background: #6d829d;"
        "}"
        "#PdfView QScrollBar::handle:horizontal:pressed {"
        "  background: #61768f;"
        "}"
        "#PdfView QScrollBar::add-line:vertical, #PdfView QScrollBar::sub-line:vertical,"
        "#PdfView QScrollBar::add-line:horizontal, #PdfView QScrollBar::sub-line:horizontal {"
        "  width: 0px;"
        "  height: 0px;"
        "}"
        "#PdfView QScrollBar::add-page:vertical, #PdfView QScrollBar::sub-page:vertical,"
        "#PdfView QScrollBar::add-page:horizontal, #PdfView QScrollBar::sub-page:horizontal {"
        "  background: transparent;"
        "}"
    );

    QFont titleFont;
    titleFont.setFamily(QStringLiteral("Segoe UI Rounded"));
    titleFont.setPixelSize(20);
    titleFont.setWeight(QFont::Bold);
    m_titleLabel->setFont(titleFont);
    m_titleLabel->setWordWrap(true);

    QFont metaFont;
    metaFont.setFamily(QStringLiteral("Segoe UI Rounded"));
    metaFont.setPixelSize(13);
    m_metaLabel->setFont(metaFont);
    m_pathTitleLabel->setFont(metaFont);
    m_pathValueLabel->setFont(metaFont);
    m_statusLabel->setFont(metaFont);
    m_pageInfoLabel->setFont(metaFont);
    m_pageInput->setFont(metaFont);
    m_pageTotalLabel->setFont(metaFont);
    m_metaLabel->setWordWrap(true);
    m_pathValueLabel->setWordWrap(true);
}

void PdfRenderer::showStatusMessage(const QString& message)
{
    if (message.trimmed().isEmpty()) {
        PreviewStateVisuals::clearStatus(m_statusLabel);
        return;
    }

    PreviewStateVisuals::showStatus(m_statusLabel, message);
    QTimer::singleShot(1400, m_statusLabel, [label = m_statusLabel]() {
        if (label) {
            PreviewStateVisuals::clearStatus(label);
        }
    });
}
