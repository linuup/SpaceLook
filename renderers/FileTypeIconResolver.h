#pragma once

#include <QIcon>
#include <QPixmap>
#include <QSize>
#include <QString>

#include "core/hovered_item_info.h"

namespace FileTypeIconResolver
{
QString iconResourcePath(const HoveredItemInfo& info);
QIcon iconForInfo(const HoveredItemInfo& info);
QPixmap pixmapForInfo(const HoveredItemInfo& info, const QSize& displaySize);
}
