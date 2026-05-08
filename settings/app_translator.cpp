#include "settings/app_translator.h"

#include "settings/spacelook_ui_settings.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>

AppTranslator& AppTranslator::instance()
{
    static AppTranslator translator;
    return translator;
}

AppTranslator::AppTranslator(QObject* parent)
    : QObject(parent)
{
}

void AppTranslator::initialize()
{
    applyLanguage(SpaceLookUiSettings::instance().language());
    connect(&SpaceLookUiSettings::instance(), &SpaceLookUiSettings::languageChanged, this, [this]() {
        applyLanguage(SpaceLookUiSettings::instance().language());
    });
}

QString AppTranslator::currentLanguage() const
{
    return m_currentLanguage;
}

void AppTranslator::applyLanguage(const QString& language)
{
    const QString normalizedLanguage = language.trimmed().toLower() == QStringLiteral("zh")
        ? QStringLiteral("zh")
        : QStringLiteral("en");

    QCoreApplication::removeTranslator(&m_translator);
    m_currentLanguage = normalizedLanguage;

    if (normalizedLanguage == QStringLiteral("zh")) {
        loadTranslator(normalizedLanguage);
    }

    emit languageChanged();
}

bool AppTranslator::loadTranslator(const QString& language)
{
    const QString translationName = language == QStringLiteral("zh")
        ? QStringLiteral("SpaceLook_zh_CN")
        : QStringLiteral("SpaceLook_en");

    const QString appTranslationDir = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("i18n"));
    const QStringList searchPaths = {
        QStringLiteral(":/SPACELOOK/i18n"),
        appTranslationDir
    };

    for (const QString& path : searchPaths) {
        if (m_translator.load(translationName, path)) {
            QCoreApplication::installTranslator(&m_translator);
            qDebug().noquote() << QStringLiteral("[SpaceLookI18n] loaded translator name=%1 path=%2")
                .arg(translationName, path);
            return true;
        }
    }

    qDebug().noquote() << QStringLiteral("[SpaceLookI18n] translator not found name=%1").arg(translationName);
    return false;
}
