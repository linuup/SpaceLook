#pragma once

#include <QObject>
#include <QString>

class SpaceLookUiSettings : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int menuButtonSize READ menuButtonSize WRITE setMenuButtonSize NOTIFY menuButtonSizeChanged)
    Q_PROPERTY(bool showMenuBorder READ showMenuBorder WRITE setShowMenuBorder NOTIFY menuAppearanceChanged)
    Q_PROPERTY(int menuPlacement READ menuPlacement WRITE setMenuPlacement NOTIFY menuAppearanceChanged)
    Q_PROPERTY(bool showSystemTray READ showSystemTray WRITE setShowSystemTray NOTIFY showSystemTrayChanged)
    Q_PROPERTY(bool showTaskbar READ showTaskbar WRITE setShowTaskbar NOTIFY showTaskbarChanged)
    Q_PROPERTY(bool performanceMode READ performanceMode WRITE setPerformanceMode NOTIFY performanceModeChanged)
    Q_PROPERTY(bool autoStart READ autoStart WRITE setAutoStart NOTIFY autoStartChanged)
    Q_PROPERTY(bool showMenuPin READ showMenuPin WRITE setShowMenuPin NOTIFY menuVisibilityChanged)
    Q_PROPERTY(bool showMenuOpen READ showMenuOpen WRITE setShowMenuOpen NOTIFY menuVisibilityChanged)
    Q_PROPERTY(bool showMenuCopy READ showMenuCopy WRITE setShowMenuCopy NOTIFY menuVisibilityChanged)
    Q_PROPERTY(bool showMenuRefresh READ showMenuRefresh WRITE setShowMenuRefresh NOTIFY menuVisibilityChanged)
    Q_PROPERTY(bool showMenuExpand READ showMenuExpand WRITE setShowMenuExpand NOTIFY menuVisibilityChanged)
    Q_PROPERTY(bool showMenuClose READ showMenuClose WRITE setShowMenuClose NOTIFY menuVisibilityChanged)
    Q_PROPERTY(bool showMenuMore READ showMenuMore WRITE setShowMenuMore NOTIFY menuVisibilityChanged)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)

public:
    static SpaceLookUiSettings& instance();

    int menuButtonSize() const;
    void setMenuButtonSize(int size);
    bool showMenuBorder() const;
    void setShowMenuBorder(bool show);
    int menuPlacement() const;
    void setMenuPlacement(int placement);
    bool showSystemTray() const;
    void setShowSystemTray(bool show);
    bool showTaskbar() const;
    void setShowTaskbar(bool show);
    bool performanceMode() const;
    void setPerformanceMode(bool enabled);
    bool autoStart() const;
    void setAutoStart(bool enabled);
    bool showMenuPin() const;
    void setShowMenuPin(bool show);
    bool showMenuOpen() const;
    void setShowMenuOpen(bool show);
    bool showMenuCopy() const;
    void setShowMenuCopy(bool show);
    bool showMenuRefresh() const;
    void setShowMenuRefresh(bool show);
    bool showMenuExpand() const;
    void setShowMenuExpand(bool show);
    bool showMenuClose() const;
    void setShowMenuClose(bool show);
    bool showMenuMore() const;
    void setShowMenuMore(bool show);
    QString language() const;
    void setLanguage(const QString& language);

signals:
    void menuButtonSizeChanged();
    void menuAppearanceChanged();
    void showSystemTrayChanged();
    void showTaskbarChanged();
    void performanceModeChanged();
    void autoStartChanged();
    void menuVisibilityChanged();
    void languageChanged();

private:
    SpaceLookUiSettings();
    void writeBoolSetting(const QString& key, bool value);
    bool updateBoolSetting(bool& field, bool value, const QString& key);
    bool loadAutoStart() const;
    void applyAutoStart(bool enabled);
    QString normalizedLanguage(const QString& language) const;

    int m_menuButtonSize = 38;
    bool m_showMenuBorder = true;
    int m_menuPlacement = 0;
    bool m_showSystemTray = false;
    bool m_showTaskbar = true;
    bool m_performanceMode = false;
    bool m_autoStart = false;
    bool m_showMenuPin = true;
    bool m_showMenuOpen = true;
    bool m_showMenuCopy = true;
    bool m_showMenuRefresh = true;
    bool m_showMenuExpand = true;
    bool m_showMenuClose = true;
    bool m_showMenuMore = true;
    QString m_language = QStringLiteral("en");
};
