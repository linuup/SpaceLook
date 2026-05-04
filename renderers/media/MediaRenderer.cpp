#include "renderers/media/MediaRenderer.h"

#include <QBoxLayout>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLibrary>
#include <QMediaPlayer>
#include <QFrame>
#include <QMouseEvent>
#include <QPushButton>
#include <QSlider>
#include <QStackedLayout>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVariant>
#include <QResizeEvent>
#include <QShowEvent>

#include "renderers/FileTypeIconResolver.h"
#include "renderers/FluentIconFont.h"
#include "renderers/OpenWithButton.h"
#include "renderers/PreviewHeaderBar.h"
#include "renderers/SelectableTitleLabel.h"
#include "widgets/SpaceLookWindow.h"

namespace {

bool isVideoType(const HoveredItemInfo& info)
{
    return info.typeKey == QStringLiteral("video");
}

bool isAudioType(const HoveredItemInfo& info)
{
    return info.typeKey == QStringLiteral("audio");
}

QString displayTitleForMedia(const HoveredItemInfo& info)
{
    if (!info.fileName.trimmed().isEmpty()) {
        return info.fileName;
    }
    if (!info.title.trimmed().isEmpty()) {
        return info.title;
    }
    return QStringLiteral("Media Preview");
}

QString playGlyph()
{
    return FluentIconFont::glyph(0xE768);
}

QString pauseGlyph()
{
    return FluentIconFont::glyph(0xE769);
}

QString volumeGlyph()
{
    return FluentIconFont::glyph(0xE15D);
}

QString mutedGlyph()
{
    return FluentIconFont::glyph(0xE74F);
}

enum mpv_format {
    MPV_FORMAT_NONE = 0,
    MPV_FORMAT_STRING = 1,
    MPV_FORMAT_OSD_STRING = 2,
    MPV_FORMAT_FLAG = 3,
    MPV_FORMAT_INT64 = 4,
    MPV_FORMAT_DOUBLE = 5
};

struct mpv_handle;

using mpv_create_fn = mpv_handle* (*)();
using mpv_initialize_fn = int (*)(mpv_handle*);
using mpv_terminate_destroy_fn = void (*)(mpv_handle*);
using mpv_set_option_string_fn = int (*)(mpv_handle*, const char*, const char*);
using mpv_set_property_fn = int (*)(mpv_handle*, const char*, mpv_format, void*);
using mpv_get_property_fn = int (*)(mpv_handle*, const char*, mpv_format, void*);
using mpv_command_fn = int (*)(mpv_handle*, const char**);
using mpv_error_string_fn = const char* (*)(int);

}

class InternalMpvVideoBackend
{
public:
    bool initialize(QWidget* videoHost, QString* errorMessage)
    {
        if (!loadLibrary(errorMessage)) {
            return false;
        }

        shutdown();

        m_handle = m_create ? m_create() : nullptr;
        if (!m_handle) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("libmpv handle creation failed.");
            }
            return false;
        }

        if (!setOptionString("terminal", "no", errorMessage) ||
            !setOptionString("input-default-bindings", "no", errorMessage) ||
            !setOptionString("input-vo-keyboard", "no", errorMessage) ||
            !setOptionString("osc", "no", errorMessage) ||
            !setOptionString("border", "no", errorMessage) ||
            !setOptionString("keep-open", "yes", errorMessage) ||
            !setOptionString("pause", "yes", errorMessage)) {
            shutdown();
            return false;
        }

        const QByteArray widValue = QByteArray::number(static_cast<qulonglong>(videoHost->winId()));
        if (!setOptionString("wid", widValue.constData(), errorMessage)) {
            shutdown();
            return false;
        }

        const int initResult = m_initialize ? m_initialize(m_handle) : -1;
        if (initResult < 0) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("libmpv initialize failed: %1").arg(errorToString(initResult));
            }
            shutdown();
            return false;
        }

        return true;
    }

    bool loadFile(const QString& filePath, QString* errorMessage)
    {
        if (!m_handle || !m_command) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("libmpv is not initialized.");
            }
            return false;
        }

        const QByteArray utf8Path = QDir::toNativeSeparators(filePath).toUtf8();
        const char* command[] = { "loadfile", utf8Path.constData(), "replace", nullptr };
        const int result = m_command(m_handle, command);
        if (result < 0) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("libmpv loadfile failed: %1").arg(errorToString(result));
            }
            return false;
        }

        return true;
    }

    void shutdown()
    {
        if (m_handle && m_terminateDestroy) {
            m_terminateDestroy(m_handle);
        }
        m_handle = nullptr;
    }

    bool isLoaded() const
    {
        return m_handle != nullptr;
    }

    bool setPaused(bool paused)
    {
        if (!m_handle || !m_setProperty) {
            return false;
        }

        int pauseFlag = paused ? 1 : 0;
        return m_setProperty(m_handle, "pause", MPV_FORMAT_FLAG, &pauseFlag) >= 0;
    }

    bool paused(bool* pausedState) const
    {
        if (!m_handle || !m_getProperty || !pausedState) {
            return false;
        }

        int pauseFlag = 0;
        const int result = m_getProperty(m_handle, "pause", MPV_FORMAT_FLAG, &pauseFlag);
        if (result < 0) {
            return false;
        }

        *pausedState = pauseFlag != 0;
        return true;
    }

    bool durationSeconds(double* duration) const
    {
        return getDoubleProperty("duration", duration);
    }

    bool positionSeconds(double* position) const
    {
        return getDoubleProperty("time-pos", position);
    }

    bool loadMedia(const QString& filePath, QString* errorMessage, bool startPaused)
    {
        if (!loadFile(filePath, errorMessage)) {
            return false;
        }

        return setPaused(startPaused);
    }

    bool seekSeconds(double seconds)
    {
        if (!m_handle || !m_setProperty) {
            return false;
        }

        double value = seconds;
        return m_setProperty(m_handle, "time-pos", MPV_FORMAT_DOUBLE, &value) >= 0;
    }

    bool setMuted(bool muted)
    {
        if (!m_handle || !m_setProperty) {
            return false;
        }

        int mutedFlag = muted ? 1 : 0;
        return m_setProperty(m_handle, "mute", MPV_FORMAT_FLAG, &mutedFlag) >= 0;
    }

    bool muted(bool* mutedState) const
    {
        if (!m_handle || !m_getProperty || !mutedState) {
            return false;
        }

        int mutedFlag = 0;
        const int result = m_getProperty(m_handle, "mute", MPV_FORMAT_FLAG, &mutedFlag);
        if (result < 0) {
            return false;
        }

        *mutedState = mutedFlag != 0;
        return true;
    }

    bool setVolume(double volume)
    {
        if (!m_handle || !m_setProperty) {
            return false;
        }

        double volumeValue = volume;
        return m_setProperty(m_handle, "volume", MPV_FORMAT_DOUBLE, &volumeValue) >= 0;
    }

    bool volume(double* volumeValue) const
    {
        if (!m_handle || !m_getProperty || !volumeValue) {
            return false;
        }

        double currentVolume = 0.0;
        const int result = m_getProperty(m_handle, "volume", MPV_FORMAT_DOUBLE, &currentVolume);
        if (result < 0) {
            return false;
        }

        *volumeValue = currentVolume;
        return true;
    }

    QString lastLibraryPath() const
    {
        return m_library.fileName();
    }

