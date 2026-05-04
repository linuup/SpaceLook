#pragma once

#include <QObject>
#include <QWidget>

#include "renderers/IPreviewRenderer.h"

class PreviewState;
class QQuickWidget;

class QmlShellRenderer : public QWidget, public IPreviewRenderer
{
    Q_OBJECT

public:
    explicit QmlShellRenderer(PreviewState* previewState, QWidget* parent = nullptr);

    QString rendererId() const override;
    bool canHandle(const HoveredItemInfo& info) const override;
    QWidget* widget() override;
    void load(const HoveredItemInfo& info) override;
    void unload() override;

    Q_INVOKABLE void openSettingsWindow();
    Q_INVOKABLE void openSettingsPage();

private:
    PreviewState* m_previewState = nullptr;
    QQuickWidget* m_quickWidget = nullptr;
};
