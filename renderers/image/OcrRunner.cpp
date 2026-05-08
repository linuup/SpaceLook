#include "renderers/image/OcrRunner.h"

#include <QCoreApplication>

#include "renderers/image/BaiduOcrService.h"
#include "renderers/image/WindowsOcrService.h"

OcrResult OcrRunner::recognize(const QImage& image, const Config& config)
{
    if (config.engine == QStringLiteral("baidu")) {
        const QString apiKey = config.baiduApiKey.trimmed();
        const QString secretKey = config.baiduSecretKey.trimmed();
        if (apiKey.isEmpty() || secretKey.isEmpty()) {
            OcrResult result;
            result.errorMessage = QCoreApplication::translate("SpaceLook", "Baidu OCR requires API_KEY and SECRET_KEY.");
            return result;
        }
        return BaiduOcrService::recognizeText(image, apiKey, secretKey);
    }

    return WindowsOcrService::recognizeText(image);
}