private:
    bool loadLibrary(QString* errorMessage)
    {
        if (m_library.isLoaded()) {
            return true;
        }

        const QString appDir = QCoreApplication::applicationDirPath();
        const QStringList candidatePaths = {
            QDir(appDir).absoluteFilePath(QStringLiteral("libmpv-2.dll")),
            QDir(appDir).absoluteFilePath(QStringLiteral("mpv-2.dll")),
            QDir(appDir).absoluteFilePath(QStringLiteral("mpv-1.dll")),
            QDir(appDir).absoluteFilePath(QStringLiteral("libmpv.dll")),
            QDir(appDir).absoluteFilePath(QStringLiteral("mpv.dll"))
        };

        for (const QString& candidatePath : candidatePaths) {
            if (!QFileInfo::exists(candidatePath)) {
                continue;
            }

            m_library.setFileName(candidatePath);
            if (m_library.load()) {
                break;
            }
        }

        if (!m_library.isLoaded()) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("libmpv DLL was not found beside SpaceLook.exe.");
            }
            return false;
        }

        m_create = resolve<mpv_create_fn>("mpv_create");
        m_initialize = resolve<mpv_initialize_fn>("mpv_initialize");
        m_terminateDestroy = resolve<mpv_terminate_destroy_fn>("mpv_terminate_destroy");
        m_setOptionString = resolve<mpv_set_option_string_fn>("mpv_set_option_string");
        m_setProperty = resolve<mpv_set_property_fn>("mpv_set_property");
        m_getProperty = resolve<mpv_get_property_fn>("mpv_get_property");
        m_command = resolve<mpv_command_fn>("mpv_command");
        m_errorString = resolve<mpv_error_string_fn>("mpv_error_string");

        if (!m_create || !m_initialize || !m_terminateDestroy || !m_setOptionString ||
            !m_setProperty || !m_getProperty || !m_command || !m_errorString) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("libmpv symbols are incomplete in: %1").arg(m_library.fileName());
            }
            m_library.unload();
            return false;
        }

        return true;
    }

    template<typename T>
    T resolve(const char* symbolName)
    {
        return reinterpret_cast<T>(m_library.resolve(symbolName));
    }

    bool setOptionString(const char* name, const char* value, QString* errorMessage)
    {
        const int result = m_setOptionString ? m_setOptionString(m_handle, name, value) : -1;
        if (result < 0) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("libmpv option %1 failed: %2")
                    .arg(QString::fromLatin1(name), errorToString(result));
            }
            return false;
        }
        return true;
    }

    bool getDoubleProperty(const char* name, double* value) const
    {
        if (!m_handle || !m_getProperty || !value) {
            return false;
        }

        double propertyValue = 0.0;
        const int result = m_getProperty(m_handle, name, MPV_FORMAT_DOUBLE, &propertyValue);
        if (result < 0) {
            return false;
        }

        *value = propertyValue;
        return true;
    }

    QString errorToString(int errorCode) const
    {
        if (!m_errorString) {
            return QStringLiteral("unknown error");
        }

        const char* text = m_errorString(errorCode);
        return text ? QString::fromUtf8(text) : QStringLiteral("unknown error");
    }

    QLibrary m_library;
    mpv_handle* m_handle = nullptr;
    mpv_create_fn m_create = nullptr;
    mpv_initialize_fn m_initialize = nullptr;
    mpv_terminate_destroy_fn m_terminateDestroy = nullptr;
    mpv_set_option_string_fn m_setOptionString = nullptr;
    mpv_set_property_fn m_setProperty = nullptr;
    mpv_get_property_fn m_getProperty = nullptr;
    mpv_command_fn m_command = nullptr;
    mpv_error_string_fn m_errorString = nullptr;
};

