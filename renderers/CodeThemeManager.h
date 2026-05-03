#pragma once

#include <QString>

namespace KSyntaxHighlighting
{
class Repository;
class Theme;
}

class CodeThemeManager
{
public:
    static QString defaultThemeName();
    static QString themeSearchRoot();
    static KSyntaxHighlighting::Theme resolveTheme(KSyntaxHighlighting::Repository& repository,
                                                   const QString& customThemePath = QString());
};
