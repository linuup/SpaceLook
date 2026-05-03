#pragma once

#include <QImage>
#include <QPixmap>
#include <QWidget>

#include "core/hovered_item_info.h"
#include "renderers/IPreviewRenderer.h"

class QLabel;
class QMovie;
class OpenWithButton;
class QScrollArea;
class SelectableTitleLabel;
class QWidget;

class ImageRenderer : public QWidget, public IPreviewRenderer
{
    Q_OBJECT

public:
    explicit ImageRenderer(QWidget* parent = nullptr);

    QString rendererId() const override;
    bool canHandle(const HoveredItemInfo& info) const override;
    QWidget* widget() override;
    void load(const HoveredItemInfo& info) override;
    void unload() override;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void applyChrome();
    QImage loadImageForPath(const QString& filePath) const;
    QImage loadThumbnailImageForPath(const QString& filePath) const;
    QImage loadHeifImageForPath(const QString& filePath) const;
    QImage imageFromHeifImage(struct heif_image* image) const;
    QImage loadShellThumbnailForPath(const QString& filePath, int edgeLength) const;
    bool tryLoadAnimatedImage(const QString& filePath);
    void clearAnimatedImage();
    void showStatusMessage(const QString& message);
    void updatePixmapView();
    void updateMovieView();
    void adjustZoom(double zoomStep, const QPoint& viewportPos);
    void resetZoom();
    QSize currentSourceSize() const;
    QSize scaledVisualSize(const QSize& sourceSize, double zoomFactor) const;
    bool canPanImage() const;
    void updateDragCursor();

    HoveredItemInfo m_info;
    QPixmap m_originalPixmap;
    quint64 m_loadRequestId = 0;
    bool m_hasHighResolutionImage = false;
    double m_zoomFactor = 1.0;
    bool m_isDragging = false;
    QPoint m_lastDragGlobalPos;
    QWidget* m_headerRow = nullptr;
    QLabel* m_iconLabel = nullptr;
    SelectableTitleLabel* m_titleLabel = nullptr;
    QLabel* m_metaLabel = nullptr;
    QWidget* m_pathRow = nullptr;
    QLabel* m_pathTitleLabel = nullptr;
    QLabel* m_pathValueLabel = nullptr;
    OpenWithButton* m_openWithButton = nullptr;
    QLabel* m_statusLabel = nullptr;
    QLabel* m_imageLabel = nullptr;
    QScrollArea* m_scrollArea = nullptr;
    QMovie* m_movie = nullptr;
    QSize m_movieFrameSize;
};
