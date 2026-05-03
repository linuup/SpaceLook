#pragma once

#include <QString>

struct HoveredItemInfo
{
    bool valid = false;
    bool exists = false;
    bool isDirectory = false;
    QString title;
    QString typeKey;
    QString typeLabel;
    QString typeDetails;
    QString sourceKind;
    QString itemKind;
    QString filePath;
    QString fileName;
    QString folderPath;
    QString resolvedPath;
    QString sourceLabel;
    QString windowClassName;
    QString statusMessage;
};
