#pragma once

#include <QRectF>
#include <QString>
#include <QVector>

struct OcrTextBox
{
    QString text;
    QRectF imageRect;
};

struct OcrResult
{
    bool success = false;
    QString text;
    QVector<OcrTextBox> boxes;
    QString errorMessage;
};
