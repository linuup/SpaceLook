#include "renderers/pdf/PdfRenderer.h"

#include <QDebug>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>

#include "renderers/pdf/PdfViewWidget.h"
#include "renderers/FileTypeIconResolver.h"
#include "renderers/OpenWithButton.h"
#include "renderers/PreviewHeaderBar.h"
#include "renderers/SelectableTitleLabel.h"
#include "widgets/SpaceLookWindow.h"

PdfRenderer::PdfRenderer(QWidget* parent)
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
    , m_pdfView(new PdfViewWidget(this))
{
    setAttribute(Qt::WA_StyledBackground, true);
    setObjectName(QStringLiteral("PdfRendererRoot"));
    m_headerRow->setObjectName(QStringLiteral("PdfHeaderRow"));
    m_iconLabel->setObjectName(QStringLiteral("PdfTypeIcon"));
    m_titleLabel->setObjectName(QStringLiteral("PdfTitle"));
    m_metaLabel->setObjectName(QStringLiteral("PdfMeta"));
    m_pathRow->setObjectName(QStringLiteral("PdfPathRow"));
    m_pathTitleLabel->setObjectName(QStringLiteral("PdfPathTitle"));
    m_pathValueLabel->setObjectName(QStringLiteral("PdfPathValue"));
    m_openWithButton->setObjectName(QStringLiteral("PdfOpenWithButton"));
    m_statusLabel->setObjectName(QStringLiteral("PdfStatus"));
    m_pdfView->setObjectName(QStringLiteral("PdfView"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 0, 12, 12);
    layout->setSpacing(14);
    layout->addWidget(m_headerRow);
    layout->addWidget(m_statusLabel);
    layout->addWidget(m_pdfView, 1);

    auto* headerLayout = new QHBoxLayout(m_headerRow);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(12);
    auto* titleBlock = new PreviewHeaderBar(m_iconLabel, m_titleLabel, m_pathRow, m_openWithButton, m_headerRow);
    headerLayout->addWidget(titleBlock, 1);

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
    m_statusLabel->hide();

    connect(m_titleLabel, &SelectableTitleLabel::copyFeedbackRequested, this, [this](const QString& message) {
        showStatusMessage(message);
    });

    connect(m_pdfView, &PdfViewWidget::zoomFactorChanged, this, [this](double factor) {
        qDebug().noquote() << QStringLiteral("[SpaceLookRender] PDF zoom factor=%1 path=\"%2\"")
            .arg(factor, 0, 'f', 2)
            .arg(m_info.filePath);
    });

    applyChrome();
}

QString PdfRenderer::rendererId() const
{
    return QStringLiteral("pdf");
}

bool PdfRenderer::canHandle(const HoveredItemInfo& info) const
{
    return info.typeKey == QStringLiteral("pdf");
}

QWidget* PdfRenderer::widget()
{
    return this;
}

void PdfRenderer::load(const HoveredItemInfo& info)
{
    m_info = info;
    qDebug().noquote() << QStringLiteral("[SpaceLookRender] PdfRenderer load path=\"%1\"").arg(info.filePath);

    m_titleLabel->setText(info.fileName.trimmed().isEmpty() ? QStringLiteral("PDF Preview") : info.fileName);
    m_titleLabel->setCopyText(info.fileName.trimmed().isEmpty() ? m_titleLabel->text() : info.fileName);
    const QIcon typeIcon(FileTypeIconResolver::iconForInfo(info));
    m_iconLabel->setPixmap(typeIcon.pixmap(128, 128));
    m_pathValueLabel->setText(info.filePath.trimmed().isEmpty() ? QStringLiteral("(Unavailable)") : info.filePath);
    m_openWithButton->setTargetContext(info.filePath, info.typeKey);

    showStatusMessage(QStringLiteral("Opening PDF document..."));
    QString errorMessage;
    if (!m_document.load(info.filePath, &errorMessage)) {
        showStatusMessage(errorMessage.isEmpty()
            ? QStringLiteral("Failed to open the PDF document.")
            : errorMessage);
        m_pdfView->clearDocument();
        return;
    }

    qDebug().noquote() << QStringLiteral("[SpaceLookRender] PdfRenderer opened pages=%1 path=\"%2\"")
        .arg(m_document.pageCount())
        .arg(info.filePath);

    showStatusMessage(QStringLiteral("Rendering first PDF page..."));
    m_pdfView->setDocument(&m_document);
    showStatusMessage(QString());
}

void PdfRenderer::unload()
{
    m_pathValueLabel->clear();
    m_openWithButton->setTargetContext(QString(), QString());
    showStatusMessage(QString());
    m_pdfView->clearDocument();
    m_document.unload();
    m_info = HoveredItemInfo();
}

void PdfRenderer::applyChrome()
{
    setStyleSheet(
        "#PdfRendererRoot {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:1,"
        "      stop:0 #fcfdff,"
        "      stop:1 #f4f8fc);"
        "  border-radius: 0px;"
        "}"
        "QLabel {"
        "  color: #18324a;"
        "}"
        "#PdfTypeIcon {"
        "  background: transparent;"
        "  border: none;"
        "  padding: 0px;"
        "}"
        "#PdfTitle {"
        "  color: #0f2740;"
        "}"
        "#PdfMeta {"
        "  color: #5c738b;"
        "}"
        "#PdfPathTitle {"
        "  color: #16324a;"
        "  font-family: 'Segoe UI Semibold';"
        "}"
        "#PdfPathValue {"
        "  color: #445d76;"
        "}"
        "#PdfOpenWithButton QToolButton {"
        "  background: rgba(238, 244, 252, 0.96);"
        "  color: #17324b;"
        "  border: 1px solid rgba(193, 208, 224, 0.95);"
        "  padding: 3px 8px;"
        "  min-height: 24px;"
        "}"
        "#PdfOpenWithButton QToolButton:hover {"
        "  background: rgba(245, 249, 255, 1.0);"
        "}"
        "#PdfOpenWithButton QToolButton:pressed {"
        "  background: rgba(224, 234, 246, 1.0);"
        "}"
        "#PdfOpenWithButton #OpenWithPrimaryButton {"
        "  border-top-left-radius: 10px;"
        "  border-bottom-left-radius: 10px;"
        "  border-top-right-radius: 0px;"
        "  border-bottom-right-radius: 0px;"
        "  min-width: 28px;"
        "}"
        "#PdfOpenWithButton #OpenWithExpandButton {"
        "  border-left: none;"
        "  border-top-left-radius: 0px;"
        "  border-bottom-left-radius: 0px;"
        "  border-top-right-radius: 10px;"
        "  border-bottom-right-radius: 10px;"
        "  min-width: 22px;"
        "  padding-left: 5px;"
        "  padding-right: 5px;"
        "}"
        "#PdfStatus {"
        "  color: #27568b;"
        "  background: rgba(220, 235, 255, 0.92);"
        "  border: 1px solid rgba(164, 193, 229, 0.95);"
        "  border-radius: 12px;"
        "  padding: 10px 14px;"
        "}"
        "#PdfView {"
        "  background: #eef3f8;"
        "  border: 1px solid #ccd6e2;"
        "  border-radius: 18px;"
        "}"
        "#PdfView QScrollBar:vertical {"
        "  background: rgba(232, 238, 245, 0.8);"
        "  border: none;"
        "  width: 8px;"
        "  margin: 10px 0 10px 0;"
        "  border-radius: 4px;"
        "}"
        "#PdfView QScrollBar::handle:vertical {"
        "  background: #7c8fa8;"
        "  min-height: 52px;"
        "  border-radius: 4px;"
        "}"
        "#PdfView QScrollBar::handle:vertical:hover {"
        "  background: #6d829d;"
        "}"
        "#PdfView QScrollBar::handle:vertical:pressed {"
        "  background: #61768f;"
        "}"
        "#PdfView QScrollBar:horizontal {"
        "  background: rgba(232, 238, 245, 0.8);"
        "  border: none;"
        "  height: 8px;"
        "  margin: 0 10px 0 10px;"
        "  border-radius: 4px;"
        "}"
        "#PdfView QScrollBar::handle:horizontal {"
        "  background: #7c8fa8;"
        "  min-width: 52px;"
        "  border-radius: 4px;"
        "}"
        "#PdfView QScrollBar::handle:horizontal:hover {"
        "  background: #6d829d;"
        "}"
        "#PdfView QScrollBar::handle:horizontal:pressed {"
        "  background: #61768f;"
        "}"
        "#PdfView QScrollBar::add-line:vertical, #PdfView QScrollBar::sub-line:vertical,"
        "#PdfView QScrollBar::add-line:horizontal, #PdfView QScrollBar::sub-line:horizontal {"
        "  width: 0px;"
        "  height: 0px;"
        "}"
        "#PdfView QScrollBar::add-page:vertical, #PdfView QScrollBar::sub-page:vertical,"
        "#PdfView QScrollBar::add-page:horizontal, #PdfView QScrollBar::sub-page:horizontal {"
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

void PdfRenderer::showStatusMessage(const QString& message)
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
