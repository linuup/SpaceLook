#include "core/TextEncodingDetector.h"

#include <QTextCodec>

namespace {

bool isValidUtf8(const QByteArray& data)
{
    int expectedContinuationBytes = 0;
    for (const uchar byte : data) {
        if (expectedContinuationBytes == 0) {
            if ((byte & 0x80) == 0) {
                continue;
            }
            if ((byte & 0xE0) == 0xC0) {
                expectedContinuationBytes = 1;
                continue;
            }
            if ((byte & 0xF0) == 0xE0) {
                expectedContinuationBytes = 2;
                continue;
            }
            if ((byte & 0xF8) == 0xF0) {
                expectedContinuationBytes = 3;
                continue;
            }
            return false;
        }

        if ((byte & 0xC0) != 0x80) {
            return false;
        }
        --expectedContinuationBytes;
    }

    return expectedContinuationBytes == 0;
}

QString codecName(QTextCodec* codec)
{
    return codec ? QString::fromLatin1(codec->name()) : QStringLiteral("UTF-8");
}

}

namespace TextEncodingDetector {

DetectedTextEncoding decode(const QByteArray& data)
{
    DetectedTextEncoding result;

    if (data.startsWith(QByteArray::fromHex("EFBBBF"))) {
        result.name = QStringLiteral("UTF-8 BOM");
        result.text = QString::fromUtf8(data.constData() + 3, data.size() - 3);
        return result;
    }

    if (data.startsWith(QByteArray::fromHex("FFFE0000"))) {
        QTextCodec* codec = QTextCodec::codecForName("UTF-32LE");
        result.name = QStringLiteral("UTF-32 LE BOM");
        result.text = codec ? codec->toUnicode(data.constData() + 4, data.size() - 4) : QString::fromUtf8(data);
        return result;
    }

    if (data.startsWith(QByteArray::fromHex("0000FEFF"))) {
        QTextCodec* codec = QTextCodec::codecForName("UTF-32BE");
        result.name = QStringLiteral("UTF-32 BE BOM");
        result.text = codec ? codec->toUnicode(data.constData() + 4, data.size() - 4) : QString::fromUtf8(data);
        return result;
    }

    if (data.startsWith(QByteArray::fromHex("FFFE"))) {
        QTextCodec* codec = QTextCodec::codecForName("UTF-16LE");
        result.name = QStringLiteral("UTF-16 LE BOM");
        result.text = codec ? codec->toUnicode(data.constData() + 2, data.size() - 2) : QString::fromUtf8(data);
        return result;
    }

    if (data.startsWith(QByteArray::fromHex("FEFF"))) {
        QTextCodec* codec = QTextCodec::codecForName("UTF-16BE");
        result.name = QStringLiteral("UTF-16 BE BOM");
        result.text = codec ? codec->toUnicode(data.constData() + 2, data.size() - 2) : QString::fromUtf8(data);
        return result;
    }

    QTextCodec* detectedCodec = QTextCodec::codecForUtfText(data, nullptr);
    if (detectedCodec) {
        result.name = codecName(detectedCodec);
        result.text = detectedCodec->toUnicode(data);
        return result;
    }

    if (isValidUtf8(data)) {
        result.name = QStringLiteral("UTF-8");
        result.text = QString::fromUtf8(data);
        return result;
    }

    QTextCodec* localeCodec = QTextCodec::codecForLocale();
    result.name = codecName(localeCodec);
    result.text = localeCodec ? localeCodec->toUnicode(data) : QString::fromUtf8(data);
    return result;
}

}
