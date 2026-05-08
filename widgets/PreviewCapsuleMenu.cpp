#include "widgets/PreviewCapsuleMenu.h"

#include "renderers/FluentIconFont.h"
#include "settings/spacelook_ui_settings.h"
#include "widgets/SpaceLookWindow.h"

#include <QAbstractButton>
#include <QBoxLayout>
#include <QGraphicsDropShadowEffect>
#include <QGridLayout>
#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QRectF>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStyle>
#include <QToolButton>
#include <QToolTip>
#include <QVariantAnimation>

namespace {

constexpr int kTooltipDurationMs = 2000;

QRectF dockPaintRect(const QWidget* widget)
{
    const qreal inset = qMax<qreal>(2.0, qMin(widget->width(), widget->height()) * 0.10);
    return QRectF(widget->rect()).adjusted(inset, inset, -inset, -inset);
}

QRectF dockGlyphRect(const QWidget* widget)
{
    const qreal horizontalInset = qMax<qreal>(7.0, widget->width() * 0.16);
    const qreal verticalInset = qMax<qreal>(4.0, widget->height() * 0.08);
    return QRectF(widget->rect()).adjusted(horizontalInset,
                                           verticalInset,
                                           -horizontalInset,
                                           -verticalInset);
}

QColor dockToolBackground(const QAbstractButton* button, bool checked)
{
    if (checked) {
        return QColor(QStringLiteral("#ffe9e7"));
    }
    if (button->isDown()) {
        return QColor(QStringLiteral("#e2ebf6"));
    }
    if (button->underMouse()) {
        return QColor(QStringLiteral("#edf3fa"));
    }
    return Qt::transparent;
}

QColor dockToolForeground(const QAbstractButton* button,
                          bool checked,
                          const QColor& normalColor,
                          const QColor& checkedColor)
{
    if (!button->isEnabled()) {
        QColor disabled = normalColor;
        disabled.setAlpha(120);
        return disabled;
    }
    return checked ? checkedColor : normalColor;
}

QString pinGlyphForState(bool checked)
{
    return checked ? FluentIconFont::glyph(0xE841) : FluentIconFont::glyph(0xE840);
}

QString collapsedPreviewGlyph()
{
    return FluentIconFont::glyph(0xE740);
}

QString expandedPreviewGlyph()
{
    return FluentIconFont::glyph(0xE73F);
}

void paintDockBackground(QPainter& painter, const QRectF& targetRect, const QColor& fillColor)
{
    if (fillColor.alpha() <= 0) {
        return;
    }

    painter.setPen(Qt::NoPen);
    painter.setBrush(fillColor);
    painter.drawRoundedRect(targetRect, 12.0, 12.0);
}

void drawDockGlyph(QPainter& painter,
                   const QRectF& glyphRect,
                   const QFont& glyphFont,
                   const QString& glyphText)
{
    const QFontMetricsF metrics(glyphFont);
    const QRectF bounds = metrics.tightBoundingRect(glyphText);
    const qreal baselineY = glyphRect.center().y() + ((metrics.ascent() - metrics.descent()) * 0.5);
    const qreal textX = glyphRect.center().x() - ((bounds.width() * 0.5) + bounds.left());
    painter.drawText(QPointF(textX, baselineY), glyphText);
}

class DockToolButton : public QToolButton
{
public:
    explicit DockToolButton(QWidget* parent = nullptr)
        : QToolButton(parent)
        , m_animation(new QVariantAnimation(this))
    {
        m_animation->setDuration(240);
        m_animation->setEasingCurve(QEasingCurve::InOutCubic);
        connect(m_animation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
            m_scale = value.toReal();
            update();
        });
    }

protected:
    bool event(QEvent* event) override
    {
        if (event && event->type() == QEvent::ToolTip && !toolTip().trimmed().isEmpty()) {
            auto* helpEvent = static_cast<QHelpEvent*>(event);
            QToolTip::showText(helpEvent->globalPos(), toolTip(), this, rect(), kTooltipDurationMs);
            return true;
        }

        return QToolButton::event(event);
    }

