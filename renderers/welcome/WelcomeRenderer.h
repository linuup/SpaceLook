#pragma once

#include <QList>
#include <QPixmap>
#include <QWidget>

#include "core/hovered_item_info.h"
#include "renderers/IPreviewRenderer.h"

class QHBoxLayout;
class QLabel;
class QPushButton;
class QTimer;

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
    void updateChipTrackGeometry();
    void scrollFeatureChips();
    void relayoutFeatureChips();
    QWidget* duplicateFirstFeatureChip();

    QWidget* m_card = nullptr;
    QLabel* m_heroLabel = nullptr;
    QLabel* m_titleLabel = nullptr;
    QLabel* m_subtitleLabel = nullptr;
    QWidget* m_chipRow = nullptr;
    QWidget* m_chipTrack = nullptr;
    QHBoxLayout* m_chipLayout = nullptr;
    QTimer* m_chipScrollTimer = nullptr;
    QList<QWidget*> m_featureChips;
    QWidget* m_trailingDuplicateChip = nullptr;
    QPushButton* m_exploreButton = nullptr;
    QLabel* m_hintLabel = nullptr;
    QPixmap m_heroPixmap;
    double m_chipTrackX = 0.0;
    int m_lastChipRowWidth = 0;
};
