#include "renderers/markup/RenderedPageRenderer.h"

#include <cmark.h>

#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QFrame>
#include <QDir>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QRegularExpression>
#include <QStringList>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QDebug>
#include <QtConcurrent/QtConcurrent>

#include "core/PreviewFileReader.h"
#include "renderers/FileTypeIconResolver.h"
#include "renderers/OpenWithButton.h"
#include "renderers/PreviewHeaderBar.h"
#include "renderers/PreviewStateVisuals.h"
#include "renderers/SelectableTitleLabel.h"
#include "renderers/markup/LiteHtmlView.h"
#include "renderers/markup/WebView2HtmlView.h"
#include "widgets/SpaceLookWindow.h"

namespace {

constexpr qint64 kMaxRenderedPagePreviewBytes = 1024 * 1024 * 2;

struct RenderedPageLoadResult
{
    QString text;
    QString statusMessage;
    bool success = false;
};

RenderedPageLoadResult loadRenderedPageContent(const QString& filePath, const PreviewCancellationToken& cancelToken)
{
    RenderedPageLoadResult result;
    if (previewCancellationRequested(cancelToken)) {
        result.statusMessage = QStringLiteral("Rendered page preview was canceled.");
        return result;
    }

    QByteArray content;
    bool truncated = false;
    if (!PreviewFileReader::readPrefix(filePath, kMaxRenderedPagePreviewBytes, &content, &truncated)) {
        result.text = QStringLiteral("Could not read:\n%1").arg(filePath);
        result.statusMessage = QStringLiteral("Failed to open the file for preview.");
        return result;
    }

    if (previewCancellationRequested(cancelToken)) {
        content.clear();
        result.statusMessage = QStringLiteral("Rendered page preview was canceled.");
        return result;
    }

    if (truncated) {
        result.statusMessage = QStringLiteral("Preview is truncated to the first 2 MB.");
    }

    result.text = QString::fromUtf8(content);
    result.success = true;
    return result;
}

QString loadingHtmlPage(const QString& title, const QString& message)
{
    return PreviewStateVisuals::htmlStatePage(title, message, PreviewStateVisuals::Kind::Loading);
}

QString webView2InstallPromptHtml(const QString& detail)
{
    return QStringLiteral(
        "<!DOCTYPE html>"
        "<html>"
        "<head>"
        "<meta charset=\"utf-8\">"
        "<style>"
        "body {"
        "  margin: 0;"
        "  padding: 32px;"
        "  background: linear-gradient(135deg, #f8fbff 0%, #edf5ff 100%);"
        "  color: #18324a;"
        "  font-family: 'Segoe UI Rounded', sans-serif;"
        "}"
        ".card {"
        "  max-width: 760px;"
        "  margin: 32px auto;"
        "  background: #ffffff;"
        "  border: 1px solid #d6e2ef;"
        "  border-radius: 18px;"
        "  padding: 26px 28px;"
        "  box-shadow: 0 14px 34px rgba(18, 45, 74, 0.08);"
        "}"
        "h2 { margin: 0 0 12px 0; color: #0f2740; }"
        "p { margin: 0 0 14px 0; line-height: 1.65; color: #536b84; }"
        "a { color: #0067c0; font-weight: 600; text-decoration: none; }"
        "a:hover { text-decoration: underline; }"
        ".detail {"
        "  margin-top: 14px;"
        "  padding: 12px 14px;"
        "  background: #f3f7fb;"
        "  border: 1px solid #d8e4ef;"
        "  border-radius: 12px;"
        "  color: #5d7188;"
        "  font-size: 13px;"
        "}"
        "</style>"
        "</head>"
        "<body><div class=\"card\">"
        "<h2>Microsoft Edge WebView2 Runtime is required</h2>"
        "<p>SpaceLook now uses Microsoft Edge WebView2 as the primary HTML preview engine.</p>"
        "<p>Please install the WebView2 Runtime from Microsoft, then reopen this preview.</p>"
        "<p><a href=\"%1\">Download Microsoft Edge WebView2 Runtime</a></p>"
        "<p class=\"detail\">%2</p>"
        "</div></body>"
        "</html>")
        .arg(WebView2HtmlView::runtimeDownloadUrl().toHtmlEscaped(),
             detail.trimmed().isEmpty() ? QStringLiteral("WebView2 Runtime was not available.") : detail.toHtmlEscaped());
}

QString markdownPreviewCss()
{
    return QStringLiteral(
        "html, body {"
        "  margin: 0;"
        "  padding: 0;"
        "  background: #ffffff;"
        "  color: #1f2328;"
        "}"
        "body {"
        "  font-family: 'Segoe UI Rounded', 'Noto Sans', Helvetica, Arial, sans-serif;"
        "  font-size: 16px;"
        "  line-height: 1.5;"
        "  padding: 32px 40px 48px 40px;"
        "}"
        "main {"
        "  max-width: 900px;"
        "  margin: 0 auto;"
        "}"
        "h1, h2, h3, h4, h5, h6 {"
        "  color: #1f2328;"
        "  font-weight: 600;"
        "  line-height: 1.25;"
        "  margin-top: 24px;"
        "  margin-bottom: 16px;"
        "}"
        "h1 {"
        "  font-size: 2em;"
        "  padding-bottom: 10px;"
        "  border-bottom: 1px solid #d8dee4;"
        "}"
        "h2 {"
        "  font-size: 1.5em;"
        "  padding-bottom: 8px;"
        "  border-bottom: 1px solid #d8dee4;"
        "}"
        "h3 { font-size: 1.25em; }"
        "h4 { font-size: 1em; }"
        "h5 { font-size: 0.875em; }"
        "h6 { font-size: 0.85em; color: #59636e; }"
        "p, blockquote, ul, ol, dl, table, pre, details {"
        "  margin-top: 0;"
        "  margin-bottom: 16px;"
        "}"
        "strong { font-weight: 600; }"
        "em { font-style: italic; }"
        "ul, ol { padding-left: 2em; }"
        "ul ul, ul ol, ol ol, ol ul { margin-top: 0; margin-bottom: 0; }"
        "li { margin: 0.25em 0; }"
        "li > p { margin-bottom: 0.5em; }"
        "dl { padding: 0; }"
        "dl dt {"
        "  padding: 0;"
        "  margin-top: 16px;"
        "  font-size: 1em;"
        "  font-style: italic;"
        "  font-weight: 600;"
        "}"
        "dl dd {"
        "  padding: 0 16px;"
        "  margin-bottom: 16px;"
        "}"
        "blockquote {"
        "  color: #59636e;"
        "  border-left: 0.25em solid #d0d7de;"
        "  padding: 0 1em;"
        "}"
        "pre, code {"
        "  font-family: 'Cascadia Mono', 'Consolas', 'SFMono-Regular', monospace;"
        "  font-size: 0.85em;"
        "}"
        "code {"
        "  padding: 0.2em 0.4em;"
        "  margin: 0;"
        "  background-color: rgba(175, 184, 193, 0.2);"
        "  border-radius: 6px;"
        "}"
        "pre {"
        "  background: #f6f8fa;"
        "  border: 1px solid #d0d7de;"
        "  border-radius: 6px;"
        "  padding: 16px;"
        "  overflow: auto;"
        "}"
        "pre code {"
        "  background: transparent;"
        "  padding: 0;"
        "  border-radius: 0;"
        "}"
        "table {"
        "  display: block;"
        "  width: max-content;"
        "  max-width: 100%;"
        "  border-collapse: collapse;"
        "  border-spacing: 0;"
        "  overflow: auto;"
        "}"
        "th, td {"
        "  border: 1px solid #d0d7de;"
        "  padding: 6px 13px;"
        "}"
        "th {"
        "  background: #f6f8fa;"
        "  font-weight: 600;"
        "}"
        "tr:nth-child(2n) {"
        "  background-color: #f6f8fa;"
        "}"
        "a {"
        "  color: #0969da;"
        "  text-decoration: none;"
        "}"
        "a:hover {"
        "  text-decoration: underline;"
        "}"
        "hr {"
        "  height: 0.25em;"
        "  padding: 0;"
        "  margin: 24px 0;"
        "  background-color: #d0d7de;"
        "  border: 0;"
        "}"
        "img {"
        "  max-width: 100%;"
        "  height: auto;"
        "}"
        "kbd {"
        "  display: inline-block;"
        "  padding: 3px 5px;"
        "  font: 11px 'Cascadia Mono', 'Consolas', monospace;"
        "  line-height: 10px;"
        "  color: #1f2328;"
        "  vertical-align: middle;"
        "  background-color: #f6f8fa;"
        "  border: solid 1px rgba(175, 184, 193, 0.2);"
        "  border-bottom-color: rgba(175, 184, 193, 0.4);"
        "  border-radius: 6px;"
        "  box-shadow: inset 0 -1px 0 rgba(175, 184, 193, 0.2);"
        "}"
    );
}

QString inlineLocalStylesheets(const QString& html, const QString& basePath)
{
    QString output = html;
    const QRegularExpression linkPattern(
        QStringLiteral(R"(<link\s+[^>]*rel\s*=\s*["']stylesheet["'][^>]*href\s*=\s*["']([^"']+)["'][^>]*>)"),
        QRegularExpression::CaseInsensitiveOption);

