#include "core/file_suffix_utils.h"

namespace FileSuffixUtils
{

QString fullSuffix(const QFileInfo& fileInfo)
{
    return fileInfo.completeSuffix().trimmed().toLower();
}

QString fullSuffix(const QString& filePath)
{
    return fullSuffix(QFileInfo(filePath));
}

QStringList suffixCandidates(const QFileInfo& fileInfo)
{
    const QString suffix = fullSuffix(fileInfo);
    if (suffix.isEmpty()) {
        return {};
    }

    const QStringList parts = suffix.split(QLatin1Char('.'), Qt::SkipEmptyParts);
    if (parts.isEmpty()) {
        return {};
    }

    QStringList candidates;
    candidates.reserve(parts.size());
    for (int index = 0; index < parts.size(); ++index) {
        candidates.append(parts.mid(index).join(QStringLiteral(".")));
    }
    return candidates;
}

QStringList suffixCandidates(const QString& filePath)
{
    return suffixCandidates(QFileInfo(filePath));
}

QString bestMatchingSuffix(const QStringList& candidates, const QSet<QString>& knownSuffixes)
{
    for (const QString& candidate : candidates) {
        if (knownSuffixes.contains(candidate)) {
            return candidate;
        }
    }
    return QString();
}

}
