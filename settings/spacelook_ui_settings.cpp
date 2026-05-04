#include "settings/spacelook_ui_settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>

namespace {

QString settingsFilePath()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("SPACELOOK.ini"));
}

QString autoStartRegistryPath()
{
    return QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run");
}

QString autoStartValueName()
{
    return QStringLiteral("LinDeskSpaceLook");
}

}

SpaceLookUiSettings& SpaceLookUiSettings::instance()
{
    static SpaceLookUiSettings settings;
    return settings;
}

SpaceLookUiSettings::SpaceLookUiSettings()
{
    QSettings settings(settingsFilePath(), QSettings::IniFormat);
    m_menuButtonSize = qBound(30, settings.value(QStringLiteral("ui/menu_button_size"), 38).toInt(), 72);
    m_showMenuBorder = settings.value(QStringLiteral("ui/menu/show_border"), false).toBool();
    m_menuPlacement = qBound(0, settings.value(QStringLiteral("ui/menu/placement"), 0).toInt(), 3);
    m_showSystemTray = settings.value(QStringLiteral("ui/show_system_tray"), false).toBool();
    m_showTaskbar = settings.value(QStringLiteral("ui/show_taskbar"), true).toBool();
    m_performanceMode = settings.value(QStringLiteral("ui/performance_mode"), false).toBool();
    m_showMenuPin = settings.value(QStringLiteral("ui/menu/show_pin"), true).toBool();
    m_showMenuOpen = settings.value(QStringLiteral("ui/menu/show_open"), true).toBool();
    m_showMenuCopy = settings.value(QStringLiteral("ui/menu/show_copy"), true).toBool();
    m_showMenuRefresh = settings.value(QStringLiteral("ui/menu/show_refresh"), true).toBool();
    m_showMenuExpand = settings.value(QStringLiteral("ui/menu/show_expand"), true).toBool();
    m_showMenuClose = settings.value(QStringLiteral("ui/menu/show_close"), true).toBool();
    m_showMenuMore = settings.value(QStringLiteral("ui/menu/show_more"), true).toBool();
    m_autoStart = loadAutoStart();
}

int SpaceLookUiSettings::menuButtonSize() const
{
    return m_menuButtonSize;
}

void SpaceLookUiSettings::setMenuButtonSize(int size)
{
    const int boundedSize = qBound(30, size, 72);
    if (m_menuButtonSize == boundedSize) {
        return;
    }

    m_menuButtonSize = boundedSize;
    QSettings settings(settingsFilePath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("ui/menu_button_size"), m_menuButtonSize);
    settings.sync();
    emit menuButtonSizeChanged();
}

bool SpaceLookUiSettings::showMenuBorder() const
{
    return m_showMenuBorder;
}

void SpaceLookUiSettings::setShowMenuBorder(bool show)
{
    if (!updateBoolSetting(m_showMenuBorder, show, QStringLiteral("ui/menu/show_border"))) {
        return;
    }

    emit menuAppearanceChanged();
}

int SpaceLookUiSettings::menuPlacement() const
{
    return m_menuPlacement;
}

void SpaceLookUiSettings::setMenuPlacement(int placement)
{
    const int boundedPlacement = qBound(0, placement, 3);
    if (m_menuPlacement == boundedPlacement) {
        return;
    }

    m_menuPlacement = boundedPlacement;
    QSettings settings(settingsFilePath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("ui/menu/placement"), m_menuPlacement);
    settings.sync();
    emit menuAppearanceChanged();
}

bool SpaceLookUiSettings::showSystemTray() const
{
    return m_showSystemTray;
}

void SpaceLookUiSettings::setShowSystemTray(bool show)
{
    if (!updateBoolSetting(m_showSystemTray, show, QStringLiteral("ui/show_system_tray"))) {
        return;
    }

    emit showSystemTrayChanged();
}

bool SpaceLookUiSettings::showTaskbar() const
{
    return m_showTaskbar;
}

void SpaceLookUiSettings::setShowTaskbar(bool show)
{
    if (!updateBoolSetting(m_showTaskbar, show, QStringLiteral("ui/show_taskbar"))) {
        return;
    }

    emit showTaskbarChanged();
}

bool SpaceLookUiSettings::performanceMode() const
{
    return m_performanceMode;
}

