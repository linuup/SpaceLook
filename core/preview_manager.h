#pragma once

#include <QObject>
#include <QString>

#include "core/file_type_detector.h"

class SpaceLookWindow;
class PreviewState;

class PreviewManager : public QObject
{
    Q_OBJECT

public:
    explicit PreviewManager(QObject* parent = nullptr);
    ~PreviewManager() override;

    void showInitialPreview();
    void openPreviewForPath(const QString& filePath);
    void showSettingsWindow();

private slots:
    void handleSpaceHotkey();

private:
    SpaceLookWindow* ensureWindow();
    void hideActivePreview();
    void showHoveredItem(const HoveredItemInfo& info);
    void toggleWindowVisibility();
    bool shouldIgnoreSpaceHotkey() const;

    FileTypeDetector m_fileTypeDetector;
    PreviewState* m_previewState = nullptr;
    SpaceLookWindow* m_window = nullptr;
    HoveredItemInfo m_lastShownItem;
};
