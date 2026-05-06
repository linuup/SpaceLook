#include "renderers/summary/ArchiveRenderer.h"

#include <QDebug>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHash>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QProcess>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

#include "renderers/FileTypeIconResolver.h"
#include "renderers/OpenWithButton.h"
#include "renderers/PreviewHeaderBar.h"
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
    , m_treeWidget(new QTreeWidget(this))
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
    m_treeWidget->setObjectName(QStringLiteral("ArchiveTree"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 0, 12, 12);
    layout->setSpacing(12);
    layout->addWidget(m_headerRow);
    layout->addWidget(m_statusLabel);
    layout->addWidget(m_treeWidget, 1);

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
    m_iconLabel->setScaledContents(true);
    m_titleLabel->setWordWrap(true);
    m_pathTitleLabel->hide();
    m_pathValueLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_pathValueLabel->setWordWrap(true);
    m_pathValueLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_pathRow->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_statusLabel->hide();

    m_treeWidget->setColumnCount(1);
    m_treeWidget->setHeaderLabel(QStringLiteral("Archive contents"));
    m_treeWidget->header()->setStretchLastSection(true);
    m_treeWidget->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_treeWidget->setRootIsDecorated(true);
    m_treeWidget->setUniformRowHeights(true);
    m_treeWidget->setAlternatingRowColors(false);
    m_treeWidget->setAnimated(true);
    m_treeWidget->setIndentation(20);

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
        }
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
    m_info = info;
    const PreviewLoadGuard::Token loadToken = m_loadGuard.begin(info.filePath);
    notifyLoadingState(true);

    qDebug().noquote() << QStringLiteral("[SpaceLookRender] ArchiveRenderer load path=\"%1\"").arg(info.filePath);

    m_titleLabel->setText(info.title.isEmpty() ? QStringLiteral("Archive Preview") : info.title);
    m_titleLabel->setCopyText(m_titleLabel->text());
    const QIcon typeIcon(FileTypeIconResolver::iconForInfo(info));
    m_iconLabel->setPixmap(typeIcon.pixmap(128, 128));
    m_pathValueLabel->setText(info.filePath.trimmed().isEmpty() ? QStringLiteral("(Unavailable)") : info.filePath);
    m_openWithButton->setTargetContext(info.filePath, info.typeKey);
    m_treeWidget->clear();
    m_statusLabel->setText(QStringLiteral("Loading archive contents..."));
    m_statusLabel->show();

    auto* watcher = new QFutureWatcher<ArchiveLoadResult>(this);
    connect(watcher, &QFutureWatcher<ArchiveLoadResult>::finished, this, [this, watcher, loadToken]() {
        const ArchiveLoadResult result = watcher->result();
        watcher->deleteLater();

        if (!m_loadGuard.isCurrent(loadToken, m_info.filePath)) {
            qDebug().noquote() << QStringLiteral("[SpaceLookRender] ArchiveRenderer discarded stale result path=\"%1\"")
                .arg(loadToken.path);
            return;
        }

        if (!result.success) {
            m_statusLabel->setText(result.statusMessage);
            m_statusLabel->show();
            notifyLoadingState(false);
            return;
        }

        populateTree(result.entries);
        if (result.entries.isEmpty()) {
            m_statusLabel->setText(QStringLiteral("The archive is empty."));
            m_statusLabel->show();
        } else {
            m_statusLabel->setText(QStringLiteral("Loaded %1 entries. Click folders to expand.")
                .arg(result.entries.size()));
            m_statusLabel->show();
        }

        qDebug().noquote() << QStringLiteral("[SpaceLookRender] ArchiveRenderer entries=%1 path=\"%2\"")
            .arg(result.entries.size())
            .arg(loadToken.path);
        notifyLoadingState(false);
    });

    watcher->setFuture(QtConcurrent::run([this, filePath = info.filePath]() {
        return loadArchiveEntries(filePath);
    }));
}

void ArchiveRenderer::unload()
{
    m_loadGuard.cancel();
    notifyLoadingState(false);
    m_treeWidget->clear();
    m_pathValueLabel->clear();
    m_openWithButton->setTargetContext(QString(), QString());
    m_statusLabel->clear();
    m_statusLabel->hide();
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
        "  font-family: 'Segoe UI Semibold';"
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
        "  font-family: 'Segoe UI Semibold';"
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
    titleFont.setFamily(QStringLiteral("Microsoft YaHei UI"));
    titleFont.setPixelSize(20);
    titleFont.setWeight(QFont::Bold);
    m_titleLabel->setFont(titleFont);

    QFont metaFont;
    metaFont.setFamily(QStringLiteral("Segoe UI"));
    metaFont.setPixelSize(13);
    m_metaLabel->setFont(metaFont);
    m_pathTitleLabel->setFont(metaFont);
    m_pathValueLabel->setFont(metaFont);
    m_statusLabel->setFont(metaFont);

    QFont treeFont;
    treeFont.setFamily(QStringLiteral("Segoe UI"));
    treeFont.setPixelSize(13);
    m_treeWidget->setFont(treeFont);
}

void ArchiveRenderer::showStatusMessage(const QString& message)
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

void ArchiveRenderer::populateTree(const QVector<ArchiveEntry>& entries)
{
    m_treeWidget->clear();

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
            item->setIcon(0, iconForArchiveEntry(currentPath, false));
            item->setData(0, Qt::UserRole, false);
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

ArchiveRenderer::ArchiveLoadResult ArchiveRenderer::loadArchiveEntries(const QString& filePath) const
{
    ArchiveLoadResult result;

    const QString trimmedPath = filePath.trimmed();
    if (trimmedPath.isEmpty()) {
        result.statusMessage = QStringLiteral("Archive path is unavailable.");
        return result;
    }

    QProcess process;
    process.setProgram(QStringLiteral("tar"));
    process.setArguments({ QStringLiteral("-tf"), QDir::toNativeSeparators(trimmedPath) });
    process.start();
    if (!process.waitForStarted(5000)) {
        result.statusMessage = QStringLiteral("Failed to start the archive listing tool.");
        return result;
    }

    if (!process.waitForFinished(15000)) {
        process.kill();
        process.waitForFinished(3000);
        result.statusMessage = QStringLiteral("Timed out while reading archive contents.");
        return result;
    }

    const QString stdOutput = QString::fromLocal8Bit(process.readAllStandardOutput());
    const QString stdError = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        result.statusMessage = stdError.isEmpty()
            ? QStringLiteral("This archive format could not be listed.")
            : stdError;
        qDebug().noquote() << QStringLiteral("[SpaceLookRender] ArchiveRenderer listing failed path=\"%1\" error=\"%2\"")
            .arg(trimmedPath, result.statusMessage);
        return result;
    }

    const QStringList lines = stdOutput.split(QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts);
    for (const QString& rawLine : lines) {
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
