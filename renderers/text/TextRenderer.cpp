#include "renderers/text/TextRenderer.h"

#include <QAbstractTextDocumentLayout>
#include <QFile>
#include <QDebug>
#include <QFutureWatcher>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPaintEvent>
#include <QPlainTextEdit>
#include <QResizeEvent>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTimer>
#include <QTextEdit>
#include <QTextBlock>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

#include "renderers/FileTypeIconResolver.h"
#include "renderers/FluentIconFont.h"
#include "renderers/OpenWithButton.h"
#include "renderers/PreviewHeaderBar.h"
#include "renderers/SelectableTitleLabel.h"
#include "widgets/SpaceLookWindow.h"

namespace {

constexpr qint64 kMaxPreviewBytes = 1024 * 1024 * 2;

struct TextLoadResult
{
    QString text;
    QString statusMessage;
    bool success = false;
};

class LineNumberTextEdit;

class LineNumberArea : public QWidget
{
public:
    explicit LineNumberArea(LineNumberTextEdit* editor)
        : QWidget(nullptr)
        , m_editor(editor)
    {
        setParent(reinterpret_cast<QWidget*>(editor));
    }

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    LineNumberTextEdit* m_editor = nullptr;
};

class LineNumberTextEdit : public QPlainTextEdit
{
public:
    explicit LineNumberTextEdit(QWidget* parent = nullptr)
        : QPlainTextEdit(parent)
        , m_lineNumberArea(new LineNumberArea(this))
    {
        connect(this, &QPlainTextEdit::blockCountChanged, this, &LineNumberTextEdit::updateLineNumberAreaWidth);
        connect(this, &QPlainTextEdit::updateRequest, this, &LineNumberTextEdit::updateLineNumberArea);
        connect(this, &QPlainTextEdit::cursorPositionChanged, this, &LineNumberTextEdit::highlightCurrentLine);
        connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this]() {
            m_lineNumberArea->update();
        });
        updateLineNumberAreaWidth(0);
        highlightCurrentLine();
    }

    int lineNumberAreaWidth() const
    {
        int digits = 1;
        int maxValue = qMax(1, blockCount());
        while (maxValue >= 10) {
            maxValue /= 10;
            ++digits;
        }

        const int spacing = 18;
        return spacing + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
    }

    void lineNumberAreaPaintEvent(QPaintEvent* event)
    {
        QPainter painter(m_lineNumberArea);
        painter.fillRect(event->rect(), QColor(QStringLiteral("#f4f8fc")));

        QTextBlock block = firstVisibleBlock();
        int blockNumber = block.blockNumber();
        int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
        int bottom = top + qRound(blockBoundingRect(block).height());

        while (block.isValid() && top <= event->rect().bottom()) {
            if (block.isVisible() && bottom >= event->rect().top()) {
                const QString number = QString::number(blockNumber + 1);
                const bool isCurrentBlock = textCursor().blockNumber() == blockNumber;
                painter.setPen(isCurrentBlock
                    ? QColor(QStringLiteral("#2e6fb1"))
                    : QColor(QStringLiteral("#7b8fa6")));
                painter.drawText(0,
                                 top,
                                 m_lineNumberArea->width() - 10,
                                 fontMetrics().height(),
                                 Qt::AlignRight | Qt::AlignVCenter,
                                 number);
            }

            block = block.next();
            top = bottom;
            bottom = top + qRound(blockBoundingRect(block).height());
            ++blockNumber;
        }

        painter.setPen(QPen(QColor(QStringLiteral("#d9e4ef")), 1));
        painter.drawLine(m_lineNumberArea->width() - 1, event->rect().top(),
                         m_lineNumberArea->width() - 1, event->rect().bottom());
    }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QPlainTextEdit::resizeEvent(event);
        const QRect editorRect = QPlainTextEdit::contentsRect();
        m_lineNumberArea->setGeometry(QRect(editorRect.left(),
                                            editorRect.top(),
                                            lineNumberAreaWidth(),
                                            editorRect.height()));
    }

private:
    void updateLineNumberAreaWidth(int)
    {
        setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
    }

    void updateLineNumberArea(const QRect& rect, int dy)
    {
        if (dy != 0) {
            m_lineNumberArea->scroll(0, dy);
        } else {
            m_lineNumberArea->update(0, rect.y(), m_lineNumberArea->width(), rect.height());
        }

        if (rect.contains(viewport()->rect())) {
            updateLineNumberAreaWidth(0);
        }
    }

    void highlightCurrentLine()
    {
        QList<QTextEdit::ExtraSelection> extraSelections;

        QTextEdit::ExtraSelection selection;
        selection.format.setBackground(QColor(QStringLiteral("#eef6ff")));
        selection.format.setProperty(QTextFormat::FullWidthSelection, true);
        selection.cursor = textCursor();
        selection.cursor.clearSelection();
        extraSelections.append(selection);

        setExtraSelections(extraSelections);
        m_lineNumberArea->update();
    }

    QWidget* m_lineNumberArea = nullptr;
};

