#include "renderers/summary/ArchiveRenderer.h"

#include <QDebug>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHash>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIcon>
#include <QImageReader>
#include <QLabel>
#include <QProcess>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSet>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QTextCodec>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

#include "renderers/FileTypeIconResolver.h"
#include "renderers/OpenWithButton.h"
#include "renderers/PreviewHeaderBar.h"
#include "renderers/PreviewStateVisuals.h"
#include "renderers/SelectableTitleLabel.h"
#include "widgets/SpaceLookWindow.h"

namespace {

QString normalizedArchivePath(const QString& path)
{
    QString normalized = path.trimmed();
    normalized.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (normalized.startsWith(QStringLiteral("./"))) {
        normalized.remove(0, 2);
    }
    while (normalized.startsWith(QLatin1Char('/'))) {
        normalized.remove(0, 1);
    }
    return normalized;
}

QIcon iconForArchiveEntry(const QString& entryPath, bool isDirectory)
{
    HoveredItemInfo info;
    info.filePath = entryPath;
    info.isDirectory = isDirectory;
    info.typeKey = isDirectory ? QStringLiteral("folder") : QStringLiteral("file");
    return FileTypeIconResolver::iconForInfo(info);
}

bool isPreviewableTextEntry(const QString& entryPath)
{
    const QString suffix = QFileInfo(entryPath).suffix().toLower();
    static const QSet<QString> suffixes = {
        QStringLiteral("txt"), QStringLiteral("log"), QStringLiteral("md"), QStringLiteral("markdown"),
        QStringLiteral("json"), QStringLiteral("jsonc"), QStringLiteral("xml"), QStringLiteral("yaml"),
        QStringLiteral("yml"), QStringLiteral("toml"), QStringLiteral("ini"), QStringLiteral("conf"),
        QStringLiteral("config"), QStringLiteral("cfg"), QStringLiteral("env"), QStringLiteral("csv"),
        QStringLiteral("tsv"), QStringLiteral("properties"), QStringLiteral("html"), QStringLiteral("htm"),
        QStringLiteral("css"), QStringLiteral("scss"), QStringLiteral("sass"), QStringLiteral("less"),
        QStringLiteral("js"), QStringLiteral("mjs"), QStringLiteral("cjs"), QStringLiteral("jsx"),
        QStringLiteral("ts"), QStringLiteral("tsx"), QStringLiteral("vue"), QStringLiteral("svelte"),
        QStringLiteral("py"), QStringLiteral("rb"), QStringLiteral("php"), QStringLiteral("sh"),
        QStringLiteral("bash"), QStringLiteral("zsh"), QStringLiteral("fish"), QStringLiteral("ps1"),
        QStringLiteral("sql"), QStringLiteral("c"), QStringLiteral("cc"), QStringLiteral("cpp"),
        QStringLiteral("cxx"), QStringLiteral("h"), QStringLiteral("hpp"), QStringLiteral("cs"),
        QStringLiteral("java"), QStringLiteral("kt"), QStringLiteral("swift"), QStringLiteral("go"),
        QStringLiteral("rs"), QStringLiteral("qss"), QStringLiteral("qml"), QStringLiteral("pro"),
        QStringLiteral("pri"), QStringLiteral("cmake"), QStringLiteral("gitignore"), QStringLiteral("gitattributes")
    };
    return suffixes.contains(suffix);
}

bool isPreviewableImageEntry(const QString& entryPath)
{
    const QString suffix = QFileInfo(entryPath).suffix().toLower();
    static const QSet<QString> suffixes = {
        QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("jpe"),
        QStringLiteral("bmp"), QStringLiteral("dib"), QStringLiteral("gif"), QStringLiteral("webp"),
        QStringLiteral("tif"), QStringLiteral("tiff"), QStringLiteral("svg"), QStringLiteral("ico"),
        QStringLiteral("tga")
    };
    return suffixes.contains(suffix);
}

QString archiveSizeText(qint64 size)
{
    if (size < 0) {
        return QString();
    }
    if (size < 1024) {
        return QStringLiteral("%1 B").arg(size);
    }
    if (size < 1024 * 1024) {
        return QStringLiteral("%1 KB").arg(QString::number(size / 1024.0, 'f', 1));
    }
    if (size < 1024ll * 1024ll * 1024ll) {
        return QStringLiteral("%1 MB").arg(QString::number(size / 1024.0 / 1024.0, 'f', 1));
    }
    return QStringLiteral("%1 GB").arg(QString::number(size / 1024.0 / 1024.0 / 1024.0, 'f', 1));
}

QString textFromArchiveBytes(const QByteArray& data)
{
    if (data.startsWith("\xEF\xBB\xBF")) {
        return QString::fromUtf8(data.constData() + 3, data.size() - 3);
    }
    if (data.size() >= 2) {
        const uchar first = static_cast<uchar>(data.at(0));
        const uchar second = static_cast<uchar>(data.at(1));
        if (first == 0xff && second == 0xfe) {
            return QString::fromUtf16(reinterpret_cast<const char16_t*>(data.constData() + 2), (data.size() - 2) / 2);
        }
        if (first == 0xfe && second == 0xff) {
            QTextCodec* codec = QTextCodec::codecForName("UTF-16BE");
            return codec ? codec->toUnicode(data.constData() + 2, data.size() - 2) : QString::fromUtf8(data);
        }
    }
    QTextCodec::ConverterState state;
    QTextCodec* utf8 = QTextCodec::codecForName("UTF-8");
    const QString decoded = utf8 ? utf8->toUnicode(data.constData(), data.size(), &state) : QString::fromUtf8(data);
    if (state.invalidChars == 0) {
        return decoded;
    }
    QTextCodec* localCodec = QTextCodec::codecForLocale();
    return localCodec ? localCodec->toUnicode(data) : QString::fromLocal8Bit(data);
}

QString bundledSevenZipPath()
{
    const QString applicationDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(applicationDir).filePath(QStringLiteral("7zip/7z.exe")),
        QDir(applicationDir).filePath(QStringLiteral("7z.exe")),
#ifdef SPACELOOK_PROJECT_DIR
        QDir(QStringLiteral(SPACELOOK_PROJECT_DIR)).filePath(QStringLiteral("third_party/7zip/private/7z.exe")),
#endif
        QDir(QDir::currentPath()).filePath(QStringLiteral("third_party/7zip/private/7z.exe"))
    };

