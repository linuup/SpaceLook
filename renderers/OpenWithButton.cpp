#include "renderers/OpenWithButton.h"

#include "renderers/FluentIconFont.h"
#include "catalog/open_with_catalog.h"
#include "core/file_suffix_utils.h"

#include <QCoreApplication>
#include <QAction>
#include <QDebug>
#include <QDir>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QMenu>
#include <QProcess>
#include <QSettings>
#include <QStyle>
#include <QToolButton>

namespace {

QIcon iconFromLocation(const QString& iconPath, int iconIndex)
{
    Q_UNUSED(iconIndex);
    if (iconPath.trimmed().isEmpty()) {
        return QIcon();
    }

    QFileIconProvider iconProvider;
    return iconProvider.icon(QFileInfo(iconPath));
}

QIcon explorerAppIcon()
{
    static const QIcon icon = iconFromLocation(QStringLiteral("C:/Windows/explorer.exe"), 0);
    return icon;
}

QString settingsFilePath()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("SPACELOOK.ini"));
}

}

OpenWithButton::OpenWithButton(QWidget* parent)
    : QWidget(parent)
    , m_primaryButton(new QToolButton(this))
    , m_expandButton(new QToolButton(this))
    , m_menu(new QMenu(this))
{
    setObjectName(QStringLiteral("OpenWithWidget"));

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_primaryButton->setObjectName(QStringLiteral("OpenWithPrimaryButton"));
    m_primaryButton->setCursor(Qt::PointingHandCursor);
    m_primaryButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_primaryButton->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));
    m_primaryButton->setEnabled(false);
    m_primaryButton->setFixedSize(30, 28);
    m_primaryButton->setIconSize(QSize(15, 15));

    m_expandButton->setObjectName(QStringLiteral("OpenWithExpandButton"));
    m_expandButton->setCursor(Qt::PointingHandCursor);
    m_expandButton->setFont(FluentIconFont::iconFont(11, QFont::Normal));
    m_expandButton->setText(FluentIconFont::glyph(0xE70D));
    m_expandButton->setMenu(m_menu);
    m_expandButton->setPopupMode(QToolButton::InstantPopup);
    m_expandButton->setEnabled(false);
    m_expandButton->setFixedSize(20, 28);
    m_expandButton->setStyleSheet(
        "#OpenWithExpandButton::menu-indicator {"
        "  image: none;"
        "  width: 0px;"
        "}"
    );

    layout->addWidget(m_primaryButton);
    layout->addWidget(m_expandButton);

    connect(m_primaryButton, &QToolButton::clicked, this, [this]() {
        launchCurrentHandler();
    });
}

OpenWithButton::~OpenWithButton()
{
    clearHandlers();
}

bool OpenWithButton::hasAvailableHandlers() const
{
    return !m_handlers.isEmpty();
}

void OpenWithButton::setTargetPath(const QString& filePath)
{
    setTargetContext(filePath, m_targetTypeKey);
}

void OpenWithButton::setTargetContext(const QString& filePath, const QString& typeKey)
{
    const QString cleanedPath = QDir::cleanPath(filePath.trimmed());
    const QString cleanedTypeKey = typeKey.trimmed().toLower();
    if (m_targetPath == cleanedPath && m_targetTypeKey == cleanedTypeKey) {
        return;
    }

    m_targetPath = cleanedPath;
    m_targetTypeKey = cleanedTypeKey;
    qDebug().noquote() << QStringLiteral("[SpaceLookOpenWith] setTargetContext path=\"%1\" typeKey=\"%2\"")
        .arg(m_targetPath, m_targetTypeKey);
    refreshHandlers();
}

void OpenWithButton::setStatusCallback(std::function<void(const QString&)> callback)
{
    m_statusCallback = std::move(callback);
}

void OpenWithButton::setLaunchSuccessCallback(std::function<void()> callback)
{
    m_launchSuccessCallback = std::move(callback);
}

void OpenWithButton::showMenuAtGlobalPos(const QPoint& globalPos)
{
    if (!m_expandButton->isEnabled() || m_menu->isEmpty()) {
        return;
    }

    const QSize menuSizeHint = m_menu->sizeHint();
    const QPoint anchorTopRight = globalPos.isNull()
        ? m_expandButton->mapToGlobal(QPoint(m_expandButton->width(), m_expandButton->height() + 8))
        : globalPos;
    const QPoint popupPos(anchorTopRight.x() - menuSizeHint.width(), anchorTopRight.y());
    m_menu->popup(popupPos);
}

void OpenWithButton::clearHandlers()
{
    m_handlers.clear();
    m_currentHandlerIndex = -1;
}

