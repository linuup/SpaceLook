#include "renderers/markup/LiteHtmlView.h"

#include <QDesktopServices>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QScrollBar>
#include <QUrl>
#include <QFileInfo>
#include <QWheelEvent>

LiteHtmlView::LiteHtmlView(QWidget* parent)
    : QAbstractScrollArea(parent)
    , m_container(viewport())
{
    setMouseTracking(true);
    setFrameShape(QFrame::NoFrame);
    viewport()->setAutoFillBackground(false);

    m_container.setAnchorCallback([this](const QtLiteHtmlContainer::AnchorNavigation& navigation) {
        handleAnchorNavigation(navigation);
    });
    m_container.setCursorCallback([this](Qt::CursorShape shape) {
        viewport()->setCursor(shape);
    });
}

void LiteHtmlView::clearDocument()
{
    m_document.reset();
    m_html.clear();
    m_filePath.clear();
    viewport()->unsetCursor();
    horizontalScrollBar()->setValue(0);
    verticalScrollBar()->setValue(0);
    updateScrollBars();
    viewport()->update();
}

void LiteHtmlView::setDocumentHtml(const QString& html, const QString& filePath)
{
    m_html = html;
    m_filePath = filePath;
    m_container.setBasePath(filePath);
    m_container.setViewportSize(viewport()->width(), viewport()->height());

    m_document = litehtml::document::createFromString(
        std::string(html.toUtf8().constData(), static_cast<size_t>(html.toUtf8().size())),
        &m_container);

    relayout();
}

bool LiteHtmlView::hasDocument() const
{
    return static_cast<bool>(m_document);
}

void LiteHtmlView::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(viewport());
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.fillRect(viewport()->rect(), Qt::white);

    if (!m_document) {
        return;
    }

    const litehtml::position clip(
        static_cast<litehtml::pixel_t>(horizontalScrollBar()->value()),
        static_cast<litehtml::pixel_t>(verticalScrollBar()->value()),
        static_cast<litehtml::pixel_t>(viewport()->width()),
        static_cast<litehtml::pixel_t>(viewport()->height()));

    painter.translate(-horizontalScrollBar()->value(), -verticalScrollBar()->value());
    m_document->draw(reinterpret_cast<litehtml::uint_ptr>(&painter), 0, 0, &clip);
}

void LiteHtmlView::resizeEvent(QResizeEvent* event)
{
    QAbstractScrollArea::resizeEvent(event);
    relayout();
}

void LiteHtmlView::wheelEvent(QWheelEvent* event)
{
    if (!m_document) {
        QAbstractScrollArea::wheelEvent(event);
        return;
    }

    QScrollBar* targetBar = verticalScrollBar();
    int delta = event->angleDelta().y();
    if (delta == 0 && !event->pixelDelta().isNull()) {
        delta = event->pixelDelta().y();
    }

    if (delta == 0) {
        QAbstractScrollArea::wheelEvent(event);
        return;
    }

    const int singleStep = std::max(24, fontMetrics().height() * 3);
    const int steps = delta / 120;
    const int effectiveSteps = steps != 0 ? steps : (delta > 0 ? 1 : -1);
    targetBar->setValue(targetBar->value() - effectiveSteps * singleStep);
    event->accept();
}

void LiteHtmlView::mouseMoveEvent(QMouseEvent* event)
{
    QAbstractScrollArea::mouseMoveEvent(event);
    if (!m_document) {
        return;
    }

    litehtml::position::vector redrawBoxes;
    const QPoint docPos = documentPositionFromViewport(event->pos());
    if (m_document->on_mouse_over(docPos.x(), docPos.y(), event->pos().x(), event->pos().y(), redrawBoxes)) {
        viewport()->update();
    }
}

void LiteHtmlView::leaveEvent(QEvent* event)
{
    QAbstractScrollArea::leaveEvent(event);
    viewport()->unsetCursor();
    if (!m_document) {
        return;
    }

    litehtml::position::vector redrawBoxes;
    if (m_document->on_mouse_leave(redrawBoxes)) {
        viewport()->update();
    }
}