    QRegularExpressionMatchIterator iterator = linkPattern.globalMatch(output);
    QStringList cssBlocks;
    while (iterator.hasNext()) {
        const QRegularExpressionMatch match = iterator.next();
        const QString href = match.captured(1).trimmed();
        if (href.isEmpty()) {
            continue;
        }

        QString cssFilePath = href;
        if (QUrl(href).isLocalFile()) {
            cssFilePath = QUrl(href).toLocalFile();
        } else if (!QDir::isAbsolutePath(cssFilePath)) {
            cssFilePath = QDir(basePath).absoluteFilePath(cssFilePath);
        }

        QByteArray cssBytes;
        if (!PreviewFileReader::readAll(cssFilePath, &cssBytes)) {
            continue;
        }

        const QString cssText = QString::fromUtf8(cssBytes);
        if (!cssText.trimmed().isEmpty()) {
            cssBlocks.append(cssText);
        }
    }

    if (cssBlocks.isEmpty()) {
        return output;
    }

    output.remove(linkPattern);
    const QString styleBlock = QStringLiteral("<style>\n%1\n</style>").arg(cssBlocks.join(QStringLiteral("\n")));
    const int headIndex = output.indexOf(QStringLiteral("</head>"), 0, Qt::CaseInsensitive);
    if (headIndex >= 0) {
        output.insert(headIndex, styleBlock);
        return output;
    }