void OpenWithButton::refreshHandlers()
{
    clearHandlers();
    m_menu->clear();

    const QFileInfo fileInfo(m_targetPath);
    if (m_targetPath.isEmpty() || !fileInfo.exists()) {
        qDebug().noquote() << QStringLiteral("[SpaceLookOpenWith] refreshHandlers hidden because path is empty or missing path=\"%1\"")
            .arg(m_targetPath);
        setVisible(false);
        updatePrimaryButton();
        return;
    }

    const QVector<OpenWithCatalog::AppEntry> apps = OpenWithCatalog::instance().registryAppsForFile(m_targetPath);
    for (const OpenWithCatalog::AppEntry& app : apps) {
        HandlerEntry entry;
        entry.displayName = app.displayName;
        entry.icon = app.icon;
        entry.kind = EntryKind::DirectCommand;
        entry.executablePath = app.executablePath;
        entry.arguments = QStringList() << QDir::toNativeSeparators(fileInfo.absoluteFilePath());
        if (containsHandlerName(entry.displayName)) {
            continue;
        }
        m_handlers.append(entry);
    }

    HandlerEntry explorerEntry;
    explorerEntry.displayName = QStringLiteral("Explorer");
    explorerEntry.icon = explorerAppIcon();
    explorerEntry.kind = EntryKind::ExplorerLocation;
    m_handlers.append(explorerEntry);
    logEntry(QStringLiteral("[SpaceLookOpenWith] appended fixed entry"), explorerEntry);

    if (m_handlers.isEmpty()) {
        setVisible(false);
        updatePrimaryButton();
        return;
    }

    m_currentHandlerIndex = preferredHandlerIndex();
    qDebug().noquote() << QStringLiteral("[SpaceLookOpenWith] refreshHandlers totalEntries=%1 currentIndex=%2 path=\"%3\"")
        .arg(m_handlers.size())
        .arg(m_currentHandlerIndex)
        .arg(m_targetPath);
    rebuildMenu();
    updatePrimaryButton();
    setVisible(true);
}

