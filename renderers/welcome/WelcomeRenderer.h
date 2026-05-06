#pragma once

#include <QPixmap>
#include <QWidget>

#include "core/hovered_item_info.h"
#include "renderers/IPreviewRenderer.h"

class QLabel;

class WelcomeRenderer : public QWidget, public IPreviewRenderer
{
    Q_OBJECT

public:
    explicit WelcomeRenderer(QWidget* parent = nullptr);

    QString rendererId() const override;
    bool canHandle(const HoveredItemInfo& info) const override;
    QWidget* widget() override;
    void load(const HoveredItemInfo& info) override;
    void unload() override;

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void applyChrome();
    void updateHeroPixmap();

    QLabel* m_heroLabel = nullptr;
    QLabel* m_titleLabel = nullptr;
    QLabel* m_subtitleLabel = nullptr;
    QLabel* m_hintLabel = nullptr;
    QPixmap m_heroPixmap;
};
