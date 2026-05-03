#pragma once

#include <QIcon>
#include <QString>

#include "core/hovered_item_info.h"

namespace FileTypeIconResolver
{
QString iconResourcePath(const HoveredItemInfo& info);
QIcon iconForInfo(const HoveredItemInfo& info);
}
