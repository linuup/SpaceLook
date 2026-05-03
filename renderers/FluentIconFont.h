#pragma once

#include <QFont>
#include <QString>

namespace FluentIconFont {

QString family();
QFont iconFont(int pixelSize, int weight = QFont::Normal);
QString glyph(quint16 codePoint);

}