bool OpenWithButton::containsHandlerName(const QString& displayName) const
{
    for (const HandlerEntry& entry : m_handlers) {
        if (entry.displayName.compare(displayName, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

QString OpenWithButton::entryKindName(EntryKind kind) const
{
    switch (kind) {
    case EntryKind::DirectCommand:
        return QStringLiteral("DirectCommand");
    case EntryKind::ExplorerLocation:
        return QStringLiteral("ExplorerLocation");
    }
    return QStringLiteral("Unknown");
}

void OpenWithButton::logEntry(const QString& prefix, const HandlerEntry& entry) const
{
    qDebug().noquote() << QStringLiteral("%1 name=\"%2\" kind=%3 recommended=%4 exe=\"%5\" args=\"%6\" path=\"%7\"")
        .arg(prefix,
             entry.displayName,
             entryKindName(entry.kind),
             entry.recommended ? QStringLiteral("true") : QStringLiteral("false"),
             entry.executablePath,
             entry.arguments.join(QStringLiteral(" | ")),
             m_targetPath);
}

void OpenWithButton::rebuildMenu()
{
    m_menu->clear();

    for (int index = 0; index < m_handlers.size(); ++index) {
        const HandlerEntry& entry = m_handlers.at(index);
        QAction* action = new QAction(entry.icon, entry.displayName, m_menu);
        action->setCheckable(true);
        action->setChecked(index == m_currentHandlerIndex);
        if (entry.recommended) {
            action->setToolTip(QStringLiteral("Recommended application"));
        }
        connect(action, &QAction::triggered, this, [this, index]() {
            selectHandler(index, true);
        });
        m_menu->addAction(action);
    }
}

void OpenWithButton::updatePrimaryButton()
{
    const bool enabled = m_currentHandlerIndex >= 0 && m_currentHandlerIndex < m_handlers.size();
    m_primaryButton->setEnabled(enabled);
    m_expandButton->setEnabled(enabled);

    if (!enabled) {
        m_primaryButton->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));
        m_primaryButton->setToolTip(QString());
        return;
    }

    const HandlerEntry& entry = m_handlers.at(m_currentHandlerIndex);
    m_primaryButton->setIcon(entry.icon.isNull()
        ? style()->standardIcon(QStyle::SP_DialogOpenButton)
        : entry.icon);
    m_primaryButton->setToolTip(QStringLiteral("Open with %1").arg(entry.displayName));
}

void OpenWithButton::selectHandler(int index, bool launchAfterSelect)
{
    if (index < 0 || index >= m_handlers.size()) {
        return;
    }

    m_currentHandlerIndex = index;
    persistSelectedHandler(m_handlers.at(index));
    logEntry(QStringLiteral("[SpaceLookOpenWith] selected entry"), m_handlers.at(index));
    rebuildMenu();
    updatePrimaryButton();
    if (launchAfterSelect) {
        launchCurrentHandler();
    }
}

void OpenWithButton::launchCurrentHandler()
{
    launchHandler(m_currentHandlerIndex);
}

bool OpenWithButton::launchHandler(int index)
{
    if (index < 0 || index >= m_handlers.size()) {
        qDebug().noquote() << QStringLiteral("[SpaceLookOpenWith] launchHandler ignored invalidIndex=%1 totalEntries=%2")
            .arg(index)
            .arg(m_handlers.size());
        return false;
    }

    const HandlerEntry& entry = m_handlers.at(index);
    logEntry(QStringLiteral("[SpaceLookOpenWith] launch requested"), entry);
    if (entry.kind == EntryKind::DirectCommand) {
        return launchDirectEntry(entry);
    }

    if (entry.kind == EntryKind::ExplorerLocation) {
        const QFileInfo fileInfo(m_targetPath);
        if (!fileInfo.exists()) {
            reportStatus(QStringLiteral("The file location is unavailable."));
            return false;
        }

        const QString nativePath = QDir::toNativeSeparators(fileInfo.absoluteFilePath());
        if (!QProcess::startDetached(QStringLiteral("explorer.exe"),
                                     { QStringLiteral("/select,"), nativePath })) {
            reportStatus(QStringLiteral("Failed to open file location."));
            return false;
        }

        qDebug().noquote() << QStringLiteral("[SpaceLookOpenWith] Opened Explorer for \"%1\"").arg(nativePath);
        reportStatus(QStringLiteral("Opened file location"));
        if (m_launchSuccessCallback) {
            m_launchSuccessCallback();
        }
        return true;
    }
    return false;
}

bool OpenWithButton::launchDirectEntry(const HandlerEntry& entry)
{
    if (entry.executablePath.trimmed().isEmpty()) {
        reportStatus(QStringLiteral("The application path is unavailable."));
        return false;
    }

    if (!QProcess::startDetached(entry.executablePath, entry.arguments)) {
        qDebug().noquote() << QStringLiteral("[SpaceLookOpenWith] Direct launch failed for %1 exe=\"%2\" args=%3")
            .arg(entry.displayName, entry.executablePath, entry.arguments.join(QStringLiteral(" | ")));
        reportStatus(QStringLiteral("Failed to open with %1").arg(entry.displayName));
        return false;
    }

    qDebug().noquote() << QStringLiteral("[SpaceLookOpenWith] Direct launch %1 exe=\"%2\" path=\"%3\"")
        .arg(entry.displayName, entry.executablePath, m_targetPath);
    reportStatus(QStringLiteral("Opened with %1").arg(entry.displayName));
    if (m_launchSuccessCallback) {
        m_launchSuccessCallback();
    }
    return true;
}

QString OpenWithButton::preferenceScopeKey() const
{
    const QString normalizedTypeKey = m_targetTypeKey.trimmed().toLower();
    if (!normalizedTypeKey.isEmpty()) {
        return normalizedTypeKey;
    }

    const QString suffix = FileSuffixUtils::fullSuffix(m_targetPath);
    if (!suffix.isEmpty()) {
        return QStringLiteral("suffix:%1").arg(suffix);
    }

    return QStringLiteral("unknown");
}

QString OpenWithButton::handlerPreferenceKey() const
{
    return QStringLiteral("open_with/default_handler/%1").arg(preferenceScopeKey());
}

QString OpenWithButton::handlerIdentity(const HandlerEntry& entry) const
{
    if (entry.kind == EntryKind::ExplorerLocation) {
        return QStringLiteral("explorer");
    }

    return QStringLiteral("app:%1").arg(entry.executablePath.trimmed().toLower());
}

int OpenWithButton::preferredHandlerIndex() const
{
    if (m_handlers.isEmpty()) {
        return -1;
    }

    QSettings settings(settingsFilePath(), QSettings::IniFormat);
    const QString preferredIdentity = settings.value(handlerPreferenceKey(), QStringLiteral("explorer")).toString().trimmed().toLower();
    if (!preferredIdentity.isEmpty()) {
        for (int index = 0; index < m_handlers.size(); ++index) {
            if (handlerIdentity(m_handlers.at(index)) == preferredIdentity) {
                return index;
            }
        }
    }

    for (int index = 0; index < m_handlers.size(); ++index) {
        if (m_handlers.at(index).kind == EntryKind::ExplorerLocation) {
            return index;
        }
    }

    return 0;
}

void OpenWithButton::persistSelectedHandler(const HandlerEntry& entry) const
{
    QSettings settings(settingsFilePath(), QSettings::IniFormat);
    settings.setValue(handlerPreferenceKey(), handlerIdentity(entry));
    settings.sync();
    qDebug().noquote() << QStringLiteral("[SpaceLookOpenWith] persisted handler key=\"%1\" value=\"%2\" file=\"%3\"")
        .arg(handlerPreferenceKey(), handlerIdentity(entry), QDir::toNativeSeparators(settingsFilePath()));
}

void OpenWithButton::reportStatus(const QString& message) const
{
    if (m_statusCallback) {
        m_statusCallback(message);
    }
}
