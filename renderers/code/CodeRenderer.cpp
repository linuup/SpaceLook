#include "renderers/code/CodeRenderer.h"

#include <QCoreApplication>
#include <QFile>
#include <QFutureWatcher>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTimer>
#include <QTextEdit>
#include <QToolButton>
#include <QVBoxLayout>
#include <QDebug>
#include <QtConcurrent/QtConcurrent>

#include "renderers/CodeThemeManager.h"
#include "renderers/FileTypeIconResolver.h"
#include "renderers/OpenWithButton.h"
#include "renderers/PreviewHeaderBar.h"
#include "renderers/SelectableTitleLabel.h"
#include "widgets/SpaceLookWindow.h"
#include "third_party/ksyntaxhighlighting/src/lib/definition.h"
#include "third_party/ksyntaxhighlighting/src/lib/repository.h"
#include "third_party/ksyntaxhighlighting/src/lib/syntaxhighlighter.h"
#include "third_party/ksyntaxhighlighting/src/lib/theme.h"

namespace {

constexpr qint64 kMaxCodePreviewBytes = 1024 * 1024 * 2;

struct CodeLoadResult
{
    QString text;
    QString statusMessage;
    bool success = false;
};

class CodeTextEdit : public QPlainTextEdit
{
public:
    explicit CodeTextEdit(QWidget* parent = nullptr)
        : QPlainTextEdit(parent)
    {
        connect(this, &QPlainTextEdit::cursorPositionChanged, this, &CodeTextEdit::highlightCurrentLine);
        highlightCurrentLine();
    }

private:
    void highlightCurrentLine()
    {
        QList<QTextEdit::ExtraSelection> extraSelections;

        QTextEdit::ExtraSelection selection;
        selection.format.setBackground(QColor(QStringLiteral("#f6f8fa")));
        selection.format.setProperty(QTextFormat::FullWidthSelection, true);
        selection.cursor = textCursor();
        selection.cursor.clearSelection();
        extraSelections.append(selection);

        setExtraSelections(extraSelections);
    }
};

CodeLoadResult loadCodePreviewContent(const QString& filePath)
{
    CodeLoadResult result;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        result.text = QStringLiteral("Could not read:\n%1").arg(filePath);
        result.statusMessage = QStringLiteral("Failed to open the file for preview.");
        return result;
    }

    QByteArray content = file.read(kMaxCodePreviewBytes + 1);
    file.close();

    if (content.size() > kMaxCodePreviewBytes) {
        content.chop(content.size() - static_cast<int>(kMaxCodePreviewBytes));
        result.statusMessage = QStringLiteral("Preview is truncated to the first 2 MB.");
    }

    result.text = QString::fromUtf8(content);
    result.success = true;
    return result;
}

}

CodeRenderer::CodeRenderer(QWidget* parent)
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
    , m_textEdit(new CodeTextEdit(this))
{
    setAttribute(Qt::WA_StyledBackground, true);
    setObjectName(QStringLiteral("CodeRendererRoot"));
    m_headerRow->setObjectName(QStringLiteral("CodeHeaderRow"));
    m_iconLabel->setObjectName(QStringLiteral("CodeTypeIcon"));
    m_titleLabel->setObjectName(QStringLiteral("CodeTitle"));
    m_metaLabel->setObjectName(QStringLiteral("CodeMeta"));
    m_pathRow->setObjectName(QStringLiteral("CodePathRow"));
    m_pathTitleLabel->setObjectName(QStringLiteral("CodePathTitle"));
    m_pathValueLabel->setObjectName(QStringLiteral("CodePathValue"));
    m_openWithButton->setObjectName(QStringLiteral("CodeOpenWithButton"));
    m_statusLabel->setObjectName(QStringLiteral("CodeStatus"));
    m_contentStack->setObjectName(QStringLiteral("CodeContentStack"));
    m_loadingCard->setObjectName(QStringLiteral("CodeLoadingCard"));
    m_loadingTitleLabel->setObjectName(QStringLiteral("CodeLoadingTitle"));
    m_loadingMessageLabel->setObjectName(QStringLiteral("CodeLoadingMessage"));
    m_textEdit->setObjectName(QStringLiteral("CodeContent"));

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
    m_loadingTitleLabel->setText(QStringLiteral("Preparing code preview"));
    m_loadingMessageLabel->setText(QStringLiteral("Header details are ready. Source content is loading in the background."));
    m_loadingMessageLabel->setWordWrap(true);

    connect(m_titleLabel, &SelectableTitleLabel::copyFeedbackRequested, this, [this](const QString& message) {
        showStatusMessage(message);
    });

    applyChrome();
}

CodeRenderer::~CodeRenderer() = default;

QString CodeRenderer::rendererId() const
{
    return QStringLiteral("code");
}

bool CodeRenderer::canHandle(const HoveredItemInfo& info) const
{
    return info.typeKey == QStringLiteral("code");
}