MediaRenderer::MediaRenderer(QWidget* parent)
    : QWidget(parent)
    , m_headerRow(new QWidget(this))
    , m_iconLabel(new QLabel(this))
    , m_titleLabel(new SelectableTitleLabel(this))
    , m_metaLabel(new QLabel(this))
    , m_pathRow(new QWidget(this))
    , m_pathTitleLabel(new QLabel(this))
    , m_pathValueLabel(new QLabel(this))
    , m_openWithButton(new OpenWithButton(this))
    , m_statusLabel(new QLabel(this))
    , m_stageCard(new QWidget(this))
    , m_stageLayout(new QVBoxLayout(m_stageCard))
    , m_controlsCard(new QWidget(this))
    , m_mediaViewport(new QWidget(this))
    , m_centerOverlayLabel(new QLabel(m_mediaViewport))
    , m_audioPlaceholder(new QLabel(m_mediaViewport))
    , m_videoPlaceholder(new QLabel(m_mediaViewport))
    , m_videoHost(new QWidget(m_mediaViewport))
    , m_playPauseButton(new QPushButton(m_controlsCard))
    , m_volumeButton(new QPushButton(m_controlsCard))
    , m_positionLabel(new QLabel(m_controlsCard))
    , m_durationLabel(new QLabel(m_controlsCard))
    , m_positionSlider(new QSlider(Qt::Horizontal, m_controlsCard))
    , m_volumeValueLabel(new QLabel(m_controlsCard))
    , m_volumeSlider(new QSlider(Qt::Horizontal, m_controlsCard))
    , m_videoPollTimer(new QTimer(this))
    , m_mpvBackend(new InternalMpvVideoBackend())
{
    setAttribute(Qt::WA_StyledBackground, true);
    setObjectName(QStringLiteral("MediaRendererRoot"));
    m_headerRow->setObjectName(QStringLiteral("MediaHeaderRow"));
    m_iconLabel->setObjectName(QStringLiteral("MediaTypeIcon"));
    m_titleLabel->setObjectName(QStringLiteral("MediaTitle"));
    m_metaLabel->setObjectName(QStringLiteral("MediaMeta"));
    m_pathRow->setObjectName(QStringLiteral("MediaPathRow"));
    m_pathTitleLabel->setObjectName(QStringLiteral("MediaPathTitle"));
    m_pathValueLabel->setObjectName(QStringLiteral("MediaPathValue"));
    m_openWithButton->setObjectName(QStringLiteral("MediaOpenWithButton"));
    m_statusLabel->setObjectName(QStringLiteral("MediaStatus"));
    m_stageCard->setObjectName(QStringLiteral("MediaStage"));
    m_controlsCard->setObjectName(QStringLiteral("MediaControlsCard"));
    m_mediaViewport->setObjectName(QStringLiteral("MediaViewport"));
    m_centerOverlayLabel->setObjectName(QStringLiteral("MediaCenterOverlay"));
    m_audioPlaceholder->setObjectName(QStringLiteral("MediaAudioPlaceholder"));
    m_videoPlaceholder->setObjectName(QStringLiteral("MediaVideoPlaceholder"));
    m_videoHost->setObjectName(QStringLiteral("MediaVideoHost"));
    m_playPauseButton->setObjectName(QStringLiteral("MediaPlayPause"));
    m_volumeButton->setObjectName(QStringLiteral("MediaVolumeButton"));
    m_volumeValueLabel->setObjectName(QStringLiteral("MediaVolumeValue"));
    m_volumeSlider->setObjectName(QStringLiteral("MediaVolumeSlider"));
    m_positionSlider->setObjectName(QStringLiteral("MediaSlider"));
    m_positionLabel->setObjectName(QStringLiteral("MediaTimeLabel"));
    m_durationLabel->setObjectName(QStringLiteral("MediaTimeLabel"));

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(12, 0, 12, 12);
    rootLayout->setSpacing(8);
    rootLayout->addWidget(m_headerRow);
    rootLayout->addWidget(m_statusLabel);
    rootLayout->addWidget(m_stageCard, 1);

    auto* headerLayout = new QHBoxLayout(m_headerRow);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(12);
    auto* titleBlock = new PreviewHeaderBar(m_iconLabel, m_titleLabel, m_pathRow, m_openWithButton, m_headerRow);
    headerLayout->addWidget(titleBlock->contentWidget(), 1);

    auto* pathLayout = new QHBoxLayout(m_pathRow);
    pathLayout->setContentsMargins(0, 0, 0, 0);
    pathLayout->setSpacing(8);
    pathLayout->addWidget(m_pathValueLabel, 1);

    m_stageLayout->setContentsMargins(10, 10, 10, 10);
    m_stageLayout->setSpacing(8);
    auto* viewportLayout = new QStackedLayout(m_mediaViewport);
    viewportLayout->setStackingMode(QStackedLayout::StackAll);
    viewportLayout->setContentsMargins(0, 0, 0, 0);
    viewportLayout->addWidget(m_videoPlaceholder);
    viewportLayout->addWidget(m_videoHost);
    viewportLayout->addWidget(m_audioPlaceholder);
    viewportLayout->addWidget(m_centerOverlayLabel);
    m_stageLayout->addWidget(m_mediaViewport, 1);

    auto* controlsShellLayout = new QVBoxLayout(m_controlsCard);
    controlsShellLayout->setContentsMargins(10, 8, 10, 8);
    controlsShellLayout->setSpacing(6);

    auto* controlsLayout = new QHBoxLayout();
    controlsLayout->setSpacing(8);
    controlsLayout->addWidget(m_playPauseButton);
    controlsLayout->addWidget(m_positionLabel);
    controlsLayout->addWidget(m_positionSlider, 1);
    controlsLayout->addWidget(m_durationLabel);
    controlsLayout->addWidget(m_volumeButton);
    controlsLayout->addWidget(m_volumeSlider);
    controlsLayout->addWidget(m_volumeValueLabel);
    controlsShellLayout->addLayout(controlsLayout);
    m_stageLayout->addWidget(m_controlsCard);

    m_audioPlaceholder->setAlignment(Qt::AlignCenter);
    m_audioPlaceholder->setWordWrap(true);
    m_audioPlaceholder->hide();
    m_audioPlaceholder->installEventFilter(this);
    m_audioPlaceholder->setCursor(Qt::PointingHandCursor);
    m_audioPlaceholder->setScaledContents(true);
    m_videoPlaceholder->setAlignment(Qt::AlignCenter);
    m_videoPlaceholder->hide();
    m_videoPlaceholder->installEventFilter(this);
    m_videoPlaceholder->setScaledContents(true);
    m_centerOverlayLabel->setAlignment(Qt::AlignCenter);
    m_centerOverlayLabel->installEventFilter(this);
    m_centerOverlayLabel->setCursor(Qt::PointingHandCursor);
    m_centerOverlayLabel->hide();
    m_iconLabel->setFixedSize(72, 72);
    m_iconLabel->setScaledContents(true);
    m_titleLabel->setWordWrap(true);
    m_pathTitleLabel->hide();
    m_pathValueLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_pathValueLabel->setWordWrap(true);
    m_pathValueLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_pathRow->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_openWithButton->setStatusCallback([this](const QString& message) {
        updateStatusLabel(message);
    });
    m_openWithButton->setLaunchSuccessCallback([this]() {
        if (SpaceLookWindow* previewWindow = qobject_cast<SpaceLookWindow*>(window())) {
            previewWindow->hidePreview();
        }
    });
    connect(titleBlock->closeButton(), &QToolButton::clicked, this, [this]() {
        if (SpaceLookWindow* previewWindow = qobject_cast<SpaceLookWindow*>(window())) {
            previewWindow->hidePreview();
        }
    });

    m_videoHost->setAttribute(Qt::WA_NativeWindow, true);
    m_videoHost->setAttribute(Qt::WA_DontCreateNativeAncestors, true);
    m_videoHost->setAutoFillBackground(false);
    m_videoHost->hide();
    m_videoHost->installEventFilter(this);

    m_positionSlider->setRange(0, 0);
    m_positionLabel->setText(QStringLiteral("00:00"));
    m_durationLabel->setText(QStringLiteral("00:00"));
    m_positionSlider->installEventFilter(this);
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(m_audioVolume);
    m_volumeSlider->setFixedWidth(74);
    m_playPauseButton->setText(playGlyph());
    m_volumeButton->setText(volumeGlyph());
    m_volumeValueLabel->setText(QString::number(m_audioVolume));

    m_videoPollTimer->setInterval(200);

    connect(m_playPauseButton, &QPushButton::clicked, this, [this]() {
        togglePlayback();
    });

    connect(m_volumeButton, &QPushButton::clicked, this, [this]() {
        toggleVolumePopup();
    });

    connect(m_volumeSlider, &QSlider::valueChanged, this, [this](int value) {
        setVolumeLevel(value);
    });

    connect(m_positionSlider, &QSlider::sliderPressed, this, [this]() {
        m_isSeeking = true;
    });

    connect(m_positionSlider, &QSlider::sliderReleased, this, [this]() {
        m_isSeeking = false;
        seekToSliderValue(m_positionSlider->value());
    });

    connect(m_positionSlider, &QSlider::sliderMoved, this, [this](int value) {
        m_positionLabel->setText(formatTime(value));
    });

    connect(m_videoPollTimer, &QTimer::timeout, this, [this]() {
        if (!m_usingMpvPlayback || !m_mpvBackend || !m_mpvBackend->isLoaded()) {
            return;
        }

        double durationSeconds = 0.0;
        if (m_mpvBackend->durationSeconds(&durationSeconds)) {
            m_positionSlider->setRange(0, qMax(0, static_cast<int>(durationSeconds * 1000.0)));
            m_durationLabel->setText(formatTime(static_cast<qint64>(durationSeconds * 1000.0)));
        }

        if (!m_isSeeking) {
            double positionSeconds = 0.0;
            if (m_mpvBackend->positionSeconds(&positionSeconds)) {
                const qint64 positionMilliseconds = static_cast<qint64>(positionSeconds * 1000.0);
                m_positionSlider->setValue(static_cast<int>(positionMilliseconds));
                m_positionLabel->setText(formatTime(positionMilliseconds));
                m_videoPreviewReady = true;
            }
        }

        bool paused = m_mpvPaused;
        if (m_mpvBackend->paused(&paused)) {
            m_mpvPaused = paused;
        }

        updatePlaybackButton();
        updateCenterOverlay();
    });
    connect(m_titleLabel, &SelectableTitleLabel::copyFeedbackRequested, this, [this](const QString& message) {
        updateStatusLabel(message);
    });

    applyChrome();
    updatePlaybackButton();
    updateVolumeButton();
    updateVolumePopup();
    updateCenterOverlay();
}

