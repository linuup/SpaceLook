#pragma once

#include <QImage>
#include <QString>

#include "renderers/image/OcrTypes.h"

class BaiduOcrService
{
public:
    static OcrResult recognizeText(const QImage& sourceImage, const QString& apiKey, const QString& secretKey);
};
