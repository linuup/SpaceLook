#pragma once

#include "core/hovered_item_info.h"

class ExplorerItemLocator
{
public:
    HoveredItemInfo locateItemUnderCursor() const;
};