MediaRenderer::~MediaRenderer()
{
    unload();
    delete m_mpvBackend;
    m_mpvBackend = nullptr;
}

bool MediaRenderer::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_positionSlider && event) {
        if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonDblClick) {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                const int targetValue = sliderValueForMousePosition(m_positionSlider, mouseEvent->pos());
                m_isSeeking = true;
                m_positionSlider->setValue(targetValue);
                m_positionLabel->setText(formatTime(targetValue));
                m_isSeeking = false;
                seekToSliderValue(targetValue);
                return true;
            }
        }
    }

    if (watched == m_videoHost && event && m_usingMpvVideo) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                togglePlayback();
                return true;
            }
        }
    }

    if (watched == m_audioPlaceholder && event && isAudioType(m_info)) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                togglePlayback();
                return true;
            }
        }
    }

    if (watched == m_videoPlaceholder && event && isVideoType(m_info)) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                togglePlayback();
                return true;
            }
        }
    }

    if (watched == m_centerOverlayLabel && event) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                togglePlayback();
                return true;
            }
        }
    }

    return QWidget::eventFilter(watched, event);
}

void MediaRenderer::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    syncVideoViewportGeometry();
    updateCenterOverlay();
}

void MediaRenderer::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);

    QTimer::singleShot(0, this, [this]() {
        syncVideoViewportGeometry();
        updateCenterOverlay();
    });
}

QString MediaRenderer::rendererId() const
{
    return QStringLiteral("media");
}

bool MediaRenderer::canHandle(const HoveredItemInfo& info) const
{
    return isAudioType(info) || isVideoType(info);
}

QWidget* MediaRenderer::widget()
{
    return this;
}

