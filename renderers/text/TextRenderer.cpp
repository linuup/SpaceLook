#include "renderers/text/TextRenderer.h"

#include <QAbstractTextDocumentLayout>
#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPaintEvent>
#include <QPlainTextEdit>
#include <QResizeEvent>
#include <QScrollBar>
#include <QShortcut>
#include <QStackedWidget>
#include <QTimer>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTextBlock>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtXml/QDomDocument>
#include <QtConcurrent/QtConcurrent>

#include "core/PreviewFileReader.h"
#include "core/TextEncodingDetector.h"
#include "renderers/FileTypeIconResolver.h"
#include "renderers/FluentIconFont.h"
#include "renderers/ModeSwitchButton.h"
#include "renderers/OpenWithButton.h"
#include "renderers/PreviewHeaderBar.h"
#include "renderers/PreviewStateVisuals.h"
#include "renderers/SelectableTitleLabel.h"
#include "core/preview_state.h"
#include "widgets/SpaceLookWindow.h"

namespace {

constexpr qint64 kMaxPreviewBytes = 1024 * 1024 * 2;

struct TextLoadResult
{
    QString text;
    QString encodingName;
    QString statusMessage;
    bool success = false;
};

bool isStructuredOverrideSuffix(const QString& filePath)
{
    const QString suffix = QFileInfo(filePath).suffix().trimmed().toLower();
    return suffix == QStringLiteral("json")
        || suffix == QStringLiteral("jsonc")
        || suffix == QStringLiteral("xml")
        || suffix == QStringLiteral("yaml")
        || suffix == QStringLiteral("yml");
}

QString formattedJsonPreviewText(const QString& text)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || document.isNull()) {
        return text;
    }

    return QString::fromUtf8(document.toJson(QJsonDocument::Indented));
}

QString formattedXmlPreviewText(const QString& text)
{
    QDomDocument document;
    QString errorMessage;
    int errorLine = 0;
    int errorColumn = 0;
    if (!document.setContent(text, &errorMessage, &errorLine, &errorColumn)) {
        Q_UNUSED(errorMessage);
        Q_UNUSED(errorLine);
        Q_UNUSED(errorColumn);
        return text;
    }

    return document.toString(2);
}

QString formattedStructuredPreviewText(const QString& filePath, const QString& text)
{
    const QString suffix = QFileInfo(filePath).suffix().trimmed().toLower();
    if (suffix == QStringLiteral("json")) {
        return formattedJsonPreviewText(text);
    }
    if (suffix == QStringLiteral("xml")) {
        return formattedXmlPreviewText(text);
    }
    return text;
}

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

TextLoadResult loadTextPreviewContent(const QString& filePath, const PreviewCancellationToken& cancelToken)
{
    TextLoadResult result;
    if (previewCancellationRequested(cancelToken)) {
        result.statusMessage = QCoreApplication::translate("SpaceLook", "Text preview was canceled.");
        return result;
    }

    QByteArray content;
    bool truncated = false;
    if (!PreviewFileReader::readPrefix(filePath, kMaxPreviewBytes, &content, &truncated)) {
        result.text = QCoreApplication::translate("SpaceLook", "Could not read:\n%1").arg(filePath);
        result.statusMessage = QCoreApplication::translate("SpaceLook", "Failed to open the file for preview.");
        return result;
    }

    if (previewCancellationRequested(cancelToken)) {
        content.clear();
        result.statusMessage = QCoreApplication::translate("SpaceLook", "Text preview was canceled.");
        return result;
    }

    if (truncated) {
        result.statusMessage = QCoreApplication::translate("SpaceLook", "Preview is truncated to the first 2 MB.");
    }

    const DetectedTextEncoding decoded = TextEncodingDetector::decode(content);
    result.text = decoded.text;
    result.encodingName = decoded.name;
    result.success = true;
    return result;
}

}