    for (const QString& candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return QDir::toNativeSeparators(candidate);
        }
    }

    return QString();
}

}

ArchiveRenderer::ArchiveRenderer(QWidget* parent)
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
    , m_contentSplitter(new QSplitter(Qt::Horizontal, this))
    , m_treeWidget(new QTreeWidget(m_contentSplitter))
    , m_previewStack(new QStackedWidget(m_contentSplitter))
    , m_emptyPreviewLabel(new QLabel(m_previewStack))
    , m_textPreview(new QTextEdit(m_previewStack))
    , m_imageScrollArea(new QScrollArea(m_previewStack))
    , m_imagePreview(new QLabel(m_imageScrollArea))
{
    setAttribute(Qt::WA_StyledBackground, true);
    setObjectName(QStringLiteral("ArchiveRendererRoot"));
    m_headerRow->setObjectName(QStringLiteral("ArchiveHeaderRow"));
    m_iconLabel->setObjectName(QStringLiteral("ArchiveTypeIcon"));
    m_titleLabel->setObjectName(QStringLiteral("ArchiveTitle"));
    m_metaLabel->setObjectName(QStringLiteral("ArchiveMeta"));
    m_pathRow->setObjectName(QStringLiteral("ArchivePathRow"));
    m_pathTitleLabel->setObjectName(QStringLiteral("ArchivePathTitle"));
    m_pathValueLabel->setObjectName(QStringLiteral("ArchivePathValue"));
    m_openWithButton->setObjectName(QStringLiteral("ArchiveOpenWithButton"));
    m_statusLabel->setObjectName(QStringLiteral("ArchiveStatus"));
    m_contentSplitter->setObjectName(QStringLiteral("ArchiveContentSplitter"));
    m_treeWidget->setObjectName(QStringLiteral("ArchiveTree"));
    m_previewStack->setObjectName(QStringLiteral("ArchivePreviewStack"));
    m_emptyPreviewLabel->setObjectName(QStringLiteral("ArchiveEmptyPreview"));
    m_textPreview->setObjectName(QStringLiteral("ArchiveTextPreview"));
    m_imageScrollArea->setObjectName(QStringLiteral("ArchiveImageScrollArea"));
    m_imagePreview->setObjectName(QStringLiteral("ArchiveImagePreview"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 0, 12, 12);
    layout->setSpacing(12);
    layout->addWidget(m_headerRow);
    layout->addWidget(m_statusLabel);
    layout->addWidget(m_contentSplitter, 1);

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
    m_pathTitleLabel->hide();
    m_pathValueLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_pathValueLabel->setWordWrap(true);
    m_pathValueLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_pathRow->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    PreviewStateVisuals::prepareStatusLabel(m_statusLabel);
    m_statusLabel->hide();

    m_treeWidget->setColumnCount(4);
    m_treeWidget->setHeaderLabels({
        QCoreApplication::translate("SpaceLook", "Name"),
        QCoreApplication::translate("SpaceLook", "Size"),
        QCoreApplication::translate("SpaceLook", "Packed"),
        QCoreApplication::translate("SpaceLook", "Modified")
    });
    m_treeWidget->header()->setStretchLastSection(true);
    m_treeWidget->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_treeWidget->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_treeWidget->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_treeWidget->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_treeWidget->setRootIsDecorated(true);
    m_treeWidget->setUniformRowHeights(false);
    m_treeWidget->setAlternatingRowColors(false);
    m_treeWidget->setAnimated(true);
    m_treeWidget->setIndentation(20);

    m_emptyPreviewLabel->setAlignment(Qt::AlignCenter);
    m_emptyPreviewLabel->setWordWrap(true);
    m_emptyPreviewLabel->setText(QCoreApplication::translate("SpaceLook", "Select a text or image file inside the archive to preview it."));
    m_textPreview->setReadOnly(true);
    m_textPreview->setLineWrapMode(QTextEdit::NoWrap);
    m_imagePreview->setAlignment(Qt::AlignCenter);
    m_imagePreview->setBackgroundRole(QPalette::Base);
    m_imageScrollArea->setWidget(m_imagePreview);
    m_imageScrollArea->setWidgetResizable(true);
    m_imageScrollArea->setAlignment(Qt::AlignCenter);
    m_previewStack->addWidget(m_emptyPreviewLabel);
    m_previewStack->addWidget(m_textPreview);
    m_previewStack->addWidget(m_imageScrollArea);
    m_contentSplitter->addWidget(m_treeWidget);
    m_contentSplitter->addWidget(m_previewStack);
    m_contentSplitter->setStretchFactor(0, 3);
    m_contentSplitter->setStretchFactor(1, 2);
    m_contentSplitter->setSizes({ 620, 420 });

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
    connect(m_treeWidget, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item, int) {
        if (!item) {
            return;
        }
        if (item->data(0, Qt::UserRole).toBool()) {
            item->setExpanded(!item->isExpanded());
            clearEntryPreview();
            return;
        }
        previewArchiveEntry(item);
    });

    applyChrome();
}

