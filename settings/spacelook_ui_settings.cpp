#include "settings/spacelook_ui_settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QVariant>

namespace {

constexpr int kBaiduCredentialTestTimeoutMs = 20000;

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

QString baiduJsonErrorMessage(const QJsonObject& object)
{
    const QString errorDescription = object.value(QStringLiteral("error_description")).toString().trimmed();
    if (!errorDescription.isEmpty()) {
        return errorDescription;
    }

    const QString error = object.value(QStringLiteral("error")).toString().trimmed();
    if (!error.isEmpty()) {
        return error;
    }

    const QString errorMessage = object.value(QStringLiteral("error_msg")).toString().trimmed();
    if (!errorMessage.isEmpty()) {
        return errorMessage;
    }

    const int errorCode = object.value(QStringLiteral("error_code")).toInt();
    if (errorCode != 0) {
        return QCoreApplication::translate("SpaceLook", "Baidu OCR error %1.").arg(errorCode);
    }

    return QString();
}

}

SpaceLookUiSettings& SpaceLookUiSettings::instance()
{
    static SpaceLookUiSettings settings;
    return settings;
}

SpaceLookUiSettings::SpaceLookUiSettings()
{
    m_networkAccessManager = new QNetworkAccessManager(this);

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
    m_showMenuOcr = settings.value(QStringLiteral("ui/menu/show_ocr"), true).toBool();
    m_showMenuRefresh = settings.value(QStringLiteral("ui/menu/show_refresh"), true).toBool();
    m_showMenuExpand = settings.value(QStringLiteral("ui/menu/show_expand"), true).toBool();
    m_showMenuClose = settings.value(QStringLiteral("ui/menu/show_close"), true).toBool();
    m_showMenuMore = settings.value(QStringLiteral("ui/menu/show_more"), true).toBool();
    m_language = normalizedLanguage(settings.value(QStringLiteral("ui/language"), QStringLiteral("en")).toString());
    m_ocrEngine = normalizedOcrEngine(settings.value(QStringLiteral("ui/ocr/engine"), QStringLiteral("windows")).toString());
    m_baiduOcrApiKey = settings.value(QStringLiteral("ui/ocr/baidu_api_key")).toString();
    m_baiduOcrSecretKey = settings.value(QStringLiteral("ui/ocr/baidu_secret_key")).toString();
    m_alwaysUnblockProtectedView = settings.value(QStringLiteral("ui/document/always_unblock_protected_view"), false).toBool();
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

bool SpaceLookUiSettings::showMenuOcr() const
{
    return m_showMenuOcr;
}

void SpaceLookUiSettings::setShowMenuOcr(bool show)
{
    if (!updateBoolSetting(m_showMenuOcr, show, QStringLiteral("ui/menu/show_ocr"))) {
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

QString SpaceLookUiSettings::language() const
{
    return m_language;
}

void SpaceLookUiSettings::setLanguage(const QString& language)
{
    const QString nextLanguage = normalizedLanguage(language);
    if (m_language == nextLanguage) {
        return;
    }

    m_language = nextLanguage;
    QSettings settings(settingsFilePath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("ui/language"), m_language);
    settings.sync();
    emit languageChanged();
}

QString SpaceLookUiSettings::ocrEngine() const
{
    return m_ocrEngine;
}

void SpaceLookUiSettings::setOcrEngine(const QString& engine)
{
    const QString nextEngine = normalizedOcrEngine(engine);
    if (!updateStringSetting(m_ocrEngine, nextEngine, QStringLiteral("ui/ocr/engine"))) {
        return;
    }

    emit ocrSettingsChanged();
}

QString SpaceLookUiSettings::baiduOcrApiKey() const
{
    return m_baiduOcrApiKey;
}

void SpaceLookUiSettings::setBaiduOcrApiKey(const QString& apiKey)
{
    if (!updateStringSetting(m_baiduOcrApiKey, apiKey.trimmed(), QStringLiteral("ui/ocr/baidu_api_key"))) {
        return;
    }

    m_baiduOcrCredentialTestMessage.clear();
    emit ocrSettingsChanged();
}

QString SpaceLookUiSettings::baiduOcrSecretKey() const
{
    return m_baiduOcrSecretKey;
}

void SpaceLookUiSettings::setBaiduOcrSecretKey(const QString& secretKey)
{
    if (!updateStringSetting(m_baiduOcrSecretKey, secretKey.trimmed(), QStringLiteral("ui/ocr/baidu_secret_key"))) {
        return;
    }

    m_baiduOcrCredentialTestMessage.clear();
    emit ocrSettingsChanged();
}

bool SpaceLookUiSettings::baiduOcrCredentialTestBusy() const
{
    return m_baiduOcrCredentialTestBusy;
}

QString SpaceLookUiSettings::baiduOcrCredentialTestMessage() const
{
    return m_baiduOcrCredentialTestMessage;
}

void SpaceLookUiSettings::testBaiduOcrCredentials()
{
    if (m_baiduOcrCredentialTestBusy) {
        return;
    }

    const QString apiKey = m_baiduOcrApiKey.trimmed();
    const QString secretKey = m_baiduOcrSecretKey.trimmed();
    if (apiKey.isEmpty() || secretKey.isEmpty()) {
        setBaiduOcrCredentialTestState(false, QCoreApplication::translate("SpaceLook", "Baidu OCR requires API_KEY and SECRET_KEY."));
        return;
    }

    QUrlQuery form;
    form.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("client_credentials"));
    form.addQueryItem(QStringLiteral("client_id"), apiKey);
    form.addQueryItem(QStringLiteral("client_secret"), secretKey);

    QNetworkRequest request{ QUrl(QStringLiteral("https://aip.baidubce.com/oauth/2.0/token")) };
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));

    setBaiduOcrCredentialTestState(true, QCoreApplication::translate("SpaceLook", "Testing Baidu OCR credentials..."));
    m_baiduOcrCredentialTestReply = m_networkAccessManager->post(request, form.toString(QUrl::FullyEncoded).toUtf8());

    QTimer* timeoutTimer = new QTimer(m_baiduOcrCredentialTestReply);
    timeoutTimer->setSingleShot(true);
    connect(timeoutTimer, &QTimer::timeout, m_baiduOcrCredentialTestReply, [this]() {
        if (m_baiduOcrCredentialTestReply) {
            m_baiduOcrCredentialTestReply->abort();
        }
    });
    timeoutTimer->start(kBaiduCredentialTestTimeoutMs);

    connect(m_baiduOcrCredentialTestReply, &QNetworkReply::finished, this, [this, timeoutTimer]() {
        QNetworkReply* reply = m_baiduOcrCredentialTestReply;
        m_baiduOcrCredentialTestReply = nullptr;
        if (!reply) {
            setBaiduOcrCredentialTestState(false, QCoreApplication::translate("SpaceLook", "Baidu credential test failed."));
            return;
        }

        timeoutTimer->stop();
        const QByteArray body = reply->readAll();
        const QNetworkReply::NetworkError networkError = reply->error();
        const QString networkErrorText = reply->errorString();
        reply->deleteLater();

        if (networkError != QNetworkReply::NoError) {
            const QString message = networkError == QNetworkReply::OperationCanceledError
                ? QCoreApplication::translate("SpaceLook", "Baidu credential test timed out.")
                : networkErrorText;
            setBaiduOcrCredentialTestState(false, message);
            return;
        }

        const QJsonDocument document = QJsonDocument::fromJson(body);
        if (!document.isObject()) {
            setBaiduOcrCredentialTestState(false, QCoreApplication::translate("SpaceLook", "Baidu token response is invalid."));
            return;
        }

        const QJsonObject object = document.object();
        const QString token = object.value(QStringLiteral("access_token")).toString().trimmed();
        if (!token.isEmpty()) {
            setBaiduOcrCredentialTestState(false, QCoreApplication::translate("SpaceLook", "Baidu OCR credentials are valid."));
            return;
        }

        const QString error = baiduJsonErrorMessage(object);
        setBaiduOcrCredentialTestState(false, error.isEmpty()
            ? QCoreApplication::translate("SpaceLook", "Baidu credential test failed.")
            : error);
    });
}