TextRenderer::TextRenderer(PreviewState* previewState, QWidget* parent)
    : QWidget(parent)
    , m_headerRow(new QWidget(this))
    , m_iconLabel(new QLabel(this))
    , m_titleLabel(new SelectableTitleLabel(this))
    , m_metaLabel(new QLabel(this))
    , m_pathRow(new QWidget(this))
    , m_pathTitleLabel(new QLabel(this))
    , m_pathValueLabel(new QLabel(this))
    , m_openWithButton(new OpenWithButton(this))
    , m_contentSection(new QWidget(this))
    , m_statusRow(new QWidget(this))
    , m_searchRow(new QWidget(this))
    , m_searchEdit(new QLineEdit(this))
    , m_searchPreviousButton(new QToolButton(this))
    , m_searchNextButton(new QToolButton(this))
    , m_searchCountLabel(new QLabel(this))
    , m_statusLabel(new QLabel(this))
    , m_modeSwitchButton(new ModeSwitchButton(this))
    , m_contentStack(new QStackedWidget(this))
    , m_loadingCard(new QWidget(this))
    , m_loadingTitleLabel(new QLabel(this))
    , m_loadingMessageLabel(new QLabel(this))
    , m_textEdit(new LineNumberTextEdit(this))
    , m_previewState(previewState)
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
    m_contentSection->setObjectName(QStringLiteral("TextContentSection"));
    m_statusRow->setObjectName(QStringLiteral("TextStatusRow"));
    m_searchRow->setObjectName(QStringLiteral("TextSearchRow"));
    m_searchEdit->setObjectName(QStringLiteral("TextSearchEdit"));
    m_searchPreviousButton->setObjectName(QStringLiteral("TextSearchPreviousButton"));
    m_searchNextButton->setObjectName(QStringLiteral("TextSearchNextButton"));
    m_searchCountLabel->setObjectName(QStringLiteral("TextSearchCount"));
    m_statusLabel->setObjectName(QStringLiteral("TextStatus"));
    m_modeSwitchButton->setObjectName(QStringLiteral("TextModeSwitchButton"));
    m_contentStack->setObjectName(QStringLiteral("TextContentStack"));
    m_loadingCard->setObjectName(QStringLiteral("TextLoadingCard"));
    m_loadingTitleLabel->setObjectName(QStringLiteral("TextLoadingTitle"));
    m_loadingMessageLabel->setObjectName(QStringLiteral("TextLoadingMessage"));
    m_textEdit->setObjectName(QStringLiteral("TextContent"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 0, 12, 12);
    layout->setSpacing(14);
    layout->addWidget(m_headerRow);
    layout->addWidget(m_contentSection, 1);

    auto* headerLayout = new QHBoxLayout(m_headerRow);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(12);
    auto* trailingActions = new QWidget(m_headerRow);
    auto* trailingActionsLayout = new QHBoxLayout(trailingActions);
    trailingActionsLayout->setContentsMargins(0, 0, 0, 0);
    trailingActionsLayout->setSpacing(12);
    trailingActionsLayout->addWidget(m_openWithButton, 0, Qt::AlignVCenter);

    auto* titleBlock = new PreviewHeaderBar(m_iconLabel, m_titleLabel, m_pathRow, trailingActions, m_headerRow);
    titleBlock->setOpenActionGlyph(FluentIconFont::glyph(0xE70F), QCoreApplication::translate("SpaceLook", "Edit"));
    headerLayout->addWidget(titleBlock->contentWidget(), 1);

    auto* pathLayout = new QHBoxLayout(m_pathRow);
    pathLayout->setContentsMargins(0, 0, 0, 0);
    pathLayout->setSpacing(8);
    pathLayout->addWidget(m_pathValueLabel, 1);

    auto* contentSectionLayout = new QVBoxLayout(m_contentSection);
    contentSectionLayout->setContentsMargins(0, 0, 0, 0);
    contentSectionLayout->setSpacing(12);
    contentSectionLayout->addWidget(m_searchRow, 0);
    contentSectionLayout->addWidget(m_contentStack, 1);
    contentSectionLayout->addWidget(m_statusRow, 0);

    auto* statusLayout = new QHBoxLayout(m_statusRow);
    statusLayout->setContentsMargins(0, 0, 0, 0);
    statusLayout->setSpacing(12);
    statusLayout->addWidget(m_statusLabel, 1);
    statusLayout->addWidget(m_modeSwitchButton, 0, Qt::AlignRight | Qt::AlignVCenter);

    auto* searchLayout = new QHBoxLayout(m_searchRow);
    searchLayout->setContentsMargins(8, 4, 8, 4);
    searchLayout->setSpacing(6);
    m_searchEdit->setPlaceholderText(QCoreApplication::translate("SpaceLook", "Search"));
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setFixedWidth(160);
    m_searchPreviousButton->setText(QStringLiteral("<"));
    m_searchNextButton->setText(QStringLiteral(">"));
    m_searchPreviousButton->setFixedSize(24, 24);
    m_searchNextButton->setFixedSize(24, 24);
    m_searchCountLabel->setMinimumWidth(46);
    searchLayout->addWidget(m_searchEdit);
    searchLayout->addWidget(m_searchPreviousButton);
    searchLayout->addWidget(m_searchNextButton);
    searchLayout->addWidget(m_searchCountLabel);
    searchLayout->addStretch(1);
    m_searchRow->hide();

    m_textEdit->setReadOnly(true);
    m_textEdit->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_textEdit->setFrameShape(QFrame::NoFrame);
    m_modeSwitchButton->hide();
    QFont modeFont;
    modeFont.setFamily(QStringLiteral("Segoe UI Rounded"));
    modeFont.setPixelSize(12);
    modeFont.setWeight(QFont::DemiBold);
    m_modeSwitchButton->setMenuFont(modeFont);
    auto* loadingLayout = new QVBoxLayout(m_loadingCard);
    loadingLayout->setContentsMargins(24, 24, 24, 24);
    loadingLayout->setSpacing(10);
    loadingLayout->addWidget(m_loadingTitleLabel);
    loadingLayout->addWidget(m_loadingMessageLabel);
    loadingLayout->addStretch(1);
    m_contentStack->addWidget(m_loadingCard);
    m_contentStack->addWidget(m_textEdit);
    m_iconLabel->setFixedSize(72, 72);
    m_iconLabel->setScaledContents(false);
    m_iconLabel->setAlignment(Qt::AlignCenter);
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
    PreviewStateVisuals::prepareStatusLabel(m_statusLabel);
    m_statusLabel->hide();
    m_loadingTitleLabel->setText(QCoreApplication::translate("SpaceLook", "Preparing text preview"));
    m_loadingMessageLabel->setText(QCoreApplication::translate("SpaceLook", "The preview shell is ready. Content is loading in the background."));
    m_loadingMessageLabel->setWordWrap(true);
    PreviewStateVisuals::prepareStateCard(
        m_loadingCard,
        m_loadingTitleLabel,
        m_loadingMessageLabel,
        nullptr,
        PreviewStateVisuals::Kind::Loading);

    connect(m_titleLabel, &SelectableTitleLabel::copyFeedbackRequested, this, [this](const QString& message) {
        showStatusMessage(message);
    });
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this]() {
        findSearchMatch(false);
    });
    connect(m_searchEdit, &QLineEdit::returnPressed, this, [this]() {
        findSearchMatch(false);
    });
    connect(m_searchPreviousButton, &QToolButton::clicked, this, [this]() {
        findSearchMatch(true);
    });
    connect(m_searchNextButton, &QToolButton::clicked, this, [this]() {
        findSearchMatch(false);
    });
    auto* findShortcut = new QShortcut(QKeySequence::Find, this);
    findShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(findShortcut, &QShortcut::activated, this, &TextRenderer::showSearchRow);
    auto* hideSearchShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), m_searchRow);
    hideSearchShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(hideSearchShortcut, &QShortcut::activated, this, [this]() {
        hideSearchRow(false);
    });
    m_modeSwitchButton->setModeChangedCallback([this](const QString& rendererId) {
        if (!m_previewState) {
            return;
        }

        const QString filePath = m_info.filePath.trimmed();
        if (!isStructuredOverrideSuffix(filePath)) {
            return;
        }
        m_previewState->setRendererOverride(rendererId);
        if (SpaceLookWindow* previewWindow = qobject_cast<SpaceLookWindow*>(window())) {
            previewWindow->refreshCurrentPreview();
        }
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

bool TextRenderer::reportsLoadingState() const
{
    return true;
}

void TextRenderer::setLoadingStateCallback(std::function<void(bool)> callback)
{
    m_loadingStateCallback = std::move(callback);
}

void TextRenderer::load(const HoveredItemInfo& info)
{
    cancelPreviewTask(m_cancelToken);
    const PreviewCancellationToken cancelToken = makePreviewCancellationToken();
    m_cancelToken = cancelToken;
    m_info = info;
    const PreviewLoadGuard::Token loadToken = m_loadGuard.begin(info.filePath);
    notifyLoadingState(true);
    qDebug().noquote() << QStringLiteral("[SpaceLookRender] TextRenderer load path=\"%1\"").arg(info.filePath);
    m_titleLabel->setText(info.title.isEmpty() ? QCoreApplication::translate("SpaceLook", "Text Preview") : info.title);
    m_titleLabel->setCopyText(m_titleLabel->text());
    m_iconLabel->setPixmap(FileTypeIconResolver::pixmapForInfo(info, m_iconLabel->contentsRect().size()));
    m_pathValueLabel->setText(info.filePath.trimmed().isEmpty() ? QCoreApplication::translate("SpaceLook", "(Unavailable)") : info.filePath);
    m_openWithButton->setTargetContext(info.filePath, info.typeKey);
    updateModeSelector(info.filePath);
    hideSearchRow(true);
    m_loadingTitleLabel->setText(QCoreApplication::translate("SpaceLook", "Preparing text preview"));
    m_loadingMessageLabel->setText(QCoreApplication::translate("SpaceLook", "The preview shell is ready. Content is loading in the background."));
    PreviewStateVisuals::prepareStateCard(
        m_loadingCard,
        m_loadingTitleLabel,
        m_loadingMessageLabel,
        nullptr,
        PreviewStateVisuals::Kind::Loading);
    m_contentStack->setCurrentWidget(m_loadingCard);
    m_textEdit->clear();
    PreviewStateVisuals::showStatus(m_statusLabel, QCoreApplication::translate("SpaceLook", "Loading text preview..."), PreviewStateVisuals::Kind::Loading);

    auto* watcher = new QFutureWatcher<TextLoadResult>(this);
    connect(watcher, &QFutureWatcher<TextLoadResult>::finished, this, [this, watcher, loadToken, cancelToken]() {
        watcher->deleteLater();

        if (previewCancellationRequested(cancelToken) || !m_loadGuard.isCurrent(loadToken, m_info.filePath)) {
            qDebug().noquote() << QStringLiteral("[SpaceLookRender] TextRenderer discarded stale async result path=\"%1\"")
                .arg(loadToken.path);
            return;
        }

        const TextLoadResult result = watcher->result();
        if (!result.success) {
            qDebug().noquote() << QStringLiteral("[SpaceLookRender] TextRenderer failed to open: %1").arg(loadToken.path);
            PreviewStateVisuals::showStatus(m_statusLabel, result.statusMessage, PreviewStateVisuals::Kind::Error);
            m_textEdit->setPlainText(result.text);
            m_contentStack->setCurrentWidget(m_textEdit);
            resetSearch();
            notifyLoadingState(false);
            return;
        }

        const QString previewText = formattedStructuredPreviewText(loadToken.path, result.text);
        m_textEdit->setPlainText(previewText);
        m_contentStack->setCurrentWidget(m_textEdit);
        resetSearch();
        if (result.statusMessage.trimmed().isEmpty()) {
            PreviewStateVisuals::showStatus(m_statusLabel, QCoreApplication::translate("SpaceLook", "Encoding: %1").arg(result.encodingName));
        } else {
            PreviewStateVisuals::showStatus(m_statusLabel, QCoreApplication::translate("SpaceLook", "Encoding: %1. %2").arg(result.encodingName, result.statusMessage));
        }

        qDebug().noquote() << QStringLiteral("[SpaceLookRender] TextRenderer loaded async chars=%1 path=\"%2\"")
            .arg(previewText.size())
            .arg(loadToken.path);
        notifyLoadingState(false);
    });
    watcher->setFuture(QtConcurrent::run([filePath = info.filePath, cancelToken]() {
        return loadTextPreviewContent(filePath, cancelToken);
    }));
}

void TextRenderer::unload()
{
    cancelPreviewTask(m_cancelToken);
    m_loadGuard.cancel();
    notifyLoadingState(false);
    m_textEdit->clear();
    hideSearchRow(true);
    m_contentStack->setCurrentWidget(m_loadingCard);
    m_pathValueLabel->clear();
    m_openWithButton->setTargetContext(QString(), QString());
    PreviewStateVisuals::clearStatus(m_statusLabel);
    m_info = HoveredItemInfo();
}

void TextRenderer::notifyLoadingState(bool loading)
{
    if (m_loadingStateCallback) {
        m_loadingStateCallback(loading);
    }
}

void TextRenderer::findSearchMatch(bool backwards)
{
    const QString query = m_searchEdit ? m_searchEdit->text() : QString();
    if (query.trimmed().isEmpty() || !m_textEdit || !m_textEdit->document()) {
        updateSearchSummary();
        return;
    }

    QTextDocument::FindFlags flags;
    if (backwards) {
        flags |= QTextDocument::FindBackward;
    }

    QTextCursor found = m_textEdit->document()->find(query, m_textEdit->textCursor(), flags);
    if (found.isNull()) {
        QTextCursor wrapCursor(m_textEdit->document());
        wrapCursor.movePosition(backwards ? QTextCursor::End : QTextCursor::Start);
        found = m_textEdit->document()->find(query, wrapCursor, flags);
    }
    if (!found.isNull()) {
        m_textEdit->setTextCursor(found);
        m_textEdit->ensureCursorVisible();
    }
    updateSearchSummary();
}

void TextRenderer::updateSearchSummary()
{
    if (!m_searchCountLabel || !m_textEdit || !m_textEdit->document()) {
        return;
    }

    const QString query = m_searchEdit ? m_searchEdit->text() : QString();
    if (query.trimmed().isEmpty()) {
        m_searchCountLabel->clear();
        return;
    }

    int total = 0;
    int current = 0;
    const int selectionStart = m_textEdit->textCursor().selectionStart();
    QTextCursor cursor(m_textEdit->document());
    while (true) {
        cursor = m_textEdit->document()->find(query, cursor);
        if (cursor.isNull()) {
            break;
        }
        ++total;
        if (cursor.selectionStart() == selectionStart) {
            current = total;
        }
    }

    m_searchCountLabel->setText(total > 0
        ? QStringLiteral("%1/%2").arg(current > 0 ? current : 1).arg(total)
        : QStringLiteral("0/0"));
}

void TextRenderer::resetSearch()
{
    if (m_searchEdit && !m_searchEdit->text().trimmed().isEmpty()) {
        findSearchMatch(false);
        return;
    }
    if (m_searchCountLabel) {
        m_searchCountLabel->clear();
    }
}

void TextRenderer::showSearchRow()
{
    if (!m_searchRow) {
        return;
    }

    m_searchRow->show();
    if (m_searchEdit) {
        m_searchEdit->setFocus(Qt::ShortcutFocusReason);
        m_searchEdit->selectAll();
    }
}

void TextRenderer::hideSearchRow(bool clearQuery)
{
    if (clearQuery && m_searchEdit) {
        m_searchEdit->clear();
    }
    if (m_searchCountLabel) {
        m_searchCountLabel->clear();
    }
    if (m_searchRow) {
        m_searchRow->hide();
    }
}

void TextRenderer::showStatusMessage(const QString& message)
{
    if (message.trimmed().isEmpty()) {
        PreviewStateVisuals::clearStatus(m_statusLabel);
        return;
    }

    PreviewStateVisuals::showStatus(m_statusLabel, message);
    QTimer::singleShot(1400, m_statusLabel, [label = m_statusLabel]() {
        if (label) {
            PreviewStateVisuals::clearStatus(label);
        }
    });
}

void TextRenderer::updateModeSelector(const QString& filePath)
{
    const bool supportsStructuredToggle = isStructuredOverrideSuffix(filePath);
    m_modeSwitchButton->setVisible(supportsStructuredToggle);
    if (!supportsStructuredToggle) {
        return;
    }

    const QString currentRenderer = m_previewState && !m_previewState->rendererOverride().trimmed().isEmpty()
        ? m_previewState->rendererOverride()
        : QStringLiteral("text");
    m_modeSwitchButton->setCurrentModeId(currentRenderer);
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
        "  font-family: 'Segoe UI Rounded';"
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
        "#TextModeSwitchButton QToolButton {"
        "  background: rgba(238, 244, 252, 0.96);"
        "  color: #17324b;"
        "  border: 1px solid rgba(193, 208, 224, 0.95);"
        "  padding: 3px 8px;"
        "  min-height: 24px;"
        "}"
        "#TextModeSwitchButton QToolButton:hover {"
        "  background: rgba(245, 249, 255, 1.0);"
        "}"
        "#TextModeSwitchButton QToolButton:pressed {"
        "  background: rgba(224, 234, 246, 1.0);"
        "}"
        "#TextModeSwitchButton #ModeSwitchPrimaryButton {"
        "  border-top-left-radius: 10px;"
        "  border-bottom-left-radius: 10px;"
        "  border-top-right-radius: 0px;"
        "  border-bottom-right-radius: 0px;"
        "  min-width: 46px;"
        "}"
        "#TextModeSwitchButton #ModeSwitchExpandButton {"
        "  border-left: none;"
        "  border-top-left-radius: 0px;"
        "  border-bottom-left-radius: 0px;"
        "  border-top-right-radius: 10px;"
        "  border-bottom-right-radius: 10px;"
        "  min-width: 20px;"
        "  padding-left: 5px;"
        "  padding-right: 5px;"
        "}"
        "#TextStatusRow {"
        "  background: transparent;"
        "}"
        "#TextSearchRow {"
        "  background: rgba(238, 244, 252, 0.96);"
        "  border: 1px solid rgba(193, 208, 224, 0.95);"
        "  border-radius: 12px;"
        "}"
        "#TextSearchEdit {"
        "  color: #17324b;"
        "  background: transparent;"
        "  border: none;"
        "  selection-background-color: #cfe3ff;"
        "}"
        "#TextSearchPreviousButton, #TextSearchNextButton {"
        "  color: #17324b;"
        "  background: transparent;"
        "  border: none;"
        "  border-radius: 8px;"
        "}"
        "#TextSearchPreviousButton:hover, #TextSearchNextButton:hover {"
        "  background: rgba(215, 229, 246, 0.95);"
        "}"
        "#TextSearchCount {"
        "  color: #526b85;"
        "}"
        "#TextContentSection {"
        "  background: transparent;"
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
    titleFont.setFamily(QStringLiteral("Segoe UI Rounded"));
    titleFont.setPixelSize(20);
    titleFont.setWeight(QFont::Bold);
    m_titleLabel->setFont(titleFont);
    m_titleLabel->setWordWrap(true);

    QFont metaFont;
    metaFont.setFamily(QStringLiteral("Segoe UI Rounded"));
    metaFont.setPixelSize(13);
    m_metaLabel->setFont(metaFont);
    m_pathTitleLabel->setFont(metaFont);
    m_pathValueLabel->setFont(metaFont);
    m_statusLabel->setFont(metaFont);
    m_metaLabel->setWordWrap(true);
    m_pathValueLabel->setWordWrap(true);
    m_loadingMessageLabel->setFont(metaFont);

    QFont loadingTitleFont;
    loadingTitleFont.setFamily(QStringLiteral("Segoe UI Rounded"));
    loadingTitleFont.setPixelSize(18);
    m_loadingTitleLabel->setFont(loadingTitleFont);

    QFont textFont;
    textFont.setFamily(QStringLiteral("Cascadia Mono"));
    textFont.setPixelSize(14);
    m_textEdit->setFont(textFont);
}