QString ArchiveRenderer::rendererId() const
{
    return QStringLiteral("archive");
}

bool ArchiveRenderer::canHandle(const HoveredItemInfo& info) const
{
    return info.typeKey == QStringLiteral("archive");
}

QWidget* ArchiveRenderer::widget()
{
    return this;
}

bool ArchiveRenderer::reportsLoadingState() const
{
    return true;
}

void ArchiveRenderer::setLoadingStateCallback(std::function<void(bool)> callback)
{
    m_loadingStateCallback = std::move(callback);
}

void ArchiveRenderer::load(const HoveredItemInfo& info)
{
    cancelPreviewTask(m_cancelToken);
    const PreviewCancellationToken cancelToken = makePreviewCancellationToken();
    m_cancelToken = cancelToken;
    m_info = info;
    const PreviewLoadGuard::Token loadToken = m_loadGuard.begin(info.filePath);
    notifyLoadingState(true);

    qDebug().noquote() << QStringLiteral("[SpaceLookRender] ArchiveRenderer load path=\"%1\"").arg(info.filePath);

    m_titleLabel->setText(info.title.isEmpty() ? QCoreApplication::translate("SpaceLook", "Archive Preview") : info.title);
    m_titleLabel->setCopyText(m_titleLabel->text());
    m_iconLabel->setPixmap(FileTypeIconResolver::pixmapForInfo(info, m_iconLabel->contentsRect().size()));
    m_pathValueLabel->setText(info.filePath.trimmed().isEmpty() ? QCoreApplication::translate("SpaceLook", "(Unavailable)") : info.filePath);
    m_openWithButton->setTargetContext(info.filePath, info.typeKey);
    m_treeWidget->clear();
    PreviewStateVisuals::showStatus(m_statusLabel, QCoreApplication::translate("SpaceLook", "Loading archive contents..."), PreviewStateVisuals::Kind::Loading);

    auto* watcher = new QFutureWatcher<ArchiveLoadResult>(this);
    connect(watcher, &QFutureWatcher<ArchiveLoadResult>::finished, this, [this, watcher, loadToken, cancelToken]() {
        watcher->deleteLater();

        if (previewCancellationRequested(cancelToken) || !m_loadGuard.isCurrent(loadToken, m_info.filePath)) {
            qDebug().noquote() << QStringLiteral("[SpaceLookRender] ArchiveRenderer discarded stale result path=\"%1\"")
                .arg(loadToken.path);
            return;
        }

        const ArchiveLoadResult result = watcher->result();
        if (!result.success) {
            PreviewStateVisuals::showStatus(m_statusLabel, result.statusMessage, PreviewStateVisuals::Kind::Error);
            notifyLoadingState(false);
            return;
        }

        populateTree(result.entries);
        if (result.entries.isEmpty()) {
            PreviewStateVisuals::showStatus(m_statusLabel, QCoreApplication::translate("SpaceLook", "The archive is empty."), PreviewStateVisuals::Kind::Empty);
        } else {
            PreviewStateVisuals::showStatus(
                m_statusLabel,
                QCoreApplication::translate("SpaceLook", "Loaded %1 entries. Click folders to expand.").arg(result.entries.size()),
                PreviewStateVisuals::Kind::Success);
        }

        qDebug().noquote() << QStringLiteral("[SpaceLookRender] ArchiveRenderer entries=%1 path=\"%2\"")
            .arg(result.entries.size())
            .arg(loadToken.path);
        notifyLoadingState(false);
    });

    watcher->setFuture(QtConcurrent::run([filePath = info.filePath, cancelToken]() {
        return loadArchiveEntries(filePath, cancelToken);
    }));
}

