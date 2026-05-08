#pragma once

#include <memory>

#include <QtGlobal>
#include <QWidget>

#include "core/hovered_item_info.h"
#include "renderers/IPreviewRenderer.h"
#include "renderers/PreviewCancellationToken.h"
#include "renderers/PreviewLoadGuard.h"

namespace KSyntaxHighlighting
{
class Repository;
class SyntaxHighlighter;
}

class QLabel;
class QLineEdit;
class ModeSwitchButton;
class OpenWithButton;
class QPlainTextEdit;
class QStackedWidget;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;
class SelectableTitleLabel;
class QWidget;
class PreviewState;

class CodeRenderer : public QWidget, public IPreviewRenderer
{
    Q_OBJECT

public:
    explicit CodeRenderer(PreviewState* previewState, QWidget* parent = nullptr);
    ~CodeRenderer() override;

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
    void ensureRepository();
    void applyDefinitionForPath(const QString& filePath);
    void toggleStructuredItem(QTreeWidgetItem* item);
    void showStructuredContextMenu(const QPoint& position);
    void updateModeSelector(const QString& filePath);
    void showStructuredPreview(const QString& filePath, const QString& text);
    void clearStructuredPreview();
    bool tryLoadJsonPreview(const QString& text);
    bool tryLoadXmlPreview(const QString& text);
    bool tryLoadYamlPreview(const QString& text);
    void notifyLoadingState(bool loading);
    void findSearchMatch(bool backwards);
    void findTextSearchMatch(bool backwards);
    void rebuildStructuredSearchMatches();
    void selectStructuredSearchMatch(int index);
    void updateSearchSummary();
    void resetSearch();
    void showSearchRow();
    void hideSearchRow(bool clearQuery);
    static bool isStructuredPreviewPath(const QString& filePath);

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
    QWidget* m_searchRow = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QToolButton* m_searchPreviousButton = nullptr;
    QToolButton* m_searchNextButton = nullptr;
    QLabel* m_searchCountLabel = nullptr;
    QLabel* m_statusLabel = nullptr;
    ModeSwitchButton* m_modeSwitchButton = nullptr;
    QStackedWidget* m_contentStack = nullptr;
    QWidget* m_loadingCard = nullptr;
    QLabel* m_loadingTitleLabel = nullptr;
    QLabel* m_loadingMessageLabel = nullptr;
    QPlainTextEdit* m_textEdit = nullptr;
    QTreeWidget* m_treeView = nullptr;
    PreviewState* m_previewState = nullptr;
    QVector<QTreeWidgetItem*> m_searchMatches;
    QString m_lastSearchQuery;
    int m_searchMatchIndex = -1;
    std::unique_ptr<KSyntaxHighlighting::Repository> m_repository;
    KSyntaxHighlighting::SyntaxHighlighter* m_highlighter = nullptr;
};
