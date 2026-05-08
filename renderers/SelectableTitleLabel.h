#pragma once

#include <QLabel>
#include <QTimer>

class SelectableTitleLabel : public QLabel
{
    Q_OBJECT

public:
    explicit SelectableTitleLabel(QWidget* parent = nullptr);

    void setCopyText(const QString& text);

signals:
    void copyFeedbackRequested(const QString& message);

protected:
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    QString m_copyText;
    QString m_defaultToolTip;
    QTimer m_toolTipResetTimer;
};
