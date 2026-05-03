#pragma once

#include <QtGlobal>
#include <QWidget>

#include "core/hovered_item_info.h"
#include "renderers/IPreviewRenderer.h"

class QLabel;
class OpenWithButton;
class SelectableTitleLabel;
class QTextBrowser;
class QWidget;

class DocumentRenderer : public QWidget, public IPreviewRenderer
{
    Q_OBJECT

public:
    explicit DocumentRenderer(QWidget* parent = nullptr);

    QString rendererId() const override;
    bool canHandle(const HoveredItemInfo& info) const override;
    QWidget* widget() override;
    void load(const HoveredItemInfo& info) override;
    void unload() override;

private:
    void applyChrome();
    void showStatusMessage(const QString& message);

    HoveredItemInfo m_info;
    quint64 m_loadRequestId = 0;
    QWidget* m_headerRow = nullptr;
    QLabel* m_iconLabel = nullptr;
    SelectableTitleLabel* m_titleLabel = nullptr;
    QLabel* m_metaLabel = nullptr;
    QWidget* m_pathRow = nullptr;
    QLabel* m_pathTitleLabel = nullptr;
    QLabel* m_pathValueLabel = nullptr;
    OpenWithButton* m_openWithButton = nullptr;
    QLabel* m_statusLabel = nullptr;
    QTextBrowser* m_textBrowser = nullptr;
};