QSize LineNumberArea::sizeHint() const
{
    return QSize(m_editor ? m_editor->lineNumberAreaWidth() : 0, 0);
}

void LineNumberArea::paintEvent(QPaintEvent* event)
{
    if (m_editor) {
        m_editor->lineNumberAreaPaintEvent(event);
    }
}

TextLoadResult loadTextPreviewContent(const QString& filePath)
{
    TextLoadResult result;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        result.text = QStringLiteral("Could not read:\n%1").arg(filePath);
        result.statusMessage = QStringLiteral("Failed to open the file for preview.");
        return result;
    }

    QByteArray content = file.read(kMaxPreviewBytes + 1);
    file.close();

    const bool truncated = content.size() > kMaxPreviewBytes;
    if (truncated) {
        content.chop(content.size() - static_cast<int>(kMaxPreviewBytes));
        result.statusMessage = QStringLiteral("Preview is truncated to the first 2 MB.");
    }

    result.text = QString::fromUtf8(content);
    result.success = true;
    return result;
}

}

TextRenderer::TextRenderer(QWidget* parent)
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
    , m_loadingCard(new QWidget(this))
    , m_loadingTitleLabel(new QLabel(this))
    , m_loadingMessageLabel(new QLabel(this))
    , m_textEdit(new LineNumberTextEdit(this))
{
    setAttribute(Qt::WA_StyledBackground, true);
    setObjectName(QStringLiteral("TextRendererRoot"));
    m_headerRow->setObjectName(QStringLiteral("TextHeaderRow"));
    m_iconLabel->setObjectName(QStringLiteral("TextTypeIcon"));
    m_titleLabel->setObjectName(QStringLiteral("TextTitle"));
    m_metaLabel->setObjectName(QStringLiteral("TextMeta"));
    m_pathRow->setObjectName(QStringLiteral("TextPathRow"));
    m_pathTitleLabel->setObjectName(QStringLiteral("TextPathTitle"));
    m_pathValueLabel->setObjectName(QStringLiteral("TextPathValue"));
    m_openWithButton->setObjectName(QStringLiteral("TextOpenWithButton"));
    m_statusLabel->setObjectName(QStringLiteral("TextStatus"));
    m_contentStack->setObjectName(QStringLiteral("TextContentStack"));
    m_loadingCard->setObjectName(QStringLiteral("TextLoadingCard"));
    m_loadingTitleLabel->setObjectName(QStringLiteral("TextLoadingTitle"));
    m_loadingMessageLabel->setObjectName(QStringLiteral("TextLoadingMessage"));
    m_textEdit->setObjectName(QStringLiteral("TextContent"));

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
    titleBlock->setOpenActionGlyph(FluentIconFont::glyph(0xE70F), QStringLiteral("Edit"));
    headerLayout->addWidget(titleBlock, 1);

    auto* pathLayout = new QHBoxLayout(m_pathRow);
    pathLayout->setContentsMargins(0, 0, 0, 0);
    pathLayout->setSpacing(8);
    pathLayout->addWidget(m_pathValueLabel, 1);

    m_textEdit->setReadOnly(true);
    m_textEdit->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_textEdit->setFrameShape(QFrame::NoFrame);
    auto* loadingLayout = new QVBoxLayout(m_loadingCard);
    loadingLayout->setContentsMargins(24, 24, 24, 24);
    loadingLayout->setSpacing(10);
    loadingLayout->addWidget(m_loadingTitleLabel);
    loadingLayout->addWidget(m_loadingMessageLabel);
    loadingLayout->addStretch(1);
    m_contentStack->addWidget(m_loadingCard);
    m_contentStack->addWidget(m_textEdit);
    m_iconLabel->setFixedSize(72, 72);
    m_iconLabel->setScaledContents(true);
    m_titleLabel->setWordWrap(true);
    m_pathTitleLabel->hide();
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
    m_loadingTitleLabel->setText(QStringLiteral("Preparing text preview"));
    m_loadingMessageLabel->setText(QStringLiteral("The preview shell is ready. Content is loading in the background."));
    m_loadingMessageLabel->setWordWrap(true);

    connect(m_titleLabel, &SelectableTitleLabel::copyFeedbackRequested, this, [this](const QString& message) {
        showStatusMessage(message);
    });

    applyChrome();
}