void LiteHtmlView::mousePressEvent(QMouseEvent* event)
{
    QAbstractScrollArea::mousePressEvent(event);
    if (!m_document || event->button() != Qt::LeftButton) {
        return;
    }

    litehtml::position::vector redrawBoxes;
    const QPoint docPos = documentPositionFromViewport(event->pos());
    if (m_document->on_lbutton_down(docPos.x(), docPos.y(), event->pos().x(), event->pos().y(), redrawBoxes)) {
        viewport()->update();
    }
}

void LiteHtmlView::mouseReleaseEvent(QMouseEvent* event)
{
    QAbstractScrollArea::mouseReleaseEvent(event);
    if (!m_document || event->button() != Qt::LeftButton) {
        return;
    }

    litehtml::position::vector redrawBoxes;
    const QPoint docPos = documentPositionFromViewport(event->pos());
    if (m_document->on_lbutton_up(docPos.x(), docPos.y(), event->pos().x(), event->pos().y(), redrawBoxes)) {
        viewport()->update();
    }
}

void LiteHtmlView::relayout()
{
    if (!m_document) {
        updateScrollBars();
        viewport()->update();
        return;
    }

    const int layoutWidth = std::max(1, viewport()->width());
    const int layoutHeight = std::max(1, viewport()->height());
    m_container.setViewportSize(layoutWidth, layoutHeight);
    m_document->render(static_cast<litehtml::pixel_t>(layoutWidth));
    updateScrollBars();
    viewport()->update();
}

void LiteHtmlView::updateScrollBars()
{
    const int documentWidth = m_document ? static_cast<int>(std::ceil(m_document->width())) : 0;
    const int documentHeight = m_document ? static_cast<int>(std::ceil(m_document->height())) : 0;

    horizontalScrollBar()->setPageStep(viewport()->width());
    horizontalScrollBar()->setRange(0, std::max(0, documentWidth - viewport()->width()));

    verticalScrollBar()->setPageStep(viewport()->height());
    verticalScrollBar()->setRange(0, std::max(0, documentHeight - viewport()->height()));
}

QPoint LiteHtmlView::documentPositionFromViewport(const QPoint& point) const
{
    return QPoint(point.x() + horizontalScrollBar()->value(), point.y() + verticalScrollBar()->value());
}

void LiteHtmlView::handleAnchorNavigation(const QtLiteHtmlContainer::AnchorNavigation& navigation)
{
    const QString rawUrl = navigation.rawUrl.trimmed();
    const QString resolvedUrl = navigation.resolvedUrl.trimmed();
    if (rawUrl.isEmpty() && resolvedUrl.isEmpty()) {
        return;
    }

    if (rawUrl.startsWith(QLatin1Char('#'))) {
        if (scrollToAnchor(rawUrl.mid(1))) {
            return;
        }
    }

    QUrl resolved = QUrl::fromUserInput(resolvedUrl);
    if (!resolved.isValid() && !rawUrl.isEmpty()) {
        resolved = QUrl::fromUserInput(rawUrl);
    }

    if (resolved.isValid() && !resolved.fragment().isEmpty()) {
        const QString localPath = resolved.isLocalFile() ? resolved.toLocalFile() : resolved.adjusted(QUrl::RemoveFragment).toString();
        const QString currentPath = m_filePath.trimmed();
        if ((resolved.isLocalFile() && QFileInfo(localPath) == QFileInfo(currentPath)) ||
            (!resolved.isLocalFile() && localPath == currentPath)) {
            if (scrollToAnchor(resolved.fragment())) {
                return;
            }
        }
    }

    if (resolved.isValid()) {
        QDesktopServices::openUrl(resolved);
    }
}

bool LiteHtmlView::scrollToAnchor(const QString& anchorName)
{
    if (!m_document || anchorName.trimmed().isEmpty()) {
        return false;
    }

    QString selector = QStringLiteral("#%1").arg(anchorName);
    litehtml::element::ptr target = m_document->root()->select_one(selector.toStdString());
    if (!target) {
        selector = QStringLiteral("[name=%1]").arg(anchorName);
        target = m_document->root()->select_one(selector.toStdString());
    }
    if (!target) {
        return false;
    }

    const litehtml::position pos = target->get_placement();
    verticalScrollBar()->setValue(std::max(0, static_cast<int>(std::floor(pos.top()))));
    viewport()->update();
    return true;
}
