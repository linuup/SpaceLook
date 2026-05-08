#pragma once

#include <QFileInfo>
#include <QSet>
#include <QString>
#include <QStringList>

namespace FileSuffixUtils
{
QString fullSuffix(const QFileInfo& fileInfo);
QString fullSuffix(const QString& filePath);
QStringList suffixCandidates(const QFileInfo& fileInfo);
QStringList suffixCandidates(const QString& filePath);
QString bestMatchingSuffix(const QStringList& candidates, const QSet<QString>& knownSuffixes);
}
