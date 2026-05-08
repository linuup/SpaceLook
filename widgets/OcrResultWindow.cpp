#include "widgets/OcrResultWindow.h"

#include <QApplication>
#include <QClipboard>
#include <QFrame>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTextDocument>
#include <QTimer>
#include <QVBoxLayout>

OcrResultImageView::OcrResultImageView(QWidget* parent)
    : QLabel(parent)
{
    setAlignment(Qt::AlignTop | Qt::AlignLeft);
    setBackgroundRole(QPalette::Base);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void OcrResultImageView::setImage(const QImage& image)
{
    m_image = image;
    setPixmap(QPixmap::fromImage(m_image));
    resize(pixmap() ? pixmap()->size() : QSize());
    update();
}

void OcrResultImageView::setBoxes(const QVector<OcrTextBox>& boxes)
{
    m_boxes = boxes;
    update();
}

void OcrResultImageView::mousePressEvent(QMouseEvent* event)
{
    if (!event || event->button() != Qt::LeftButton) {
        QLabel::mousePressEvent(event);
        return;
    }

    for (int index = m_boxes.size() - 1; index >= 0; --index) {
        if (mappedRect(m_boxes.at(index).imageRect).contains(event->pos())) {
            const QString text = m_boxes.at(index).text.trimmed();
            if (!text.isEmpty() && QApplication::clipboard()) {
                QApplication::clipboard()->setText(text);
                emit textCopied(text);
            }
            event->accept();
            return;
        }
    }

    QLabel::mousePressEvent(event);
}

void OcrResultImageView::paintEvent(QPaintEvent* event)
{
    QLabel::paintEvent(event);
    if (m_image.isNull() || m_boxes.isEmpty()) {
        return;
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(11, 127, 143, 220), 2.0));
    painter.setBrush(QColor(11, 127, 143, 42));
    for (const OcrTextBox& box : m_boxes) {
        const QRectF rect = mappedRect(box.imageRect);
        if (rect.isValid()) {
            painter.drawRoundedRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), 4.0, 4.0);
        }
    }
}

QRectF OcrResultImageView::mappedRect(const QRectF& imageRect) const
{
    if (m_image.isNull() || width() <= 0 || height() <= 0) {
        return QRectF();
    }

    const double scaleX = static_cast<double>(width()) / static_cast<double>(m_image.width());
    const double scaleY = static_cast<double>(height()) / static_cast<double>(m_image.height());
    return QRectF(imageRect.left() * scaleX,
                  imageRect.top() * scaleY,
                  imageRect.width() * scaleX,
                  imageRect.height() * scaleY);
}