void MediaRenderer::load(const HoveredItemInfo& info)
{
    m_info = info;
    m_mpvPaused = true;
    m_videoPreviewReady = false;
    m_videoHost->hide();
    m_videoPlaceholder->hide();
    m_audioPlaceholder->hide();
    m_centerOverlayLabel->hide();
    qDebug().noquote() << QStringLiteral("[SpaceLookRender] MediaRenderer load path=\"%1\" typeKey=%2")
        .arg(info.filePath, info.typeKey);

    m_titleLabel->setText(displayTitleForMedia(info));
    m_titleLabel->setCopyText(m_titleLabel->text());
    const QIcon typeIcon(FileTypeIconResolver::iconForInfo(info));
    m_iconLabel->setPixmap(typeIcon.pixmap(128, 128));
    m_pathValueLabel->setText(info.filePath.trimmed().isEmpty() ? QStringLiteral("(Unavailable)") : info.filePath);
    m_openWithButton->setTargetContext(info.filePath, info.typeKey);

    m_positionSlider->setValue(0);
    m_positionSlider->setRange(0, 0);
    m_positionLabel->setText(QStringLiteral("00:00"));
    m_durationLabel->setText(QStringLiteral("00:00"));

    if (isVideoType(info)) {
        destroyAudioBackend();
        ensureVideoBackend();
        m_usingMpvPlayback = true;

        if (!m_mpvBackend || !m_mpvBackend->isLoaded()) {
            updateStatusLabel(QStringLiteral("libmpv video backend is unavailable."));
            updateMediaUi();
            updatePlaybackButton();
            updateVolumeButton();
            updateVolumePopup();
            updateCenterOverlay();
            return;
        }

        QString errorMessage;
        if (!m_mpvBackend->loadFile(info.filePath, &errorMessage)) {
            updateStatusLabel(errorMessage);
            updateMediaUi();
            updatePlaybackButton();
            updateVolumeButton();
            updateVolumePopup();
            updateCenterOverlay();
            return;
        }

        m_videoMuted = true;
        m_videoVolume = 50;
        m_mpvBackend->setMuted(true);
        m_mpvBackend->setVolume(static_cast<double>(m_videoVolume));
        m_mpvBackend->setPaused(true);
        m_mpvPaused = true;
        updateStatusLabel(QString());
        m_videoPollTimer->start();
        updateMediaUi();
        updatePlaybackButton();
        updateVolumeButton();
        updateVolumePopup();
        updateCenterOverlay();
        return;
    }

    ensureVideoBackend();
    if (m_mpvBackend && m_mpvBackend->isLoaded()) {
        destroyAudioBackend();
        m_usingMpvPlayback = true;
        m_usingMpvVideo = false;

        QString errorMessage;
        if (!m_mpvBackend->loadMedia(info.filePath, &errorMessage, true)) {
            updateStatusLabel(errorMessage);
            updateMediaUi();
            updatePlaybackButton();
            updateVolumeButton();
            updateVolumePopup();
            updateCenterOverlay();
            return;
        }

        m_videoMuted = true;
        m_videoVolume = 50;
        m_mpvBackend->setMuted(true);
        m_mpvBackend->setVolume(static_cast<double>(m_videoVolume));
        m_mpvPaused = true;
        m_videoPollTimer->start();
        updateStatusLabel(QString());
        updateMediaUi();
        updatePlaybackButton();
        updateVolumeButton();
        updateVolumePopup();
        updateCenterOverlay();
        return;
    }

    destroyVideoBackend();
    ensureAudioBackend();
    m_usingMpvPlayback = false;
    if (!m_player) {
        updateStatusLabel(QStringLiteral("Audio backend is unavailable."));
        updateMediaUi();
        updatePlaybackButton();
        updateCenterOverlay();
        return;
    }

    m_player->stop();
    m_player->setMuted(true);
    m_player->setVolume(m_audioVolume);
    m_player->setMedia(QUrl::fromLocalFile(info.filePath));
    updateStatusLabel(QString());
    updateMediaUi();
    updatePlaybackButton();
    updateVolumeButton();
    updateVolumePopup();
    updateCenterOverlay();
}

void MediaRenderer::unload()
{
    destroyAudioBackend();
    destroyVideoBackend();
    m_info = HoveredItemInfo();
    m_mpvPaused = true;
    m_videoPreviewReady = false;
    m_videoMuted = true;
    m_positionSlider->setValue(0);
    m_positionSlider->setRange(0, 0);
    m_positionLabel->setText(QStringLiteral("00:00"));
    m_durationLabel->setText(QStringLiteral("00:00"));
    m_statusLabel->clear();
    m_pathValueLabel->clear();
    m_openWithButton->setTargetContext(QString(), QString());
    m_audioPlaceholder->clear();
    m_audioPlaceholder->hide();
    m_videoPlaceholder->clear();
    m_videoPlaceholder->hide();
    m_videoHost->hide();
    m_centerOverlayLabel->hide();
    updateVolumeButton();
    updateVolumePopup();
    updateCenterOverlay();
}

