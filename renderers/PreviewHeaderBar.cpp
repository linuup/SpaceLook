#include "renderers/PreviewHeaderBar.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QSizePolicy>
#include <QToolButton>
#include <QVBoxLayout>

#include "renderers/SelectableTitleLabel.h"

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
    , m_contentRow(new QWidget(this))
    , m_textColumn(new QWidget(m_contentRow))
    , m_trailingColumn(nullptr)
    , m_closeButton(new QToolButton(this))
{
    setObjectName(QStringLiteral("PreviewHeaderBar"));

    m_contentRow->setObjectName(QStringLiteral("PreviewTitleContentRow"));
    m_contentRow->setAttribute(Qt::WA_StyledBackground, true);
    m_contentRow->setFixedHeight(82);
    auto* contentLayout = new QHBoxLayout(m_contentRow);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(12);

    if (m_leadingWidget) {
        auto* leadingWrap = new QWidget(m_contentRow);
        auto* leadingLayout = new QVBoxLayout(leadingWrap);
        leadingLayout->setContentsMargins(0, 6, 0, 0);
        leadingLayout->setSpacing(0);
        leadingLayout->addWidget(m_leadingWidget, 0, Qt::AlignTop);
        leadingLayout->addStretch(1);
        contentLayout->addWidget(leadingWrap, 0, Qt::AlignTop);
    }

    int iconHeight = 82;
    if (m_leadingWidget) {
        iconHeight = qMax(iconHeight, m_leadingWidget->sizeHint().height() + 10);
    }

    m_textColumn->setObjectName(QStringLiteral("PreviewTitleTextColumn"));
    m_textColumn->setAttribute(Qt::WA_StyledBackground, true);
    m_textColumn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_textColumn->setFixedHeight(82);
    auto* textColumnLayout = new QVBoxLayout(m_textColumn);
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
    contentLayout->addWidget(m_textColumn, 1, Qt::AlignBottom);

    if (m_trailingWidget) {
        m_trailingColumn = new QWidget(m_contentRow);
        m_trailingColumn->setObjectName(QStringLiteral("PreviewTrailingColumn"));
        m_trailingColumn->setAttribute(Qt::WA_StyledBackground, true);
        m_trailingColumn->setFixedHeight(82);
        auto* trailingLayout = new QVBoxLayout(m_trailingColumn);
        trailingLayout->setContentsMargins(0, 0, 0, 0);
        trailingLayout->addStretch(1);
        trailingLayout->addWidget(m_trailingWidget, 0, Qt::AlignCenter);
        trailingLayout->addStretch(1);
        contentLayout->addWidget(m_trailingColumn, 0, Qt::AlignRight);
    }

    m_closeButton->setVisible(false);
    applyContentAppearance();
}

QToolButton* PreviewHeaderBar::closeButton() const
{
    return m_closeButton;
}

SelectableTitleLabel* PreviewHeaderBar::titleLabel() const
{
    return m_titleLabel;
}

QWidget* PreviewHeaderBar::contentWidget() const
{
    return m_contentRow;
}

void PreviewHeaderBar::setOpenActionGlyph(const QString& glyph, const QString& toolTip)
{
    Q_UNUSED(glyph);
    Q_UNUSED(toolTip);
}

void PreviewHeaderBar::applyContentAppearance()
{
    if (!m_contentRow) {
        return;
    }

    m_contentRow->setStyleSheet(
        "#PreviewTitleContentRow {"
        "  background: transparent;"
        "}"
        "#PreviewTitleTextColumn {"
        "  background: transparent;"
        "}"
        "#PreviewTrailingColumn {"
        "  background: transparent;"
        "}"
    );
}
