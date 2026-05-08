#pragma once

#include <QString>

struct HoveredItemInfo
{
    bool valid = false;
    bool exists = false;
    bool isDirectory = false;
    QString title;
    QString typeKey;
    QString typeDetails;
    QString rendererName;
    QString sourceKind;
    QString filePath;
    QString fileName;
    QString folderPath;
    QString resolvedPath;
    QString sourceLabel;
    QString windowClassName;
    QString statusMessage;
};