    void enterEvent(QEvent* event) override
    {
        animateTo(isDown() ? 1.10 : 1.18);
        QToolButton::enterEvent(event);
    }

    void leaveEvent(QEvent* event) override
    {
        animateTo(1.0);
        QToolButton::leaveEvent(event);
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        animateTo(1.10);
        QToolButton::mousePressEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        QToolButton::mouseReleaseEvent(event);
        animateTo(underMouse() ? 1.18 : 1.0);
    }

    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

        const QPointF center = rect().center();
        painter.translate(center);
        painter.scale(m_scale, m_scale);
        painter.translate(-center);

        const QRectF backgroundRect = dockPaintRect(this);
        const QRectF glyphRect = dockGlyphRect(this);
        const bool checked = isCheckable() && isChecked();
        paintDockBackground(painter, backgroundRect, dockToolBackground(this, checked));

        QFont glyphFont = font();
        const qreal pointSize = glyphFont.pointSizeF() > 0.0 ? glyphFont.pointSizeF() : 15.0;
        glyphFont.setPointSizeF(pointSize - 0.5);
        painter.setFont(glyphFont);
        painter.setPen(dockToolForeground(this,
                                          checked,
                                          QColor(QStringLiteral("#20354d")),
                                          QColor(QStringLiteral("#c42b1c"))));
        if (objectName() == QStringLiteral("PreviewPinButton") && checked) {
            drawDockGlyph(painter, glyphRect, glyphFont, FluentIconFont::glyph(0xE841));
            drawDockGlyph(painter, glyphRect, glyphFont, FluentIconFont::glyph(0xE840));
            return;
        }

        drawDockGlyph(painter, glyphRect, glyphFont, text());
    }

private:
    void animateTo(qreal targetScale)
    {
        if (!m_animation) {
            return;
        }

        m_animation->stop();
        m_animation->setStartValue(m_scale);
        m_animation->setEndValue(targetScale);
        m_animation->start();
    }

    QVariantAnimation* m_animation = nullptr;
    qreal m_scale = 1.0;
};

class DockSeparator : public QWidget
{
public:
    explicit DockSeparator(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setOrientation(Qt::Horizontal);
    }

    void setOrientation(Qt::Orientation orientation)
    {
        m_orientation = orientation;
        if (m_orientation == Qt::Vertical) {
            setFixedSize(22, 10);
        } else {
            setFixedSize(10, 22);
        }
        update();
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setPen(QPen(QColor(QStringLiteral("#c0ccdb")), 1.0));
        if (m_orientation == Qt::Vertical) {
            const int centerY = height() / 2;
            painter.drawLine(0, centerY, width(), centerY);
            return;
        }

        const int centerX = width() / 2;
        painter.drawLine(centerX, 0, centerX, height());
    }

private:
    Qt::Orientation m_orientation = Qt::Horizontal;
};

}

