#include "core/preview_state.h"

PreviewState::PreviewState(QObject* parent)
    : QObject(parent)
{
}

HoveredItemInfo PreviewState::hoveredItem() const
{
    return m_info;
}

QString PreviewState::title() const
{
    return m_info.title;
}

QString PreviewState::typeKey() const
{
    return m_info.typeKey;
}

QString PreviewState::typeLabel() const
{
    return m_info.typeLabel;
}

QString PreviewState::typeDetails() const
{
    return m_info.typeDetails;
}

QString PreviewState::sourceKind() const
{
    return m_info.sourceKind;
}

QString PreviewState::itemKind() const
{
    return m_info.itemKind;
}

QString PreviewState::filePath() const
{
    return m_info.filePath;
}

QString PreviewState::fileName() const
{
    return m_info.fileName;
}

QString PreviewState::folderPath() const
{
    return m_info.folderPath;
}

QString PreviewState::resolvedPath() const
{
    return m_info.resolvedPath;
}

QString PreviewState::sourceLabel() const
{
    return m_info.sourceLabel;
}

QString PreviewState::windowClassName() const
{
    return m_info.windowClassName;
}

QString PreviewState::statusMessage() const
{
    return m_info.statusMessage;
}

bool PreviewState::hasItem() const
{
    return m_info.valid;
}

bool PreviewState::exists() const
{
    return m_info.exists;
}

bool PreviewState::isDirectory() const
{
    return m_info.isDirectory;
}

void PreviewState::setHoveredItem(const HoveredItemInfo& info)
{
    m_info = info;
    emit changed();
}