OcrResultWindow::OcrResultWindow(const QImage& image, QWidget* parent)
    : QWidget(parent)
    , m_imageView(new OcrResultImageView(this))
    , m_textEdit(new QPlainTextEdit(this))
    , m_statusLabel(new QLabel(this))
    , m_textMetaLabel(new QLabel(this))
{
    setAttribute(Qt::WA_DeleteOnClose, true);
    setObjectName(QStringLiteral("OcrResultWindow"));
    setWindowTitle(QApplication::translate("SpaceLook", "OCR Result"));
    resize(1180, 760);

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(18, 18, 18, 16);
    rootLayout->setSpacing(12);

    m_statusLabel->setText(QApplication::translate("SpaceLook", "Recognizing image text..."));
    m_statusLabel->setObjectName(QStringLiteral("OcrStatusLabel"));
    rootLayout->addWidget(m_statusLabel);

    auto* contentLayout = new QHBoxLayout();
    contentLayout->setSpacing(14);
    rootLayout->addLayout(contentLayout, 1);

    auto* imagePanel = new QFrame(this);
    imagePanel->setObjectName(QStringLiteral("OcrPane"));
    auto* imagePanelLayout = new QVBoxLayout(imagePanel);
    imagePanelLayout->setContentsMargins(12, 12, 12, 12);
    imagePanelLayout->setSpacing(10);
    auto* imageTitleLabel = new QLabel(QApplication::translate("SpaceLook", "Captured Image"), imagePanel);
    imageTitleLabel->setObjectName(QStringLiteral("OcrPaneTitle"));
    imagePanelLayout->addWidget(imageTitleLabel);

    auto* imageScrollArea = new QScrollArea(this);
    imageScrollArea->setObjectName(QStringLiteral("OcrImageScrollArea"));
    imageScrollArea->setWidgetResizable(false);
    imageScrollArea->setWidget(m_imageView);
    imageScrollArea->setMinimumWidth(420);
    imagePanelLayout->addWidget(imageScrollArea, 1);
    contentLayout->addWidget(imagePanel, 1);

    auto* textPanel = new QFrame(this);
    textPanel->setObjectName(QStringLiteral("OcrPane"));
    auto* textPanelLayout = new QVBoxLayout(textPanel);
    textPanelLayout->setContentsMargins(12, 12, 12, 12);
    textPanelLayout->setSpacing(10);
    auto* textHeaderLayout = new QHBoxLayout();
    textHeaderLayout->setSpacing(8);
    auto* textTitleLabel = new QLabel(QApplication::translate("SpaceLook", "Recognized Text"), textPanel);
    textTitleLabel->setObjectName(QStringLiteral("OcrPaneTitle"));
    m_textMetaLabel->setObjectName(QStringLiteral("OcrMetaLabel"));
    textHeaderLayout->addWidget(textTitleLabel);
    textHeaderLayout->addStretch(1);
    textHeaderLayout->addWidget(m_textMetaLabel);
    textPanelLayout->addLayout(textHeaderLayout);

    m_textEdit->setObjectName(QStringLiteral("OcrTextEdit"));
    m_textEdit->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_textEdit->setPlaceholderText(QApplication::translate("SpaceLook", "Recognized text will appear here."));
    m_textEdit->document()->setDocumentMargin(12);
    m_textEdit->setTabStopDistance(32);
    QFont textFont(QStringLiteral("Segoe UI"));
    textFont.setPixelSize(14);
    m_textEdit->setFont(textFont);
    textPanelLayout->addWidget(m_textEdit, 1);
    contentLayout->addWidget(textPanel, 1);

    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch(1);
    auto* copyAllButton = new QPushButton(QApplication::translate("SpaceLook", "Copy All"), this);
    auto* closeButton = new QPushButton(QApplication::translate("SpaceLook", "Close"), this);
    buttonLayout->addWidget(copyAllButton);
    buttonLayout->addWidget(closeButton);
    rootLayout->addLayout(buttonLayout);

    connect(copyAllButton, &QPushButton::clicked, this, [this]() {
        const QString text = m_textEdit->toPlainText();
        if (text.trimmed().isEmpty()) {
            showFeedback(QApplication::translate("SpaceLook", "No recognized text to copy."));
            return;
        }
        if (QApplication::clipboard()) {
            QApplication::clipboard()->setText(text);
            showFeedback(QApplication::translate("SpaceLook", "All recognized text copied."));
        }
    });
    connect(closeButton, &QPushButton::clicked, this, &QWidget::close);
    connect(m_imageView, &OcrResultImageView::textCopied, this, [this](const QString&) {
        showFeedback(QApplication::translate("SpaceLook", "Selected text copied."));
    });

    m_imageView->setImage(image);
    setStyleSheet(
        "#OcrResultWindow {"
        "  background: #f4f8fb;"
        "}"
        "#OcrStatusLabel {"
        "  background: #ffffff;"
        "  color: #17324b;"
        "  border: 1px solid #d8e4ec;"
        "  border-radius: 10px;"
        "  padding: 9px 12px;"
        "  font-family: 'Segoe UI';"
        "  font-size: 13px;"
        "}"
        "#OcrPane {"
        "  background: #ffffff;"
        "  border: 1px solid #d8e4ec;"
        "  border-radius: 12px;"
        "}"
        "#OcrPaneTitle {"
        "  color: #14283b;"
        "  font-family: 'Segoe UI';"
        "  font-size: 14px;"
        "  font-weight: 600;"
        "}"
        "#OcrMetaLabel {"
        "  color: #667789;"
        "  font-family: 'Segoe UI';"
        "  font-size: 12px;"
        "}"
        "#OcrImageScrollArea {"
        "  background: #eef4f8;"
        "  border: 1px solid #e1e9ef;"
        "  border-radius: 8px;"
        "}"
        "#OcrTextEdit {"
        "  background: #fbfdff;"
        "  color: #14283b;"
        "  border: 1px solid #d8e4ec;"
        "  border-radius: 8px;"
        "  selection-background-color: #cceff3;"
        "  selection-color: #102536;"
        "}"
        "QPushButton {"
        "  background: #ffffff;"
        "  color: #17324b;"
        "  border: 1px solid #cbd9e3;"
        "  border-radius: 9px;"
        "  padding: 7px 14px;"
        "  font-family: 'Segoe UI';"
        "  font-size: 13px;"
        "}"
        "QPushButton:hover {"
        "  background: #eef6f8;"
        "  border-color: #8fcbd4;"
        "}"
        "QPushButton:pressed {"
        "  background: #dceef2;"
        "}"
    );
    setLoading();
}

void OcrResultWindow::setLoading()
{
    m_statusLabel->setText(QApplication::translate("SpaceLook", "Recognizing image text..."));
    m_textMetaLabel->setText(QApplication::translate("SpaceLook", "Waiting"));
}

void OcrResultWindow::setResult(const OcrResult& result)
{
    m_imageView->setBoxes(result.boxes);
    m_textEdit->setPlainText(result.text);
    const int lineCount = result.text.trimmed().isEmpty()
        ? 0
        : result.text.split(QChar::LineFeed, Qt::SkipEmptyParts).size();
    m_textMetaLabel->setText(QApplication::translate("SpaceLook", "%1 lines").arg(lineCount));
    if (result.text.trimmed().isEmpty()) {
        m_statusLabel->setText(QApplication::translate("SpaceLook", "No text was found in this image."));
        return;
    }

    m_statusLabel->setText(QApplication::translate("SpaceLook", "OCR complete. Click a marked text box to copy that line."));
}

void OcrResultWindow::setError(const QString& message)
{
    m_statusLabel->setText(message.trimmed().isEmpty()
        ? QApplication::translate("SpaceLook", "Image text recognition failed.")
        : message);
    m_textMetaLabel->setText(QApplication::translate("SpaceLook", "Failed"));
}

void OcrResultWindow::showFeedback(const QString& message)
{
    if (message.trimmed().isEmpty()) {
        return;
    }

    const QString previousText = m_statusLabel->text();
    m_statusLabel->setText(message);
    QTimer::singleShot(1600, this, [this, previousText, message]() {
        if (m_statusLabel && m_statusLabel->text() == message) {
            m_statusLabel->setText(previousText);
        }
    });
}
