#pragma once

#include <QVector>
#include <QWidget>

class QAbstractButton;
class QLabel;
class QPushButton;
class QToolButton;
class QWidget;
class SelectableTitleLabel;

class PreviewHeaderBar : public QWidget
{
    Q_OBJECT

public:
    explicit PreviewHeaderBar(QWidget* leadingWidget,
                              SelectableTitleLabel* titleLabel,
                              QWidget* secondaryWidget = nullptr,
                              QWidget* trailingWidget = nullptr,
                              QWidget* parent = nullptr);

    QPushButton* pinButton() const;
    QToolButton* closeButton() const;
    SelectableTitleLabel* titleLabel() const;
    void setExpandEnabled(bool enabled);
    void setOpenActionGlyph(const QString& glyph, const QString& toolTip = QString());
    void syncPinState(bool alwaysOnTop);
    void syncExpandState(bool expanded);

protected:
    void showEvent(QShowEvent* event) override;

private:
    enum class MenuAction {
        Pin,
        Open,
        Copy,
        Refresh,
        Expand,
        Close,
        More
    };

    struct MenuItemEntry {
        MenuAction action;
        QAbstractButton* button = nullptr;
        QWidget* separator = nullptr;
    };

    void applyMenuButtonSize();
    void applyMenuVisibility();
    QToolButton* createActionButton(const QString& objectName,
                                    const QString& glyph,
                                    const QString& toolTip,
                                    QWidget* parent);
    void registerMenuItem(MenuAction action, QAbstractButton* button, QWidget* separator = nullptr);
    QAbstractButton* menuButton(MenuAction action) const;
    QToolButton* menuToolButton(MenuAction action) const;
    QWidget* menuSeparator(MenuAction action) const;
    void triggerMenuAction(MenuAction action);
    void refreshWindowState();

    QWidget* m_leadingWidget = nullptr;
    SelectableTitleLabel* m_titleLabel = nullptr;
    QWidget* m_secondaryWidget = nullptr;
    QWidget* m_trailingWidget = nullptr;
    QVector<MenuItemEntry> m_menuItems;
    QToolButton* m_closeButton = nullptr;
    bool m_contextExpandEnabled = false;
};
