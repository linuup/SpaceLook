#include "renderers/FluentIconFont.h"

#include <QFontDatabase>

namespace {

QString loadFontFamily()
{
    static QString cachedFamily;
    static bool attemptedLoad = false;
    if (attemptedLoad) {
        return cachedFamily;
    }

    attemptedLoad = true;
    const int fontId = QFontDatabase::addApplicationFont(QStringLiteral(":/SPACELOOK/font/SegoeFluentIcons.ttf"));
    if (fontId >= 0) {
        const QStringList families = QFontDatabase::applicationFontFamilies(fontId);
        if (!families.isEmpty()) {
            cachedFamily = families.first();
        }
    }

    if (cachedFamily.isEmpty()) {
        cachedFamily = QStringLiteral("Segoe Fluent Icons");
    }

    return cachedFamily;
}

}

namespace FluentIconFont {

QString family()
{
    return loadFontFamily();
}

QFont iconFont(int pixelSize, int weight)
{
    QFont font(loadFontFamily());
    font.setPixelSize(pixelSize);
    font.setWeight(weight);
    font.setStyleStrategy(QFont::PreferAntialias);
    return font;
}

QString glyph(quint16 codePoint)
{
    return QString(QChar(codePoint));
}

}