void ArchiveRenderer::unload()
{
    cancelPreviewTask(m_cancelToken);
    cancelPreviewTask(m_entryPreviewCancelToken);
    m_loadGuard.cancel();
    notifyLoadingState(false);
    m_treeWidget->clear();
    clearEntryPreview();
    m_pathValueLabel->clear();
    m_openWithButton->setTargetContext(QString(), QString());
    PreviewStateVisuals::clearStatus(m_statusLabel);
    m_info = HoveredItemInfo();
}

void ArchiveRenderer::notifyLoadingState(bool loading)
{
    if (m_loadingStateCallback) {
        m_loadingStateCallback(loading);
    }
}

void ArchiveRenderer::applyChrome()
{
    setStyleSheet(
        "#ArchiveRendererRoot {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:1,"
        "      stop:0 #fcfdff,"
        "      stop:1 #f4f8fc);"
        "  border-radius: 0px;"
        "}"
        "QLabel {"
        "  color: #18324a;"
        "}"
        "#ArchiveTypeIcon {"
        "  background: transparent;"
        "  border: none;"
        "  padding: 0px;"
        "}"
        "#ArchiveTitle {"
        "  color: #0f2740;"
        "}"
        "#ArchiveMeta {"
        "  color: #5c738b;"
        "}"
        "#ArchivePathTitle {"
        "  color: #16324a;"
        "  font-family: 'Segoe UI Rounded';"
        "}"
        "#ArchivePathValue {"
        "  color: #445d76;"
        "}"
        "#ArchiveOpenWithButton QToolButton {"
        "  background: rgba(238, 244, 252, 0.96);"
        "  color: #17324b;"
        "  border: 1px solid rgba(193, 208, 224, 0.95);"
        "  padding: 3px 8px;"
        "  min-height: 24px;"
        "}"
        "#ArchiveOpenWithButton QToolButton:hover {"
        "  background: rgba(245, 249, 255, 1.0);"
        "}"
        "#ArchiveOpenWithButton QToolButton:pressed {"
        "  background: rgba(224, 234, 246, 1.0);"
        "}"
        "#ArchiveOpenWithButton #OpenWithPrimaryButton {"
        "  border-top-left-radius: 10px;"
        "  border-bottom-left-radius: 10px;"
        "  border-top-right-radius: 0px;"
        "  border-bottom-right-radius: 0px;"
        "  min-width: 28px;"
        "}"
        "#ArchiveOpenWithButton #OpenWithExpandButton {"
        "  border-left: none;"
        "  border-top-left-radius: 0px;"
        "  border-bottom-left-radius: 0px;"
        "  border-top-right-radius: 10px;"
        "  border-bottom-right-radius: 10px;"
        "  min-width: 22px;"
        "  padding-left: 5px;"
        "  padding-right: 5px;"
        "}"
        "#ArchiveStatus {"
        "  color: #27568b;"
        "  background: rgba(220, 235, 255, 0.92);"
        "  border: 1px solid rgba(164, 193, 229, 0.95);"
        "  border-radius: 12px;"
        "  padding: 10px 14px;"
        "}"
        "#ArchiveTree {"
        "  background: #f4f7fb;"
        "  border: 1px solid #ccd6e2;"
        "  border-radius: 18px;"
        "  color: #18324a;"
        "  padding: 8px;"
        "  outline: none;"
        "}"
        "#ArchiveTree::item {"
        "  min-height: 28px;"
        "}"
        "#ArchiveTree::item:selected {"
        "  background: rgba(126, 188, 255, 0.28);"
        "  color: #08233b;"
        "}"
        "#ArchiveTree QHeaderView::section {"
        "  background: rgba(232, 239, 247, 0.96);"
        "  color: #35506b;"
        "  border: none;"
        "  border-bottom: 1px solid rgba(204, 214, 226, 0.95);"
        "  padding: 8px 10px;"
        "  font-family: 'Segoe UI Rounded';"
        "}"
        "#ArchivePreviewStack {"
        "  background: #f7fafd;"
        "  border: 1px solid #ccd6e2;"
        "  border-radius: 18px;"
        "}"
        "#ArchiveEmptyPreview {"
        "  color: #5b7188;"
        "  padding: 24px;"
        "  background: transparent;"
        "}"
        "#ArchiveTextPreview {"
        "  background: #fbfdff;"
        "  color: #102c45;"
        "  border: 1px solid #ccd6e2;"
        "  border-radius: 18px;"
        "  padding: 12px;"
        "  selection-background-color: rgba(0, 120, 212, 0.24);"
        "}"
        "#ArchiveImageScrollArea {"
        "  background: #fbfdff;"
        "  border: 1px solid #ccd6e2;"
        "  border-radius: 18px;"
        "}"
        "#ArchiveImagePreview {"
        "  background: transparent;"
        "  color: #5b7188;"
        "}"
        "#ArchiveTree QScrollBar:vertical {"
        "  background: rgba(232, 238, 245, 0.8);"
        "  border: none;"
        "  width: 8px;"
        "  margin: 10px 0 10px 0;"
        "  border-radius: 4px;"
        "}"
        "#ArchiveTree QScrollBar::handle:vertical {"
        "  background: #7c8fa8;"
        "  min-height: 52px;"
        "  border-radius: 4px;"
        "}"
        "#ArchiveTree QScrollBar::handle:vertical:hover {"
        "  background: #6d829d;"
        "}"
        "#ArchiveTree QScrollBar::handle:vertical:pressed {"
        "  background: #61768f;"
        "}"
        "#ArchiveTree QScrollBar:horizontal {"
        "  background: rgba(232, 238, 245, 0.8);"
        "  border: none;"
        "  height: 8px;"
        "  margin: 0 10px 0 10px;"
        "  border-radius: 4px;"
        "}"
        "#ArchiveTree QScrollBar::handle:horizontal {"
        "  background: #7c8fa8;"
        "  min-width: 52px;"
        "  border-radius: 4px;"
        "}"
        "#ArchiveTree QScrollBar::handle:horizontal:hover {"
        "  background: #6d829d;"
        "}"
        "#ArchiveTree QScrollBar::handle:horizontal:pressed {"
        "  background: #61768f;"
        "}"
        "#ArchiveTree QScrollBar::add-line:vertical, #ArchiveTree QScrollBar::sub-line:vertical,"
        "#ArchiveTree QScrollBar::add-line:horizontal, #ArchiveTree QScrollBar::sub-line:horizontal {"
        "  width: 0px;"
        "  height: 0px;"
        "}"
        "#ArchiveTree QScrollBar::add-page:vertical, #ArchiveTree QScrollBar::sub-page:vertical,"
        "#ArchiveTree QScrollBar::add-page:horizontal, #ArchiveTree QScrollBar::sub-page:horizontal {"
        "  background: transparent;"
        "}"
    );

    QFont titleFont;
    titleFont.setFamily(QStringLiteral("Segoe UI Rounded"));
    titleFont.setPixelSize(20);
    titleFont.setWeight(QFont::Bold);
    m_titleLabel->setFont(titleFont);

    QFont metaFont;
    metaFont.setFamily(QStringLiteral("Segoe UI Rounded"));
    metaFont.setPixelSize(13);
    m_metaLabel->setFont(metaFont);
    m_pathTitleLabel->setFont(metaFont);
    m_pathValueLabel->setFont(metaFont);
    m_statusLabel->setFont(metaFont);
    m_emptyPreviewLabel->setFont(metaFont);

    QFont treeFont;
    treeFont.setFamily(QStringLiteral("Segoe UI Rounded"));
    treeFont.setPixelSize(13);
    m_treeWidget->setFont(treeFont);

    QFont previewFont;
    previewFont.setFamily(QStringLiteral("Cascadia Mono"));
    previewFont.setPixelSize(12);
    m_textPreview->setFont(previewFont);
}