bool SpaceLookUiSettings::alwaysUnblockProtectedView() const
{
    return m_alwaysUnblockProtectedView;
}

void SpaceLookUiSettings::setAlwaysUnblockProtectedView(bool enabled)
{
    if (!updateBoolSetting(m_alwaysUnblockProtectedView, enabled, QStringLiteral("ui/document/always_unblock_protected_view"))) {
        return;
    }

    emit documentPreviewSettingsChanged();
}

void SpaceLookUiSettings::writeBoolSetting(const QString& key, bool value)
{
    QSettings settings(settingsFilePath(), QSettings::IniFormat);
    settings.setValue(key, value);
    settings.sync();
}

void SpaceLookUiSettings::writeStringSetting(const QString& key, const QString& value)
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

bool SpaceLookUiSettings::updateStringSetting(QString& field, const QString& value, const QString& key)
{
    if (field == value) {
        return false;
    }

    field = value;
    writeStringSetting(key, value);
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

QString SpaceLookUiSettings::normalizedLanguage(const QString& language) const
{
    const QString normalized = language.trimmed().toLower();
    if (normalized == QStringLiteral("zh") || normalized == QStringLiteral("zh-cn")) {
        return QStringLiteral("zh");
    }
    return QStringLiteral("en");
}

QString SpaceLookUiSettings::normalizedOcrEngine(const QString& engine) const
{
    const QString normalized = engine.trimmed().toLower();
    if (normalized == QStringLiteral("baidu")) {
        return QStringLiteral("baidu");
    }
    return QStringLiteral("windows");
}

void SpaceLookUiSettings::setBaiduOcrCredentialTestState(bool busy, const QString& message)
{
    if (m_baiduOcrCredentialTestBusy == busy && m_baiduOcrCredentialTestMessage == message) {
        return;
    }

    m_baiduOcrCredentialTestBusy = busy;
    m_baiduOcrCredentialTestMessage = message;
    emit ocrSettingsChanged();
}