void MediaRenderer::applyChrome()
{
    setStyleSheet(
        "#MediaRendererRoot {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:1,"
        "      stop:0 #f9fbff,"
        "      stop:0.52 #f5f8fc,"
        "      stop:1 #eef3f9);"
        "  border-radius: 0px;"
        "}"
        "QLabel {"
        "  color: #18324a;"
        "}"
        "#MediaTypeIcon {"
        "  background: transparent;"
        "  border: none;"
        "  padding: 0px;"
        "}"
        "#MediaTitle {"
        "  color: #102a43;"
        "}"
        "#MediaMeta {"
        "  color: #60758d;"
        "}"
        "#MediaPathTitle {"
        "  color: #16324a;"
        "  font-family: 'Segoe UI Semibold';"
        "}"
        "#MediaPathValue {"
        "  color: #445d76;"
        "}"
        "#MediaOpenWithButton QToolButton {"
        "  background: rgba(238, 244, 252, 0.96);"
        "  color: #17324b;"
        "  border: 1px solid rgba(193, 208, 224, 0.95);"
        "  padding: 3px 8px;"
        "  min-height: 24px;"
        "}"
        "#MediaOpenWithButton QToolButton:hover {"
        "  background: rgba(245, 249, 255, 1.0);"
        "}"
        "#MediaOpenWithButton QToolButton:pressed {"
        "  background: rgba(224, 234, 246, 1.0);"
        "}"
        "#MediaOpenWithButton #OpenWithPrimaryButton {"
        "  border-top-left-radius: 10px;"
        "  border-bottom-left-radius: 10px;"
        "  border-top-right-radius: 0px;"
        "  border-bottom-right-radius: 0px;"
        "  min-width: 28px;"
        "}"
        "#MediaOpenWithButton #OpenWithExpandButton {"
        "  border-left: none;"
        "  border-top-left-radius: 0px;"
        "  border-bottom-left-radius: 0px;"
        "  border-top-right-radius: 10px;"
        "  border-bottom-right-radius: 10px;"
        "  min-width: 22px;"
        "  padding-left: 5px;"
        "  padding-right: 5px;"
        "}"
        "#MediaStatus {"
        "  color: #4f4b12;"
        "  background: rgba(255, 243, 212, 0.94);"
        "  border: 1px solid rgba(231, 202, 132, 0.95);"
        "  border-radius: 12px;"
        "  padding: 8px 10px;"
        "}"
        "#MediaStage {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:1,"
        "      stop:0 #152131,"
        "      stop:0.58 #203246,"
        "      stop:1 #2b4058);"
        "  border: 1px solid rgba(82, 106, 131, 0.68);"
        "  border-radius: 20px;"
        "}"
        "#MediaControlsCard {"
        "  background: rgba(244, 248, 255, 0.12);"
        "  border: 1px solid rgba(255, 255, 255, 0.09);"
        "  border-radius: 16px;"
        "}"
        "#MediaViewport {"
        "  background: transparent;"
        "}"
        "#MediaAudioPlaceholder, #MediaVideoHost, #MediaVideoPlaceholder {"
        "  background: rgba(255, 255, 255, 0.08);"
        "  color: #edf5ff;"
        "  border: 1px solid rgba(255, 255, 255, 0.12);"
        "  border-radius: 16px;"
        "  padding: 12px;"
        "}"
        "#MediaCenterOverlay {"
        "  background: transparent;"
        "  color: rgba(248, 252, 255, 0.92);"
        "}"
        "#MediaPlayPause {"
        "  background: rgba(249, 252, 255, 0.98);"
        "  color: #16324a;"
        "  border: none;"
        "  border-radius: 16px;"
        "  min-width: 40px;"
        "  min-height: 36px;"
        "  padding: 0px;"
        "}"
        "#MediaPlayPause:hover {"
        "  background: rgba(255, 255, 255, 1.0);"
        "}"
        "#MediaPlayPause:pressed {"
        "  background: rgba(220, 233, 246, 1.0);"
        "}"
        "#MediaVolumeButton {"
        "  background: rgba(241, 247, 255, 0.16);"
        "  color: #f5fbff;"
        "  border: 1px solid rgba(255, 255, 255, 0.16);"
        "  border-radius: 16px;"
        "  min-width: 40px;"
        "  min-height: 36px;"
        "  padding: 0px;"
        "}"
        "#MediaVolumeButton:hover {"
        "  background: rgba(241, 247, 255, 0.23);"
        "}"
        "#MediaVolumeButton:pressed {"
        "  background: rgba(241, 247, 255, 0.28);"
        "}"
        "#MediaVolumeValue {"
        "  color: #eff6ff;"
        "  font-family: 'Segoe UI Semibold';"
        "  font-size: 12px;"
        "  min-width: 28px;"
        "}"
        "#MediaTimeLabel {"
        "  color: #e2eefc;"
        "  min-width: 36px;"
        "}"
        "#MediaSlider::groove:horizontal {"
        "  height: 8px;"
        "  border-radius: 4px;"
        "  background: rgba(189, 208, 229, 0.18);"
        "}"
        "#MediaSlider::sub-page:horizontal {"
        "  border-radius: 4px;"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:0,"
        "      stop:0 rgba(120, 210, 255, 0.98),"
        "      stop:0.54 rgba(91, 170, 250, 0.98),"
        "      stop:1 rgba(74, 136, 226, 0.98));"
        "}"
        "#MediaSlider::add-page:horizontal {"
        "  border-radius: 4px;"
        "  background: rgba(196, 212, 233, 0.14);"
        "}"
        "#MediaSlider::handle:horizontal {"
        "  background: #ffffff;"
        "  border: 2px solid rgba(100, 170, 255, 0.96);"
        "  width: 14px;"
        "  margin: -4px 0;"
        "  border-radius: 7px;"
        "}"
        "#MediaSlider::handle:horizontal:hover {"
        "  background: #f8fbff;"
        "}"
        "#MediaVolumeSlider::groove:horizontal {"
        "  height: 7px;"
        "  border-radius: 3px;"
        "  background: rgba(201, 215, 235, 0.16);"
        "}"
        "#MediaVolumeSlider::sub-page:horizontal {"
        "  border-radius: 3px;"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:0,"
        "      stop:0 rgba(86, 146, 236, 0.98),"
        "      stop:1 rgba(123, 212, 255, 0.98));"
        "}"
        "#MediaVolumeSlider::add-page:horizontal {"
        "  border-radius: 3px;"
        "  background: rgba(201, 215, 235, 0.10);"
        "}"
        "#MediaVolumeSlider::handle:horizontal {"
        "  background: #ffffff;"
        "  border: 2px solid rgba(101, 171, 255, 0.96);"
        "  width: 12px;"
        "  margin: -4px 0;"
        "  border-radius: 6px;"
        "}"
    );

    QFont titleFont;
    titleFont.setFamily(QStringLiteral("Microsoft YaHei UI"));
    titleFont.setPixelSize(20);
    titleFont.setWeight(QFont::Bold);
    m_titleLabel->setFont(titleFont);
    m_titleLabel->setWordWrap(true);

    QFont metaFont;
    metaFont.setFamily(QStringLiteral("Segoe UI"));
    metaFont.setPixelSize(11);
    m_metaLabel->setFont(metaFont);
    m_pathTitleLabel->setFont(metaFont);
    m_pathValueLabel->setFont(metaFont);
    m_statusLabel->setFont(metaFont);
    m_metaLabel->setWordWrap(true);
    m_pathValueLabel->setWordWrap(true);

    QFont placeholderFont;
    placeholderFont.setFamily(QStringLiteral("Segoe UI Semibold"));
    placeholderFont.setPixelSize(18);
    m_audioPlaceholder->setFont(placeholderFont);

    const QFont transportFont = FluentIconFont::iconFont(18, QFont::Normal);
    m_playPauseButton->setFont(transportFont);
    m_volumeButton->setFont(transportFont);

    const QFont overlayFont = FluentIconFont::iconFont(56, QFont::DemiBold);
    m_centerOverlayLabel->setFont(overlayFont);
}

