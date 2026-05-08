#include "renderers/ModeSwitchButton.h"

#include <QAction>
#include <QHBoxLayout>
#include <QMenu>
#include <QToolButton>

#include "renderers/FluentIconFont.h"

ModeSwitchButton::ModeSwitchButton(QWidget* parent)
    : QWidget(parent)
    , m_primaryButton(new QToolButton(this))
    , m_expandButton(new QToolButton(this))
    , m_menu(new QMenu(this))
{
    setObjectName(QStringLiteral("ModeSwitchWidget"));

    m_modes = {
        { QStringLiteral("text"), QStringLiteral("TEXT") },
        { QStringLiteral("code"), QStringLiteral("STRUCT") }
    };

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_primaryButton->setObjectName(QStringLiteral("ModeSwitchPrimaryButton"));
    m_primaryButton->setCursor(Qt::PointingHandCursor);
    m_primaryButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_primaryButton->setFixedSize(46, 28);

    m_expandButton->setObjectName(QStringLiteral("ModeSwitchExpandButton"));
    m_expandButton->setCursor(Qt::PointingHandCursor);
    m_expandButton->setFont(FluentIconFont::iconFont(11, QFont::Normal));
    m_expandButton->setText(FluentIconFont::glyph(0xE70D));
    m_expandButton->setMenu(m_menu);
    m_expandButton->setPopupMode(QToolButton::InstantPopup);
    m_expandButton->setFixedSize(20, 28);
    m_expandButton->setStyleSheet(
        "#ModeSwitchExpandButton::menu-indicator {"
        "  image: none;"
        "  width: 0px;"
        "}");

    layout->addWidget(m_primaryButton);
    layout->addWidget(m_expandButton);

    connect(m_primaryButton, &QToolButton::clicked, this, [this]() {
        showMenu();
    });

    rebuildMenu();
    updatePrimaryButton();
}

ModeSwitchButton::~ModeSwitchButton() = default;

void ModeSwitchButton::setCurrentModeId(const QString& modeId)
{
    const QString normalized = modeId.trimmed().toLower();
    for (int index = 0; index < m_modes.size(); ++index) {
        if (m_modes.at(index).modeId == normalized) {
            selectMode(index, false);
            return;
        }
    }
}

QString ModeSwitchButton::currentModeId() const
{
    if (m_currentModeIndex < 0 || m_currentModeIndex >= m_modes.size()) {
        return QStringLiteral("text");
    }

    return m_modes.at(m_currentModeIndex).modeId;
}

void ModeSwitchButton::setModeChangedCallback(std::function<void(const QString&)> callback)
{
    m_modeChangedCallback = std::move(callback);
}

void ModeSwitchButton::setMenuFont(const QFont& font)
{
    m_primaryButton->setFont(font);
    m_expandButton->setFont(FluentIconFont::iconFont(11, QFont::Normal));
    m_menu->setFont(font);
}

void ModeSwitchButton::rebuildMenu()
{
    m_menu->clear();

    for (int index = 0; index < m_modes.size(); ++index) {
        const ModeEntry& mode = m_modes.at(index);
        QAction* action = new QAction(mode.displayText, m_menu);
        action->setCheckable(true);
        action->setChecked(index == m_currentModeIndex);
        connect(action, &QAction::triggered, this, [this, index]() {
            selectMode(index, true);
        });
        m_menu->addAction(action);
    }
}

void ModeSwitchButton::updatePrimaryButton()
{
    if (m_currentModeIndex < 0 || m_currentModeIndex >= m_modes.size()) {
        m_primaryButton->setText(QStringLiteral("TEXT"));
        return;
    }

    m_primaryButton->setText(m_modes.at(m_currentModeIndex).displayText);
}

void ModeSwitchButton::selectMode(int index, bool emitChange)
{
    if (index < 0 || index >= m_modes.size()) {
        return;
    }

    const bool changed = m_currentModeIndex != index;
    m_currentModeIndex = index;
    rebuildMenu();
    updatePrimaryButton();

    if (emitChange && changed && m_modeChangedCallback) {
        m_modeChangedCallback(m_modes.at(index).modeId);
    }
}

void ModeSwitchButton::showMenu()
{
    if (m_menu->isEmpty()) {
        return;
    }

    const QSize menuSizeHint = m_menu->sizeHint();
    const QPoint anchorTopRight = m_expandButton->mapToGlobal(QPoint(m_expandButton->width(), m_expandButton->height() + 8));
    const QPoint popupPos(anchorTopRight.x() - menuSizeHint.width(), anchorTopRight.y());
    m_menu->popup(popupPos);
}
