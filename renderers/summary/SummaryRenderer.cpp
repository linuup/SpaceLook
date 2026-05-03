#include "renderers/summary/SummaryRenderer.h"

#include <QDateTime>
#include <QDebug>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QScrollArea>
#include <QSizePolicy>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "core/preview_state.h"
#include "renderers/FileTypeIconResolver.h"
#include "renderers/OpenWithButton.h"
#include "renderers/PreviewHeaderBar.h"
#include "renderers/SelectableTitleLabel.h"
#include "widgets/SpaceLookWindow.h"

namespace {

bool shouldShowSummaryStatus(const HoveredItemInfo& info)
{
    if (!info.valid || !info.exists) {
        return true;
    }

    const QString message = info.statusMessage.trimmed().toLower();
    if (message.isEmpty()) {
        return false;
    }

    return message.contains(QStringLiteral("fail")) ||
        message.contains(QStringLiteral("error")) ||
        message.contains(QStringLiteral("unavailable")) ||
        message.contains(QStringLiteral("does not exist")) ||
        message.contains(QStringLiteral("no object")) ||
        message.contains(QStringLiteral("outside explorer")) ||
        message.contains(QStringLiteral("outside the desktop")) ||
        message.contains(QStringLiteral("warning")) ||
        message.contains(QStringLiteral("truncated"));
}

QString displayValue(const QString& value, const QString& fallback = QStringLiteral("Unavailable"))
{
    const QString trimmed = value.trimmed();
    return trimmed.isEmpty() ? fallback : trimmed;
}

QString displayDateTime(const QDateTime& value)
{
    if (!value.isValid()) {
        return QStringLiteral("Unavailable");
    }
    return value.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

QString displayFileSize(qint64 size, bool isDirectory)
{
    if (isDirectory || size < 0) {
        return QStringLiteral("Unavailable");
    }

    double currentSize = static_cast<double>(size);
    QString unit = QStringLiteral("B");
    if (currentSize >= 1024.0) {
        currentSize /= 1024.0;
        unit = QStringLiteral("KB");
    }
    if (currentSize >= 1024.0) {
        currentSize /= 1024.0;
        unit = QStringLiteral("MB");
    }
    if (currentSize >= 1024.0) {
        currentSize /= 1024.0;
        unit = QStringLiteral("GB");
    }

    const int precision = unit == QStringLiteral("B") ? 0 : 2;
    return QStringLiteral("%1 %2").arg(QString::number(currentSize, 'f', precision), unit);
}

}

SummaryRenderer::SummaryRenderer(PreviewState* previewState, QWidget* parent)
    : QWidget(parent)
    , m_headerRow(new QWidget(this))
    , m_iconLabel(new QLabel(this))
    , m_titleLabel(new SelectableTitleLabel(this))
    , m_metaLabel(new QLabel(this))
    , m_pathRow(new QWidget(this))
    , m_pathTitleLabel(new QLabel(this))
    , m_pathValueLabel(new QLabel(this))
    , m_openWithButton(new OpenWithButton(this))
    , m_statusLabel(new QLabel(this))
    , m_detailsPanel(new QWidget(this))
    , m_detailsScrollArea(new QScrollArea(this))
    , m_detailsContent(new QWidget(this))
    , m_previewState(previewState)
{
    setAttribute(Qt::WA_StyledBackground, true);
    setObjectName(QStringLiteral("SummaryRendererRoot"));
    m_headerRow->setObjectName(QStringLiteral("SummaryHeaderRow"));
    m_iconLabel->setObjectName(QStringLiteral("SummaryTypeIcon"));
    m_titleLabel->setObjectName(QStringLiteral("SummaryTitle"));
    m_metaLabel->setObjectName(QStringLiteral("SummaryMeta"));
    m_pathRow->setObjectName(QStringLiteral("SummaryPathRow"));
    m_pathTitleLabel->setObjectName(QStringLiteral("SummaryPathTitle"));
    m_pathValueLabel->setObjectName(QStringLiteral("SummaryPathValue"));
    m_openWithButton->setObjectName(QStringLiteral("SummaryOpenWithButton"));
    m_statusLabel->setObjectName(QStringLiteral("SummaryStatus"));
    m_detailsPanel->setObjectName(QStringLiteral("SummaryDetails"));
    m_detailsScrollArea->setObjectName(QStringLiteral("SummaryDetailsScrollArea"));
    m_detailsContent->setObjectName(QStringLiteral("SummaryDetailsContent"));

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(12, 0, 12, 12);
    rootLayout->setSpacing(14);
    rootLayout->addWidget(m_headerRow);
    rootLayout->addWidget(m_statusLabel);
    rootLayout->addWidget(m_detailsPanel, 1);

    auto* headerLayout = new QHBoxLayout(m_headerRow);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(12);
    auto* titleBlock = new PreviewHeaderBar(m_iconLabel, m_titleLabel, m_pathRow, m_openWithButton, m_headerRow);
    headerLayout->addWidget(titleBlock, 1);

    auto* pathLayout = new QHBoxLayout(m_pathRow);
    pathLayout->setContentsMargins(0, 0, 0, 0);
    pathLayout->setSpacing(8);
    pathLayout->addWidget(m_pathValueLabel, 1);

    auto* detailsPanelLayout = new QVBoxLayout(m_detailsPanel);
    detailsPanelLayout->setContentsMargins(18, 16, 12, 16);
    detailsPanelLayout->setSpacing(0);
    detailsPanelLayout->addWidget(m_detailsScrollArea);

    m_detailsScrollArea->setWidgetResizable(true);
    m_detailsScrollArea->setFrameShape(QFrame::NoFrame);
    m_detailsScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_detailsScrollArea->setWidget(m_detailsContent);

    auto* detailsContentLayout = new QVBoxLayout(m_detailsContent);
    detailsContentLayout->setContentsMargins(8, 8, 14, 8);
    detailsContentLayout->setSpacing(18);

    auto* topGrid = new QGridLayout();
    topGrid->setContentsMargins(0, 0, 0, 0);
    topGrid->setHorizontalSpacing(38);
    topGrid->setVerticalSpacing(0);
    topGrid->setColumnStretch(0, 1);
    topGrid->setColumnStretch(1, 1);
    topGrid->addWidget(createDetailBlock(QStringLiteral("Folder"), &m_folderValueLabel, m_detailsContent), 0, 0);
    topGrid->addWidget(createDetailBlock(QStringLiteral("Created"), &m_createdValueLabel, m_detailsContent), 0, 1);
    detailsContentLayout->addLayout(topGrid);

    detailsContentLayout->addWidget(createDetailLine(QStringLiteral("SummaryTopLine"), m_detailsContent));

    auto* bottomGrid = new QGridLayout();
    bottomGrid->setContentsMargins(0, 0, 0, 0);
    bottomGrid->setHorizontalSpacing(38);
    bottomGrid->setVerticalSpacing(0);
    bottomGrid->setColumnStretch(0, 1);
    bottomGrid->setColumnStretch(1, 1);
    bottomGrid->addWidget(createDetailBlock(QStringLiteral("Modified"), &m_modifiedValueLabel, m_detailsContent), 0, 0);
    bottomGrid->addWidget(createDetailBlock(QStringLiteral("Size"), &m_sizeValueLabel, m_detailsContent), 0, 1);
    detailsContentLayout->addLayout(bottomGrid);

    detailsContentLayout->addWidget(createDetailLine(QStringLiteral("SummaryResolvedLine"), m_detailsContent));

    m_resolvedTargetSection = createDetailBlock(QStringLiteral("Resolved target"), &m_resolvedTargetValueLabel, m_detailsContent);
    detailsContentLayout->addWidget(m_resolvedTargetSection);
    detailsContentLayout->addStretch(1);

    m_iconLabel->setFixedSize(72, 72);
    m_iconLabel->setScaledContents(true);
    m_titleLabel->setWordWrap(true);
    m_pathTitleLabel->hide();
    m_pathValueLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_pathValueLabel->setWordWrap(true);
    m_pathValueLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_pathRow->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_statusLabel->hide();

    m_openWithButton->setStatusCallback([this](const QString& message) {
        showStatusMessage(message);
    });
    m_openWithButton->setLaunchSuccessCallback([this]() {
        if (SpaceLookWindow* previewWindow = qobject_cast<SpaceLookWindow*>(window())) {
            previewWindow->hidePreview();
        }
    });

    connect(titleBlock->closeButton(), &QToolButton::clicked, this, [this]() {
        if (SpaceLookWindow* previewWindow = qobject_cast<SpaceLookWindow*>(window())) {
            previewWindow->hidePreview();
        }
    });

    connect(m_titleLabel, &SelectableTitleLabel::copyFeedbackRequested, this, [this](const QString& message) {
        showStatusMessage(message);
    });

    if (m_previewState) {
        connect(m_previewState, &PreviewState::changed, this, [this]() {
            if (!isVisible()) {
                return;
            }

            const HoveredItemInfo info = m_previewState->hoveredItem();
            if (info.typeKey == QStringLiteral("summary") ||
                info.typeKey == QStringLiteral("welcome") ||
                info.typeKey == QStringLiteral("folder") ||
                info.typeKey == QStringLiteral("shortcut") ||
                info.typeKey == QStringLiteral("shell_folder") ||
                info.typeKey == QStringLiteral("file") ||
                info.typeKey == QStringLiteral("shell_item") ||
                info.itemKind == QStringLiteral("Folder") ||
                info.itemKind == QStringLiteral("Shell Folder") ||
                info.itemKind == QStringLiteral("Shortcut")) {
                applyInfo(info);
            }
        });
    }

    applyChrome();
    setDetailValues(HoveredItemInfo());
}

QString SummaryRenderer::rendererId() const
{
    return QStringLiteral("summary");
}

bool SummaryRenderer::canHandle(const HoveredItemInfo& info) const
{
    Q_UNUSED(info);
    return true;
}

QWidget* SummaryRenderer::widget()
{
    return this;
}

void SummaryRenderer::load(const HoveredItemInfo& info)
{
    qDebug().noquote() << QStringLiteral("[SpaceLookRender] SummaryRenderer load typeKey=%1 title=\"%2\"")
        .arg(info.typeKey, info.title);
    applyInfo(info);
}

void SummaryRenderer::unload()
{
    m_currentInfo = HoveredItemInfo();
    m_pathValueLabel->clear();
    m_openWithButton->setTargetContext(QString(), QString());
    m_statusLabel->clear();
    m_statusLabel->hide();
    setDetailValues(HoveredItemInfo());
}

QWidget* SummaryRenderer::createDetailBlock(const QString& title, QLabel** valueLabel, QWidget* parent)
{
    auto* block = new QWidget(parent);
    block->setObjectName(QStringLiteral("SummaryDetailBlock"));

    auto* layout = new QVBoxLayout(block);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    auto* titleLabel = new QLabel(title, block);
    titleLabel->setObjectName(QStringLiteral("SummaryDetailTitle"));
    titleLabel->setTextFormat(Qt::PlainText);
    titleLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);

