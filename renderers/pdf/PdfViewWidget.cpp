#include "renderers/pdf/PdfViewWidget.h"

#include <QPainter>
#include <QResizeEvent>
#include <QScrollBar>
#include <QTimer>
#include <QWheelEvent>
#include <QtMath>

#include "renderers/pdf/PdfDocument.h"

namespace {

constexpr int kPageMargin = 24;
constexpr int kPageSpacing = 18;
constexpr int kMaxReadablePageWidth = 1120;
constexpr double kMinZoom = 0.25;
constexpr double kMaxZoom = 4.0;

}

PdfViewWidget::PdfViewWidget(QWidget* parent)
    : QAbstractScrollArea(parent)
    , m_loadingTimer(new QTimer(this))
{
    viewport()->setAutoFillBackground(false);
    setFrameShape(QFrame::NoFrame);
    setFocusPolicy(Qt::StrongFocus);
    horizontalScrollBar()->setSingleStep(28);
    verticalScrollBar()->setSingleStep(42);
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this]() {
        updateCurrentPageFromScroll();
    });
    connect(m_loadingTimer, &QTimer::timeout, this, [this]() {
        m_loadingFrame = (m_loadingFrame + 1) % 8;
        viewport()->update();
    });
    m_loadingTimer->start(120);
}

void PdfViewWidget::setDocument(PdfDocument* document)
{
    m_document = document;
    m_renderedPages.clear();
    m_pageSizesPoints.clear();
    if (m_document && m_document->isLoaded()) {
        for (int pageIndex = 0; pageIndex < m_document->pageCount(); ++pageIndex) {
            m_pageSizesPoints.append(m_document->pageSizePoints(pageIndex));
        }
        m_zoomFactor = fitPageHeightZoom();
        m_currentPageIndex = m_pageSizesPoints.isEmpty() ? -1 : 0;
    } else {
        m_zoomFactor = 1.0;
        m_currentPageIndex = -1;
    }
    updateLayout();
    updateCurrentPageFromScroll();
}

void PdfViewWidget::clearDocument()
{
    m_document = nullptr;
    m_renderedPages.clear();
    m_pageSizesPoints.clear();
    m_zoomFactor = 1.0;
    m_currentPageIndex = -1;
    updateLayout();
    emit currentPageChanged(-1, 0);
}

void PdfViewWidget::resetZoomToFitWidth()
{
    if (!m_document || !m_document->isLoaded()) {
        return;
    }
    setZoomFactor(fitWidthZoom());
}

void PdfViewWidget::setZoomFactor(double factor)
{
    const double clamped = qBound(kMinZoom, factor, kMaxZoom);
    if (qFuzzyCompare(m_zoomFactor, clamped)) {
        return;
    }

    m_zoomFactor = clamped;
    m_renderedPages.clear();
    updateLayout();
    emit zoomFactorChanged(m_zoomFactor);
}

double PdfViewWidget::zoomFactor() const
{
    return m_zoomFactor;
}

int PdfViewWidget::currentPageIndex() const
{
    return m_currentPageIndex;
}

void PdfViewWidget::scrollToPage(int pageIndex)
{
    if (!m_document || !m_document->isLoaded() || pageIndex < 0 || pageIndex >= m_pageSizesPoints.size()) {
        return;
    }

    verticalScrollBar()->setValue(pageTopOffset(pageIndex));
    updateCurrentPageFromScroll();
}

void PdfViewWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(viewport());
    painter.fillRect(rect(), QColor(QStringLiteral("#eef3f8")));

    if (!m_document || !m_document->isLoaded()) {
        return;
    }

    renderVisiblePages();

    for (int pageIndex = 0; pageIndex < m_pageSizesPoints.size(); ++pageIndex) {
        const QRect target = pageRect(pageIndex).translated(-horizontalScrollBar()->value(), -verticalScrollBar()->value());
        if (!target.intersects(viewport()->rect().adjusted(-80, -80, 80, 80))) {
            continue;
        }

        painter.fillRect(target.adjusted(-1, -1, 1, 1), QColor(QStringLiteral("#c8d3de")));
        painter.fillRect(target, QColor(QStringLiteral("#fbfcfe")));

        const auto pageIt = m_renderedPages.constFind(pageIndex);
        if (pageIt != m_renderedPages.constEnd() && !pageIt->image.isNull()) {
            painter.drawImage(target.topLeft(), pageIt->image);
        } else {
            const QPointF center = target.center();
            constexpr int dotCount = 8;
            constexpr double radius = 18.0;
            constexpr double pi = 3.14159265358979323846;
            painter.setPen(Qt::NoPen);
            for (int dotIndex = 0; dotIndex < dotCount; ++dotIndex) {
                const int phase = (dotIndex + m_loadingFrame) % dotCount;
                QColor color(QStringLiteral("#2f7fbf"));
                color.setAlpha(60 + phase * 22);
                painter.setBrush(color);
                const double angle = (2.0 * pi * dotIndex) / dotCount;
                const QPointF dotCenter(
                    center.x() + qCos(angle) * radius,
                    center.y() + qSin(angle) * radius);
                painter.drawEllipse(dotCenter, 3.8, 3.8);
            }
        }
    }
}

void PdfViewWidget::resizeEvent(QResizeEvent* event)
{
    const int previousViewportWidth = event && event->oldSize().isValid()
        ? event->oldSize().width()
        : viewport()->width();
    const int previousViewportHeight = event && event->oldSize().isValid()
        ? event->oldSize().height()
        : viewport()->height();

    QAbstractScrollArea::resizeEvent(event);

    if (m_document && m_document->isLoaded() &&
        (previousViewportWidth != viewport()->width() || previousViewportHeight != viewport()->height())) {
        m_zoomFactor = fitPageHeightZoom();
        m_renderedPages.clear();
        horizontalScrollBar()->setValue(0);
    }
    updateLayout();
}

void PdfViewWidget::wheelEvent(QWheelEvent* event)
{
    if (event->modifiers().testFlag(Qt::ControlModifier)) {
        const double nextZoom = event->angleDelta().y() > 0
            ? m_zoomFactor * 1.1
            : m_zoomFactor / 1.1;
        setZoomFactor(nextZoom);
        event->accept();
        return;
    }

    QAbstractScrollArea::wheelEvent(event);
}

void PdfViewWidget::updateLayout()
{
    if (!m_document || !m_document->isLoaded()) {
        horizontalScrollBar()->setRange(0, 0);
        verticalScrollBar()->setRange(0, 0);
        viewport()->update();
        return;
    }

    horizontalScrollBar()->setPageStep(viewport()->width());
    verticalScrollBar()->setPageStep(viewport()->height());
    horizontalScrollBar()->setRange(0, qMax(0, contentWidth() - viewport()->width()));
    verticalScrollBar()->setRange(0, qMax(0, contentHeight() - viewport()->height()));
    updateCurrentPageFromScroll();
    viewport()->update();
}

void PdfViewWidget::renderVisiblePages()
{
    if (!m_document || !m_document->isLoaded()) {
        return;
    }

    const QRect visibleRect(
        horizontalScrollBar()->value(),
        verticalScrollBar()->value(),
        viewport()->width(),
        viewport()->height());

    for (int pageIndex = 0; pageIndex < m_pageSizesPoints.size(); ++pageIndex) {
        const QRect targetRect = pageRect(pageIndex);
        if (!targetRect.intersects(visibleRect.adjusted(-120, -120, 120, 120))) {
            continue;
        }

        const QSize requiredSize = scaledPageSize(pageIndex);
        const auto pageIt = m_renderedPages.constFind(pageIndex);
        if (pageIt != m_renderedPages.constEnd() &&
            pageIt->pixelSize == requiredSize &&
            !pageIt->image.isNull()) {
            continue;
        }

        QString errorMessage;
        PdfRenderedPage renderedPage;
        renderedPage.image = m_document->renderPage(pageIndex, m_zoomFactor, Qt::white, &errorMessage);
        renderedPage.pixelSize = requiredSize;
        if (!renderedPage.image.isNull()) {
            m_renderedPages.insert(pageIndex, renderedPage);
        }
        Q_UNUSED(errorMessage);
    }
}