PreviewCapsuleMenu::PreviewCapsuleMenu(QWidget* parent)
    : QWidget(parent)
    , m_toolbar(new QWidget(this))
    , m_actionCapsule(new QWidget(m_toolbar))
    , m_toolbarLayout(new QGridLayout(m_toolbar))
    , m_actionLayout(new QBoxLayout(QBoxLayout::LeftToRight, m_actionCapsule))
    , m_capsuleShadow(new QGraphicsDropShadowEffect(this))
{
    setObjectName(QStringLiteral("PreviewCapsuleMenu"));
    setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);

    m_toolbar->setObjectName(QStringLiteral("PreviewTitleToolbar"));
    m_toolbar->setAttribute(Qt::WA_StyledBackground, true);
    m_toolbar->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    m_toolbarLayout->setContentsMargins(0, 0, 0, 0);
    m_toolbarLayout->addWidget(m_actionCapsule, 0, 0, Qt::AlignCenter);
    m_toolbarLayout->setRowStretch(0, 1);
    m_toolbarLayout->setColumnStretch(0, 1);

    auto* rootLayout = new QGridLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->addWidget(m_toolbar, 0, 0, Qt::AlignCenter);

    m_actionCapsule->setObjectName(QStringLiteral("PreviewActionCapsule"));
    m_actionLayout->setContentsMargins(8, 0, 8, 0);
    m_actionLayout->setSpacing(0);

    m_capsuleShadow->setBlurRadius(20.0);
    m_capsuleShadow->setOffset(0.0, 6.0);
    m_capsuleShadow->setColor(QColor(26, 50, 84, 18));
    m_actionCapsule->setGraphicsEffect(m_capsuleShadow);

    auto addSeparator = [this]() {
        auto* separator = new DockSeparator(m_actionCapsule);
        separator->setObjectName(QStringLiteral("PreviewActionSeparator"));
        m_actionLayout->addWidget(separator, 0, Qt::AlignCenter);
        return separator;
    };

    struct MenuDescriptor {
        MenuAction action;
        QString objectName;
        QString glyph;
        QString toolTip;
        bool pin = false;
        bool checkable = false;
    };

    const QVector<MenuDescriptor> menuDescriptors = {
        { MenuAction::Pin, QStringLiteral("PreviewPinButton"), FluentIconFont::glyph(0xE840), QStringLiteral("Pin preview"), true, true },
        { MenuAction::Open, QStringLiteral("PreviewOpenFolderButton"), FluentIconFont::glyph(0xE8A7), QStringLiteral("Open item") },
        { MenuAction::Copy, QStringLiteral("PreviewCopyButton"), FluentIconFont::glyph(0xE8C8), QStringLiteral("Copy path") },
        { MenuAction::Refresh, QStringLiteral("PreviewRefreshButton"), FluentIconFont::glyph(0xE72C), QStringLiteral("Refresh preview") },
        { MenuAction::Expand, QStringLiteral("PreviewExpandButton"), collapsedPreviewGlyph(), QStringLiteral("Expand preview") },
        { MenuAction::Close, QStringLiteral("PreviewCloseButton"), FluentIconFont::glyph(0xE711), QStringLiteral("Close preview") },
        { MenuAction::More, QStringLiteral("PreviewMoreButton"), FluentIconFont::glyph(0xE713), QStringLiteral("Show settings") }
    };

    for (int i = 0; i < menuDescriptors.size(); ++i) {
        const MenuDescriptor& descriptor = menuDescriptors.at(i);
        auto* button = createActionButton(descriptor.objectName,
                                          descriptor.glyph,
                                          descriptor.toolTip,
                                          m_actionCapsule);
        if (descriptor.checkable) {
            button->setCheckable(true);
        }
        m_actionLayout->addWidget(button, 0, Qt::AlignCenter);
        QWidget* separator = nullptr;
        if (i + 1 < menuDescriptors.size()) {
            separator = addSeparator();
        }
        registerMenuItem(descriptor.action, button, separator);
        connect(button, &QAbstractButton::clicked, this, [this, action = descriptor.action]() {
            triggerMenuAction(action);
        });
    }

    m_toolbar->setStyleSheet(
        "#PreviewTitleToolbar {"
        "  background: transparent;"
        "}"
        "#PreviewActionCapsule {"
        "  background: rgba(252, 253, 255, 0.96);"
        "  border: 1px solid transparent;"
        "  border-radius: 21px;"
        "}"
        "#PreviewActionSeparator {"
        "  background: transparent;"
        "}"
        "#PreviewPinButton, #PreviewOpenFolderButton, #PreviewCopyButton, #PreviewRefreshButton,"
        "#PreviewExpandButton, #PreviewCloseButton, #PreviewMoreButton {"
        "  background: transparent;"
        "  border: none;"
        "  min-width: 44px;"
        "  max-width: 44px;"
        "  min-height: 44px;"
        "  max-height: 44px;"
        "  border-radius: 14px;"
        "  padding: 0px;"
        "}"
        "#PreviewOpenFolderButton:hover, #PreviewCopyButton:hover, #PreviewRefreshButton:hover,"
        "#PreviewExpandButton:hover, #PreviewCloseButton:hover, #PreviewMoreButton:hover {"
        "  background: rgba(237, 243, 250, 0.98);"
        "}"
        "#PreviewOpenFolderButton:pressed, #PreviewCopyButton:pressed, #PreviewRefreshButton:pressed,"
        "#PreviewExpandButton:pressed, #PreviewCloseButton:pressed, #PreviewMoreButton:pressed {"
        "  background: rgba(226, 235, 246, 1.0);"
        "}"
    );

    connect(&SpaceLookUiSettings::instance(), &SpaceLookUiSettings::menuButtonSizeChanged, this, [this]() {
        applyMenuButtonSize();
    });
    connect(&SpaceLookUiSettings::instance(), &SpaceLookUiSettings::menuAppearanceChanged, this, [this]() {
        applyMenuAppearance();
        applyMenuPlacement();
    });
    connect(&SpaceLookUiSettings::instance(), &SpaceLookUiSettings::menuVisibilityChanged, this, [this]() {
        applyMenuVisibility();
    });

    syncPinState(false);
    syncExpandState(false);
    setExpandEnabled(false);
    applyMenuButtonSize();
    applyMenuAppearance();
    applyMenuPlacement();
    applyMenuVisibility();
}

