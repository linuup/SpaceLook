#include "renderers/pdf/PdfViewWidget.h"

#include <QPainter>
#include <QResizeEvent>
#include <QScrollBar>
#include <QWheelEvent>

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
{
    viewport()->setAutoFillBackground(false);
    setFrameShape(QFrame::NoFrame);
    setFocusPolicy(Qt::StrongFocus);
    horizontalScrollBar()->setSingleStep(28);
    verticalScrollBar()->setSingleStep(42);
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
        m_zoomFactor = fitWidthZoom();
    } else {
        m_zoomFactor = 1.0;
    }
    updateLayout();
}

void PdfViewWidget::clearDocument()
{
    m_document = nullptr;
    m_renderedPages.clear();
    m_pageSizesPoints.clear();
    m_zoomFactor = 1.0;
    updateLayout();
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
            painter.setPen(QColor(QStringLiteral("#7c91a8")));
            painter.drawText(target, Qt::AlignCenter, QStringLiteral("Loading page %1").arg(pageIndex + 1));
        }
    }
}

void PdfViewWidget::resizeEvent(QResizeEvent* event)
{
    const int previousViewportWidth = event && event->oldSize().isValid()
        ? event->oldSize().width()
        : viewport()->width();

    QAbstractScrollArea::resizeEvent(event);

    if (m_document && m_document->isLoaded() && previousViewportWidth != viewport()->width()) {
        m_zoomFactor = fitWidthZoom();
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