void ArchiveRenderer::showStatusMessage(const QString& message)
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

void ArchiveRenderer::populateTree(const QVector<ArchiveEntry>& entries)
{
    m_treeWidget->clear();
    clearEntryPreview();

    QHash<QString, QTreeWidgetItem*> folderItems;
    for (const ArchiveEntry& entry : entries) {
        const QString normalizedPath = normalizedArchivePath(entry.path);
        if (normalizedPath.isEmpty()) {
            continue;
        }

        const QStringList parts = normalizedPath.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        if (parts.isEmpty()) {
            continue;
        }

        QString currentPath;
        QTreeWidgetItem* parentItem = nullptr;
        for (int index = 0; index < parts.size(); ++index) {
            const QString& part = parts.at(index);
            currentPath = currentPath.isEmpty() ? part : currentPath + QLatin1Char('/') + part;
            const bool isLeaf = index == parts.size() - 1;
            const bool shouldBeFolder = !isLeaf || entry.isDirectory;

            if (shouldBeFolder) {
                parentItem = ensureFolderItem(currentPath, parentItem, part);
                continue;
            }

            auto* item = new QTreeWidgetItem();
            item->setText(0, part);
            item->setText(1, entry.sizeText.isEmpty() ? archiveSizeText(entry.size) : entry.sizeText);
            item->setText(2, entry.packedSizeText);
            item->setText(3, entry.modifiedText);
            item->setIcon(0, iconForArchiveEntry(currentPath, false));
            item->setData(0, Qt::UserRole, false);
            item->setData(0, Qt::UserRole + 1, currentPath);
            item->setData(0, Qt::UserRole + 2, QVariant::fromValue(entry.size));
            if (parentItem) {
                parentItem->addChild(item);
            } else {
                m_treeWidget->addTopLevelItem(item);
            }
        }
    }

    for (int index = 0; index < m_treeWidget->topLevelItemCount(); ++index) {
        if (QTreeWidgetItem* item = m_treeWidget->topLevelItem(index)) {
            item->setExpanded(false);
        }
    }
}

