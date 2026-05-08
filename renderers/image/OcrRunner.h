#pragma once

#include <QImage>
#include <QString>

#include "renderers/image/OcrTypes.h"

class OcrRunner
{
public:
    struct Config {
        QString engine;
        QString baiduApiKey;
        QString baiduSecretKey;
    };

    static OcrResult recognize(const QImage& image, const Config& config);
};
