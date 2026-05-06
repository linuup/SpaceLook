#pragma once

#include <functional>

#include <QString>

#include "core/hovered_item_info.h"

class QWidget;

class IPreviewRenderer
{
public:
    virtual ~IPreviewRenderer() = default;

    virtual QString rendererId() const = 0;
    virtual bool canHandle(const HoveredItemInfo& info) const = 0;
    virtual QWidget* widget() = 0;
    virtual void load(const HoveredItemInfo& info) = 0;
    virtual void unload() = 0;
    virtual bool reportsLoadingState() const { return false; }
    virtual void setLoadingStateCallback(std::function<void(bool)> callback) { (void)callback; }
};