void SpaceLookUiSettings::setPerformanceMode(bool enabled)
{
    if (!updateBoolSetting(m_performanceMode, enabled, QStringLiteral("ui/performance_mode"))) {
        return;
    }

    emit performanceModeChanged();
}

bool SpaceLookUiSettings::autoStart() const
{
    return m_autoStart;
}

void SpaceLookUiSettings::setAutoStart(bool enabled)
{
    if (m_autoStart == enabled) {
        return;
    }

    applyAutoStart(enabled);
    m_autoStart = loadAutoStart();

    QSettings settings(settingsFilePath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("ui/auto_start"), m_autoStart);
    settings.sync();
    emit autoStartChanged();
}

bool SpaceLookUiSettings::showMenuPin() const
{
    return m_showMenuPin;
}

void SpaceLookUiSettings::setShowMenuPin(bool show)
{
    if (!updateBoolSetting(m_showMenuPin, show, QStringLiteral("ui/menu/show_pin"))) {
        return;
    }

    emit menuVisibilityChanged();
}

bool SpaceLookUiSettings::showMenuOpen() const
{
    return m_showMenuOpen;
}

void SpaceLookUiSettings::setShowMenuOpen(bool show)
{
    if (!updateBoolSetting(m_showMenuOpen, show, QStringLiteral("ui/menu/show_open"))) {
        return;
    }

    emit menuVisibilityChanged();
}

bool SpaceLookUiSettings::showMenuCopy() const
{
    return m_showMenuCopy;
}

void SpaceLookUiSettings::setShowMenuCopy(bool show)
{
    if (!updateBoolSetting(m_showMenuCopy, show, QStringLiteral("ui/menu/show_copy"))) {
        return;
    }

    emit menuVisibilityChanged();
}

bool SpaceLookUiSettings::showMenuRefresh() const
{
    return m_showMenuRefresh;
}

void SpaceLookUiSettings::setShowMenuRefresh(bool show)
{
    if (!updateBoolSetting(m_showMenuRefresh, show, QStringLiteral("ui/menu/show_refresh"))) {
        return;
    }

    emit menuVisibilityChanged();
}

bool SpaceLookUiSettings::showMenuExpand() const
{
    return m_showMenuExpand;
}

void SpaceLookUiSettings::setShowMenuExpand(bool show)
{
    if (!updateBoolSetting(m_showMenuExpand, show, QStringLiteral("ui/menu/show_expand"))) {
        return;
    }

    emit menuVisibilityChanged();
}

bool SpaceLookUiSettings::showMenuClose() const
{
    return m_showMenuClose;
}

void SpaceLookUiSettings::setShowMenuClose(bool show)
{
    if (!updateBoolSetting(m_showMenuClose, show, QStringLiteral("ui/menu/show_close"))) {
        return;
    }

    emit menuVisibilityChanged();
}

bool SpaceLookUiSettings::showMenuMore() const
{
    return m_showMenuMore;
}

void SpaceLookUiSettings::setShowMenuMore(bool show)
{
    if (!updateBoolSetting(m_showMenuMore, show, QStringLiteral("ui/menu/show_more"))) {
        return;
    }

    emit menuVisibilityChanged();
}

void SpaceLookUiSettings::writeBoolSetting(const QString& key, bool value)
{
    QSettings settings(settingsFilePath(), QSettings::IniFormat);
    settings.setValue(key, value);
    settings.sync();
}

bool SpaceLookUiSettings::updateBoolSetting(bool& field, bool value, const QString& key)
{
    if (field == value) {
        return false;
    }

    field = value;
    writeBoolSetting(key, value);
    return true;
}

bool SpaceLookUiSettings::loadAutoStart() const
{
    QSettings runSettings(autoStartRegistryPath(), QSettings::NativeFormat);
    const QString storedValue = runSettings.value(autoStartValueName()).toString().trimmed();
    return !storedValue.isEmpty();
}

void SpaceLookUiSettings::applyAutoStart(bool enabled)
{
    QSettings runSettings(autoStartRegistryPath(), QSettings::NativeFormat);
    if (!enabled) {
        runSettings.remove(autoStartValueName());
        runSettings.sync();
        return;
    }

    const QString executablePath = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    const QString command = QStringLiteral("\"%1\"").arg(executablePath);
    runSettings.setValue(autoStartValueName(), command);
    runSettings.sync();
}
