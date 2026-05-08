#pragma once

#include <QByteArray>
#include <QString>

namespace PreviewFileReader {

bool readAll(const QString& filePath, QByteArray* data, QString* errorMessage = nullptr);
bool readPrefix(const QString& filePath, qint64 maxBytes, QByteArray* data, bool* truncated = nullptr, QString* errorMessage = nullptr);

}