void PreviewCapsuleMenu::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    applyMenuAppearance();
    applyMenuPlacement();
    applyMenuVisibility();
    refreshWindowState();
}

void PreviewCapsuleMenu::syncToWindowState()
{
    refreshWindowState();
}

void PreviewCapsuleMenu::refreshPlacement()
{
    applyMenuPlacement();
}

QToolButton* PreviewCapsuleMenu::createActionButton(const QString& objectName,
                                                    const QString& glyph,
                                                    const QString& toolTip,
                                                    QWidget* parent)
{
    auto* button = new DockToolButton(parent);
    button->setObjectName(objectName);
    button->setFont(FluentIconFont::iconFont(15, QFont::Normal));
    button->setText(glyph);
    button->setToolTip(toolTip);
    button->setAutoRaise(true);
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    return button;
}

void PreviewCapsuleMenu::registerMenuItem(MenuAction action, QAbstractButton* button, QWidget* separator)
{
    m_menuItems.push_back({ action, button, separator });
}

QAbstractButton* PreviewCapsuleMenu::menuButton(MenuAction action) const
{
    for (const MenuItemEntry& entry : m_menuItems) {
        if (entry.action == action) {
            return entry.button;
        }
    }
    return nullptr;
}

QToolButton* PreviewCapsuleMenu::menuToolButton(MenuAction action) const
{
    return qobject_cast<QToolButton*>(menuButton(action));
}

QWidget* PreviewCapsuleMenu::menuSeparator(MenuAction action) const
{
    for (const MenuItemEntry& entry : m_menuItems) {
        if (entry.action == action) {
            return entry.separator;
        }
    }
    return nullptr;
}

void PreviewCapsuleMenu::triggerMenuAction(MenuAction action)
{
    SpaceLookWindow* previewWindow = qobject_cast<SpaceLookWindow*>(window());
    if (!previewWindow) {
        return;
    }

    switch (action) {
    case MenuAction::Pin:
        previewWindow->toggleAlwaysOnTop();
        syncPinState(previewWindow->isAlwaysOnTop());
        return;
    case MenuAction::Open:
        previewWindow->openCurrentWithDefaultApp();
        return;
    case MenuAction::Copy:
        previewWindow->copyCurrentPath();
        return;
    case MenuAction::Refresh:
        previewWindow->refreshCurrentPreview();
        refreshWindowState();
        return;
    case MenuAction::Expand:
        previewWindow->toggleExpandedPreview();
        syncExpandState(previewWindow->isExpandedPreview());
        return;
    case MenuAction::Close:
        previewWindow->hidePreview();
        return;
    case MenuAction::More:
        previewWindow->showSettingsWindow();
        return;
    }
}

void PreviewCapsuleMenu::setExpandEnabled(bool enabled)
{
    m_contextExpandEnabled = enabled;
    if (QAbstractButton* expandButton = menuButton(MenuAction::Expand)) {
        expandButton->setVisible(enabled && SpaceLookUiSettings::instance().showMenuExpand());
    }
    if (QWidget* expandSeparator = menuSeparator(MenuAction::Expand)) {
        expandSeparator->setVisible(enabled && SpaceLookUiSettings::instance().showMenuExpand());
    }
}

