#include "renderers/welcome/WelcomeRenderer.h"

#include <QGraphicsDropShadowEffect>
#include <QCoreApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QTimer>
#include <QVBoxLayout>

#include "renderers/FluentIconFont.h"
#include "widgets/SpaceLookWindow.h"

namespace {

QPixmap roundedCoverPixmap(const QPixmap& source, const QSize& targetSize, int radius)
{
    if (source.isNull() || targetSize.isEmpty()) {
        return QPixmap();
    }

    const QPixmap scaled = source.scaled(targetSize, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    const QPoint cropOffset(
        qMax(0, (scaled.width() - targetSize.width()) / 2),
        qMax(0, (scaled.height() - targetSize.height()) / 2));

    QPixmap cropped = scaled.copy(QRect(cropOffset, targetSize));
    QPixmap rounded(targetSize);
    rounded.fill(Qt::transparent);

    QPainter painter(&rounded);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath clipPath;
    clipPath.addRoundedRect(QRectF(QPointF(0, 0), QSizeF(targetSize)), radius, radius);
    painter.setClipPath(clipPath);
    painter.drawPixmap(0, 0, cropped);
    return rounded;
}

QWidget* createFeatureChip(const QString& iconPath, const QString& title, QWidget* parent)
{
    auto* chip = new QWidget(parent);
    chip->setObjectName(QStringLiteral("WelcomeFeatureChip"));
    chip->setProperty("welcomeIconPath", iconPath);
    chip->setProperty("welcomeTitle", title);
    chip->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    auto* layout = new QHBoxLayout(chip);
    layout->setContentsMargins(15, 9, 18, 9);
    layout->setSpacing(9);

    auto* iconLabel = new QLabel(chip);
    iconLabel->setObjectName(QStringLiteral("WelcomeFeatureIcon"));
    iconLabel->setFixedSize(26, 26);
    iconLabel->setAlignment(Qt::AlignCenter);
    iconLabel->setPixmap(QIcon(iconPath).pixmap(25, 25));
    layout->addWidget(iconLabel);

    auto* titleLabel = new QLabel(title, chip);
    titleLabel->setObjectName(QStringLiteral("WelcomeFeatureTitle"));
    titleLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    layout->addWidget(titleLabel);
    return chip;
}

}

WelcomeRenderer::WelcomeRenderer(QWidget* parent)
    : QWidget(parent)
    , m_card(new QWidget(this))
    , m_heroLabel(new QLabel(this))
    , m_titleLabel(new QLabel(this))
    , m_subtitleLabel(new QLabel(this))
    , m_chipRow(new QWidget(this))
    , m_chipTrack(new QWidget(m_chipRow))
    , m_chipLayout(new QHBoxLayout(m_chipTrack))
    , m_chipScrollTimer(new QTimer(this))
    , m_exploreButton(new QPushButton(this))
    , m_hintLabel(new QLabel(this))
    , m_heroPixmap(QStringLiteral(":/SPACELOOK/image/welcome-hero.png"))
{
    setAttribute(Qt::WA_StyledBackground, true);
    setObjectName(QStringLiteral("WelcomeRendererRoot"));
    m_card->setObjectName(QStringLiteral("WelcomeCard"));
    m_heroLabel->setObjectName(QStringLiteral("WelcomeHero"));
    m_titleLabel->setObjectName(QStringLiteral("WelcomeTitle"));
    m_subtitleLabel->setObjectName(QStringLiteral("WelcomeSubtitle"));
    m_chipRow->setObjectName(QStringLiteral("WelcomeChipRow"));
    m_chipTrack->setObjectName(QStringLiteral("WelcomeChipTrack"));
    m_exploreButton->setObjectName(QStringLiteral("WelcomeExploreButton"));
    m_hintLabel->setObjectName(QStringLiteral("WelcomeHint"));

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(2, 2, 2, 2);
    rootLayout->setSpacing(0);
    rootLayout->addWidget(m_card, 1);

    auto* cardLayout = new QVBoxLayout(m_card);
    cardLayout->setContentsMargins(86, 26, 86, 28);
    cardLayout->setSpacing(8);
    cardLayout->addWidget(m_heroLabel, 1);
    cardLayout->addSpacing(12);
    cardLayout->addWidget(m_titleLabel);
    cardLayout->addSpacing(2);
    cardLayout->addWidget(m_subtitleLabel);
    cardLayout->addSpacing(14);
    cardLayout->addWidget(m_chipRow);
    cardLayout->addSpacing(12);
    cardLayout->addWidget(m_exploreButton, 0, Qt::AlignHCenter);
    cardLayout->addStretch(1);
    cardLayout->addWidget(m_hintLabel, 0, Qt::AlignRight);

    m_heroLabel->setAlignment(Qt::AlignCenter);
    m_heroLabel->setMinimumHeight(300);
    m_heroLabel->setMaximumHeight(350);
    m_heroLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_chipRow->setFixedHeight(44);
    m_chipRow->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_titleLabel->setText(QCoreApplication::translate("SpaceLook", "Welcome to SpaceLook"));
    m_subtitleLabel->setText(QCoreApplication::translate("SpaceLook", "Hover a file or folder and press Space to preview it instantly."));
    m_exploreButton->setText(QCoreApplication::translate("SpaceLook", "Explore"));
    m_exploreButton->setCursor(Qt::PointingHandCursor);
    m_exploreButton->setFixedSize(198, 44);
    connect(m_exploreButton, &QPushButton::clicked, this, [this]() {
        if (SpaceLookWindow* previewWindow = qobject_cast<SpaceLookWindow*>(window())) {
            previewWindow->hidePreview();
        }
    });
    m_hintLabel->setText(QStringLiteral(
        "<a href=\"https://github.com/linuup\" style=\"color:#1680ff; text-decoration: underline;\">%3</a>"
        "  <a href=\"https://github.com/linuup\" style=\"color:#1680ff; text-decoration: none; font-family:'%1'; font-size:16px; font-weight:700;\">%2</a>")
        .arg(FluentIconFont::family().toHtmlEscaped(), FluentIconFont::glyph(0xE72D), QCoreApplication::translate("SpaceLook", "Learn more")));
    m_hintLabel->setTextFormat(Qt::RichText);
    m_hintLabel->setOpenExternalLinks(true);
    m_titleLabel->setAlignment(Qt::AlignCenter);
    m_subtitleLabel->setAlignment(Qt::AlignCenter);
    m_hintLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_subtitleLabel->setWordWrap(true);
    m_hintLabel->setCursor(Qt::PointingHandCursor);

    m_chipLayout->setContentsMargins(0, 0, 0, 0);
    m_chipLayout->setSpacing(14);
    m_featureChips.append(createFeatureChip(QStringLiteral(":/SPACELOOK/welcome-icons/welcome-document.svg"), QCoreApplication::translate("SpaceLook", "PDF"), m_chipTrack));
    m_featureChips.append(createFeatureChip(QStringLiteral(":/SPACELOOK/welcome-icons/welcome-image.svg"), QCoreApplication::translate("SpaceLook", "Images"), m_chipTrack));
    m_featureChips.append(createFeatureChip(QStringLiteral(":/SPACELOOK/welcome-icons/welcome-code.svg"), QCoreApplication::translate("SpaceLook", "Code"), m_chipTrack));
    m_featureChips.append(createFeatureChip(QStringLiteral(":/SPACELOOK/welcome-icons/welcome-folder.svg"), QCoreApplication::translate("SpaceLook", "Folders"), m_chipTrack));
    m_featureChips.append(createFeatureChip(QStringLiteral(":/SPACELOOK/welcome-icons/welcome-archive.svg"), QCoreApplication::translate("SpaceLook", "Archives"), m_chipTrack));
    m_featureChips.append(createFeatureChip(QStringLiteral(":/SPACELOOK/welcome-icons/welcome-text.svg"), QCoreApplication::translate("SpaceLook", "Text"), m_chipTrack));
    relayoutFeatureChips();

    m_chipScrollTimer->setInterval(16);
    connect(m_chipScrollTimer, &QTimer::timeout, this, &WelcomeRenderer::scrollFeatureChips);
    m_chipScrollTimer->start();

    auto* heroShadow = new QGraphicsDropShadowEffect(m_heroLabel);
    heroShadow->setBlurRadius(36);
    heroShadow->setOffset(0, 16);
    heroShadow->setColor(QColor(62, 95, 132, 36));
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
    updateChipTrackGeometry();
}

void WelcomeRenderer::applyChrome()
{
    setStyleSheet(QStringLiteral(
        "#WelcomeRendererRoot {"
        "  background: transparent;"
        "  border-radius: 0px;"
        "}"
        "#WelcomeCard {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:1,"
        "      stop:0 #fbfdff,"
        "      stop:0.52 #f7fbff,"
        "      stop:1 #fffaf4);"
        "  border: none;"
        "  border-radius: 8px;"
        "}"
        "#WelcomeHero {"
        "  background: transparent;"
        "  border: none;"
        "  padding: 0px;"
        "}"
        "#WelcomeTitle {"
        "  color: #0f1f33;"
        "  font-family: 'Segoe UI Rounded';"
        "  font-size: 36px;"
        "  font-weight: 800;"
        "}"
        "#WelcomeSubtitle {"
        "  color: #7a8baa;"
        "  font-family: 'Segoe UI Rounded';"
        "  font-size: 16px;"
        "  font-weight: 650;"
        "}"
        "#WelcomeChipRow {"
        "  background: transparent;"
        "}"
        "#WelcomeChipTrack {"
        "  background: transparent;"
        "}"
        "#WelcomeFeatureChip {"
        "  background: rgba(255, 255, 255, 0.72);"
        "  border: none;"
        "  border-radius: 22px;"
        "}"
        "#WelcomeFeatureIcon {"
        "  background: transparent;"
        "  border: none;"
        "  padding: 0px;"
        "}"
        "#WelcomeFeatureTitle {"
        "  color: #152b45;"
        "  font-family: 'Segoe UI Rounded';"
        "  font-size: 14px;"
        "  font-weight: 650;"
        "}"
        "#WelcomeExploreButton {"
        "  color: #ffffff;"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:0,"
        "      stop:0 #198fff,"
        "      stop:0.62 #4a8aff,"
        "      stop:1 #8a72f7);"
        "  border: 1px solid rgba(44, 132, 255, 0.95);"
        "  border-radius: 22px;"
        "  font-family: 'Segoe UI Rounded';"
        "  font-size: 17px;"
        "  font-weight: 750;"
        "  letter-spacing: 0.2px;"
        "  padding-bottom: 1px;"
        "}"
        "#WelcomeExploreButton:hover {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:0,"
        "      stop:0 #0d84f5,"
        "      stop:0.62 #3f7ff4,"
        "      stop:1 #8067ee);"
        "}"
        "#WelcomeExploreButton:pressed {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:0,"
        "      stop:0 #0a73d7,"
        "      stop:0.62 #346cdc,"
        "      stop:1 #7059d6);"
        "}"
        "#WelcomeHint {"
        "  color: #1680ff;"
        "  font-family: 'Segoe UI Rounded';"
        "  font-size: 14px;"
        "  font-weight: 650;"
        "}"
    ));
}

void WelcomeRenderer::updateHeroPixmap()
{
    if (m_heroPixmap.isNull() || !m_heroLabel) {
        return;
    }

    const QSize targetSize = m_heroLabel->contentsRect().size().boundedTo(QSize(860, 350));
    if (!targetSize.isValid()) {
        return;
    }

    m_heroLabel->setPixmap(roundedCoverPixmap(m_heroPixmap, targetSize, 12));
}

void WelcomeRenderer::updateChipTrackGeometry()
{
    if (!m_chipRow || !m_chipTrack || !m_chipLayout) {
        return;
    }

    const QSize trackSize = m_chipLayout->sizeHint();
    const int trackWidth = qMax(trackSize.width(), 1);
    const int trackHeight = m_chipRow->height();
    const int baseX = qMax(0, (m_chipRow->width() - trackWidth) / 2);
    if (m_lastChipRowWidth != m_chipRow->width()) {
        m_chipTrackX = static_cast<double>(baseX);
        m_lastChipRowWidth = m_chipRow->width();
    }
    m_chipTrack->setGeometry(static_cast<int>(m_chipTrackX), 0, trackWidth, trackHeight);
}

void WelcomeRenderer::scrollFeatureChips()
{
    if (!m_chipTrack || !m_chipLayout || !m_chipRow || m_featureChips.size() < 2) {
        return;
    }

    updateChipTrackGeometry();
    const int step = m_featureChips.first()->sizeHint().width() + m_chipLayout->spacing();
    if (step <= 0) {
        return;
    }

    const QSize trackSize = m_chipLayout->sizeHint();
    const int baseX = qMax(0, (m_chipRow->width() - qMax(trackSize.width(), 1)) / 2);
    m_chipTrackX -= 0.45;
    if (m_chipTrackX <= static_cast<double>(baseX - step)) {
        m_featureChips.append(m_featureChips.takeFirst());
        relayoutFeatureChips();
        m_chipTrackX += static_cast<double>(step);
    }
    m_chipTrack->move(static_cast<int>(m_chipTrackX), 0);
}

void WelcomeRenderer::relayoutFeatureChips()
{
    if (!m_chipLayout) {
        return;
    }

    while (QLayoutItem* item = m_chipLayout->takeAt(0)) {
        delete item;
    }

    for (QWidget* chip : std::as_const(m_featureChips)) {
        if (chip) {
            m_chipLayout->addWidget(chip);
        }
    }
    if (QWidget* duplicateChip = duplicateFirstFeatureChip()) {
        m_chipLayout->addWidget(duplicateChip);
    }
    m_chipLayout->activate();
}

QWidget* WelcomeRenderer::duplicateFirstFeatureChip()
{
    if (m_featureChips.isEmpty() || !m_chipTrack) {
        return nullptr;
    }

    if (m_trailingDuplicateChip) {
        m_trailingDuplicateChip->deleteLater();
        m_trailingDuplicateChip = nullptr;
    }

    QWidget* firstChip = m_featureChips.first();
    if (!firstChip) {
        return nullptr;
    }

    const QString iconPath = firstChip->property("welcomeIconPath").toString();
    const QString title = firstChip->property("welcomeTitle").toString();
    if (iconPath.isEmpty() || title.isEmpty()) {
        return nullptr;
    }

    m_trailingDuplicateChip = createFeatureChip(iconPath, title, m_chipTrack);
    return m_trailingDuplicateChip;
}
