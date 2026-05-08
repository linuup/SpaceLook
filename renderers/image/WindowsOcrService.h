#pragma once

#include <QImage>
#include <QString>

struct WindowsOcrResult
{
    bool success = false;
    bool unavailable = false;
    QString text;
    QString errorMessage;
};

class WindowsOcrService
{
public:
    static WindowsOcrResult recognizeText(const QImage& sourceImage);
};
