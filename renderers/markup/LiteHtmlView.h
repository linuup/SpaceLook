#pragma once

#include <memory>

#include <QAbstractScrollArea>
#include <QString>

#include <litehtml.h>

#include "renderers/markup/QtLiteHtmlContainer.h"

class LiteHtmlView : public QAbstractScrollArea
{
    Q_OBJECT

public:
    explicit LiteHtmlView(QWidget* parent = nullptr);

    void clearDocument();
    void setDocumentHtml(const QString& html, const QString& filePath);
    bool hasDocument() const;

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void relayout();
    void updateScrollBars();
    QPoint documentPositionFromViewport(const QPoint& point) const;
    void handleAnchorNavigation(const QtLiteHtmlContainer::AnchorNavigation& navigation);
    bool scrollToAnchor(const QString& anchorName);

    QtLiteHtmlContainer m_container;
    litehtml::document::ptr m_document;
    QString m_filePath;
    QString m_html;
};
