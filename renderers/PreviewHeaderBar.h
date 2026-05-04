#pragma once

#include <QWidget>

class QLabel;
class QToolButton;
class SelectableTitleLabel;

class PreviewHeaderBar : public QWidget
{
public:
    explicit PreviewHeaderBar(QWidget* leadingWidget,
                              SelectableTitleLabel* titleLabel,
                              QWidget* secondaryWidget = nullptr,
                              QWidget* trailingWidget = nullptr,
                              QWidget* parent = nullptr);

    QToolButton* closeButton() const;
    SelectableTitleLabel* titleLabel() const;
    QWidget* contentWidget() const;
    void setOpenActionGlyph(const QString& glyph, const QString& toolTip = QString());

private:
    void applyContentAppearance();

    QWidget* m_leadingWidget = nullptr;
    SelectableTitleLabel* m_titleLabel = nullptr;
    QWidget* m_secondaryWidget = nullptr;
    QWidget* m_trailingWidget = nullptr;
    QWidget* m_contentRow = nullptr;
    QWidget* m_textColumn = nullptr;
    QWidget* m_trailingColumn = nullptr;
    QToolButton* m_closeButton = nullptr;
};
