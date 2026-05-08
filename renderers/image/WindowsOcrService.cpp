#include "renderers/image/WindowsOcrService.h"

#include <QCoreApplication>
#include <QSize>
#include <QStringList>

#include <cstring>
#include <stdexcept>
#include <string>

#include <Unknwn.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Storage.Streams.h>

struct __declspec(uuid("5B0D3235-4DBA-4D44-865E-8F1D0E4FD04D")) IMemoryBufferByteAccess : ::IUnknown
{
    virtual HRESULT __stdcall GetBuffer(uint8_t** value, uint32_t* capacity) = 0;
};

namespace {

constexpr int kMaxOcrImageSide = 2400;

QString unavailableMessage()
{
    return QCoreApplication::translate("SpaceLook", "Windows OCR is unavailable in this runtime.");
}

QImage normalizedOcrImage(const QImage& sourceImage)
{
    if (sourceImage.isNull()) {
        return QImage();
    }

    QImage image = sourceImage;
    const QSize sourceSize = image.size();
    const int longestSide = qMax(sourceSize.width(), sourceSize.height());
    if (longestSide > kMaxOcrImageSide) {
        image = image.scaled(QSize(kMaxOcrImageSide, kMaxOcrImageSide),
                             Qt::KeepAspectRatio,
                             Qt::SmoothTransformation);
    }

    return image.convertToFormat(QImage::Format_RGBA8888);
}

winrt::Windows::Graphics::Imaging::SoftwareBitmap softwareBitmapFromImage(const QImage& sourceImage)
{
    using namespace winrt::Windows::Graphics::Imaging;
    using namespace winrt::Windows::Storage::Streams;

    const QImage image = normalizedOcrImage(sourceImage);
    if (image.isNull()) {
        return nullptr;
    }

    SoftwareBitmap bitmap(BitmapPixelFormat::Rgba8,
                          image.width(),
                          image.height(),
                          BitmapAlphaMode::Premultiplied);
    BitmapBuffer buffer = bitmap.LockBuffer(BitmapBufferAccessMode::Write);
    winrt::Windows::Foundation::IMemoryBufferReference reference = buffer.CreateReference();
    auto byteAccess = reference.as<IMemoryBufferByteAccess>();

    uint8_t* destination = nullptr;
    uint32_t capacity = 0;
    winrt::check_hresult(byteAccess->GetBuffer(&destination, &capacity));
    if (!destination || capacity == 0) {
        return nullptr;
    }

    const BitmapPlaneDescription plane = buffer.GetPlaneDescription(0);
    const int copyWidth = image.width() * 4;
    const int copyHeight = image.height();
    if (plane.Stride <= 0 || copyWidth <= 0 || copyHeight <= 0) {
        return nullptr;
    }

    for (int y = 0; y < copyHeight; ++y) {
        const auto source = image.constScanLine(y);
        auto target = destination + plane.StartIndex + (static_cast<size_t>(y) * static_cast<size_t>(plane.Stride));
        memcpy(target, source, static_cast<size_t>(copyWidth));
    }

    return bitmap;
}

QString textFromOcrResult(const winrt::Windows::Media::Ocr::OcrResult& ocrResult)
{
    QStringList lines;
    const auto ocrLines = ocrResult.Lines();
    for (uint32_t index = 0; index < ocrLines.Size(); ++index) {
        const auto line = ocrLines.GetAt(index);
        const QString text = QString::fromStdWString(std::wstring(line.Text())).trimmed();
        if (!text.isEmpty()) {
            lines.push_back(text);
        }
    }
    return lines.join(QChar::LineFeed);
}

}

OcrResult WindowsOcrService::recognizeText(const QImage& sourceImage)
{
    ::OcrResult result;
    if (sourceImage.isNull()) {
        result.errorMessage = QCoreApplication::translate("SpaceLook", "Image is unavailable for OCR.");
        return result;
    }

    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);

        const winrt::Windows::Media::Ocr::OcrEngine engine =
            winrt::Windows::Media::Ocr::OcrEngine::TryCreateFromUserProfileLanguages();
        if (!engine) {
            result.errorMessage = QCoreApplication::translate("SpaceLook", "No Windows OCR language is available.");
            return result;
        }

        const auto bitmap = softwareBitmapFromImage(sourceImage);
        if (!bitmap) {
            result.errorMessage = QCoreApplication::translate("SpaceLook", "Failed to prepare image for OCR.");
            return result;
        }

        const winrt::Windows::Media::Ocr::OcrResult ocrResult = engine.RecognizeAsync(bitmap).get();
        result.text = textFromOcrResult(ocrResult).trimmed();
        result.success = true;
        return result;
    } catch (const winrt::hresult_error& error) {
        result.errorMessage = unavailableMessage() + QStringLiteral(" 0x%1")
            .arg(static_cast<quint32>(error.code()), 8, 16, QLatin1Char('0'));
        return result;
    } catch (const std::exception& error) {
        result.errorMessage = QString::fromLocal8Bit(error.what()).trimmed();
        if (result.errorMessage.isEmpty()) {
            result.errorMessage = unavailableMessage();
        }
        return result;
    }
}