void PreviewCapsuleMenu::setOpenActionGlyph(const QString& glyph, const QString& toolTip)
{
    QToolButton* openButton = menuToolButton(MenuAction::Open);
    if (!openButton) {
        return;
    }

    if (!glyph.isEmpty()) {
        openButton->setText(glyph);
    }
    if (!toolTip.trimmed().isEmpty()) {
        openButton->setToolTip(toolTip);
    }
}

void PreviewCapsuleMenu::refreshWindowState()
{
    if (SpaceLookWindow* previewWindow = qobject_cast<SpaceLookWindow*>(window())) {
        syncPinState(previewWindow->isAlwaysOnTop());
        syncExpandState(previewWindow->isExpandedPreview());
        setExpandEnabled(previewWindow->supportsExpandedPreview());
        return;
    }

    syncPinState(false);
    syncExpandState(false);
    setExpandEnabled(false);
}

void PreviewCapsuleMenu::applyMenuButtonSize()
{
    const int buttonSize = SpaceLookUiSettings::instance().menuButtonSize();
    const QSize size(buttonSize, buttonSize);
    const int iconSide = qMax(16, buttonSize - 18);

    for (const MenuItemEntry& entry : m_menuItems) {
        QAbstractButton* button = entry.button;
        if (!button) {
            continue;
        }
        button->setMinimumSize(size);
        button->setMaximumSize(size);
        button->setIconSize(QSize(iconSide, iconSide));
    }

    updateToolbarRegionMetrics();
}

void PreviewCapsuleMenu::applyMenuAppearance()
{
    const bool showMenuBorder = SpaceLookUiSettings::instance().showMenuBorder();
    const QString borderColor = showMenuBorder
        ? QStringLiteral("rgba(202, 214, 228, 0.98)")
        : QStringLiteral("transparent");

    if (m_actionLayout) {
        m_actionLayout->setContentsMargins(8, 0, 8, 0);
    }

    m_actionCapsule->setStyleSheet(QStringLiteral(
        "#PreviewActionCapsule {"
        "  background: rgba(252, 253, 255, 0.96);"
        "  border: 2px solid %1;"
        "  border-radius: 21px;"
        "}").arg(borderColor));

    updateToolbarRegionMetrics();
}