QRect PdfViewWidget::pageRect(int pageIndex) const
{
    const QSize size = scaledPageSize(pageIndex);
    if (!size.isValid()) {
        return QRect(kPageMargin, kPageMargin, 0, 0);
    }

    const int layoutWidth = qMax(contentWidth(), viewport()->width());
    const int x = qMax(kPageMargin, (layoutWidth - size.width()) / 2);
    const int y = pageTopOffset(pageIndex);
    return QRect(x, y, size.width(), size.height());
}

QSize PdfViewWidget::scaledPageSize(int pageIndex) const
{
    if (pageIndex < 0 || pageIndex >= m_pageSizesPoints.size()) {
        return QSize();
    }

    const QSizeF points = m_pageSizesPoints.at(pageIndex);
    return QSize(
        qMax(1, qRound(points.width() * m_zoomFactor)),
        qMax(1, qRound(points.height() * m_zoomFactor)));
}

int PdfViewWidget::pageTopOffset(int pageIndex) const
{
    int y = kPageMargin;
    for (int index = 0; index < pageIndex; ++index) {
        y += scaledPageSize(index).height() + kPageSpacing;
    }
    return y;
}

int PdfViewWidget::contentWidth() const
{
    int maxWidth = 0;
    for (int pageIndex = 0; pageIndex < m_pageSizesPoints.size(); ++pageIndex) {
        maxWidth = qMax(maxWidth, scaledPageSize(pageIndex).width());
    }
    return maxWidth + kPageMargin * 2;
}

int PdfViewWidget::contentHeight() const
{
    if (m_pageSizesPoints.isEmpty()) {
        return kPageMargin * 2;
    }

    int totalHeight = kPageMargin * 2;
    for (int pageIndex = 0; pageIndex < m_pageSizesPoints.size(); ++pageIndex) {
        totalHeight += scaledPageSize(pageIndex).height();
        if (pageIndex + 1 < m_pageSizesPoints.size()) {
            totalHeight += kPageSpacing;
        }
    }
    return totalHeight;
}

double PdfViewWidget::fitWidthZoom() const
{
    if (!m_document || !m_document->isLoaded()) {
        return 1.0;
    }

    const QSizeF points = m_pageSizesPoints.isEmpty() ? QSizeF() : m_pageSizesPoints.first();
    if (points.width() <= 0.0) {
        return 1.0;
    }

    const int viewportWidth = qMax(200, viewport()->width());
    const int centeredReadableWidth = qMin(kMaxReadablePageWidth, viewportWidth - kPageMargin * 2);
    const double availableWidth = qMax(200, static_cast<int>(centeredReadableWidth));
    return qBound(kMinZoom, availableWidth / points.width(), kMaxZoom);
}

double PdfViewWidget::fitPageHeightZoom() const
{
    if (!m_document || !m_document->isLoaded()) {
        return 1.0;
    }

    const QSizeF points = m_pageSizesPoints.isEmpty() ? QSizeF() : m_pageSizesPoints.first();
    if (points.height() <= 0.0) {
        return 1.0;
    }

    const int viewportHeight = qMax(260, viewport()->height());
    const double availableHeight = qMax(220, viewportHeight - kPageMargin * 2);
    return qBound(kMinZoom, availableHeight / points.height(), kMaxZoom);
}

void PdfViewWidget::updateCurrentPageFromScroll()
{
    if (!m_document || !m_document->isLoaded() || m_pageSizesPoints.isEmpty()) {
        if (m_currentPageIndex != -1) {
            m_currentPageIndex = -1;
            emit currentPageChanged(-1, 0);
        }
        return;
    }

    const int referenceY = verticalScrollBar()->value() + qMax(1, viewport()->height() / 3);
    int nextPageIndex = 0;
    for (int pageIndex = 0; pageIndex < m_pageSizesPoints.size(); ++pageIndex) {
        const QRect rect = pageRect(pageIndex);
        if (referenceY >= rect.top()) {
            nextPageIndex = pageIndex;
        }
        if (referenceY < rect.bottom()) {
            break;
        }
    }

    if (m_currentPageIndex == nextPageIndex) {
        return;
    }

    m_currentPageIndex = nextPageIndex;
    emit currentPageChanged(m_currentPageIndex, m_pageSizesPoints.size());
}
