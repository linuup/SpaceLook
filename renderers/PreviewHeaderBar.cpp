#include "renderers/PreviewHeaderBar.h"

#include "renderers/FluentIconFont.h"
#include "settings/spacelook_ui_settings.h"

#include <QGraphicsDropShadowEffect>
#include <QHelpEvent>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QRectF>
#include <QSignalBlocker>
#include <QShowEvent>
#include <QSizePolicy>
#include <QStyle>
#include <QToolButton>
#include <QToolTip>
#include <QVariantAnimation>
#include <QWidget>
#include <QVBoxLayout>

#include "renderers/SelectableTitleLabel.h"
#include "widgets/SpaceLookWindow.h"

namespace {

constexpr int kTooltipDurationMs = 2000;

QRectF dockPaintRect(const QWidget* widget, qreal scale)
{
    const qreal inset = qMax<qreal>(2.0, qMin(widget->width(), widget->height()) * 0.10);
    const QRectF baseRect = QRectF(widget->rect()).adjusted(inset, inset, -inset, -inset);
    const QPointF center = baseRect.center();
    const QSizeF scaledSize(baseRect.width() * scale, baseRect.height() * scale);
    return QRectF(center.x() - (scaledSize.width() * 0.5),
                  center.y() - (scaledSize.height() * 0.5),
                  scaledSize.width(),
                  scaledSize.height());
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

void paintDockBackground(QPainter& painter, const QRectF& targetRect, const QColor& fillColor)
{
    if (fillColor.alpha() <= 0) {
        return;
    }

    painter.setPen(Qt::NoPen);
    painter.setBrush(fillColor);
    painter.drawRoundedRect(targetRect, 12.0, 12.0);
}

class DockToolButton : public QToolButton
{
public:
    explicit DockToolButton(QWidget* parent = nullptr)
        : QToolButton(parent)
        , m_animation(new QVariantAnimation(this))
    {
        m_animation->setDuration(160);
        m_animation->setEasingCurve(QEasingCurve::OutCubic);
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

        const QRectF targetRect = dockPaintRect(this, m_scale);
        paintDockBackground(painter, targetRect, dockToolBackground(this, false));

        QFont glyphFont = font();
        const qreal pointSize = glyphFont.pointSizeF() > 0.0 ? glyphFont.pointSizeF() : 15.0;
        glyphFont.setPointSizeF(pointSize * m_scale);
        painter.setFont(glyphFont);
        painter.setPen(dockToolForeground(this,
                                          false,
                                          QColor(QStringLiteral("#20354d")),
                                          QColor(QStringLiteral("#20354d"))));
        painter.drawText(targetRect, Qt::AlignCenter, text());
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

bool isPinButton(const QAbstractButton* button)
{
    return button && button->objectName() == QStringLiteral("PreviewPinButton");
}

class DockPushButton : public QPushButton
{
public:
    explicit DockPushButton(QWidget* parent = nullptr)
        : QPushButton(parent)
        , m_animation(new QVariantAnimation(this))
    {
        m_animation->setDuration(160);
        m_animation->setEasingCurve(QEasingCurve::OutCubic);
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

        return QPushButton::event(event);
    }

    void enterEvent(QEvent* event) override
    {
        animateTo(isDown() ? 1.10 : 1.18);
        QPushButton::enterEvent(event);
    }

    void leaveEvent(QEvent* event) override
    {
        animateTo(1.0);
        QPushButton::leaveEvent(event);
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        animateTo(1.10);
        QPushButton::mousePressEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        QPushButton::mouseReleaseEvent(event);
        animateTo(underMouse() ? 1.18 : 1.0);
    }

    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

        const QRectF targetRect = dockPaintRect(this, m_scale);
        const bool checked = isChecked();
        paintDockBackground(painter, targetRect, dockToolBackground(this, checked));

        const QColor glyphColor = dockToolForeground(this,
                                                     checked,
                                                     QColor(QStringLiteral("#46617f")),
                                                     QColor(QStringLiteral("#c42b1c")));

        if (isPinButton(this)) {
            QFont glyphFont = font();
            const qreal pointSize = glyphFont.pointSizeF() > 0.0 ? glyphFont.pointSizeF() : 15.0;
            glyphFont.setPointSizeF(pointSize * m_scale);
            painter.setFont(glyphFont);
            if (checked) {
                painter.setPen(QColor(QStringLiteral("#c42b1c")));
                painter.drawText(targetRect, Qt::AlignCenter, FluentIconFont::glyph(0xE841));
            }
            painter.setPen(checked ? QColor(QStringLiteral("#20354d")) : glyphColor);
            painter.drawText(targetRect, Qt::AlignCenter, FluentIconFont::glyph(0xE840));
            return;
        }

        const QSize iconTargetSize = iconSize().isValid()
            ? QSize(qMax(16, qRound(iconSize().width() * m_scale)),
                    qMax(16, qRound(iconSize().height() * m_scale)))
            : QSize(qMax(16, qRound(targetRect.width() * 0.55)),
                    qMax(16, qRound(targetRect.height() * 0.55)));

        if (!icon().isNull()) {
            const QIcon::Mode mode = isEnabled() ? QIcon::Normal : QIcon::Disabled;
            const QPixmap pixmap = icon().pixmap(iconTargetSize, mode, checked ? QIcon::On : QIcon::Off);
            const QPointF topLeft(targetRect.center().x() - (pixmap.width() * 0.5),
                                  targetRect.center().y() - (pixmap.height() * 0.5));
            painter.drawPixmap(topLeft, pixmap);
            return;
        }

        QFont glyphFont = font();
        const qreal pointSize = glyphFont.pointSizeF() > 0.0 ? glyphFont.pointSizeF() : 15.0;
        glyphFont.setPointSizeF(pointSize * m_scale);
        painter.setFont(glyphFont);
        painter.setPen(glyphColor);
        painter.drawText(targetRect, Qt::AlignCenter, text());
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

QString collapsedPreviewGlyph()
{
    return FluentIconFont::glyph(0xE740);
}

QString expandedPreviewGlyph()
{
    return FluentIconFont::glyph(0xE73F);
}

}

PreviewHeaderBar::PreviewHeaderBar(QWidget* leadingWidget,
                                   SelectableTitleLabel* titleLabel,
                                   QWidget* secondaryWidget,
                                   QWidget* trailingWidget,
                                   QWidget* parent)
    : QWidget(parent)
    , m_leadingWidget(leadingWidget)
    , m_titleLabel(titleLabel)
    , m_secondaryWidget(secondaryWidget)
    , m_trailingWidget(trailingWidget)
    , m_closeButton(new QToolButton(this))
{
    setObjectName(QStringLiteral("PreviewHeaderBar"));

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 12, 0, 0);
    rootLayout->setSpacing(4);

    auto* toolbar = new QWidget(this);
    toolbar->setObjectName(QStringLiteral("PreviewTitleToolbar"));
    auto* toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(0, 0, 0, 0);
    toolbarLayout->setSpacing(12);

    auto* actionCapsule = new QWidget(toolbar);
    actionCapsule->setObjectName(QStringLiteral("PreviewActionCapsule"));
    auto* actionLayout = new QHBoxLayout(actionCapsule);
    actionLayout->setContentsMargins(12, 6, 12, 6);
    actionLayout->setSpacing(0);

    auto* capsuleShadow = new QGraphicsDropShadowEffect(this);
    capsuleShadow->setBlurRadius(24.0);
    capsuleShadow->setOffset(0.0, 8.0);
    capsuleShadow->setColor(QColor(26, 50, 84, 24));
    actionCapsule->setGraphicsEffect(capsuleShadow);

    auto* pinButton = new DockPushButton(this);
    pinButton->setObjectName(QStringLiteral("PreviewPinButton"));
    pinButton->setFont(FluentIconFont::iconFont(15, QFont::Normal));
    pinButton->setText(FluentIconFont::glyph(0xE840));
    pinButton->setIconSize(QSize(20, 20));
    pinButton->setCursor(Qt::PointingHandCursor);
    pinButton->setToolTip(QStringLiteral("Pin preview"));
    pinButton->setFlat(true);
    pinButton->setCheckable(true);

    m_closeButton->setVisible(false);

    auto addSeparator = [actionCapsule, actionLayout]() {
        auto* separator = new QWidget(actionCapsule);
        separator->setObjectName(QStringLiteral("PreviewActionSeparator"));
        separator->setFixedSize(1, 18);
        actionLayout->addWidget(separator, 0, Qt::AlignVCenter);
        return separator;
    };

    struct MenuDescriptor {
        MenuAction action;
        QString objectName;
        QString glyph;
        QString toolTip;
        bool isPin = false;
    };

    const QVector<MenuDescriptor> menuDescriptors = {
        { MenuAction::Pin, QStringLiteral("PreviewPinButton"), FluentIconFont::glyph(0xE840), QStringLiteral("Pin preview"), true },
        { MenuAction::Open, QStringLiteral("PreviewOpenFolderButton"), FluentIconFont::glyph(0xE8E5), QStringLiteral("Open item"), false },
        { MenuAction::Copy, QStringLiteral("PreviewCopyButton"), FluentIconFont::glyph(0xE8C8), QStringLiteral("Copy path"), false },
        { MenuAction::Refresh, QStringLiteral("PreviewRefreshButton"), FluentIconFont::glyph(0xE72C), QStringLiteral("Refresh preview"), false },
        { MenuAction::Expand, QStringLiteral("PreviewExpandButton"), collapsedPreviewGlyph(), QStringLiteral("Expand preview"), false },
        { MenuAction::Close, QStringLiteral("PreviewShareButton"), FluentIconFont::glyph(0xE711), QStringLiteral("Close preview"), false },
        { MenuAction::More, QStringLiteral("PreviewMoreButton"), FluentIconFont::glyph(0xE712), QStringLiteral("More actions"), false }
    };

    for (int i = 0; i < menuDescriptors.size(); ++i) {
        const MenuDescriptor& descriptor = menuDescriptors.at(i);
        QAbstractButton* button = nullptr;
        if (descriptor.isPin) {
            button = pinButton;
        } else {
            button = createActionButton(descriptor.objectName,
                                        descriptor.glyph,
                                        descriptor.toolTip,
                                        actionCapsule);
        }

        actionLayout->addWidget(button, 0, Qt::AlignVCenter);
        QWidget* separator = nullptr;
        if (i + 1 < menuDescriptors.size()) {
            separator = addSeparator();
        }
        registerMenuItem(descriptor.action, button, separator);
        connect(button, &QAbstractButton::clicked, this, [this, action = descriptor.action]() {
            triggerMenuAction(action);
        });
    }

    toolbarLayout->addStretch(1);
    toolbarLayout->addWidget(actionCapsule, 0, Qt::AlignHCenter);
    toolbarLayout->addStretch(1);
    rootLayout->addWidget(toolbar);

    auto* contentRow = new QWidget(this);
    contentRow->setObjectName(QStringLiteral("PreviewTitleContentRow"));
    auto* contentLayout = new QHBoxLayout(contentRow);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(12);

    if (m_leadingWidget) {
        contentLayout->addWidget(m_leadingWidget, 0, Qt::AlignTop);
    }

    int iconHeight = 72;
    if (m_leadingWidget) {
        iconHeight = qMax(iconHeight, m_leadingWidget->sizeHint().height());
    }

    auto* textColumn = new QWidget(contentRow);
    textColumn->setObjectName(QStringLiteral("PreviewTitleTextColumn"));
    textColumn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    textColumn->setFixedHeight(iconHeight);
    auto* textColumnLayout = new QVBoxLayout(textColumn);
    textColumnLayout->setContentsMargins(0, 0, 0, 0);
    textColumnLayout->setSpacing(0);

    const int topSpacerHeight = qMax(8, static_cast<int>(iconHeight * 0.20));
    const int titleAreaHeight = qMax(24, static_cast<int>(iconHeight * 0.50));
    const int pathAreaHeight = qMax(20, iconHeight - topSpacerHeight - titleAreaHeight);

    textColumnLayout->addSpacing(topSpacerHeight);

    if (m_titleLabel) {
        m_titleLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        m_titleLabel->setMinimumHeight(titleAreaHeight);
        m_titleLabel->setMaximumHeight(titleAreaHeight);
        if (auto* titleLabelWidget = qobject_cast<QLabel*>(m_titleLabel)) {
            titleLabelWidget->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        }
        textColumnLayout->addWidget(m_titleLabel);
    }
    if (m_secondaryWidget) {
        m_secondaryWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        m_secondaryWidget->setMinimumHeight(pathAreaHeight);
        m_secondaryWidget->setMaximumHeight(pathAreaHeight);
        textColumnLayout->addWidget(m_secondaryWidget);
    }
    contentLayout->addWidget(textColumn, 1, Qt::AlignBottom);

    if (m_trailingWidget) {
        auto* trailingColumn = new QWidget(contentRow);
        trailingColumn->setObjectName(QStringLiteral("PreviewTrailingColumn"));
        trailingColumn->setFixedHeight(iconHeight);
        auto* trailingLayout = new QVBoxLayout(trailingColumn);
        trailingLayout->setContentsMargins(0, 0, 0, 0);
        trailingLayout->addStretch(1);
        trailingLayout->addWidget(m_trailingWidget, 0, Qt::AlignCenter);
        trailingLayout->addStretch(1);
        contentLayout->addWidget(trailingColumn, 0, Qt::AlignRight);
    }
    rootLayout->addWidget(contentRow);

    setStyleSheet(
        "#PreviewTitleToolbar {"
        "  background: transparent;"
        "}"
        "#PreviewActionCapsule {"
        "  background: rgba(252, 253, 255, 0.96);"
        "  border: 1px solid rgba(202, 214, 228, 0.98);"
        "  border-radius: 19px;"
        "}"
        "#PreviewActionSeparator {"
        "  background: rgba(192, 204, 219, 0.95);"
        "}"
        "#PreviewTitleContentRow {"
        "  background: transparent;"
        "}"
        "#PreviewTitleTextColumn {"
        "  background: transparent;"
        "}"
        "#PreviewTrailingColumn {"
        "  background: transparent;"
        "}"
        "#PreviewPinButton, #PreviewOpenFolderButton, #PreviewCopyButton, #PreviewRefreshButton,"
        "#PreviewExpandButton, #PreviewShareButton, #PreviewMoreButton {"
        "  background: transparent;"
        "  border: none;"
        "  min-width: 38px;"
        "  max-width: 38px;"
        "  min-height: 30px;"
        "  max-height: 30px;"
        "  border-radius: 12px;"
        "  padding: 0px;"
        "}"
        "#PreviewPinButton {"
        "  color: #46617f;"
        "}"
        "#PreviewPinButton:hover {"
        "  background: rgba(237, 243, 250, 0.98);"
        "}"
        "#PreviewPinButton:pressed {"
        "  background: rgba(226, 235, 246, 1.0);"
        "}"
        "#PreviewPinButton:checked {"
        "  color: #c42b1c;"
        "  background: rgba(255, 233, 231, 1.0);"
        "}"
        "#PreviewOpenFolderButton:hover, #PreviewCopyButton:hover, #PreviewRefreshButton:hover,"
        "#PreviewExpandButton:hover,"
        "#PreviewShareButton:hover, #PreviewMoreButton:hover {"
        "  background: rgba(237, 243, 250, 0.98);"
        "}"
        "#PreviewOpenFolderButton:pressed, #PreviewCopyButton:pressed, #PreviewRefreshButton:pressed,"
        "#PreviewExpandButton:pressed,"
        "#PreviewShareButton:pressed, #PreviewMoreButton:pressed {"
        "  background: rgba(226, 235, 246, 1.0);"
        "}"
    );
    connect(&SpaceLookUiSettings::instance(), &SpaceLookUiSettings::menuButtonSizeChanged, this, [this]() {
        applyMenuButtonSize();
    });
    connect(&SpaceLookUiSettings::instance(), &SpaceLookUiSettings::menuVisibilityChanged, this, [this]() {
        applyMenuVisibility();
    });

    if (SpaceLookWindow* previewWindow = qobject_cast<SpaceLookWindow*>(window())) {
        syncPinState(previewWindow->isAlwaysOnTop());
        syncExpandState(previewWindow->isExpandedPreview());
        setExpandEnabled(previewWindow->supportsExpandedPreview());
    } else {
        syncPinState(false);
        syncExpandState(false);
        setExpandEnabled(false);
    }

    applyMenuButtonSize();
    applyMenuVisibility();
}

void PreviewHeaderBar::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    refreshWindowState();
}

QToolButton* PreviewHeaderBar::createActionButton(const QString& objectName,
                                                  const QString& glyph,
                                                  const QString& toolTip,
                                                  QWidget* parent)
{
    auto* button = new DockToolButton(parent);
    button->setObjectName(objectName);
    button->setFont(FluentIconFont::iconFont(15, QFont::Normal));
    button->setText(glyph);
    button->setToolTip(toolTip);
    button->setCursor(Qt::PointingHandCursor);
    button->setAutoRaise(true);
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    return button;
}

void PreviewHeaderBar::registerMenuItem(MenuAction action, QAbstractButton* button, QWidget* separator)
{
    m_menuItems.push_back({ action, button, separator });
}

QAbstractButton* PreviewHeaderBar::menuButton(MenuAction action) const
{
    for (const MenuItemEntry& entry : m_menuItems) {
        if (entry.action == action) {
            return entry.button;
        }
    }
    return nullptr;
}

QToolButton* PreviewHeaderBar::menuToolButton(MenuAction action) const
{
    return qobject_cast<QToolButton*>(menuButton(action));
}

QWidget* PreviewHeaderBar::menuSeparator(MenuAction action) const
{
    for (const MenuItemEntry& entry : m_menuItems) {
        if (entry.action == action) {
            return entry.separator;
        }
    }
    return nullptr;
}

void PreviewHeaderBar::triggerMenuAction(MenuAction action)
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

QPushButton* PreviewHeaderBar::pinButton() const
{
    return qobject_cast<QPushButton*>(menuButton(MenuAction::Pin));
}

SelectableTitleLabel* PreviewHeaderBar::titleLabel() const
{
    return m_titleLabel;
}

QToolButton* PreviewHeaderBar::closeButton() const
{
    return m_closeButton;
}

void PreviewHeaderBar::setExpandEnabled(bool enabled)
{
    m_contextExpandEnabled = enabled;
    if (QAbstractButton* expandButton = menuButton(MenuAction::Expand)) {
        expandButton->setVisible(enabled && SpaceLookUiSettings::instance().showMenuExpand());
    }
    if (QWidget* expandSeparator = menuSeparator(MenuAction::Expand)) {
        expandSeparator->setVisible(enabled && SpaceLookUiSettings::instance().showMenuExpand());
    }
}

void PreviewHeaderBar::setOpenActionGlyph(const QString& glyph, const QString& toolTip)
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

void PreviewHeaderBar::refreshWindowState()
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

void PreviewHeaderBar::applyMenuButtonSize()
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
}

void PreviewHeaderBar::applyMenuVisibility()
{
    SpaceLookUiSettings& settings = SpaceLookUiSettings::instance();

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
        button->setVisible(settings.showMenuMore());
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
            if (nextButton && nextButton->isVisible()) {
                hasVisibleNext = true;
                break;
            }
            ++nextVisibleIndex;
        }

        entry.separator->setVisible(entry.button->isVisible() && hasVisibleNext);
    }
}

void PreviewHeaderBar::syncPinState(bool alwaysOnTop)
{
    QPushButton* pinControl = pinButton();
    if (!pinControl) {
        return;
    }

    const QSignalBlocker blocker(pinControl);
    pinControl->setChecked(alwaysOnTop);
    pinControl->setIcon(QIcon());
    pinControl->setText(FluentIconFont::glyph(0xE840));
    pinControl->setToolTip(alwaysOnTop
                               ? QStringLiteral("Disable always on top")
                               : QStringLiteral("Pin preview"));
    pinControl->style()->unpolish(pinControl);
    pinControl->style()->polish(pinControl);
    pinControl->update();
}

void PreviewHeaderBar::syncExpandState(bool expanded)
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