void ArchiveRenderer::clearEntryPreview(const QString& message)
{
    cancelPreviewTask(m_entryPreviewCancelToken);
    m_textPreview->clear();
    m_imagePreview->clear();
    m_emptyPreviewLabel->setText(message.trimmed().isEmpty()
        ? QCoreApplication::translate("SpaceLook", "Select a text or image file inside the archive to preview it.")
        : message);
    m_previewStack->setCurrentWidget(m_emptyPreviewLabel);
}

void ArchiveRenderer::previewArchiveEntry(QTreeWidgetItem* item)
{
    if (!item) {
        clearEntryPreview();
        return;
    }

    const QString entryPath = item->data(0, Qt::UserRole + 1).toString();
    const bool isDirectory = item->data(0, Qt::UserRole).toBool();
    if (entryPath.trimmed().isEmpty() || isDirectory) {
        clearEntryPreview();
        return;
    }

    if (!isPreviewableTextEntry(entryPath) && !isPreviewableImageEntry(entryPath)) {
        clearEntryPreview(QCoreApplication::translate("SpaceLook", "Preview is available for text and image files inside the archive."));
        return;
    }

    const qint64 size = item->data(0, Qt::UserRole + 2).toLongLong();
    if (size > 8ll * 1024ll * 1024ll) {
        clearEntryPreview(QCoreApplication::translate("SpaceLook", "This archive entry is too large for inline preview."));
        return;
    }

    clearEntryPreview(QCoreApplication::translate("SpaceLook", "Loading entry preview..."));
    const PreviewCancellationToken cancelToken = makePreviewCancellationToken();
    m_entryPreviewCancelToken = cancelToken;

    ArchiveEntry entry;
    entry.path = entryPath;
    entry.size = size;
    auto* watcher = new QFutureWatcher<EntryPreviewResult>(this);
    connect(watcher, &QFutureWatcher<EntryPreviewResult>::finished, this, [this, watcher, cancelToken, requestedPath = entryPath]() {
        watcher->deleteLater();
        if (previewCancellationRequested(cancelToken)) {
            return;
        }

        const EntryPreviewResult result = watcher->result();
        if (result.entryPath != requestedPath) {
            return;
        }

        if (!result.success) {
            clearEntryPreview(result.statusMessage);
            return;
        }

        if (result.isImage) {
            QPixmap pixmap;
            if (!pixmap.loadFromData(result.data)) {
                clearEntryPreview(QCoreApplication::translate("SpaceLook", "This image entry could not be decoded."));
                return;
            }
            m_imagePreview->setPixmap(pixmap);
            m_imagePreview->resize(pixmap.size());
            m_previewStack->setCurrentWidget(m_imageScrollArea);
            return;
        }

        if (result.isText) {
            m_textPreview->setPlainText(textFromArchiveBytes(result.data));
            m_previewStack->setCurrentWidget(m_textPreview);
            return;
        }

        clearEntryPreview(QCoreApplication::translate("SpaceLook", "Preview is available for text and image files inside the archive."));
    });

    watcher->setFuture(QtConcurrent::run([archivePath = m_info.filePath, entry, cancelToken]() {
        return loadEntryPreview(archivePath, entry, cancelToken);
    }));
}

QTreeWidgetItem* ArchiveRenderer::ensureFolderItem(const QString& folderPath,
                                                   QTreeWidgetItem* parentItem,
                                                   const QString& folderName)
{
    QTreeWidgetItem* existingItem = nullptr;
    const QList<QTreeWidgetItem*> matches = m_treeWidget->findItems(folderName, Qt::MatchExactly | Qt::MatchRecursive, 0);
    for (QTreeWidgetItem* candidate : matches) {
        if (candidate && candidate->data(0, Qt::UserRole + 1).toString() == folderPath) {
            existingItem = candidate;
            break;
        }
    }
    if (existingItem) {
        return existingItem;
    }

    auto* item = new QTreeWidgetItem();
    item->setText(0, folderName);
    item->setIcon(0, iconForArchiveEntry(folderPath, true));
    item->setData(0, Qt::UserRole, true);
    item->setData(0, Qt::UserRole + 1, folderPath);
    if (parentItem) {
        parentItem->addChild(item);
    } else {
        m_treeWidget->addTopLevelItem(item);
    }
    return item;
}

