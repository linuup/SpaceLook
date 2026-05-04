#pragma once

#include <QWidget>
#include <QPoint>

struct HoveredItemInfo;
class PreviewState;
class PreviewHost;
class PreviewCapsuleMenu;
class QSystemTrayIcon;
class QMenu;

class SpaceLookWindow : public QWidget
{
    Q_OBJECT

public:
    explicit SpaceLookWindow(PreviewState* previewState, QWidget* parent = nullptr);
    ~SpaceLookWindow() override;

    void showPreview(const HoveredItemInfo& info);
    void hidePreview();
    bool isPreviewVisible() const;
    bool containsGlobalPoint(const QPoint& globalPos) const;
    void handleGlobalSpacePressed();
    void toggleAlwaysOnTop();
    bool isAlwaysOnTop() const;
    void openCurrentInExplorer();
    void copyCurrentPath();
    void refreshCurrentPreview();
    void openCurrentWithDefaultApp();
    void showOpenWithMenuAt(const QPoint& globalPos);
    void showSettingsWindow();
    void toggleExpandedPreview();
    bool isExpandedPreview() const;
    bool supportsExpandedPreview() const;
    Q_INVOKABLE void requestSettingsWindowMinimize();
    Q_INVOKABLE void requestSettingsWindowToggleMaximize();
    Q_INVOKABLE void requestSettingsWindowClose();

signals:
    void spaceHotkeyPressed();

protected:
    void hideEvent(QHideEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void applyWindowChromeStyle();
    void applyTaskbarVisibility();
    void applyPerformanceMode();
    void ensureTrayIcon();
    void updateTrayVisibility();
    bool isDocumentPreview(const HoveredItemInfo& info) const;
    bool isLargeContentPreview(const HoveredItemInfo& info) const;
    bool isMediumContentPreview(const HoveredItemInfo& info) const;
    bool isCompactSummaryPreview(const HoveredItemInfo& info) const;
    bool supportsExpandedPreview(const HoveredItemInfo& info) const;
    void applyPreferredSizeForPreview(const HoveredItemInfo& info);
    void applyExpandedPreviewSize(const HoveredItemInfo& info);
    void installSpaceHook();
    void uninstallSpaceHook();
    Qt::Edges resizeEdgesForPosition(const QPoint& localPos) const;
    void updateCursorForPosition(const QPoint& localPos);
    QString currentPreviewPath() const;

    QWidget* m_container = nullptr;
    QWidget* m_menuRegion = nullptr;
    QWidget* m_surface = nullptr;
    PreviewHost* m_previewHost = nullptr;
    PreviewCapsuleMenu* m_menuBar = nullptr;
    PreviewState* m_previewState = nullptr;
    bool m_alwaysOnTop = false;
    bool m_expandedPreview = false;
    bool m_suppressPreviewStopOnHide = false;
    bool m_draggingWindow = false;
    bool m_resizingWindow = false;
    QPoint m_dragOffset;
    QPoint m_resizeStartGlobalPos;
    QRect m_resizeStartGeometry;
    Qt::Edges m_activeResizeEdges = Qt::Edges();
    QSystemTrayIcon* m_trayIcon = nullptr;
    QMenu* m_trayMenu = nullptr;
};
