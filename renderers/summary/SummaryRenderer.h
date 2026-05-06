#pragma once

#include <QWidget>

#include "core/hovered_item_info.h"
#include "renderers/IPreviewRenderer.h"

class QLabel;
class OpenWithButton;
class QFrame;
class PreviewState;
class QScrollArea;
class SelectableTitleLabel;
class QWidget;

class SummaryRenderer : public QWidget, public IPreviewRenderer
{
    Q_OBJECT

public:
    explicit SummaryRenderer(PreviewState* previewState = nullptr, QWidget* parent = nullptr);

    QString rendererId() const override;
    bool canHandle(const HoveredItemInfo& info) const override;
    QWidget* widget() override;
    void load(const HoveredItemInfo& info) override;
    void unload() override;

private:
    void applyChrome();
    QWidget* createDetailBlock(const QString& title, QLabel** valueLabel, QWidget* parent);
    QFrame* createDetailLine(const QString& objectName, QWidget* parent);
    void showStatusMessage(const QString& message);
    void setDetailValues(const HoveredItemInfo& info);
    void applyInfo(const HoveredItemInfo& info);

    QWidget* m_headerRow = nullptr;
    QLabel* m_iconLabel = nullptr;
    SelectableTitleLabel* m_titleLabel = nullptr;
    QLabel* m_metaLabel = nullptr;
    QWidget* m_pathRow = nullptr;
    QLabel* m_pathTitleLabel = nullptr;
    QLabel* m_pathValueLabel = nullptr;
    OpenWithButton* m_openWithButton = nullptr;
    QLabel* m_statusLabel = nullptr;
    QWidget* m_detailsPanel = nullptr;
    QScrollArea* m_detailsScrollArea = nullptr;
    QWidget* m_detailsContent = nullptr;
    QLabel* m_folderValueLabel = nullptr;
    QLabel* m_createdValueLabel = nullptr;
    QLabel* m_modifiedValueLabel = nullptr;
    QLabel* m_sizeValueLabel = nullptr;
    QWidget* m_resolvedTargetSection = nullptr;
    QLabel* m_resolvedTargetValueLabel = nullptr;
    HoveredItemInfo m_currentInfo;
};