QWidget* CodeRenderer::widget()
{
    return this;
}

void CodeRenderer::load(const HoveredItemInfo& info)
{
    m_info = info;
    ++m_loadRequestId;
    const quint64 requestId = m_loadRequestId;
    qDebug().noquote() << QStringLiteral("[SpaceLookRender] CodeRenderer load path=\"%1\"").arg(info.filePath);

    m_titleLabel->setText(info.title.isEmpty() ? QStringLiteral("Code Preview") : info.title);
    m_titleLabel->setCopyText(m_titleLabel->text());
    const QIcon typeIcon(FileTypeIconResolver::iconForInfo(info));
    m_iconLabel->setPixmap(typeIcon.pixmap(128, 128));
    m_pathValueLabel->setText(info.filePath.trimmed().isEmpty() ? QStringLiteral("(Unavailable)") : info.filePath);
    m_openWithButton->setTargetContext(info.filePath, info.typeKey);
    m_loadingTitleLabel->setText(QStringLiteral("Preparing code preview"));
    m_loadingMessageLabel->setText(QStringLiteral("Header details are ready. Source content is loading in the background."));
    m_contentStack->setCurrentWidget(m_loadingCard);
    m_textEdit->clear();
    m_statusLabel->setText(QStringLiteral("Loading code preview..."));
    m_statusLabel->show();

    auto* watcher = new QFutureWatcher<CodeLoadResult>(this);
    connect(watcher, &QFutureWatcher<CodeLoadResult>::finished, this, [this, watcher, requestId, filePath = info.filePath]() {
        const CodeLoadResult result = watcher->result();
        watcher->deleteLater();

        if (requestId != m_loadRequestId || m_info.filePath != filePath) {
            qDebug().noquote() << QStringLiteral("[SpaceLookRender] CodeRenderer discarded stale async result path=\"%1\"")
                .arg(filePath);
            return;
        }

        if (!result.success) {
            m_statusLabel->setText(result.statusMessage);
            m_statusLabel->show();
            m_textEdit->setPlainText(result.text);
            m_contentStack->setCurrentWidget(m_textEdit);
            return;
        }

        ensureRepository();
        m_textEdit->setPlainText(result.text);
        m_contentStack->setCurrentWidget(m_textEdit);
        applyDefinitionForPath(filePath);
        if (result.statusMessage.trimmed().isEmpty()) {
            m_statusLabel->clear();
            m_statusLabel->hide();
        } else {
            m_statusLabel->setText(result.statusMessage);
            m_statusLabel->show();
        }

        qDebug().noquote() << QStringLiteral("[SpaceLookRender] CodeRenderer loaded async chars=%1 path=\"%2\"")
            .arg(result.text.size())
            .arg(filePath);
    });
    watcher->setFuture(QtConcurrent::run([filePath = info.filePath]() {
        return loadCodePreviewContent(filePath);
    }));
}

void CodeRenderer::unload()
{
    ++m_loadRequestId;
    m_textEdit->clear();
    m_contentStack->setCurrentWidget(m_loadingCard);
    m_pathValueLabel->clear();
    m_openWithButton->setTargetContext(QString(), QString());
    m_statusLabel->clear();
    m_statusLabel->hide();
    if (m_highlighter) {
        m_highlighter->setDocument(nullptr);
        delete m_highlighter;
        m_highlighter = nullptr;
    }
    m_info = HoveredItemInfo();
}

void CodeRenderer::showStatusMessage(const QString& message)
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