ArchiveRenderer::ArchiveLoadResult ArchiveRenderer::loadArchiveEntries(const QString& filePath,
                                                                       const PreviewCancellationToken& cancelToken)
{
    ArchiveLoadResult result;
    if (previewCancellationRequested(cancelToken)) {
        result.statusMessage = QCoreApplication::translate("SpaceLook", "Archive preview was canceled.");
        return result;
    }

    const QString trimmedPath = filePath.trimmed();
    if (trimmedPath.isEmpty()) {
        result.statusMessage = QCoreApplication::translate("SpaceLook", "Archive path is unavailable.");
        return result;
    }

    auto runProcess = [&cancelToken](const QString& program, const QStringList& arguments, QByteArray* stdOutput, QString* errorMessage) {
        QProcess process;
        process.setProgram(program);
        process.setArguments(arguments);
        process.start();
        if (!process.waitForStarted(5000)) {
            if (errorMessage) {
                *errorMessage = QCoreApplication::translate("SpaceLook", "Failed to start archive listing tool: %1").arg(program);
            }
            return false;
        }

        QElapsedTimer timeout;
        timeout.start();
        while (!process.waitForFinished(100)) {
            if (previewCancellationRequested(cancelToken)) {
                process.kill();
                process.waitForFinished(1000);
                if (errorMessage) {
                    *errorMessage = QCoreApplication::translate("SpaceLook", "Archive preview was canceled.");
                }
                return false;
            }

            if (timeout.elapsed() < 15000) {
                continue;
            }

            process.kill();
            process.waitForFinished(3000);
            if (errorMessage) {
                *errorMessage = QCoreApplication::translate("SpaceLook", "Timed out while reading archive contents.");
            }
            return false;
        }

        if (stdOutput) {
            *stdOutput = process.readAllStandardOutput();
        }

        const QString stdError = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
            if (errorMessage) {
                *errorMessage = stdError.isEmpty()
                    ? QCoreApplication::translate("SpaceLook", "This archive format could not be listed.")
                    : stdError;
            }
            return false;
        }

        return true;
    };

    QByteArray sevenZipOutput;
    QString sevenZipError;
    const QString sevenZipPath = bundledSevenZipPath();
    if (sevenZipPath.isEmpty()) {
        result.statusMessage = QCoreApplication::translate("SpaceLook", "Bundled 7-Zip runtime was not found.");
        return result;
    }

    if (runProcess(sevenZipPath,
                   { QStringLiteral("l"), QStringLiteral("-slt"), QStringLiteral("-ba"), QStringLiteral("-sccUTF-8"), QDir::toNativeSeparators(trimmedPath) },
                   &sevenZipOutput,
                   &sevenZipError)) {
        const QString stdOutput = QString::fromUtf8(sevenZipOutput);
        ArchiveEntry currentEntry;
        bool hasCurrentEntry = false;
        const QStringList lines = stdOutput.split(QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::KeepEmptyParts);
        for (const QString& rawLine : lines) {
            if (previewCancellationRequested(cancelToken)) {
                result.entries.clear();
                result.statusMessage = QCoreApplication::translate("SpaceLook", "Archive preview was canceled.");
                return result;
            }

            if (rawLine.startsWith(QStringLiteral("Path = "))) {
                if (hasCurrentEntry && !normalizedArchivePath(currentEntry.path).isEmpty()) {
                    currentEntry.path = normalizedArchivePath(currentEntry.path);
                    result.entries.append(currentEntry);
                }

                currentEntry = ArchiveEntry();
                currentEntry.path = rawLine.mid(QStringLiteral("Path = ").size()).trimmed();
                hasCurrentEntry = true;
                continue;
            }

            if (hasCurrentEntry && rawLine.startsWith(QStringLiteral("Folder = "))) {
                currentEntry.isDirectory = rawLine.mid(QStringLiteral("Folder = ").size()).trimmed() == QStringLiteral("+");
                continue;
            }

            if (hasCurrentEntry && rawLine.startsWith(QStringLiteral("Size = "))) {
                bool ok = false;
                const qint64 size = rawLine.mid(QStringLiteral("Size = ").size()).trimmed().toLongLong(&ok);
                if (ok) {
                    currentEntry.size = size;
                    currentEntry.sizeText = archiveSizeText(size);
                }
                continue;
            }

            if (hasCurrentEntry && rawLine.startsWith(QStringLiteral("Packed Size = "))) {
                bool ok = false;
                const qint64 packedSize = rawLine.mid(QStringLiteral("Packed Size = ").size()).trimmed().toLongLong(&ok);
                currentEntry.packedSizeText = ok ? archiveSizeText(packedSize) : QString();
                continue;
            }

            if (hasCurrentEntry && rawLine.startsWith(QStringLiteral("Modified = "))) {
                currentEntry.modifiedText = rawLine.mid(QStringLiteral("Modified = ").size()).trimmed();
            }
        }

        if (hasCurrentEntry && !normalizedArchivePath(currentEntry.path).isEmpty()) {
            currentEntry.path = normalizedArchivePath(currentEntry.path);
            result.entries.append(currentEntry);
        }

        if (result.entries.isEmpty() && !stdOutput.trimmed().isEmpty()) {
            ArchiveEntry streamEntry;
            streamEntry.path = QFileInfo(trimmedPath).completeBaseName();
            if (streamEntry.path.trimmed().isEmpty()) {
                streamEntry.path = QFileInfo(trimmedPath).fileName();
            }
            streamEntry.isDirectory = false;
            result.entries.append(streamEntry);
        }

        result.success = true;
        return result;
    }

    QByteArray tarOutput;
    QString tarError;
    if (!runProcess(QStringLiteral("tar"),
                    { QStringLiteral("-tf"), QDir::toNativeSeparators(trimmedPath) },
                    &tarOutput,
                    &tarError)) {
        result.statusMessage = sevenZipError.isEmpty() ? tarError : sevenZipError;
        if (!tarError.isEmpty() && tarError != result.statusMessage) {
            result.statusMessage += QStringLiteral("\n") + tarError;
        }
        qDebug().noquote() << QStringLiteral("[SpaceLookRender] ArchiveRenderer listing failed path=\"%1\" error=\"%2\"")
            .arg(trimmedPath, result.statusMessage);
        return result;
    }

    const QString stdOutput = QString::fromLocal8Bit(tarOutput);
    const QStringList lines = stdOutput.split(QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts);
    for (const QString& rawLine : lines) {
        if (previewCancellationRequested(cancelToken)) {
            result.entries.clear();
            result.statusMessage = QCoreApplication::translate("SpaceLook", "Archive preview was canceled.");
            return result;
        }

        const QString normalizedPath = normalizedArchivePath(rawLine);
        if (normalizedPath.isEmpty()) {
            continue;
        }

        ArchiveEntry entry;
        entry.path = normalizedPath;
        entry.isDirectory = rawLine.trimmed().endsWith(QLatin1Char('/'));
        result.entries.append(entry);
    }

    result.success = true;
    return result;
}

