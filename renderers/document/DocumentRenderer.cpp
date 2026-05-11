#include "renderers/document/DocumentRenderer.h"

#include <utility>

#include <QDebug>
#include <QFont>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QStackedWidget>
#include <QTextBrowser>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "renderers/FileTypeIconResolver.h"
#include "renderers/OpenWithButton.h"
#include "renderers/PreviewHeaderBar.h"
#include "renderers/SelectableTitleLabel.h"
#include "renderers/document/PreviewHandlerHost.h"
#include "widgets/SpaceLookWindow.h"

namespace {

QString htmlPage(const QString& body)
{
    return QStringLiteral(
        "<!DOCTYPE html>"
        "<html>"
        "<head>"
        "<meta charset=\"utf-8\">"
        "<style>"
        "body {"
        "  margin: 0;"
        "  padding: 28px;"
        "  background: linear-gradient(135deg, #f8fbff 0%%, #eef5fb 100%%);"
        "  color: #16324a;"
        "  font-family: 'Segoe UI', sans-serif;"
        "}"
        ".sheet {"
        "  max-width: 1100px;"
        "  margin: 0 auto;"
        "}"
        ".card {"
        "  background: rgba(255, 255, 255, 0.96);"
        "  border: 1px solid #dce6f0;"
        "  border-radius: 18px;"
        "  box-shadow: 0 10px 30px rgba(17, 39, 63, 0.06);"
        "  padding: 22px 24px;"
        "  margin-bottom: 18px;"
        "}"
        "h2 {"
        "  margin: 0 0 14px 0;"
        "  color: #0f2740;"
        "} "
        "p {"
        "  margin: 0 0 12px 0;"
        "  line-height: 1.65;"
        "  white-space: pre-wrap;"
        "} "
        "</style>"
        "</head>"
        "<body><div class=\"sheet\">%1</div></body>"
        "</html>").arg(body);
}

QString fileTitleForPreview(const HoveredItemInfo& info)
{
    if (!info.fileName.trimmed().isEmpty()) {
        return info.fileName;
    }
    if (!info.title.trimmed().isEmpty()) {
        return info.title;
    }
    return QStringLiteral("Document Preview");
}

QString previewUnavailableHtml(const QString& message)
{
    return htmlPage(QStringLiteral(
        "<div class=\"card\"><h2>Preview Unavailable</h2>"
        "<p>%1</p></div>").arg(message.toHtmlEscaped()));
}

}

