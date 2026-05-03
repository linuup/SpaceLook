#include "platform/explorer_item_locator.h"

#include "core/file_type_detector.h"

HoveredItemInfo ExplorerItemLocator::locateItemUnderCursor() const
{
    return FileTypeDetector().inspectItemUnderCursor();
}