    return styleBlock + output;
}

QString wrapMarkdownHtml(const QString& bodyHtml)
{
    return QStringLiteral(
        "<!DOCTYPE html>"
        "<html>"
        "<head>"
        "<meta charset=\"utf-8\">"
        "<style>%1</style>"
        "</head>"
        "<body><main>%2</main></body>"
        "</html>")
        .arg(markdownPreviewCss(), bodyHtml);
}

QStringList splitMarkdownTableRow(const QString& line)
{
    QString row = line.trimmed();
    if (row.startsWith(QLatin1Char('|'))) {
        row.remove(0, 1);
    }
    if (row.endsWith(QLatin1Char('|'))) {
        row.chop(1);
    }

    QStringList cells;
    QString current;
    bool escaping = false;
    for (const QChar ch : row) {
        if (escaping) {
            current += ch;
            escaping = false;
            continue;
        }
        if (ch == QLatin1Char('\\')) {
            escaping = true;
            continue;
        }
        if (ch == QLatin1Char('|')) {
            cells.append(current.trimmed());
            current.clear();
            continue;
        }
        current += ch;
    }
    cells.append(current.trimmed());
    return cells;
}

bool looksLikeMarkdownTableSeparator(const QString& line, int expectedColumns)
{
    const QStringList parts = splitMarkdownTableRow(line);
    if (parts.size() != expectedColumns || parts.isEmpty()) {
        return false;
    }

    static const QRegularExpression separatorPattern(QStringLiteral(R"(^:?-{3,}:?$)"));
    for (const QString& part : parts) {
        if (!separatorPattern.match(part.trimmed()).hasMatch()) {
            return false;
        }
    }
    return true;
}

