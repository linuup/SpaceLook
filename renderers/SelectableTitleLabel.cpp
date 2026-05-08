#include "renderers/SelectableTitleLabel.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QMouseEvent>

SelectableTitleLabel::SelectableTitleLabel(QWidget* parent)
    : QLabel(parent)
{
    setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_defaultToolTip = QCoreApplication::translate("SpaceLook", "Double click to copy file name");
    setToolTip(m_defaultToolTip);
    m_toolTipResetTimer.setSingleShot(true);
    m_toolTipResetTimer.setInterval(1400);
    connect(&m_toolTipResetTimer, &QTimer::timeout, this, [this]() {
        setToolTip(m_defaultToolTip);
    });
}

void SelectableTitleLabel::setCopyText(const QString& text)
{
    m_copyText = text;
}

void SelectableTitleLabel::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event && event->button() == Qt::LeftButton) {
        if (QClipboard* clipboard = QGuiApplication::clipboard()) {
            clipboard->setText(m_copyText.isEmpty() ? text() : m_copyText);
        }
        emit copyFeedbackRequested(QCoreApplication::translate("SpaceLook", "File name copied"));
        event->accept();
        return;
    }

    QLabel::mouseDoubleClickEvent(event);
}
