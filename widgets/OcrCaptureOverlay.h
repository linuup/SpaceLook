#pragma once

#include <QImage>
#include <QPixmap>
#include <QRectF>
#include <QWidget>

class QScreen;

class OcrCaptureOverlay : public QWidget
{
    Q_OBJECT

public:
    explicit OcrCaptureOverlay(QScreen* screen, QWidget* parent = nullptr);

signals:
    void captureSelected(const QImage& image);
    void captureCanceled(const QString& message);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    QImage selectedImage() const;
    QRect selectedRect() const;
    QRect selectionUpdateRect(const QRect& selection) const;
    QRectF pixmapSourceRect(const QRect& logicalRect) const;

    QScreen* m_screen = nullptr;
    QPixmap m_screenPixmap;
    QPoint m_startPos;
    QPoint m_endPos;
    bool m_selecting = false;
};
