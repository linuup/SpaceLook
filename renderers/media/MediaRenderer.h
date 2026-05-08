#pragma once

#include <QWidget>

#include "core/hovered_item_info.h"
#include "renderers/IPreviewRenderer.h"

class QLabel;
class OpenWithButton;
class QPushButton;
class QSlider;
class QVBoxLayout;
class QMediaPlayer;
class QTimer;
class QEvent;
class QPoint;
class QVideoWidget;
class InternalMpvVideoBackend;
class WindowsMediaPlayerAudioBackend;
class SelectableTitleLabel;

class MediaRenderer : public QWidget, public IPreviewRenderer
{
    Q_OBJECT

public:
    explicit MediaRenderer(QWidget* parent = nullptr);
    ~MediaRenderer() override;

    QString rendererId() const override;
    bool canHandle(const HoveredItemInfo& info) const override;
    QWidget* widget() override;
    void load(const HoveredItemInfo& info) override;
    void unload() override;
    void warmUp() override;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void applyChrome();
    void ensureAudioBackend();
    void destroyAudioBackend();
    void ensureVideoBackend();
    void destroyVideoBackend();
    bool tryStartMpvVideo();
    bool tryStartMpvAudio();
    bool tryStartWindowsAudioFallback(const QString& reason);
    void fallbackToMpvVideo(const QString& reason);
    void fallbackToMpvAudio(const QString& reason);
    QString mpvInstallHint(const QString& reason) const;
    QString windowsBackendFailureMessage(const QString& reason) const;
    bool isMpvInstallHintMessage(const QString& message) const;
    void seekToSliderValue(int value);
    int sliderValueForMousePosition(QSlider* slider, const QPoint& position) const;
    void togglePlayback();
    void toggleVolumePopup();
    void setVolumeLevel(int value);
    int currentVolumeLevel() const;
    void updatePlaybackButton();
    void updateVolumeButton();
    void updateVolumePopup();
    void updateMediaUi();
    void syncVideoViewportGeometry();
    void updateStatusLabel(const QString& message);
    void updateCenterOverlay();
    QString formatTime(qint64 milliseconds) const;
    bool isMutedState() const;
    bool isPlaybackPaused() const;
    bool isPlaybackRunning() const;
    bool shouldShowVideoPlaceholder() const;

    HoveredItemInfo m_info;
    bool m_isSeeking = false;
    bool m_usingMpvVideo = false;
    bool m_usingMpvPlayback = false;
    bool m_usingWindowsAudioFallback = false;
    bool m_mpvPaused = true;
    bool m_videoPreviewReady = false;
    bool m_videoStartedPlayback = false;
    bool m_triedMpvVideoFallback = false;
    bool m_triedMpvAudioFallback = false;
    bool m_allowMpvFallbackForCurrentMedia = false;
    QString m_currentCodecSummary;
    QWidget* m_headerRow = nullptr;
    QLabel* m_iconLabel = nullptr;
    SelectableTitleLabel* m_titleLabel = nullptr;
    QLabel* m_metaLabel = nullptr;
    QWidget* m_pathRow = nullptr;
    QLabel* m_pathTitleLabel = nullptr;
    QLabel* m_pathValueLabel = nullptr;
    OpenWithButton* m_openWithButton = nullptr;
    QWidget* m_statusRow = nullptr;
    QLabel* m_statusLabel = nullptr;
    QPushButton* m_mpvHelpButton = nullptr;
    QWidget* m_stageCard = nullptr;
    QVBoxLayout* m_stageLayout = nullptr;
    QWidget* m_controlsCard = nullptr;
    QWidget* m_mediaViewport = nullptr;
    QLabel* m_centerOverlayLabel = nullptr;
    QLabel* m_audioPlaceholder = nullptr;
    QLabel* m_videoPlaceholder = nullptr;
    QWidget* m_videoHost = nullptr;
    QVideoWidget* m_qtVideoWidget = nullptr;
    QPushButton* m_playPauseButton = nullptr;
    QPushButton* m_volumeButton = nullptr;
    QLabel* m_positionLabel = nullptr;
    QLabel* m_durationLabel = nullptr;
    QSlider* m_positionSlider = nullptr;
    QLabel* m_volumeValueLabel = nullptr;
    QSlider* m_volumeSlider = nullptr;
    QMediaPlayer* m_player = nullptr;
    QTimer* m_videoPollTimer = nullptr;
    InternalMpvVideoBackend* m_mpvBackend = nullptr;
    WindowsMediaPlayerAudioBackend* m_windowsAudioBackend = nullptr;
    bool m_videoMuted = true;
    int m_audioVolume = 50;
    int m_videoVolume = 50;
};
