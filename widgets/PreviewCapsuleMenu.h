#pragma once

#include <QVector>
#include <QWidget>

class QAbstractButton;
class QBoxLayout;
class QGraphicsDropShadowEffect;
class QGridLayout;
class QToolButton;
class QVariantAnimation;

class PreviewCapsuleMenu : public QWidget
{
public:
    explicit PreviewCapsuleMenu(QWidget* parent = nullptr);

    void setExpandEnabled(bool enabled);
    void setOpenActionGlyph(const QString& glyph, const QString& toolTip = QString());
    void syncPinState(bool alwaysOnTop);
    void syncExpandState(bool expanded);
    void syncToWindowState();
    void refreshPlacement();

protected:
    void showEvent(QShowEvent* event) override;

private:
    enum class MenuAction {
        Pin,
        Open,
        Copy,
        Ocr,
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

    void registerMenuItem(MenuAction action, QAbstractButton* button, QWidget* separator = nullptr);
    QAbstractButton* menuButton(MenuAction action) const;
    QToolButton* menuToolButton(MenuAction action) const;
    QWidget* menuSeparator(MenuAction action) const;
    QToolButton* createActionButton(const QString& objectName,
                                    const QString& glyph,
                                    const QString& toolTip,
                                    QWidget* parent);
    void triggerMenuAction(MenuAction action);
    void refreshWindowState();
    void applyMenuButtonSize();
    void applyMenuAppearance();
    void applyMenuPlacement();
    void applyMenuVisibility();
    void updateToolbarRegionMetrics();

    QWidget* m_toolbar = nullptr;
    QWidget* m_actionCapsule = nullptr;
    QGridLayout* m_toolbarLayout = nullptr;
    QBoxLayout* m_actionLayout = nullptr;
    QGraphicsDropShadowEffect* m_capsuleShadow = nullptr;
    QVector<MenuItemEntry> m_menuItems;
    bool m_contextExpandEnabled = false;
};