QString TextRenderer::rendererId() const
{
    return QStringLiteral("text");
}

bool TextRenderer::canHandle(const HoveredItemInfo& info) const
{
    return info.typeKey == QStringLiteral("text");
}

QWidget* TextRenderer::widget()
{
    return this;
}

void TextRenderer::load(const HoveredItemInfo& info)
{
    m_info = info;
    ++m_loadRequestId;
    const quint64 requestId = m_loadRequestId;
    qDebug().noquote() << QStringLiteral("[SpaceLookRender] TextRenderer load path=\"%1\"").arg(info.filePath);
    m_titleLabel->setText(info.title.isEmpty() ? QStringLiteral("Text Preview") : info.title);
    m_titleLabel->setCopyText(m_titleLabel->text());
    const QIcon typeIcon(FileTypeIconResolver::iconForInfo(info));
    m_iconLabel->setPixmap(typeIcon.pixmap(128, 128));
    m_pathValueLabel->setText(info.filePath.trimmed().isEmpty() ? QStringLiteral("(Unavailable)") : info.filePath);
    m_openWithButton->setTargetContext(info.filePath, info.typeKey);
    m_loadingTitleLabel->setText(QStringLiteral("Preparing text preview"));
    m_loadingMessageLabel->setText(QStringLiteral("The preview shell is ready. Content is loading in the background."));
    m_contentStack->setCurrentWidget(m_loadingCard);
    m_textEdit->clear();
    m_statusLabel->setText(QStringLiteral("Loading text preview..."));
    m_statusLabel->show();

    auto* watcher = new QFutureWatcher<TextLoadResult>(this);
    connect(watcher, &QFutureWatcher<TextLoadResult>::finished, this, [this, watcher, requestId, filePath = info.filePath]() {
        const TextLoadResult result = watcher->result();
        watcher->deleteLater();

        if (requestId != m_loadRequestId || m_info.filePath != filePath) {
            qDebug().noquote() << QStringLiteral("[SpaceLookRender] TextRenderer discarded stale async result path=\"%1\"")
                .arg(filePath);
            return;
        }

        if (!result.success) {
            qDebug().noquote() << QStringLiteral("[SpaceLookRender] TextRenderer failed to open: %1").arg(filePath);
            m_statusLabel->setText(result.statusMessage);
            m_statusLabel->show();
            m_textEdit->setPlainText(result.text);
            m_contentStack->setCurrentWidget(m_textEdit);
            return;
        }

        m_textEdit->setPlainText(result.text);
        m_contentStack->setCurrentWidget(m_textEdit);
        if (result.statusMessage.trimmed().isEmpty()) {
            m_statusLabel->clear();
            m_statusLabel->hide();
        } else {
            m_statusLabel->setText(result.statusMessage);
            m_statusLabel->show();
        }

        qDebug().noquote() << QStringLiteral("[SpaceLookRender] TextRenderer loaded async chars=%1 path=\"%2\"")
            .arg(result.text.size())
            .arg(filePath);
    });
    watcher->setFuture(QtConcurrent::run([filePath = info.filePath]() {
        return loadTextPreviewContent(filePath);
    }));
}

void TextRenderer::unload()
{
    ++m_loadRequestId;
    m_textEdit->clear();
    m_contentStack->setCurrentWidget(m_loadingCard);
    m_pathValueLabel->clear();
    m_openWithButton->setTargetContext(QString(), QString());
    m_statusLabel->clear();
    m_statusLabel->hide();
    m_info = HoveredItemInfo();
}

void TextRenderer::showStatusMessage(const QString& message)
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