void PreviewCapsuleMenu::applyMenuPlacement()
{
    if (!m_actionLayout) {
        return;
    }

    const int menuPlacement = SpaceLookUiSettings::instance().menuPlacement();
    const bool verticalMenu = menuPlacement == 2 || menuPlacement == 3;
    m_actionLayout->setDirection(verticalMenu ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    m_toolbarLayout->setContentsMargins(0, 0, 0, 0);

    for (const MenuItemEntry& entry : m_menuItems) {
        auto* separator = static_cast<DockSeparator*>(entry.separator);
        if (!separator) {
            continue;
        }
        separator->setOrientation(verticalMenu ? Qt::Vertical : Qt::Horizontal);
    }

    updateToolbarRegionMetrics();
}

void PreviewCapsuleMenu::applyMenuVisibility()
{
    SpaceLookUiSettings& settings = SpaceLookUiSettings::instance();
    auto menuItemRequestedVisible = [](QAbstractButton* button) {
        return button && !button->isHidden();
    };

    if (QAbstractButton* button = menuButton(MenuAction::Pin)) {
        button->setVisible(settings.showMenuPin());
    }
    if (QAbstractButton* button = menuButton(MenuAction::Open)) {
        button->setVisible(settings.showMenuOpen());
    }
    if (QAbstractButton* button = menuButton(MenuAction::Copy)) {
        button->setVisible(settings.showMenuCopy());
    }
    if (QAbstractButton* button = menuButton(MenuAction::Refresh)) {
        button->setVisible(settings.showMenuRefresh());
    }
    if (QAbstractButton* button = menuButton(MenuAction::Close)) {
        button->setVisible(settings.showMenuClose());
    }
    if (QAbstractButton* button = menuButton(MenuAction::More)) {
        button->setVisible(true);
    }

    if (QAbstractButton* button = menuButton(MenuAction::Expand)) {
        button->setVisible(m_contextExpandEnabled && settings.showMenuExpand());
    }
    if (QWidget* separator = menuSeparator(MenuAction::Expand)) {
        separator->setVisible(m_contextExpandEnabled && settings.showMenuExpand());
    }

    for (const MenuItemEntry& entry : m_menuItems) {
        if (!entry.separator || !entry.button) {
            continue;
        }

        int nextVisibleIndex = -1;
        for (int i = 0; i < m_menuItems.size(); ++i) {
            if (m_menuItems.at(i).button == entry.button) {
                nextVisibleIndex = i + 1;
                break;
            }
        }

        bool hasVisibleNext = false;
        while (nextVisibleIndex >= 0 && nextVisibleIndex < m_menuItems.size()) {
            QAbstractButton* nextButton = m_menuItems.at(nextVisibleIndex).button;
            if (menuItemRequestedVisible(nextButton)) {
                hasVisibleNext = true;
                break;
            }
            ++nextVisibleIndex;
        }

        entry.separator->setVisible(menuItemRequestedVisible(entry.button) && hasVisibleNext);
    }

    updateToolbarRegionMetrics();
}

void PreviewCapsuleMenu::updateToolbarRegionMetrics()
{
    if (!m_toolbar || !m_actionCapsule || !m_toolbarLayout) {
        return;
    }

    m_actionLayout->activate();
    m_toolbarLayout->activate();
    m_actionCapsule->adjustSize();

    const QSize capsuleSize = m_actionCapsule->sizeHint().expandedTo(m_actionCapsule->minimumSizeHint());
    const QMargins margins = m_toolbarLayout->contentsMargins();
    const int regionWidth = capsuleSize.width() + margins.left() + margins.right();
    const int regionHeight = capsuleSize.height() + margins.top() + margins.bottom();
    const bool verticalMenu = SpaceLookUiSettings::instance().menuPlacement() == 2 ||
        SpaceLookUiSettings::instance().menuPlacement() == 3;

    if (verticalMenu) {
        m_toolbar->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        m_toolbar->setMinimumWidth(regionWidth);
        m_toolbar->setMaximumWidth(regionWidth);
        m_toolbar->setMinimumHeight(regionHeight);
        m_toolbar->setMaximumHeight(regionHeight);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        setMinimumWidth(regionWidth);
        setMaximumWidth(regionWidth);
        setMinimumHeight(regionHeight);
        setMaximumHeight(regionHeight);
        return;
    }

    m_toolbar->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_toolbar->setMinimumHeight(regionHeight);
    m_toolbar->setMaximumHeight(regionHeight);
    m_toolbar->setMinimumWidth(regionWidth);
    m_toolbar->setMaximumWidth(regionWidth);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setMinimumWidth(regionWidth);
    setMaximumWidth(regionWidth);
    setMinimumHeight(regionHeight);
    setMaximumHeight(regionHeight);
}

void PreviewCapsuleMenu::syncPinState(bool alwaysOnTop)
{
    QAbstractButton* pinControl = menuButton(MenuAction::Pin);
    if (!pinControl) {
        return;
    }

    const QSignalBlocker blocker(pinControl);
    pinControl->setChecked(alwaysOnTop);
    pinControl->setText(pinGlyphForState(alwaysOnTop));
    pinControl->setToolTip(alwaysOnTop
                               ? QStringLiteral("Disable always on top")
                               : QStringLiteral("Pin preview"));
    pinControl->style()->unpolish(pinControl);
    pinControl->style()->polish(pinControl);
    pinControl->update();
}

void PreviewCapsuleMenu::syncExpandState(bool expanded)
{
    QToolButton* expandButton = menuToolButton(MenuAction::Expand);
    if (!expandButton) {
        return;
    }

    expandButton->setText(expanded ? expandedPreviewGlyph() : collapsedPreviewGlyph());
    expandButton->setToolTip(expanded
        ? QStringLiteral("Restore preview size")
        : QStringLiteral("Expand preview"));
    expandButton->update();
}