QString markdownCellToHtml(const QString& text)
{
    QString value = text.toHtmlEscaped();
    value.replace(QRegularExpression(QStringLiteral(R"(\*\*(.+?)\*\*)")), QStringLiteral("<strong>\\1</strong>"));
    value.replace(QRegularExpression(QStringLiteral(R"(\*(.+?)\*)")), QStringLiteral("<em>\\1</em>"));
    value.replace(QRegularExpression(QStringLiteral(R"(`([^`]+)`)")), QStringLiteral("<code>\\1</code>"));
    value.replace(QRegularExpression(QStringLiteral(R"(\[([^\]]+)\]\(([^)]+)\))")), QStringLiteral("<a href=\"\\2\">\\1</a>"));
    return value;
}

QString renderMarkdownTableBlock(const QStringList& lines, int& consumedLineCount)
{
    consumedLineCount = 0;
    if (lines.size() < 2) {
        return QString();
    }

    const QString headerLine = lines.at(0);
    const QString separatorLine = lines.at(1);
    const QStringList headers = splitMarkdownTableRow(headerLine);
    if (headers.isEmpty() || !looksLikeMarkdownTableSeparator(separatorLine, headers.size())) {
        return QString();
    }

    consumedLineCount = 2;
    QStringList bodyRows;
    while (consumedLineCount < lines.size()) {
        const QString candidate = lines.at(consumedLineCount);
        if (candidate.trimmed().isEmpty() || !candidate.contains(QLatin1Char('|'))) {
            break;
        }
        const QStringList rowCells = splitMarkdownTableRow(candidate);
        if (rowCells.size() != headers.size()) {
            break;
        }
        QString rowHtml = QStringLiteral("<tr>");
        for (const QString& cell : rowCells) {
            rowHtml += QStringLiteral("<td>%1</td>").arg(markdownCellToHtml(cell));
        }
        rowHtml += QStringLiteral("</tr>");
        bodyRows.append(rowHtml);
        ++consumedLineCount;
    }

    if (bodyRows.isEmpty()) {
        consumedLineCount = 0;
        return QString();
    }

    QString html = QStringLiteral("<table><thead><tr>");
    for (const QString& header : headers) {
        html += QStringLiteral("<th>%1</th>").arg(markdownCellToHtml(header));
    }
    html += QStringLiteral("</tr></thead><tbody>");
    html += bodyRows.join(QString());
    html += QStringLiteral("</tbody></table>");
    return html;
}

QString preprocessMarkdownTables(const QString& markdownText)
{
    const QStringList lines = markdownText.split(QLatin1Char('\n'));
    QStringList output;

    int index = 0;
    bool inFence = false;
    while (index < lines.size()) {
        const QString line = lines.at(index);
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QStringLiteral("```")) || trimmed.startsWith(QStringLiteral("~~~"))) {
            inFence = !inFence;
            output.append(line);
            ++index;
            continue;
        }

        if (!inFence && line.contains(QLatin1Char('|'))) {
            int consumedLineCount = 0;
            const QString tableHtml = renderMarkdownTableBlock(lines.mid(index), consumedLineCount);
            if (!tableHtml.isEmpty() && consumedLineCount > 0) {
                output.append(QString());
                output.append(tableHtml);
                output.append(QString());
                index += consumedLineCount;
                continue;
            }
        }

        output.append(line);
        ++index;
    }

    return output.join(QStringLiteral("\n"));
}