DocumentRenderer::DocumentRenderer(QWidget* parent)
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
    , m_contentStack(new QStackedWidget(this))
    , m_previewHandlerHost(new PreviewHandlerHost(this))
    , m_textBrowser(new QTextBrowser(this))
{
    setAttribute(Qt::WA_StyledBackground, true);
    setObjectName(QStringLiteral("DocumentRendererRoot"));
    m_headerRow->setObjectName(QStringLiteral("DocumentHeaderRow"));
    m_iconLabel->setObjectName(QStringLiteral("DocumentTypeIcon"));
    m_titleLabel->setObjectName(QStringLiteral("DocumentTitle"));
    m_metaLabel->setObjectName(QStringLiteral("DocumentMeta"));
    m_pathRow->setObjectName(QStringLiteral("DocumentPathRow"));
    m_pathTitleLabel->setObjectName(QStringLiteral("DocumentPathTitle"));
    m_pathValueLabel->setObjectName(QStringLiteral("DocumentPathValue"));
    m_openWithButton->setObjectName(QStringLiteral("DocumentOpenWithButton"));
    m_statusLabel->setObjectName(QStringLiteral("DocumentStatus"));
    m_contentStack->setObjectName(QStringLiteral("DocumentContentStack"));
    m_textBrowser->setObjectName(QStringLiteral("DocumentBrowser"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 0, 12, 12);
    layout->setSpacing(14);
    layout->addWidget(m_headerRow);
    layout->addWidget(m_statusLabel);
    layout->addWidget(m_contentStack, 1);

    auto* headerLayout = new QHBoxLayout(m_headerRow);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(12);
    auto* titleBlock = new PreviewHeaderBar(m_iconLabel, m_titleLabel, m_pathRow, m_openWithButton, m_headerRow);
    headerLayout->addWidget(titleBlock->contentWidget(), 1);

    auto* pathLayout = new QHBoxLayout(m_pathRow);
    pathLayout->setContentsMargins(0, 0, 0, 0);
    pathLayout->setSpacing(8);
    pathLayout->addWidget(m_pathValueLabel, 1);

    m_pathTitleLabel->hide();
    m_iconLabel->setFixedSize(72, 72);
    m_iconLabel->setScaledContents(true);
    m_titleLabel->setWordWrap(true);
    m_pathValueLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_pathValueLabel->setWordWrap(true);
    m_pathValueLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_pathRow->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
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
    m_textBrowser->setOpenExternalLinks(false);
    m_statusLabel->hide();
    m_contentStack->addWidget(m_previewHandlerHost);
    m_contentStack->addWidget(m_textBrowser);
    m_contentStack->setCurrentWidget(m_textBrowser);

    connect(m_titleLabel, &SelectableTitleLabel::copyFeedbackRequested, this, [this](const QString& message) {
        showStatusMessage(message);
    });

    applyChrome();
}

QString DocumentRenderer::rendererId() const
{
    return QStringLiteral("document");
}

bool DocumentRenderer::canHandle(const HoveredItemInfo& info) const
{
    return info.typeKey == QStringLiteral("office");
}

QWidget* DocumentRenderer::widget()
{
    return this;
}

void DocumentRenderer::warmUp()
{
    m_previewHandlerHost->warmUp();
}

bool DocumentRenderer::reportsLoadingState() const
{
    return true;
}

void DocumentRenderer::setLoadingStateCallback(std::function<void(bool)> callback)
{
    m_loadingStateCallback = std::move(callback);
}

void DocumentRenderer::setSummaryFallbackCallback(std::function<void(const HoveredItemInfo&, const QString&)> callback)
{
    m_summaryFallbackCallback = std::move(callback);
}

void DocumentRenderer::load(const HoveredItemInfo& info)
{
    m_info = info;
    m_loadGuard.begin(info.filePath);
    notifyLoadingState(true);
    qDebug().noquote() << QStringLiteral("[SpaceLookRender] DocumentRenderer load path=\"%1\" typeKey=%2")
        .arg(info.filePath, info.typeKey);

    m_titleLabel->setText(fileTitleForPreview(info));
    m_titleLabel->setCopyText(m_titleLabel->text());
    const QIcon typeIcon(FileTypeIconResolver::iconForInfo(info));
    m_iconLabel->setPixmap(typeIcon.pixmap(128, 128));
    m_pathValueLabel->setText(info.filePath.trimmed().isEmpty() ? QStringLiteral("(Unavailable)") : info.filePath);
    m_openWithButton->setTargetContext(info.filePath, info.typeKey);
    m_textBrowser->clear();
    m_previewHandlerHost->unload();

    m_contentStack->setCurrentWidget(m_previewHandlerHost);
    const PreviewHandlerOpenResult openResult = m_previewHandlerHost->openFileDetailed(info.filePath);
    if (openResult.success) {
        m_statusLabel->clear();
        m_statusLabel->hide();
        notifyLoadingState(false);
        return;
    }

    qDebug().noquote() << QStringLiteral("[SpaceLookRender] Windows Preview Handler unavailable path=\"%1\" reason=\"%2\" processArch=%3 registeredArch=%4 clsid=%5 mismatch=%6")
        .arg(info.filePath,
             openResult.message,
             openResult.processArchitecture,
             openResult.registeredArchitecture,
             openResult.handlerGuid,
             openResult.architectureMismatch ? QStringLiteral("true") : QStringLiteral("false"));

    const QString message = openResult.message.trimmed().isEmpty()
        ? QStringLiteral("No Windows Preview Handler is available for this Office document.")
        : openResult.message.trimmed();

    if (m_summaryFallbackCallback) {
        notifyLoadingState(false);
        m_summaryFallbackCallback(info, message);
        return;
    }

    m_contentStack->setCurrentWidget(m_textBrowser);
    m_statusLabel->setText(message);
    m_statusLabel->show();
    m_textBrowser->setHtml(previewUnavailableHtml(message));
    notifyLoadingState(false);
}

void DocumentRenderer::unload()
{
    m_loadGuard.cancel();
    notifyLoadingState(false);
    m_previewHandlerHost->unload();
    m_pathValueLabel->clear();
    m_openWithButton->setTargetContext(QString(), QString());
    showStatusMessage(QString());
    m_info = HoveredItemInfo();
    m_textBrowser->clear();
}

void DocumentRenderer::notifyLoadingState(bool loading)
{
    if (m_loadingStateCallback) {
        m_loadingStateCallback(loading);
    }
}

void DocumentRenderer::applyChrome()
{
    setStyleSheet(
        "#DocumentRendererRoot {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:1,"
        "      stop:0 #fcfdff,"
        "      stop:1 #f4f8fc);"
        "  border-radius: 0px;"
        "}"
        "QLabel {"
        "  color: #18324a;"
        "}"
        "#DocumentTypeIcon {"
        "  background: transparent;"
        "  border: none;"
        "  padding: 0px;"
        "}"
        "#DocumentTitle {"
        "  color: #0f2740;"
        "}"
        "#DocumentMeta {"
        "  color: #5c738b;"
        "}"
        "#DocumentPathTitle {"
        "  color: #16324a;"
        "  font-family: 'Segoe UI Semibold';"
        "}"
        "#DocumentPathValue {"
        "  color: #445d76;"
        "}"
        "#DocumentOpenWithButton QToolButton {"
        "  background: rgba(238, 244, 252, 0.96);"
        "  color: #17324b;"
        "  border: 1px solid rgba(193, 208, 224, 0.95);"
        "  padding: 3px 8px;"
        "  min-height: 24px;"
        "}"
        "#DocumentOpenWithButton QToolButton:hover {"
        "  background: rgba(245, 249, 255, 1.0);"
        "}"
        "#DocumentOpenWithButton QToolButton:pressed {"
        "  background: rgba(224, 234, 246, 1.0);"
        "}"
        "#DocumentOpenWithButton #OpenWithPrimaryButton {"
        "  border-top-left-radius: 10px;"
        "  border-bottom-left-radius: 10px;"
        "  border-top-right-radius: 0px;"
        "  border-bottom-right-radius: 0px;"
        "  min-width: 28px;"
        "}"
        "#DocumentOpenWithButton #OpenWithExpandButton {"
        "  border-left: none;"
        "  border-top-left-radius: 0px;"
        "  border-bottom-left-radius: 0px;"
        "  border-top-right-radius: 10px;"
        "  border-bottom-right-radius: 10px;"
        "  min-width: 22px;"
        "  padding-left: 5px;"
        "  padding-right: 5px;"
        "}"
        "#DocumentStatus {"
        "  color: #27568b;"
        "  background: rgba(220, 235, 255, 0.92);"
        "  border: 1px solid rgba(164, 193, 229, 0.95);"
        "  border-radius: 12px;"
        "  padding: 10px 14px;"
        "}"
        "#DocumentContentStack {"
        "  background: transparent;"
        "  border: none;"
        "}"
        "#DocumentBrowser {"
        "  background: #f4f7fb;"
        "  border: 1px solid #ccd6e2;"
        "  border-radius: 18px;"
        "  padding: 0px;"
        "}"
        "#DocumentBrowser QScrollBar:vertical {"
        "  background: rgba(232, 238, 245, 0.8);"
        "  border: none;"
        "  width: 8px;"
        "  margin: 10px 0 10px 0;"
        "  border-radius: 4px;"
        "}"
        "#DocumentBrowser QScrollBar::handle:vertical {"
        "  background: #7c8fa8;"
        "  min-height: 52px;"
        "  border-radius: 4px;"
        "}"
        "#DocumentBrowser QScrollBar::handle:vertical:hover {"
        "  background: #6d829d;"
        "}"
        "#DocumentBrowser QScrollBar::handle:vertical:pressed {"
        "  background: #61768f;"
        "}"
        "#DocumentBrowser QScrollBar:horizontal {"
        "  background: rgba(232, 238, 245, 0.8);"
        "  border: none;"
        "  height: 8px;"
        "  margin: 0 10px 0 10px;"
        "  border-radius: 4px;"
        "}"
        "#DocumentBrowser QScrollBar::handle:horizontal {"
        "  background: #7c8fa8;"
        "  min-width: 52px;"
        "  border-radius: 4px;"
        "}"
        "#DocumentBrowser QScrollBar::handle:horizontal:hover {"
        "  background: #6d829d;"
        "}"
        "#DocumentBrowser QScrollBar::handle:horizontal:pressed {"
        "  background: #61768f;"
        "}"
        "#DocumentBrowser QScrollBar::add-line:vertical, #DocumentBrowser QScrollBar::sub-line:vertical,"
        "#DocumentBrowser QScrollBar::add-line:horizontal, #DocumentBrowser QScrollBar::sub-line:horizontal {"
        "  width: 0px;"
        "  height: 0px;"
        "}"
        "#DocumentBrowser QScrollBar::add-page:vertical, #DocumentBrowser QScrollBar::sub-page:vertical,"
        "#DocumentBrowser QScrollBar::add-page:horizontal, #DocumentBrowser QScrollBar::sub-page:horizontal {"
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

void DocumentRenderer::showStatusMessage(const QString& message)
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
