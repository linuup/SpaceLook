#pragma once

#include <QAbstractScrollArea>
#include <QHash>
#include <QImage>
#include <QPoint>
#include <QVector>

class PdfDocument;
class QTimer;

struct PdfRenderedPage
{
    QImage image;
    QSize pixelSize;
    quint64 lastUsed = 0;
};

class PdfViewWidget : public QAbstractScrollArea
{
    Q_OBJECT

public:
    explicit PdfViewWidget(QWidget* parent = nullptr);

    void setDocument(PdfDocument* document);
    void clearDocument();
    void resetZoomToFitWidth();
    void setZoomFactor(double factor);
    double zoomFactor() const;
    int currentPageIndex() const;
    void scrollToPage(int pageIndex);

signals:
    void zoomFactorChanged(double factor);
    void currentPageChanged(int pageIndex, int pageCount);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    void clearRenderedPages();
    void updateLayout();
    void renderVisiblePages();
    void pruneRenderedPages(const QRect& keepRect);
    void enforceRenderedPageBudget(const QRect& visibleRect);
    qsizetype renderedPageCacheBytes() const;
    QRect pageRect(int pageIndex) const;
    QSize scaledPageSize(int pageIndex) const;
    int pageTopOffset(int pageIndex) const;
    int contentWidth() const;
    int contentHeight() const;
    double fitWidthZoom() const;
    double fitPageHeightZoom() const;
    void updateCurrentPageFromScroll();
    bool canPanView() const;
    void updatePanCursor();

    PdfDocument* m_document = nullptr;
    QHash<int, PdfRenderedPage> m_renderedPages;
    QVector<QSizeF> m_pageSizesPoints;
    QTimer* m_loadingTimer = nullptr;
    double m_zoomFactor = 1.0;
    int m_currentPageIndex = -1;
    int m_loadingFrame = 0;
    quint64 m_renderSerial = 0;
    bool m_autoFitZoom = true;
    bool m_panning = false;
    QPoint m_lastPanGlobalPos;
};
