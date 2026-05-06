#pragma once

#include <QObject>

#include "core/hovered_item_info.h"

class PreviewState : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString typeKey READ typeKey NOTIFY changed)
    Q_PROPERTY(QString typeDetails READ typeDetails NOTIFY changed)
    Q_PROPERTY(QString rendererName READ rendererName NOTIFY changed)
    Q_PROPERTY(QString sourceKind READ sourceKind NOTIFY changed)
    Q_PROPERTY(QString filePath READ filePath NOTIFY changed)
    Q_PROPERTY(QString fileName READ fileName NOTIFY changed)
    Q_PROPERTY(QString folderPath READ folderPath NOTIFY changed)
    Q_PROPERTY(QString resolvedPath READ resolvedPath NOTIFY changed)
    Q_PROPERTY(QString sourceLabel READ sourceLabel NOTIFY changed)
    Q_PROPERTY(QString windowClassName READ windowClassName NOTIFY changed)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY changed)
    Q_PROPERTY(QString rendererOverride READ rendererOverride NOTIFY changed)
    Q_PROPERTY(bool hasItem READ hasItem NOTIFY changed)
    Q_PROPERTY(bool exists READ exists NOTIFY changed)
    Q_PROPERTY(bool isDirectory READ isDirectory NOTIFY changed)

public:
    explicit PreviewState(QObject* parent = nullptr);

    HoveredItemInfo hoveredItem() const;
    QString title() const;
    QString typeKey() const;
    QString typeDetails() const;
    QString rendererName() const;
    QString sourceKind() const;
    QString filePath() const;
    QString fileName() const;
    QString folderPath() const;
    QString resolvedPath() const;
    QString sourceLabel() const;
    QString windowClassName() const;
    QString statusMessage() const;
    QString rendererOverride() const;
    bool hasItem() const;
    bool exists() const;
    bool isDirectory() const;

    void setHoveredItem(const HoveredItemInfo& info);
    void setRendererOverride(const QString& rendererId);
    void clearRendererOverride();

signals:
    void changed();

private:
    HoveredItemInfo m_info;
    QString m_rendererOverride;
};
