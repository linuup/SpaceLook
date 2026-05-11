#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

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
    Q_PROPERTY(bool showMenuOcr READ showMenuOcr WRITE setShowMenuOcr NOTIFY menuVisibilityChanged)
    Q_PROPERTY(bool showMenuRefresh READ showMenuRefresh WRITE setShowMenuRefresh NOTIFY menuVisibilityChanged)
    Q_PROPERTY(bool showMenuExpand READ showMenuExpand WRITE setShowMenuExpand NOTIFY menuVisibilityChanged)
    Q_PROPERTY(bool showMenuClose READ showMenuClose WRITE setShowMenuClose NOTIFY menuVisibilityChanged)
    Q_PROPERTY(bool showMenuMore READ showMenuMore WRITE setShowMenuMore NOTIFY menuVisibilityChanged)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(QString ocrEngine READ ocrEngine WRITE setOcrEngine NOTIFY ocrSettingsChanged)
    Q_PROPERTY(QString baiduOcrApiKey READ baiduOcrApiKey WRITE setBaiduOcrApiKey NOTIFY ocrSettingsChanged)
    Q_PROPERTY(QString baiduOcrSecretKey READ baiduOcrSecretKey WRITE setBaiduOcrSecretKey NOTIFY ocrSettingsChanged)
    Q_PROPERTY(bool baiduOcrCredentialTestBusy READ baiduOcrCredentialTestBusy NOTIFY ocrSettingsChanged)
    Q_PROPERTY(QString baiduOcrCredentialTestMessage READ baiduOcrCredentialTestMessage NOTIFY ocrSettingsChanged)
    Q_PROPERTY(bool alwaysUnblockProtectedView READ alwaysUnblockProtectedView WRITE setAlwaysUnblockProtectedView NOTIFY documentPreviewSettingsChanged)

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
    bool showMenuOcr() const;
    void setShowMenuOcr(bool show);
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
    QString ocrEngine() const;
    void setOcrEngine(const QString& engine);
    QString baiduOcrApiKey() const;
    void setBaiduOcrApiKey(const QString& apiKey);
    QString baiduOcrSecretKey() const;
    void setBaiduOcrSecretKey(const QString& secretKey);
    bool baiduOcrCredentialTestBusy() const;
    QString baiduOcrCredentialTestMessage() const;
    Q_INVOKABLE void testBaiduOcrCredentials();
    bool alwaysUnblockProtectedView() const;
    void setAlwaysUnblockProtectedView(bool enabled);

signals:
    void menuButtonSizeChanged();
    void menuAppearanceChanged();
    void showSystemTrayChanged();
    void showTaskbarChanged();
    void performanceModeChanged();
    void autoStartChanged();
    void menuVisibilityChanged();
    void languageChanged();
    void ocrSettingsChanged();
    void documentPreviewSettingsChanged();

private:
    SpaceLookUiSettings();
    void writeBoolSetting(const QString& key, bool value);
    void writeStringSetting(const QString& key, const QString& value);
    bool updateBoolSetting(bool& field, bool value, const QString& key);
    bool updateStringSetting(QString& field, const QString& value, const QString& key);
    bool loadAutoStart() const;
    void applyAutoStart(bool enabled);
    QString normalizedLanguage(const QString& language) const;
    QString normalizedOcrEngine(const QString& engine) const;
    void setBaiduOcrCredentialTestState(bool busy, const QString& message);

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
    bool m_showMenuOcr = true;
    bool m_showMenuRefresh = true;
    bool m_showMenuExpand = true;
    bool m_showMenuClose = true;
    bool m_showMenuMore = true;
    QString m_language = QStringLiteral("en");
    QString m_ocrEngine = QStringLiteral("windows");
    QString m_baiduOcrApiKey;
    QString m_baiduOcrSecretKey;
    bool m_baiduOcrCredentialTestBusy = false;
    QString m_baiduOcrCredentialTestMessage;
    bool m_alwaysUnblockProtectedView = false;
    QNetworkAccessManager* m_networkAccessManager = nullptr;
    QNetworkReply* m_baiduOcrCredentialTestReply = nullptr;
};