ArchiveRenderer::EntryPreviewResult ArchiveRenderer::loadEntryPreview(const QString& archivePath,
                                                                      const ArchiveEntry& entry,
                                                                      const PreviewCancellationToken& cancelToken)
{
    EntryPreviewResult result;
    result.entryPath = entry.path;
    result.isText = isPreviewableTextEntry(entry.path);
    result.isImage = isPreviewableImageEntry(entry.path);

    if (previewCancellationRequested(cancelToken)) {
        result.statusMessage = QCoreApplication::translate("SpaceLook", "Entry preview was canceled.");
        return result;
    }

    const QString sevenZipPath = bundledSevenZipPath();
    if (sevenZipPath.isEmpty()) {
        result.statusMessage = QCoreApplication::translate("SpaceLook", "Bundled 7-Zip runtime was not found.");
        return result;
    }

    QProcess process;
    process.setProgram(sevenZipPath);
    process.setArguments({
        QStringLiteral("e"),
        QStringLiteral("-so"),
        QStringLiteral("-sccUTF-8"),
        QDir::toNativeSeparators(archivePath),
        entry.path
    });
    process.start();
    if (!process.waitForStarted(5000)) {
        result.statusMessage = QCoreApplication::translate("SpaceLook", "Failed to start archive extraction tool.");
        return result;
    }

    QByteArray data;
    QElapsedTimer timeout;
    timeout.start();
    while (!process.waitForFinished(100)) {
        data += process.readAllStandardOutput();
        if (data.size() > 8 * 1024 * 1024) {
            process.kill();
            process.waitForFinished(1000);
            result.statusMessage = QCoreApplication::translate("SpaceLook", "This archive entry is too large for inline preview.");
            return result;
        }

        if (previewCancellationRequested(cancelToken)) {
            process.kill();
            process.waitForFinished(1000);
            result.statusMessage = QCoreApplication::translate("SpaceLook", "Entry preview was canceled.");
            return result;
        }

        if (timeout.elapsed() <= 15000) {
            continue;
        }

        process.kill();
        process.waitForFinished(3000);
        result.statusMessage = QCoreApplication::translate("SpaceLook", "Timed out while extracting archive entry.");
        return result;
    }

    data += process.readAllStandardOutput();
    const QString stdError = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        result.statusMessage = stdError.isEmpty()
            ? QCoreApplication::translate("SpaceLook", "This archive entry could not be extracted.")
            : stdError;
        return result;
    }

    if (data.isEmpty()) {
        result.statusMessage = QCoreApplication::translate("SpaceLook", "This archive entry is empty.");
        return result;
    }

    result.data = data;
    result.success = true;
    return result;
}
