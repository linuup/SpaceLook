#include "renderers/welcome/WelcomeRenderer.h"

#include <QGraphicsDropShadowEffect>
#include <QLabel>
#include <QPixmap>
#include <QResizeEvent>
#include <QVBoxLayout>

WelcomeRenderer::WelcomeRenderer(QWidget* parent)
    : QWidget(parent)
    , m_heroLabel(new QLabel(this))
    , m_titleLabel(new QLabel(this))
    , m_subtitleLabel(new QLabel(this))
    , m_hintLabel(new QLabel(this))
    , m_heroPixmap(QStringLiteral(":/SPACELOOK/image/welcome-hero.png"))
{
    setAttribute(Qt::WA_StyledBackground, true);
    setObjectName(QStringLiteral("WelcomeRendererRoot"));
    m_heroLabel->setObjectName(QStringLiteral("WelcomeHero"));
    m_titleLabel->setObjectName(QStringLiteral("WelcomeTitle"));
    m_subtitleLabel->setObjectName(QStringLiteral("WelcomeSubtitle"));
    m_hintLabel->setObjectName(QStringLiteral("WelcomeHint"));

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(34, 28, 34, 34);
    rootLayout->setSpacing(18);
    rootLayout->addWidget(m_heroLabel, 1);
    rootLayout->addWidget(m_titleLabel);
    rootLayout->addWidget(m_subtitleLabel);
    rootLayout->addWidget(m_hintLabel);

    m_heroLabel->setAlignment(Qt::AlignCenter);
    m_heroLabel->setMinimumHeight(260);
    m_heroLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_titleLabel->setText(QStringLiteral("Welcome to SpaceLook"));
    m_subtitleLabel->setText(QStringLiteral("Hover a file or folder and press Space to preview it instantly."));
    m_hintLabel->setText(QStringLiteral("Supported previews include PDF, images, media, code, markup, folders, archives, and text."));
    m_titleLabel->setAlignment(Qt::AlignCenter);
    m_subtitleLabel->setAlignment(Qt::AlignCenter);
    m_hintLabel->setAlignment(Qt::AlignCenter);
    m_subtitleLabel->setWordWrap(true);
    m_hintLabel->setWordWrap(true);

    auto* heroShadow = new QGraphicsDropShadowEffect(m_heroLabel);
    heroShadow->setBlurRadius(32);
    heroShadow->setOffset(0, 14);
    heroShadow->setColor(QColor(28, 58, 82, 42));
    m_heroLabel->setGraphicsEffect(heroShadow);

    applyChrome();
    updateHeroPixmap();
}

QString WelcomeRenderer::rendererId() const
{
    return QStringLiteral("welcome");
}

bool WelcomeRenderer::canHandle(const HoveredItemInfo& info) const
{
    return info.typeKey == QStringLiteral("welcome");
}

QWidget* WelcomeRenderer::widget()
{
    return this;
}

void WelcomeRenderer::load(const HoveredItemInfo& info)
{
    Q_UNUSED(info);
    updateHeroPixmap();
}

void WelcomeRenderer::unload()
{
}

void WelcomeRenderer::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateHeroPixmap();
}

void WelcomeRenderer::applyChrome()
{
    setStyleSheet(QStringLiteral(
        "#WelcomeRendererRoot {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:1,"
        "      stop:0 #fbfdff,"
        "      stop:0.52 #eef7f8,"
        "      stop:1 #f9f2e9);"
        "  border-radius: 0px;"
        "}"
        "#WelcomeHero {"
        "  background: rgba(255, 255, 255, 0.66);"
        "  border: 1px solid rgba(203, 218, 229, 0.86);"
        "  border-radius: 28px;"
        "  padding: 10px;"
        "}"
        "#WelcomeTitle {"
        "  color: #10263a;"
        "  font-family: 'Segoe UI Variable Display';"
        "  font-size: 32px;"
        "  font-weight: 700;"
        "}"
        "#WelcomeSubtitle {"
        "  color: #49677f;"
        "  font-family: 'Segoe UI Variable Text';"
        "  font-size: 16px;"
        "}"
        "#WelcomeHint {"
        "  color: #6e8497;"
        "  font-family: 'Segoe UI';"
        "  font-size: 13px;"
        "}"
    ));
}

void WelcomeRenderer::updateHeroPixmap()
{
    if (m_heroPixmap.isNull() || !m_heroLabel) {
        return;
    }

    const QSize targetSize = m_heroLabel->contentsRect().size().boundedTo(QSize(1280, 720));
    if (!targetSize.isValid()) {
        return;
    }

    m_heroLabel->setPixmap(m_heroPixmap.scaled(
        targetSize,
        Qt::KeepAspectRatio,
        Qt::SmoothTransformation));
}