void CodeRenderer::applyChrome()
{
    setStyleSheet(
        "#CodeRendererRoot {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:1,"
        "      stop:0 #fcfdff,"
        "      stop:1 #f4f8fc);"
        "  border-radius: 0px;"
        "}"
        "QLabel {"
        "  color: #18324a;"
        "}"
        "#CodeTypeIcon {"
        "  background: transparent;"
        "  border: none;"
        "  padding: 0px;"
        "}"
        "#CodeTitle {"
        "  color: #0f2740;"
        "}"
        "#CodeMeta {"
        "  color: #5c738b;"
        "}"
        "#CodePathTitle {"
        "  color: #16324a;"
        "  font-family: 'Segoe UI Semibold';"
        "}"
        "#CodePathValue {"
        "  color: #445d76;"
        "}"
        "#CodeOpenWithButton QToolButton {"
        "  background: rgba(238, 244, 252, 0.96);"
        "  color: #17324b;"
        "  border: 1px solid rgba(193, 208, 224, 0.95);"
        "  padding: 3px 8px;"
        "  min-height: 24px;"
        "}"
        "#CodeOpenWithButton QToolButton:hover {"
        "  background: rgba(245, 249, 255, 1.0);"
        "}"
        "#CodeOpenWithButton QToolButton:pressed {"
        "  background: rgba(224, 234, 246, 1.0);"
        "}"
        "#CodeOpenWithButton #OpenWithPrimaryButton {"
        "  border-top-left-radius: 10px;"
        "  border-bottom-left-radius: 10px;"
        "  border-top-right-radius: 0px;"
        "  border-bottom-right-radius: 0px;"
        "  min-width: 28px;"
        "}"
        "#CodeOpenWithButton #OpenWithExpandButton {"
        "  border-left: none;"
        "  border-top-left-radius: 0px;"
        "  border-bottom-left-radius: 0px;"
        "  border-top-right-radius: 10px;"
        "  border-bottom-right-radius: 10px;"
        "  min-width: 22px;"
        "  padding-left: 5px;"
        "  padding-right: 5px;"
        "}"
        "#CodeStatus {"
        "  color: #6e4f12;"
        "  background: rgba(255, 239, 206, 0.92);"
        "  border: 1px solid rgba(232, 201, 131, 0.95);"
        "  border-radius: 12px;"
        "  padding: 10px 14px;"
        "}"
        "#CodeContentStack {"
        "  background: transparent;"
        "}"
        "#CodeLoadingCard {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:1,"
        "      stop:0 rgba(248, 250, 253, 0.98),"
        "      stop:1 rgba(240, 244, 249, 0.98));"
        "  border: 1px solid rgba(206, 216, 227, 0.96);"
        "  border-radius: 18px;"
        "}"
        "#CodeLoadingTitle {"
        "  color: #10273f;"
        "}"
        "#CodeLoadingMessage {"
        "  color: #5c7187;"
        "}"
        "#CodeContent {"
        "  background: #f4f7fb;"
        "  border: 1px solid #ccd6e2;"
        "  border-radius: 18px;"
        "  color: #24292e;"
        "  selection-background-color: #dee6fc;"
        "  selection-color: #24292e;"
        "}"
        "#CodeContent QScrollBar:vertical {"
        "  background: rgba(232, 238, 245, 0.8);"
        "  border: none;"
        "  width: 8px;"
        "  margin: 10px 0 10px 0;"
        "  border-radius: 4px;"
        "}"
        "#CodeContent QScrollBar::handle:vertical {"
        "  background: #7c8fa8;"
        "  min-height: 52px;"
        "  border-radius: 4px;"
        "}"
        "#CodeContent QScrollBar::handle:vertical:hover {"
        "  background: #6d829d;"
        "}"
        "#CodeContent QScrollBar::handle:vertical:pressed {"
        "  background: #61768f;"
        "}"
        "#CodeContent QScrollBar:horizontal {"
        "  background: rgba(232, 238, 245, 0.8);"
        "  border: none;"
        "  height: 8px;"
        "  margin: 0 10px 0 10px;"
        "  border-radius: 4px;"
        "}"
        "#CodeContent QScrollBar::handle:horizontal {"
        "  background: #7c8fa8;"
        "  min-width: 52px;"
        "  border-radius: 4px;"
        "}"
        "#CodeContent QScrollBar::handle:horizontal:hover {"
        "  background: #6d829d;"
        "}"
        "#CodeContent QScrollBar::handle:horizontal:pressed {"
        "  background: #61768f;"
        "}"
        "#CodeContent QScrollBar::add-line:vertical, #CodeContent QScrollBar::sub-line:vertical,"
        "#CodeContent QScrollBar::add-line:horizontal, #CodeContent QScrollBar::sub-line:horizontal {"
        "  width: 0px;"
        "  height: 0px;"
        "}"
        "#CodeContent QScrollBar::add-page:vertical, #CodeContent QScrollBar::sub-page:vertical,"
        "#CodeContent QScrollBar::add-page:horizontal, #CodeContent QScrollBar::sub-page:horizontal {"
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

void CodeRenderer::ensureRepository()
{
    if (!m_repository) {
        m_repository = std::make_unique<KSyntaxHighlighting::Repository>();
        const QString searchRoot = CodeThemeManager::themeSearchRoot();
        if (!searchRoot.trimmed().isEmpty()) {
            m_repository->addCustomSearchPath(searchRoot);
        }
    }

    if (!m_highlighter) {
        m_highlighter = new KSyntaxHighlighting::SyntaxHighlighter(m_textEdit->document());
        m_highlighter->setTheme(CodeThemeManager::resolveTheme(*m_repository));
    }
}

void CodeRenderer::applyDefinitionForPath(const QString& filePath)
{
    if (!m_repository || !m_highlighter) {
        return;
    }

    const KSyntaxHighlighting::Definition definition = m_repository->definitionForFileName(filePath);
    if (!definition.isValid()) {
        qDebug().noquote() << QStringLiteral("[SpaceLookRender] CodeRenderer no syntax definition for: %1").arg(filePath);
        return;
    }

    qDebug().noquote() << QStringLiteral("[SpaceLookRender] CodeRenderer definition=\"%1\"").arg(definition.name());
    m_highlighter->setDefinition(definition);
    Q_UNUSED(definition);
}