void MediaRenderer::ensureAudioBackend()
{
    m_usingMpvVideo = false;
    if (m_player) {
        return;
    }

    m_player = new QMediaPlayer(this);

    connect(m_player, &QMediaPlayer::positionChanged, this, [this](qint64 position) {
        if (!m_isSeeking) {
            m_positionSlider->setValue(static_cast<int>(position));
            m_positionLabel->setText(formatTime(position));
        }
    });

    connect(m_player, &QMediaPlayer::durationChanged, this, [this](qint64 duration) {
        m_positionSlider->setRange(0, static_cast<int>(duration));
        m_durationLabel->setText(formatTime(duration));
    });

    connect(m_player, &QMediaPlayer::stateChanged, this, [this]() {
        updatePlaybackButton();
        updateCenterOverlay();
    });

    connect(m_player, &QMediaPlayer::mutedChanged, this, [this](bool) {
        updateVolumeButton();
        updateVolumePopup();
    });

    connect(m_player, &QMediaPlayer::volumeChanged, this, [this](int volume) {
        m_audioVolume = volume;
        updateVolumeButton();
        updateVolumePopup();
    });

    connect(m_player, QOverload<QMediaPlayer::Error>::of(&QMediaPlayer::error), this, [this](QMediaPlayer::Error) {
        const QString errorText = m_player && !m_player->errorString().trimmed().isEmpty()
            ? m_player->errorString()
            : QStringLiteral("Audio preview failed to load.");
        qDebug().noquote() << QStringLiteral("[SpaceLookRender] Audio backend error path=\"%1\" message=\"%2\"")
            .arg(m_info.filePath, errorText);
        updateStatusLabel(errorText);
    });
}

void MediaRenderer::destroyAudioBackend()
{
    m_usingMpvPlayback = false;
    if (m_player) {
        m_player->stop();
        m_player->setMedia(QUrl());
        m_player->deleteLater();
        m_player = nullptr;
    }
    m_audioPlaceholder->hide();
}

void MediaRenderer::ensureVideoBackend()
{
    m_usingMpvVideo = true;
    if (!m_mpvBackend) {
        return;
    }

    if (m_mpvBackend->isLoaded()) {
        return;
    }

    QString errorMessage;
    if (!m_mpvBackend->initialize(m_videoHost, &errorMessage)) {
        qDebug().noquote() << QStringLiteral("[SpaceLookRender] libmpv initialize failed: %1").arg(errorMessage);
        updateStatusLabel(errorMessage);
    }
}

void MediaRenderer::destroyVideoBackend()
{
    m_videoPollTimer->stop();
    m_usingMpvVideo = false;
    m_usingMpvPlayback = false;
    m_mpvPaused = true;
    m_videoPreviewReady = false;
    if (m_mpvBackend) {
        m_mpvBackend->shutdown();
    }
    m_videoHost->hide();
    m_videoPlaceholder->hide();
    m_centerOverlayLabel->hide();
}

void MediaRenderer::seekToSliderValue(int value)
{
    if (m_usingMpvPlayback) {
        if (m_mpvBackend && m_mpvBackend->isLoaded()) {
            m_mpvBackend->seekSeconds(static_cast<double>(value) / 1000.0);
        }
        return;
    }

    if (m_player) {
        m_player->setPosition(value);
    }
}

void MediaRenderer::togglePlayback()
{
    if (m_usingMpvPlayback) {
        if (!m_mpvBackend || !m_mpvBackend->isLoaded()) {
            return;
        }

        bool paused = m_mpvPaused;
        if (m_mpvBackend->paused(&paused)) {
            if (m_mpvBackend->setPaused(!paused)) {
                m_mpvPaused = !paused;
                updatePlaybackButton();
                updateCenterOverlay();
            }
        }
        return;
    }

    if (!m_player) {
        return;
    }

    if (m_player->state() == QMediaPlayer::PlayingState) {
        m_player->pause();
    } else {
        m_player->play();
    }
    updateCenterOverlay();
}

void MediaRenderer::toggleVolumePopup()
{
    setVolumeLevel(isMutedState() ? 50 : 0);
}

void MediaRenderer::setVolumeLevel(int value)
{
    const int clampedValue = qBound(0, value, 100);

    if (m_usingMpvPlayback) {
        m_videoVolume = clampedValue;
        m_videoMuted = clampedValue == 0;
        if (m_mpvBackend && m_mpvBackend->isLoaded()) {
            m_mpvBackend->setMuted(m_videoMuted);
            m_mpvBackend->setVolume(static_cast<double>(clampedValue));
        }
        updateVolumeButton();
        updateVolumePopup();
        return;
    }

    m_audioVolume = clampedValue;
    if (m_player) {
        m_player->setMuted(clampedValue == 0);
        m_player->setVolume(clampedValue);
    }
    updateVolumeButton();
    updateVolumePopup();
}

int MediaRenderer::currentVolumeLevel() const
{
    if (m_usingMpvPlayback) {
        return m_videoMuted ? 0 : m_videoVolume;
    }

    if (!m_player) {
        return m_audioVolume;
    }

    return m_player->isMuted() ? 0 : m_player->volume();
}