QString markdownToHtml(const QString& markdownText)
{
    const QByteArray utf8 = preprocessMarkdownTables(markdownText).toUtf8();
    char* html = cmark_markdown_to_html(
        utf8.constData(),
        static_cast<size_t>(utf8.size()),
        CMARK_OPT_DEFAULT | CMARK_OPT_UNSAFE);
    if (!html) {
        return QString();
    }

    const QString result = QString::fromUtf8(html);
    free(html);
    return result;
}

}

RenderedPageRenderer::RenderedPageRenderer(QWidget* parent)
    : QWidget(parent)
    , m_headerRow(new QWidget(this))
    , m_iconLabel(new QLabel(this))
    , m_titleLabel(new SelectableTitleLabel(this))
    , m_pathRow(new QWidget(this))
    , m_pathValueLabel(new QLabel(this))
    , m_openWithButton(new OpenWithButton(this))
    , m_statusLabel(new QLabel(this))
    , m_contentStack(new QStackedWidget(this))
    , m_webView(new WebView2HtmlView(this))
    , m_fallbackHtmlView(new LiteHtmlView(this))
{
    setAttribute(Qt::WA_StyledBackground, true);
    setObjectName(QStringLiteral("RenderedPageRendererRoot"));
    m_headerRow->setObjectName(QStringLiteral("RenderedPageHeaderRow"));
    m_iconLabel->setObjectName(QStringLiteral("RenderedPageTypeIcon"));
    m_titleLabel->setObjectName(QStringLiteral("RenderedPageTitle"));
    m_pathRow->setObjectName(QStringLiteral("RenderedPagePathRow"));
    m_pathValueLabel->setObjectName(QStringLiteral("RenderedPagePathValue"));
    m_openWithButton->setObjectName(QStringLiteral("RenderedPageOpenWithButton"));
    m_statusLabel->setObjectName(QStringLiteral("RenderedPageStatus"));
    m_contentStack->setObjectName(QStringLiteral("RenderedPageContentStack"));
    m_webView->setObjectName(QStringLiteral("RenderedPageBrowser"));
    m_fallbackHtmlView->setObjectName(QStringLiteral("RenderedPageFallbackBrowser"));

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

    m_iconLabel->setFixedSize(72, 72);
    m_iconLabel->setScaledContents(false);
    m_iconLabel->setAlignment(Qt::AlignCenter);
    m_titleLabel->setWordWrap(true);
    m_pathValueLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_pathValueLabel->setWordWrap(true);
    m_pathValueLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_pathRow->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    PreviewStateVisuals::prepareStatusLabel(m_statusLabel);
    m_statusLabel->hide();
    m_contentStack->addWidget(m_webView);
    m_contentStack->addWidget(m_fallbackHtmlView);
    m_contentStack->setCurrentWidget(m_webView);

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
    connect(m_webView, &WebView2HtmlView::documentLoaded, this, [this]() {
        if (m_contentStack) {
            m_contentStack->setCurrentWidget(m_webView);
        }
        PreviewStateVisuals::clearStatus(m_statusLabel);
        notifyLoadingState(false);
    });
    connect(m_webView, &WebView2HtmlView::unavailable, this, [this](const QString& message) {
        m_contentStack->setCurrentWidget(m_fallbackHtmlView);
        m_fallbackHtmlView->setDocumentHtml(webView2InstallPromptHtml(message), m_info.filePath);
        PreviewStateVisuals::showStatus(
            m_statusLabel,
            QStringLiteral("Microsoft Edge WebView2 Runtime is required for modern HTML preview."),
            PreviewStateVisuals::Kind::Error);
        notifyLoadingState(false);
    });

    applyChrome();
}

QString RenderedPageRenderer::rendererId() const
{
    return QStringLiteral("rendered_page");
}

