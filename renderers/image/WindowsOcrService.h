#pragma once

#include <QImage>

#include "renderers/image/OcrTypes.h"

class WindowsOcrService
{
public:
    static OcrResult recognizeText(const QImage& sourceImage);
};