    auto* dataLabel = new QLabel(QStringLiteral("Unavailable"), block);
    dataLabel->setObjectName(QStringLiteral("SummaryDetailValue"));
    dataLabel->setTextFormat(Qt::PlainText);
    dataLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    dataLabel->setWordWrap(true);
    dataLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    dataLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    layout->addWidget(titleLabel);
    layout->addWidget(dataLabel);

    if (valueLabel) {
        *valueLabel = dataLabel;
    }

    return block;
}

QFrame* SummaryRenderer::createDetailLine(const QString& objectName, QWidget* parent)
{
    auto* line = new QFrame(parent);
    line->setObjectName(objectName);
    line->setFrameShape(QFrame::NoFrame);
    line->setFixedHeight(1);
    line->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    return line;
}

void SummaryRenderer::showStatusMessage(const QString& message)
{
    if (message.trimmed().isEmpty()) {
        m_statusLabel->clear();
        m_statusLabel->hide();
        return;
    }

    m_statusLabel->setText(message);
    m_statusLabel->show();
    QTimer::singleShot(1400, m_statusLabel, [label = m_statusLabel]() {
        if (label) {
            label->clear();
            label->hide();
        }
    });
}

void SummaryRenderer::setDetailValues(const HoveredItemInfo& info)
{
    const QString displayPath = info.filePath.trimmed().isEmpty()
        ? info.resolvedPath.trimmed()
        : info.filePath.trimmed();
    const QFileInfo fileInfo(displayPath);
    const bool hasRealPath = !displayPath.isEmpty() && fileInfo.exists();
    const QString folderValue = !info.folderPath.trimmed().isEmpty()
        ? info.folderPath.trimmed()
        : (hasRealPath ? fileInfo.absolutePath() : QString());
    const QString createdValue = hasRealPath
        ? displayDateTime(fileInfo.birthTime())
        : QStringLiteral("Unavailable");
    const QString modifiedValue = hasRealPath
        ? displayDateTime(fileInfo.lastModified())
        : QStringLiteral("Unavailable");
    const QString fileSizeValue = hasRealPath
        ? displayFileSize(fileInfo.size(), fileInfo.isDir())
        : QStringLiteral("Unavailable");
    const QString resolvedTarget = displayValue(info.resolvedPath);

    qDebug().noquote() << QStringLiteral("[SpaceLookRender] Summary details path=\"%1\" folder=\"%2\" created=\"%3\" modified=\"%4\" size=\"%5\" resolved=\"%6\"")
        .arg(displayPath,
             folderValue,
             createdValue,
             modifiedValue,
             fileSizeValue,
             resolvedTarget);

    if (m_folderValueLabel) {
        m_folderValueLabel->setText(displayValue(folderValue));
    }
    if (m_createdValueLabel) {
        m_createdValueLabel->setText(createdValue);
    }
    if (m_modifiedValueLabel) {
        m_modifiedValueLabel->setText(modifiedValue);
    }
    if (m_sizeValueLabel) {
        m_sizeValueLabel->setText(fileSizeValue);
    }
    if (m_resolvedTargetValueLabel) {
        m_resolvedTargetValueLabel->setText(resolvedTarget);
    }

    m_detailsContent->updateGeometry();
    m_detailsPanel->updateGeometry();
    m_detailsPanel->update();
}

