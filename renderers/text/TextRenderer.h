#pragma once

#include <QtGlobal>
#include <QWidget>

#include "core/hovered_item_info.h"
#include "renderers/IPreviewRenderer.h"
#include "renderers/PreviewCancellationToken.h"
#include "renderers/PreviewLoadGuard.h"
class QLabel;
class ModeSwitchButton;
class OpenWithButton;
class QPlainTextEdit;
class QStackedWidget;
class SelectableTitleLabel;
class QWidget;
class PreviewState;

class TextRenderer : public QWidget, public IPreviewRenderer
{
    Q_OBJECT

public:
    explicit TextRenderer(PreviewState* previewState, QWidget* parent = nullptr);

    QString rendererId() const override;
    bool canHandle(const HoveredItemInfo& info) const override;
    QWidget* widget() override;
    void load(const HoveredItemInfo& info) override;
    void unload() override;
    bool reportsLoadingState() const override;
    void setLoadingStateCallback(std::function<void(bool)> callback) override;

private:
    void applyChrome();
    void showStatusMessage(const QString& message);
    void updateModeSelector(const QString& filePath);
    void notifyLoadingState(bool loading);

    HoveredItemInfo m_info;
    PreviewLoadGuard m_loadGuard;
    PreviewCancellationToken m_cancelToken;
    std::function<void(bool)> m_loadingStateCallback;
    QWidget* m_headerRow = nullptr;
    QLabel* m_iconLabel = nullptr;
    SelectableTitleLabel* m_titleLabel = nullptr;
    QLabel* m_metaLabel = nullptr;
    QWidget* m_pathRow = nullptr;
    QLabel* m_pathTitleLabel = nullptr;
    QLabel* m_pathValueLabel = nullptr;
    OpenWithButton* m_openWithButton = nullptr;
    QWidget* m_contentSection = nullptr;
    QWidget* m_statusRow = nullptr;
    QLabel* m_statusLabel = nullptr;
    ModeSwitchButton* m_modeSwitchButton = nullptr;
    QStackedWidget* m_contentStack = nullptr;
    QWidget* m_loadingCard = nullptr;
    QLabel* m_loadingTitleLabel = nullptr;
    QLabel* m_loadingMessageLabel = nullptr;
    QPlainTextEdit* m_textEdit = nullptr;
    PreviewState* m_previewState = nullptr;
};
