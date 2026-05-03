#pragma once

#include <QAbstractScrollArea>
#include <QHash>
#include <QImage>
#include <QVector>

class PdfDocument;

struct PdfRenderedPage
{
    QImage image;
    QSize pixelSize;
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

signals:
    void zoomFactorChanged(double factor);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    void updateLayout();
    void renderVisiblePages();
    QRect pageRect(int pageIndex) const;
    QSize scaledPageSize(int pageIndex) const;
    int pageTopOffset(int pageIndex) const;
    int contentWidth() const;
    int contentHeight() const;
    double fitWidthZoom() const;

    PdfDocument* m_document = nullptr;
    QHash<int, PdfRenderedPage> m_renderedPages;
    QVector<QSizeF> m_pageSizesPoints;
    double m_zoomFactor = 1.0;
};