void SummaryRenderer::applyInfo(const HoveredItemInfo& info)
{
    m_currentInfo = info;
    m_titleLabel->setText(info.title.isEmpty() ? QStringLiteral("Summary Preview") : info.title);
    m_titleLabel->setCopyText(m_titleLabel->text());

    const QIcon typeIcon(FileTypeIconResolver::iconForInfo(info));
    m_iconLabel->setPixmap(typeIcon.pixmap(128, 128));

    const QString displayPath = info.filePath.trimmed().isEmpty()
        ? info.resolvedPath.trimmed()
        : info.filePath.trimmed();
    m_pathValueLabel->setText(displayPath.isEmpty() ? QStringLiteral("(Unavailable)") : displayPath);
    m_openWithButton->setTargetContext(info.filePath, info.typeKey);

    if (shouldShowSummaryStatus(info)) {
        m_statusLabel->setText(info.statusMessage.isEmpty()
            ? QStringLiteral("No object information is available.")
            : info.statusMessage);
        m_statusLabel->show();
    } else {
        m_statusLabel->clear();
        m_statusLabel->hide();
    }

    setDetailValues(info);
}

void SummaryRenderer::applyChrome()
{
    setStyleSheet(
        "#SummaryRendererRoot {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:1,"
        "      stop:0 #fcfdff,"
        "      stop:1 #f4f8fc);"
        "  border-radius: 0px;"
        "}"
        "QLabel {"
        "  color: #18324a;"
        "}"
        "#SummaryTypeIcon {"
        "  background: transparent;"
        "  border: none;"
        "  padding: 0px;"
        "}"
        "#SummaryTitle {"
        "  color: #0f2740;"
        "}"
        "#SummaryMeta {"
        "  color: #5c738b;"
        "}"
        "#SummaryPathTitle {"
        "  color: #16324a;"
        "  font-family: 'Segoe UI Semibold';"
        "}"
        "#SummaryPathValue {"
        "  color: #445d76;"
        "}"
        "#SummaryOpenWithButton QToolButton {"
        "  background: rgba(238, 244, 252, 0.96);"
        "  color: #17324b;"
        "  border: 1px solid rgba(193, 208, 224, 0.95);"
        "  padding: 3px 8px;"
        "  min-height: 24px;"
        "}"
        "#SummaryOpenWithButton QToolButton:hover {"
        "  background: rgba(245, 249, 255, 1.0);"
        "}"
        "#SummaryOpenWithButton QToolButton:pressed {"
        "  background: rgba(224, 234, 246, 1.0);"
        "}"
        "#SummaryOpenWithButton #OpenWithPrimaryButton {"
        "  border-top-left-radius: 10px;"
        "  border-bottom-left-radius: 10px;"
        "  border-top-right-radius: 0px;"
        "  border-bottom-right-radius: 0px;"
        "  min-width: 28px;"
        "}"
        "#SummaryOpenWithButton #OpenWithExpandButton {"
        "  border-left: none;"
        "  border-top-left-radius: 0px;"
        "  border-bottom-left-radius: 0px;"
        "  border-top-right-radius: 10px;"
        "  border-bottom-right-radius: 10px;"
        "  min-width: 22px;"
        "  padding-left: 5px;"
        "  padding-right: 5px;"
        "}"
        "#SummaryStatus {"
        "  color: #215f4d;"
        "  background: rgba(207, 241, 229, 0.92);"
        "  border: 1px solid rgba(132, 196, 172, 0.9);"
        "  border-radius: 12px;"
        "  padding: 10px 14px;"
        "}"
        "#SummaryDetails {"
        "  background: #ffffff;"
        "  border: 1px solid #d9e1ea;"
        "  border-radius: 26px;"
        "}"
        "#SummaryDetailsScrollArea {"
        "  background: transparent;"
        "  border: none;"
        "}"
        "#SummaryDetailsContent {"
        "  background: transparent;"
        "}"
        "#SummaryDetailBlock {"
        "  background: transparent;"
        "}"
        "#SummaryDetailTitle {"
        "  color: #577199;"
        "  background: transparent;"
        "  font-family: 'Segoe UI';"
        "  font-size: 13px;"
        "  font-weight: 700;"
        "}"
        "#SummaryDetailValue {"
        "  color: #1e2c3b;"
        "  background: transparent;"
        "  font-family: 'Segoe UI';"
        "  font-size: 15px;"
        "  line-height: 1.35;"
        "  selection-background-color: #cfe3ff;"
        "  selection-color: #1e2c3b;"
        "}"
        "#SummaryTopLine, #SummaryResolvedLine {"
        "  background: #d9e1ea;"
        "  border: none;"
        "  min-height: 1px;"
        "  max-height: 1px;"
        "}"
        "#SummaryDetails QScrollBar:vertical {"
        "  background: rgba(232, 238, 245, 0.8);"
        "  border: none;"
        "  width: 8px;"
        "  margin: 10px 0 10px 0;"
        "  border-radius: 4px;"
        "}"
        "#SummaryDetails QScrollBar::handle:vertical {"
        "  background: #7c8fa8;"
        "  min-height: 52px;"
        "  border-radius: 4px;"
        "}"
        "#SummaryDetails QScrollBar::handle:vertical:hover {"
        "  background: #6d829d;"
        "}"
        "#SummaryDetails QScrollBar::handle:vertical:pressed {"
        "  background: #61768f;"
        "}"
        "#SummaryDetails QScrollBar::add-line:vertical, #SummaryDetails QScrollBar::sub-line:vertical {"
        "  height: 0px;"
        "}"
        "#SummaryDetails QScrollBar::add-page:vertical, #SummaryDetails QScrollBar::sub-page:vertical {"
        "  background: transparent;"
        "}"
    );

    QFont titleFont;
    titleFont.setFamily(QStringLiteral("Microsoft YaHei UI"));
    titleFont.setPixelSize(20);
    titleFont.setWeight(QFont::Bold);
    m_titleLabel->setFont(titleFont);
    m_titleLabel->setWordWrap(true);

    QFont metaFont;
    metaFont.setFamily(QStringLiteral("Segoe UI"));
    metaFont.setPixelSize(13);
    m_metaLabel->setFont(metaFont);
    m_pathTitleLabel->setFont(metaFont);
    m_pathValueLabel->setFont(metaFont);
    m_statusLabel->setFont(metaFont);
    m_metaLabel->setWordWrap(true);
    m_pathValueLabel->setWordWrap(true);
}
