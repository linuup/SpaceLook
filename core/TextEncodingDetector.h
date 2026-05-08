#pragma once

#include <QByteArray>
#include <QString>

struct DetectedTextEncoding
{
    QString name;
    QString text;
};

namespace TextEncodingDetector {

DetectedTextEncoding decode(const QByteArray& data);

}