bool RenderedPageRenderer::canHandle(const HoveredItemInfo& info) const
{
    return info.typeKey == QStringLiteral("markdown") ||
        info.typeKey == QStringLiteral("html");
}

QWidget* RenderedPageRenderer::widget()
{
    return this;
}

void RenderedPageRenderer::warmUp()
{
    QString errorMessage;
    if (!m_webView->warmUp(&errorMessage) && !errorMessage.trimmed().isEmpty()) {
        qDebug().noquote() << QStringLiteral("[SpaceLookRender] RenderedPageRenderer warmup skipped: %1").arg(errorMessage);
    }
    m_fallbackHtmlView->setDocumentHtml(QStringLiteral("<!doctype html><html><body></body></html>"), QString());
}

bool RenderedPageRenderer::reportsLoadingState() const
{
    return true;
}

void RenderedPageRenderer::setLoadingStateCallback(std::function<void(bool)> callback)
{
    m_loadingStateCallback = std::move(callback);
}

void RenderedPageRenderer::load(const HoveredItemInfo& info)
{
    cancelPreviewTask(m_cancelToken);
    const PreviewCancellationToken cancelToken = makePreviewCancellationToken();
    m_cancelToken = cancelToken;
    m_info = info;
    const PreviewLoadGuard::Token loadToken = m_loadGuard.begin(info.filePath);
    notifyLoadingState(true);
    qDebug().noquote() << QStringLiteral("[SpaceLookRender] RenderedPageRenderer load path=\"%1\" type=\"%2\"")
        .arg(info.filePath, info.typeKey);

    const QString previewTitle = info.title.isEmpty()
        ? (info.typeKey == QStringLiteral("html") ? QStringLiteral("HTML Preview") : QStringLiteral("Markdown Preview"))
        : info.title;
    const QString loadingMessage = info.typeKey == QStringLiteral("html")
        ? QStringLiteral("Preparing rendered HTML preview...")
        : QStringLiteral("Preparing rendered Markdown preview...");

    m_titleLabel->setText(previewTitle);
    m_titleLabel->setCopyText(previewTitle);
    m_iconLabel->setPixmap(FileTypeIconResolver::pixmapForInfo(info, m_iconLabel->contentsRect().size()));
    m_pathValueLabel->setText(info.filePath.trimmed().isEmpty() ? QStringLiteral("(Unavailable)") : info.filePath);
    m_openWithButton->setTargetContext(info.filePath, info.typeKey);
    PreviewStateVisuals::showStatus(m_statusLabel, loadingMessage, PreviewStateVisuals::Kind::Loading);
    m_contentStack->setCurrentWidget(m_webView);
    QString webViewError;
    if (!m_webView->setDocumentHtml(loadingHtmlPage(previewTitle, loadingMessage), info.filePath, &webViewError)) {
        m_contentStack->setCurrentWidget(m_fallbackHtmlView);
        m_fallbackHtmlView->setDocumentHtml(webView2InstallPromptHtml(webViewError), info.filePath);
    }

    auto* watcher = new QFutureWatcher<RenderedPageLoadResult>(this);
    connect(watcher, &QFutureWatcher<RenderedPageLoadResult>::finished, this, [this, watcher, loadToken, typeKey = info.typeKey, cancelToken]() {
        watcher->deleteLater();

        if (previewCancellationRequested(cancelToken) || !m_loadGuard.isCurrent(loadToken, m_info.filePath)) {
            qDebug().noquote() << QStringLiteral("[SpaceLookRender] RenderedPageRenderer discarded stale async result path=\"%1\"")
                .arg(loadToken.path);
            return;
        }

        const RenderedPageLoadResult result = watcher->result();
        if (!result.success) {
            PreviewStateVisuals::showStatus(m_statusLabel, result.statusMessage, PreviewStateVisuals::Kind::Error);
            QString errorMessage;
            const QString unavailableHtml = PreviewStateVisuals::htmlStatePage(
                QStringLiteral("Preview Unavailable"),
                result.text,
                PreviewStateVisuals::Kind::Error);
            if (m_webView->setDocumentHtml(unavailableHtml, loadToken.path, &errorMessage)) {
                m_contentStack->setCurrentWidget(m_webView);
            } else {
                m_contentStack->setCurrentWidget(m_fallbackHtmlView);
                m_fallbackHtmlView->setDocumentHtml(webView2InstallPromptHtml(errorMessage), loadToken.path);
            }
            notifyLoadingState(false);
            return;
        }

        const bool markdownSource = typeKey == QStringLiteral("markdown");
        const QString htmlDocument = prepareHtmlDocument(result.text, loadToken.path, markdownSource);
        QString webViewError;
        if (m_webView->setDocumentHtml(htmlDocument, loadToken.path, &webViewError)) {
            m_contentStack->setCurrentWidget(m_webView);
        } else {
            m_contentStack->setCurrentWidget(m_fallbackHtmlView);
            m_fallbackHtmlView->setDocumentHtml(webView2InstallPromptHtml(webViewError), loadToken.path);
        }

        if (result.statusMessage.trimmed().isEmpty()) {
            PreviewStateVisuals::clearStatus(m_statusLabel);
        } else {
            PreviewStateVisuals::showStatus(m_statusLabel, result.statusMessage);
        }

        qDebug().noquote() << QStringLiteral("[SpaceLookRender] RenderedPageRenderer loaded async chars=%1 path=\"%2\"")
            .arg(result.text.size())
            .arg(loadToken.path);
        notifyLoadingState(false);
    });
    watcher->setFuture(QtConcurrent::run([filePath = info.filePath, cancelToken]() {
        return loadRenderedPageContent(filePath, cancelToken);
    }));
}

