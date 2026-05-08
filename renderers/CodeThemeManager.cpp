#include "renderers/CodeThemeManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#include "third_party/ksyntaxhighlighting/src/lib/repository.h"
#include "third_party/ksyntaxhighlighting/src/lib/theme.h"

QString CodeThemeManager::defaultThemeName()
{
    return QStringLiteral("GitHub Light");
}

QString CodeThemeManager::themeSearchRoot()
{
    const QDir appDir(QCoreApplication::applicationDirPath());
    return appDir.absoluteFilePath(QStringLiteral("ksyntaxhighlighting"));
}

KSyntaxHighlighting::Theme CodeThemeManager::resolveTheme(KSyntaxHighlighting::Repository& repository,
                                                          const QString& customThemePath)
{
    if (!customThemePath.trimmed().isEmpty()) {
        const QFileInfo customThemeInfo(customThemePath);
        if (customThemeInfo.exists()) {
            const QString customRoot = customThemeInfo.isDir()
                ? customThemeInfo.absoluteFilePath()
                : customThemeInfo.absolutePath();
            if (!customRoot.trimmed().isEmpty()) {
                repository.addCustomSearchPath(customRoot);
                const KSyntaxHighlighting::Theme customTheme = repository.theme(customThemeInfo.completeBaseName());
                if (customTheme.isValid()) {
                    return customTheme;
                }
            }
        }
    }

    const KSyntaxHighlighting::Theme githubLight = repository.theme(defaultThemeName());
    if (githubLight.isValid()) {
        return githubLight;
    }

    return repository.defaultTheme(KSyntaxHighlighting::Repository::LightTheme);
}