void TextRenderer::applyChrome()
{
    setStyleSheet(
        "#TextRendererRoot {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:1,"
        "      stop:0 #fcfdff,"
        "      stop:1 #f4f8fc);"
        "  border-radius: 0px;"
        "}"
        "QLabel {"
        "  color: #18324a;"
        "}"
        "#TextTypeIcon {"
        "  background: transparent;"
        "  border: none;"
        "  padding: 0px;"
        "}"
        "#TextTitle {"
        "  color: #0f2740;"
        "}"
        "#TextMeta {"
        "  color: #5c738b;"
        "}"
        "#TextPathTitle {"
        "  color: #16324a;"
        "  font-family: 'Segoe UI Semibold';"
        "}"
        "#TextPathValue {"
        "  color: #445d76;"
        "}"
        "#TextOpenWithButton QToolButton {"
        "  background: rgba(238, 244, 252, 0.96);"
        "  color: #17324b;"
        "  border: 1px solid rgba(193, 208, 224, 0.95);"
        "  padding: 3px 8px;"
        "  min-height: 24px;"
        "}"
        "#TextOpenWithButton QToolButton:hover {"
        "  background: rgba(245, 249, 255, 1.0);"
        "}"
        "#TextOpenWithButton QToolButton:pressed {"
        "  background: rgba(224, 234, 246, 1.0);"
        "}"
        "#TextOpenWithButton #OpenWithPrimaryButton {"
        "  border-top-left-radius: 10px;"
        "  border-bottom-left-radius: 10px;"
        "  border-top-right-radius: 0px;"
        "  border-bottom-right-radius: 0px;"
        "  min-width: 28px;"
        "}"
        "#TextOpenWithButton #OpenWithExpandButton {"
        "  border-left: none;"
        "  border-top-left-radius: 0px;"
        "  border-bottom-left-radius: 0px;"
        "  border-top-right-radius: 10px;"
        "  border-bottom-right-radius: 10px;"
        "  min-width: 22px;"
        "  padding-left: 5px;"
        "  padding-right: 5px;"
        "}"
        "#TextStatus {"
        "  color: #27568b;"
        "  background: rgba(220, 235, 255, 0.92);"
        "  border: 1px solid rgba(164, 193, 229, 0.95);"
        "  border-radius: 12px;"
        "  padding: 10px 14px;"
        "}"
        "#TextContentStack {"
        "  background: transparent;"
        "}"
        "#TextLoadingCard {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:1,"
        "      stop:0 rgba(247, 250, 254, 0.98),"
        "      stop:1 rgba(239, 245, 251, 0.98));"
        "  border: 1px solid rgba(206, 218, 231, 0.96);"
        "  border-radius: 18px;"
        "}"
        "#TextLoadingTitle {"
        "  color: #12304d;"
        "}"
        "#TextLoadingMessage {"
        "  color: #58708a;"
        "}"
        "#TextContent {"
        "  background: #f4f7fb;"
        "  border: 1px solid #ccd6e2;"
        "  border-radius: 18px;"
        "  color: #18324a;"
        "  selection-background-color: #78b8ff;"
        "  selection-color: #08233b;"
        "}"
        "#TextContent QScrollBar:vertical {"
        "  background: rgba(232, 238, 245, 0.8);"
        "  border: none;"
        "  width: 8px;"
        "  margin: 10px 0 10px 0;"
        "  border-radius: 4px;"
        "}"
        "#TextContent QScrollBar::handle:vertical {"
        "  background: #7c8fa8;"
        "  min-height: 52px;"
        "  border-radius: 4px;"
        "}"
        "#TextContent QScrollBar::handle:vertical:hover {"
        "  background: #6d829d;"
        "}"
        "#TextContent QScrollBar::handle:vertical:pressed {"
        "  background: #61768f;"
        "}"
        "#TextContent QScrollBar:horizontal {"
        "  background: rgba(232, 238, 245, 0.8);"
        "  border: none;"
        "  height: 8px;"
        "  margin: 0 10px 0 10px;"
        "  border-radius: 4px;"
        "}"
        "#TextContent QScrollBar::handle:horizontal {"
        "  background: #7c8fa8;"
        "  min-width: 52px;"
        "  border-radius: 4px;"
        "}"
        "#TextContent QScrollBar::handle:horizontal:hover {"
        "  background: #6d829d;"
        "}"
        "#TextContent QScrollBar::handle:horizontal:pressed {"
        "  background: #61768f;"
        "}"
        "#TextContent QScrollBar::add-line:vertical, #TextContent QScrollBar::sub-line:vertical,"
        "#TextContent QScrollBar::add-line:horizontal, #TextContent QScrollBar::sub-line:horizontal {"
        "  width: 0px;"
        "  height: 0px;"
        "}"
        "#TextContent QScrollBar::add-page:vertical, #TextContent QScrollBar::sub-page:vertical,"
        "#TextContent QScrollBar::add-page:horizontal, #TextContent QScrollBar::sub-page:horizontal {"
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
    m_loadingMessageLabel->setFont(metaFont);

    QFont loadingTitleFont;
    loadingTitleFont.setFamily(QStringLiteral("Segoe UI Semibold"));
    loadingTitleFont.setPixelSize(18);
    m_loadingTitleLabel->setFont(loadingTitleFont);

    QFont textFont;
    textFont.setFamily(QStringLiteral("Cascadia Mono"));
    textFont.setPixelSize(14);
    m_textEdit->setFont(textFont);
}