void RenderedPageRenderer::unload()
{
    cancelPreviewTask(m_cancelToken);
    m_loadGuard.cancel();
    notifyLoadingState(false);
    m_webView->clearDocument();
    m_fallbackHtmlView->clearDocument();
    m_pathValueLabel->clear();
    m_openWithButton->setTargetContext(QString(), QString());
    PreviewStateVisuals::clearStatus(m_statusLabel);
    m_info = HoveredItemInfo();
}

void RenderedPageRenderer::notifyLoadingState(bool loading)
{
    if (m_loadingStateCallback) {
        m_loadingStateCallback(loading);
    }
}

void RenderedPageRenderer::showStatusMessage(const QString& message)
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

QString RenderedPageRenderer::prepareHtmlDocument(const QString& html, const QString& filePath, bool markdownSource) const
{
    if (markdownSource) {
        return wrapMarkdownHtml(markdownToHtml(html));
    }
    return inlineLocalStylesheets(html, QFileInfo(filePath).absolutePath());
}

void RenderedPageRenderer::applyChrome()
{
    setStyleSheet(
        "#RenderedPageRendererRoot {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:1,"
        "      stop:0 #fcfdff,"
        "      stop:1 #f4f8fc);"
        "  border-radius: 0px;"
        "}"
        "QLabel {"
        "  color: #18324a;"
        "}"
        "#RenderedPageTypeIcon {"
        "  background: transparent;"
        "  border: none;"
        "  padding: 0px;"
        "}"
        "#RenderedPageTitle {"
        "  color: #0f2740;"
        "}"
        "#RenderedPagePathValue {"
        "  color: #445d76;"
        "}"
        "#RenderedPageOpenWithButton QToolButton {"
        "  background: rgba(238, 244, 252, 0.96);"
        "  color: #17324b;"
        "  border: 1px solid rgba(193, 208, 224, 0.95);"
        "  padding: 3px 8px;"
        "  min-height: 24px;"
        "}"
        "#RenderedPageOpenWithButton QToolButton:hover {"
        "  background: rgba(245, 249, 255, 1.0);"
        "}"
        "#RenderedPageOpenWithButton QToolButton:pressed {"
        "  background: rgba(224, 234, 246, 1.0);"
        "}"
        "#RenderedPageOpenWithButton #OpenWithPrimaryButton {"
        "  border-top-left-radius: 10px;"
        "  border-bottom-left-radius: 10px;"
        "  border-top-right-radius: 0px;"
        "  border-bottom-right-radius: 0px;"
        "  min-width: 28px;"
        "}"
        "#RenderedPageOpenWithButton #OpenWithExpandButton {"
        "  border-left: none;"
        "  border-top-left-radius: 0px;"
        "  border-bottom-left-radius: 0px;"
        "  border-top-right-radius: 10px;"
        "  border-bottom-right-radius: 10px;"
        "  min-width: 22px;"
        "  padding-left: 5px;"
        "  padding-right: 5px;"
        "}"
        "#RenderedPageStatus {"
        "  color: #27568b;"
        "  background: rgba(220, 235, 255, 0.92);"
        "  border: 1px solid rgba(164, 193, 229, 0.95);"
        "  border-radius: 12px;"
        "  padding: 10px 14px;"
        "}"
        "#RenderedPageContentStack {"
        "  background: transparent;"
        "  border: none;"
        "}"
        "#RenderedPageBrowser, #RenderedPageFallbackBrowser {"
        "  background: #ffffff;"
        "  border: 1px solid #d0d7de;"
        "  border-radius: 12px;"
        "  padding: 0px;"
        "}"
        "#RenderedPageFallbackBrowser QScrollBar:vertical {"
        "  background: rgba(232, 238, 245, 0.8);"
        "  border: none;"
        "  width: 8px;"
        "  margin: 10px 0 10px 0;"
        "  border-radius: 4px;"
        "}"
        "#RenderedPageFallbackBrowser QScrollBar::handle:vertical {"
        "  background: #7c8fa8;"
        "  min-height: 52px;"
        "  border-radius: 4px;"
        "}"
        "#RenderedPageFallbackBrowser QScrollBar::handle:vertical:hover {"
        "  background: #6d829d;"
        "}"
        "#RenderedPageFallbackBrowser QScrollBar::handle:vertical:pressed {"
        "  background: #61768f;"
        "}"
        "#RenderedPageFallbackBrowser QScrollBar:horizontal {"
        "  background: rgba(232, 238, 245, 0.8);"
        "  border: none;"
        "  height: 8px;"
        "  margin: 0 10px 0 10px;"
        "  border-radius: 4px;"
        "}"
        "#RenderedPageFallbackBrowser QScrollBar::handle:horizontal {"
        "  background: #7c8fa8;"
        "  min-width: 52px;"
        "  border-radius: 4px;"
        "}"
        "#RenderedPageFallbackBrowser QScrollBar::handle:horizontal:hover {"
        "  background: #6d829d;"
        "}"
        "#RenderedPageFallbackBrowser QScrollBar::handle:horizontal:pressed {"
        "  background: #61768f;"
        "}"
        "#RenderedPageFallbackBrowser QScrollBar::add-line:vertical, #RenderedPageFallbackBrowser QScrollBar::sub-line:vertical,"
        "#RenderedPageFallbackBrowser QScrollBar::add-line:horizontal, #RenderedPageFallbackBrowser QScrollBar::sub-line:horizontal {"
        "  width: 0px;"
        "  height: 0px;"
        "}"
        "#RenderedPageFallbackBrowser QScrollBar::add-page:vertical, #RenderedPageFallbackBrowser QScrollBar::sub-page:vertical,"
        "#RenderedPageFallbackBrowser QScrollBar::add-page:horizontal, #RenderedPageFallbackBrowser QScrollBar::sub-page:horizontal {"
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
    m_pathValueLabel->setFont(metaFont);
    m_statusLabel->setFont(metaFont);
    m_pathValueLabel->setWordWrap(true);
}
