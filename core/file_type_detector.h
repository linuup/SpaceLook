#pragma once

#include <QString>

#include "core/hovered_item_info.h"

struct DetectedTypeInfo
{
    QString typeKey;
    QString typeLabel;
    QString typeDetails;
    QString itemKind;
    bool isDirectory = false;
};

class FileTypeDetector
{
public:
    QString detectTypeLabel(const QString& filePath) const;
    HoveredItemInfo inspectPath(const QString& filePath,
                                const QString& sourceLabel = QStringLiteral("Command Line"),
                                const QString& windowClassName = QStringLiteral("SpaceLook")) const;
    HoveredItemInfo inspectItemUnderCursor() const;

private:
    DetectedTypeInfo detectTypeInfo(const QString& filePath, bool shellItem = false) const;
};