int MediaRenderer::sliderValueForMousePosition(QSlider* slider, const QPoint& position) const
{
    if (!slider) {
        return 0;
    }

    QStyleOptionSlider option;
    option.initFrom(slider);
    option.orientation = slider->orientation();
    option.minimum = slider->minimum();
    option.maximum = slider->maximum();
    option.sliderPosition = slider->sliderPosition();
    option.sliderValue = slider->value();
    option.singleStep = slider->singleStep();
    option.pageStep = slider->pageStep();
    option.upsideDown = false;
    if (slider->orientation() == Qt::Horizontal) {
        option.state |= QStyle::State_Horizontal;
    }
    const QRect grooveRect = slider->style()->subControlRect(
        QStyle::CC_Slider, &option, QStyle::SC_SliderGroove, slider);
    const QRect handleRect = slider->style()->subControlRect(
        QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, slider);

    if (slider->orientation() == Qt::Vertical) {
        const int sliderMin = grooveRect.top();
        const int sliderMax = grooveRect.bottom() - handleRect.height() + 1;
        return QStyle::sliderValueFromPosition(
            slider->minimum(),
            slider->maximum(),
            sliderMax - qBound(sliderMin, position.y() - (handleRect.height() / 2), sliderMax),
            qMax(1, sliderMax - sliderMin),
            option.upsideDown);
    }

    const int sliderMin = grooveRect.left();
    const int sliderMax = grooveRect.right() - handleRect.width() + 1;
    return QStyle::sliderValueFromPosition(
        slider->minimum(),
        slider->maximum(),
        qBound(sliderMin, position.x() - (handleRect.width() / 2), sliderMax) - sliderMin,
        qMax(1, sliderMax - sliderMin),
        option.upsideDown);
}

void MediaRenderer::updatePlaybackButton()
{
    if (m_usingMpvPlayback) {
        m_playPauseButton->setText(m_mpvPaused ? playGlyph() : pauseGlyph());
        return;
    }

    m_playPauseButton->setText(m_player && m_player->state() == QMediaPlayer::PlayingState
        ? pauseGlyph()
        : playGlyph());
}

void MediaRenderer::updateVolumeButton()
{
    m_volumeButton->setVisible(true);
    m_volumeButton->setText(isMutedState()
        ? mutedGlyph()
        : volumeGlyph());
}

void MediaRenderer::updateVolumePopup()
{
    const int volume = currentVolumeLevel();
    const bool sliderWasBlocked = m_volumeSlider->blockSignals(true);
    m_volumeSlider->setValue(volume);
    m_volumeSlider->blockSignals(sliderWasBlocked);
    m_volumeValueLabel->setText(QString::number(volume));
}

void MediaRenderer::updateMediaUi()
{
    const bool video = isVideoType(m_info);
    m_videoHost->setVisible(video);
    m_videoPlaceholder->setVisible(video && shouldShowVideoPlaceholder());
    m_audioPlaceholder->setVisible(!video);

    if (!video) {
        m_audioPlaceholder->setPixmap(QPixmap());
        m_audioPlaceholder->setText(QStringLiteral("%1\n\nClick to play or pause").arg(
            displayTitleForMedia(m_info)));
    } else {
        m_videoPlaceholder->setPixmap(QPixmap());
        m_videoPlaceholder->setText(QString());
    }

    syncVideoViewportGeometry();
    updateCenterOverlay();
}

void MediaRenderer::syncVideoViewportGeometry()
{
    if (!m_mediaViewport || !m_videoHost || !m_videoPlaceholder) {
        return;
    }

    const QRect viewportRect = m_mediaViewport->contentsRect();
    if (!viewportRect.isValid()) {
        return;
    }

    if (m_videoHost->geometry() != viewportRect) {
        m_videoHost->setGeometry(viewportRect);
    }
    if (m_videoPlaceholder->geometry() != viewportRect) {
        m_videoPlaceholder->setGeometry(viewportRect);
    }
    if (!isVideoType(m_info)) {
        m_audioPlaceholder->setGeometry(viewportRect);
    }

    m_videoHost->updateGeometry();
    m_videoHost->update();
    m_videoPlaceholder->updateGeometry();
    m_videoPlaceholder->update();
    m_audioPlaceholder->updateGeometry();
    m_audioPlaceholder->update();
}

void MediaRenderer::updateStatusLabel(const QString& message)
{
    if (message.trimmed().isEmpty()) {
        m_statusLabel->clear();
        m_statusLabel->hide();
        return;
    }

    m_statusLabel->setText(QStringLiteral("%1\n%2").arg(message, m_info.filePath));
    m_statusLabel->show();
}

QString MediaRenderer::formatTime(qint64 milliseconds) const
{
    const int totalSeconds = static_cast<int>(milliseconds / 1000);
    const int hours = totalSeconds / 3600;
    const int minutes = (totalSeconds % 3600) / 60;
    const int seconds = totalSeconds % 60;

    if (hours > 0) {
        return QStringLiteral("%1:%2:%3")
            .arg(hours, 2, 10, QLatin1Char('0'))
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(seconds, 2, 10, QLatin1Char('0'));
    }

    return QStringLiteral("%1:%2")
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'));
}

bool MediaRenderer::isMutedState() const
{
    return currentVolumeLevel() <= 0;
}

bool MediaRenderer::isPlaybackPaused() const
{
    if (m_usingMpvPlayback) {
        return m_mpvPaused;
    }

    return !(m_player && m_player->state() == QMediaPlayer::PlayingState);
}

bool MediaRenderer::isPlaybackRunning() const
{
    return !isPlaybackPaused();
}

bool MediaRenderer::shouldShowVideoPlaceholder() const
{
    if (!isVideoType(m_info)) {
        return false;
    }

    if (!m_usingMpvPlayback) {
        return isPlaybackPaused();
    }

    return m_mpvPaused || !m_videoPreviewReady;
}

void MediaRenderer::updateCenterOverlay()
{
    if (!m_centerOverlayLabel) {
        return;
    }

    if (m_info.filePath.trimmed().isEmpty()) {
        m_centerOverlayLabel->hide();
        return;
    }

    if (isVideoType(m_info)) {
        const bool showVideoPlaceholder = shouldShowVideoPlaceholder();
        if (m_videoPlaceholder) {
            m_videoPlaceholder->setVisible(showVideoPlaceholder);
        }

        if (showVideoPlaceholder) {
            m_centerOverlayLabel->setText(playGlyph());
            m_centerOverlayLabel->show();
            m_centerOverlayLabel->raise();
        } else {
            m_centerOverlayLabel->hide();
        }
        return;
    }

    const bool showOverlay = !isPlaybackRunning();
    if (!showOverlay) {
        m_centerOverlayLabel->hide();
        return;
    }

    m_centerOverlayLabel->setText(isPlaybackPaused()
        ? playGlyph()
        : pauseGlyph());
    m_centerOverlayLabel->show();
    m_centerOverlayLabel->raise();
}
