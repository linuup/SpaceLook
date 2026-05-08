#include "widgets/OcrCaptureOverlay.h"

#include <QApplication>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QScreen>

namespace {

constexpr int kMinimumCaptureSize = 8;

}

OcrCaptureOverlay::OcrCaptureOverlay(QScreen* screen, QWidget* parent)
    : QWidget(parent)
    , m_screen(screen ? screen : QGuiApplication::primaryScreen())
{
    setWindowFlags(Qt::FramelessWindowHint |
                   Qt::WindowStaysOnTopHint |
                   Qt::Tool);
    setAttribute(Qt::WA_DeleteOnClose, true);
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent, true);

    if (m_screen) {
        setGeometry(m_screen->geometry());
        m_screenPixmap = m_screen->grabWindow(0);
    }
}

void OcrCaptureOverlay::keyPressEvent(QKeyEvent* event)
{
    if (event && event->key() == Qt::Key_Escape) {
        emit captureCanceled(QApplication::translate("SpaceLook", "OCR capture canceled."));
        close();
        return;
    }

    QWidget::keyPressEvent(event);
}

void OcrCaptureOverlay::mousePressEvent(QMouseEvent* event)
{
    if (!event || event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    m_selecting = true;
    m_startPos = event->pos();
    m_endPos = event->pos();
    update(selectionUpdateRect(selectedRect()));
    event->accept();
}

void OcrCaptureOverlay::mouseMoveEvent(QMouseEvent* event)
{
    if (event && m_selecting) {
        const QRect previousRect = selectedRect();
        m_endPos = event->pos();
        update(selectionUpdateRect(previousRect).united(selectionUpdateRect(selectedRect())));
        event->accept();
        return;
    }

    QWidget::mouseMoveEvent(event);
}

void OcrCaptureOverlay::mouseReleaseEvent(QMouseEvent* event)
{
    if (!event || event->button() != Qt::LeftButton || !m_selecting) {
        QWidget::mouseReleaseEvent(event);
        return;
    }

    m_endPos = event->pos();
    m_selecting = false;
    const QRect rect = selectedRect();
    if (rect.width() < kMinimumCaptureSize || rect.height() < kMinimumCaptureSize) {
        emit captureCanceled(QApplication::translate("SpaceLook", "OCR capture area is too small."));
        close();
        return;
    }

    const QImage image = selectedImage();
    if (image.isNull()) {
        emit captureCanceled(QApplication::translate("SpaceLook", "Failed to capture OCR area."));
        close();
        return;
    }

    emit captureSelected(image);
    close();
}

void OcrCaptureOverlay::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    const QRect dirtyRect = event ? event->rect() : rect();
    if (!m_screenPixmap.isNull()) {
        painter.drawPixmap(dirtyRect, m_screenPixmap, pixmapSourceRect(dirtyRect));
    } else {
        painter.fillRect(dirtyRect, QColor(20, 28, 36));
    }

    painter.fillRect(dirtyRect, QColor(0, 0, 0, 110));

    const QRect selection = selectedRect();
    if (selection.isValid()) {
        const QRect exposedSelection = selection.intersected(dirtyRect);
        if (!m_screenPixmap.isNull()) {
            painter.drawPixmap(exposedSelection, m_screenPixmap, pixmapSourceRect(exposedSelection));
        }
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(QPen(QColor(255, 255, 255, 235), 2.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(selection.adjusted(0, 0, -1, -1), 4.0, 4.0);
        painter.setPen(QPen(QColor(11, 127, 143, 230), 1.0));
        painter.drawRoundedRect(selection.adjusted(2, 2, -3, -3), 3.0, 3.0);
    }
}

QImage OcrCaptureOverlay::selectedImage() const
{
    if (m_screenPixmap.isNull()) {
        return QImage();
    }

    const QRect rect = selectedRect().intersected(this->rect());
    if (!rect.isValid()) {
        return QImage();
    }

    const qreal dpr = m_screenPixmap.devicePixelRatioF();
    const QRect deviceRect(qRound(rect.x() * dpr),
                           qRound(rect.y() * dpr),
                           qRound(rect.width() * dpr),
                           qRound(rect.height() * dpr));
    return m_screenPixmap.toImage().copy(deviceRect);
}

QRect OcrCaptureOverlay::selectedRect() const
{
    return QRect(m_startPos, m_endPos).normalized();
}

QRect OcrCaptureOverlay::selectionUpdateRect(const QRect& selection) const
{
    if (!selection.isValid()) {
        return QRect(m_startPos, QSize(1, 1)).adjusted(-8, -8, 8, 8).intersected(rect());
    }

    return selection.adjusted(-8, -8, 8, 8).intersected(rect());
}

QRectF OcrCaptureOverlay::pixmapSourceRect(const QRect& logicalRect) const
{
    const qreal dpr = m_screenPixmap.devicePixelRatioF();
    return QRectF(logicalRect.x() * dpr,
                  logicalRect.y() * dpr,
                  logicalRect.width() * dpr,
                  logicalRect.height() * dpr);
}
