#include "renderers/media/MediaRenderer.h"

#include <QAxObject>
#include <QBoxLayout>
#include <QCoreApplication>
#include <QDebug>
#include <QDesktopServices>
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
#include <QVideoWidget>

#include <MediaInfo/MediaInfo.h>

#include <Windows.h>
#include <propkey.h>
#include <propsys.h>
#include <shobjidl.h>

#include "renderers/FileTypeIconResolver.h"
#include "renderers/FluentIconFont.h"
#include "renderers/OpenWithButton.h"
#include "renderers/PreviewHeaderBar.h"
#include "renderers/PreviewStateVisuals.h"
#include "renderers/SelectableTitleLabel.h"
#include "widgets/SpaceLookWindow.h"

namespace {

constexpr int kDefaultMediaVolume = 50;
constexpr int kMaxMediaVolume = 100;
constexpr double kMpvVolumeGain = 2.0;
constexpr int kMaxQtPlayerVolume = 100;
constexpr const char* kMpvRuntimeHelpUrl = "https://github.com/linuup/SpaceLook/releases/tag/main";

enum class MediaPlaybackPolicy
{
    WindowsDefault,
    WindowsThenExtensionOrMpv,
    MpvPreferred
};

struct MediaCodecMetadata
{
    QString containerFormat;
    QString videoCompression;
    QString videoFourCc;
    QString videoBitrate;
    QString videoWidth;
    QString videoHeight;
    QString audioFormat;
    QString audioCompression;
};

QString mediaInfoStringToQString(const MediaInfoLib::String& value)
{
#if defined(UNICODE) || defined(_UNICODE)
    return QString::fromWCharArray(value.c_str()).trimmed();
#else
    return QString::fromUtf8(value.c_str()).trimmed();
#endif
}

MediaInfoLib::String mediaInfoParameter(const char* value)
{
#if defined(UNICODE) || defined(_UNICODE)
    return MediaInfoLib::String(reinterpret_cast<const wchar_t*>(QString::fromLatin1(value).utf16()));
#else
    return MediaInfoLib::String(value);
#endif
}

MediaInfoLib::String mediaInfoPath(const QString& filePath)
{
#if defined(UNICODE) || defined(_UNICODE)
    return MediaInfoLib::String(reinterpret_cast<const wchar_t*>(QDir::toNativeSeparators(filePath).utf16()));
#else
    return MediaInfoLib::String(QDir::toNativeSeparators(filePath).toUtf8().constData());
#endif
}

QString mediaInfoGet(MediaInfoLib::MediaInfo& mediaInfo, MediaInfoLib::stream_t streamKind, const char* parameter)
{
    return mediaInfoStringToQString(mediaInfo.Get(streamKind, 0, mediaInfoParameter(parameter)));
}

bool isVideoType(const HoveredItemInfo& info)
{
    return info.typeKey == QStringLiteral("video");
}

bool isAudioType(const HoveredItemInfo& info)
{
    return info.typeKey == QStringLiteral("audio");
}

bool isWindowsPlayerFallbackAudioFile(const HoveredItemInfo& info)
{
    const QString suffix = QFileInfo(info.filePath).suffix().trimmed().toLower();
    return suffix == QStringLiteral("aif") ||
        suffix == QStringLiteral("aiff") ||
        suffix == QStringLiteral("alac") ||
        suffix == QStringLiteral("mid") ||
        suffix == QStringLiteral("midi");
}

QString displayTitleForMedia(const HoveredItemInfo& info)
{
    if (!info.fileName.trimmed().isEmpty()) {
        return info.fileName;
    }
    if (!info.title.trimmed().isEmpty()) {
        return info.title;
    }
    return QCoreApplication::translate("SpaceLook", "Media Preview");
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

QString propVariantToString(const PROPVARIANT& value)
{
    switch (value.vt) {
    case VT_LPWSTR:
        return value.pwszVal ? QString::fromWCharArray(value.pwszVal).trimmed() : QString();
    case VT_BSTR:
        return value.bstrVal ? QString::fromWCharArray(value.bstrVal).trimmed() : QString();
    case VT_LPSTR:
        return value.pszVal ? QString::fromLocal8Bit(value.pszVal).trimmed() : QString();
    case VT_UI4:
        return QString::number(value.ulVal);
    case VT_UI8:
        return QString::number(value.uhVal.QuadPart);
    case VT_I4:
        return QString::number(value.lVal);
    default:
        return QString();
    }
}

QString shellPropertyString(IPropertyStore* propertyStore, const PROPERTYKEY& key)
{
    if (!propertyStore) {
        return QString();
    }

    PROPVARIANT value;
    PropVariantInit(&value);
    QString result;
    if (SUCCEEDED(propertyStore->GetValue(key, &value))) {
        result = propVariantToString(value);
    }
    PropVariantClear(&value);
    return result;
}

QString fourCcText(const QString& rawValue)
{
    bool ok = false;
    const quint32 fourCc = rawValue.toUInt(&ok);
    if (!ok || fourCc == 0) {
        return rawValue.trimmed();
    }

    QString text;
    for (int index = 0; index < 4; ++index) {
        const QChar ch(static_cast<char>((fourCc >> (index * 8)) & 0xff));
        if (!ch.isPrint()) {
            return rawValue.trimmed();
        }
        text.append(ch);
    }
    return text.trimmed();
}

QString bitrateText(const QString& rawValue)
{
    bool ok = false;
    const double bitsPerSecond = rawValue.toDouble(&ok);
    if (!ok || bitsPerSecond <= 0.0) {
        return QString();
    }

    if (bitsPerSecond >= 1000.0 * 1000.0) {
        return QStringLiteral("%1 Mbps").arg(QString::number(bitsPerSecond / 1000.0 / 1000.0, 'f', 2));
    }
    return QStringLiteral("%1 Kbps").arg(QString::number(bitsPerSecond / 1000.0, 'f', 0));
}

MediaCodecMetadata mediaInfoCodecMetadataForFile(const QString& filePath)
{
    MediaCodecMetadata metadata;
    const QString cleanPath = filePath.trimmed();
    if (cleanPath.isEmpty()) {
        return metadata;
    }

    MediaInfoLib::MediaInfo mediaInfo;
    mediaInfo.Option(mediaInfoParameter("ParseUnknownExtensions"), mediaInfoParameter("1"));
    if (mediaInfo.Open(mediaInfoPath(cleanPath)) == 0) {
        return metadata;
    }

    metadata.containerFormat = mediaInfoGet(mediaInfo, MediaInfoLib::Stream_General, "Format");
    metadata.videoCompression = mediaInfoGet(mediaInfo, MediaInfoLib::Stream_Video, "Format");
    metadata.videoFourCc = mediaInfoGet(mediaInfo, MediaInfoLib::Stream_Video, "CodecID");
    metadata.videoBitrate = mediaInfoGet(mediaInfo, MediaInfoLib::Stream_Video, "BitRate/String");
    metadata.videoWidth = mediaInfoGet(mediaInfo, MediaInfoLib::Stream_Video, "Width");
    metadata.videoHeight = mediaInfoGet(mediaInfo, MediaInfoLib::Stream_Video, "Height");
    metadata.audioFormat = mediaInfoGet(mediaInfo, MediaInfoLib::Stream_Audio, "Format");
    metadata.audioCompression = mediaInfoGet(mediaInfo, MediaInfoLib::Stream_Audio, "CodecID");
    mediaInfo.Close();
    return metadata;
}

MediaCodecMetadata mediaCodecMetadataForFile(const QString& filePath)
{
    MediaCodecMetadata metadata = mediaInfoCodecMetadataForFile(filePath);
    if (!metadata.videoCompression.isEmpty() ||
        !metadata.videoFourCc.isEmpty() ||
        !metadata.audioFormat.isEmpty() ||
        !metadata.audioCompression.isEmpty()) {
        return metadata;
    }

    const QString cleanPath = filePath.trimmed();
    if (cleanPath.isEmpty()) {
        return metadata;
    }

    const HRESULT initHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool shouldUninitialize = SUCCEEDED(initHr);
    if (FAILED(initHr) && initHr != RPC_E_CHANGED_MODE) {
        return metadata;
    }

    IPropertyStore* propertyStore = nullptr;
    const QString nativePath = QDir::toNativeSeparators(cleanPath);
    const HRESULT storeHr = SHGetPropertyStoreFromParsingName(
        reinterpret_cast<PCWSTR>(nativePath.utf16()),
        nullptr,
        GPS_DEFAULT,
        IID_PPV_ARGS(&propertyStore));
    if (FAILED(storeHr) || !propertyStore) {
        if (shouldUninitialize) {
            CoUninitialize();
        }
        return metadata;
    }

    metadata.videoCompression = shellPropertyString(propertyStore, PKEY_Video_Compression);
    metadata.videoFourCc = fourCcText(shellPropertyString(propertyStore, PKEY_Video_FourCC));
    metadata.videoBitrate = bitrateText(shellPropertyString(propertyStore, PKEY_Video_EncodingBitrate));
    metadata.videoWidth = shellPropertyString(propertyStore, PKEY_Video_FrameWidth);
    metadata.videoHeight = shellPropertyString(propertyStore, PKEY_Video_FrameHeight);
    metadata.audioFormat = shellPropertyString(propertyStore, PKEY_Audio_Format);
    metadata.audioCompression = shellPropertyString(propertyStore, PKEY_Audio_Compression);

    propertyStore->Release();
    if (shouldUninitialize) {
        CoUninitialize();
    }

    return metadata;
}

QString codecProbeText(const HoveredItemInfo& info, const MediaCodecMetadata& metadata)
{
    return QStringList{
        QFileInfo(info.filePath).suffix(),
        metadata.containerFormat,
        metadata.videoCompression,
        metadata.videoFourCc,
        metadata.audioFormat,
        metadata.audioCompression
    }.join(QStringLiteral(" ")).toLower();
}

bool codecTextContainsAny(const QString& text, const QStringList& needles)
{
    for (const QString& needle : needles) {
        if (text.contains(needle, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

bool isCodecMissingMediaError(const QString& errorText)
{
    const QString lower = errorText.trimmed().toLower();
    return lower.contains(QStringLiteral("unsupported media")) ||
        lower.contains(QStringLiteral("codec is missing")) ||
        lower.contains(QStringLiteral("missing codec")) ||
        lower.contains(QStringLiteral("codec not found")) ||
        lower.contains(QStringLiteral("unsupported codec"));
}

QString normalizedCodecToken(const QString& value)
{
    QString token = value.trimmed().toLower();
    token.remove(QLatin1Char('.'));
    token.remove(QLatin1Char('-'));
    token.remove(QLatin1Char('_'));
    token.remove(QLatin1Char(' '));
    return token;
}

QString friendlyVideoCodecName(const MediaCodecMetadata& metadata)
{
    const QString probe = codecProbeText(HoveredItemInfo(), metadata);
    const QString token = normalizedCodecToken(metadata.videoFourCc + QLatin1Char(' ') + metadata.videoCompression);

    if (token.contains(QStringLiteral("h264")) ||
        token.contains(QStringLiteral("avc")) ||
        token.contains(QStringLiteral("x264")) ||
        probe.contains(QStringLiteral("34363248"))) {
        return QStringLiteral("H.264 video");
    }
    if (token.contains(QStringLiteral("hevc")) ||
        token.contains(QStringLiteral("h265")) ||
        token.contains(QStringLiteral("hvc1")) ||
        token.contains(QStringLiteral("hev1"))) {
        return QStringLiteral("H.265 video");
    }
    if (token.contains(QStringLiteral("av1")) ||
        token.contains(QStringLiteral("av01"))) {
        return QStringLiteral("AV1 video");
    }
    if (token.contains(QStringLiteral("vp9")) ||
        token.contains(QStringLiteral("vp09"))) {
        return QStringLiteral("VP9 video");
    }
    if (token.contains(QStringLiteral("wmv"))) {
        return QStringLiteral("WMV video");
    }
    if (token.contains(QStringLiteral("avc"))) {
        return QStringLiteral("H.264 video");
    }
    if (token.contains(QStringLiteral("mpg")) ||
        token.contains(QStringLiteral("mpeg"))) {
        return QStringLiteral("MPEG video");
    }
    if (token.contains(QStringLiteral("theora"))) {
        return QStringLiteral("Theora video");
    }

    return QString();
}

QString friendlyAudioCodecName(const MediaCodecMetadata& metadata)
{
    const QString token = normalizedCodecToken(metadata.audioFormat + QLatin1Char(' ') + metadata.audioCompression);

    if (token.contains(QStringLiteral("aac")) ||
        token.contains(QStringLiteral("00001610"))) {
        return QStringLiteral("AAC audio");
    }
    if (token.contains(QStringLiteral("mp3")) ||
        token.contains(QStringLiteral("mpeglayer3")) ||
        token.contains(QStringLiteral("0055"))) {
        return QStringLiteral("MP3 audio");
    }
    if (token.contains(QStringLiteral("opus"))) {
        return QStringLiteral("Opus audio");
    }
    if (token.contains(QStringLiteral("vorbis"))) {
        return QStringLiteral("Vorbis audio");
    }
    if (token.contains(QStringLiteral("flac"))) {
        return QStringLiteral("FLAC audio");
    }
    if (token.contains(QStringLiteral("wma"))) {
        return QStringLiteral("WMA audio");
    }
    if (token.contains(QStringLiteral("pcm"))) {
        return QStringLiteral("PCM audio");
    }

    return QString();
}

QString normalizedDisplayToken(const QString& value)
{
    return value.trimmed().replace(QLatin1Char('_'), QLatin1Char(' '));
}

QString friendlyCodecSummaryForMetadata(const MediaCodecMetadata& metadata)
{
    QStringList parts;
    QString videoCodec = friendlyVideoCodecName(metadata);
    if (videoCodec.isEmpty() && !metadata.videoCompression.trimmed().isEmpty()) {
        videoCodec = QStringLiteral("%1 video").arg(normalizedDisplayToken(metadata.videoCompression));
    }
    if (!videoCodec.isEmpty()) {
        parts.append(videoCodec);
    }

    QString audioCodec = friendlyAudioCodecName(metadata);
    if (audioCodec.isEmpty() && !metadata.audioFormat.trimmed().isEmpty()) {
        audioCodec = QStringLiteral("%1 audio").arg(normalizedDisplayToken(metadata.audioFormat));
    }
    if (!audioCodec.isEmpty()) {
        parts.append(audioCodec);
    }

    if (!metadata.videoWidth.isEmpty() && !metadata.videoHeight.isEmpty()) {
        parts.append(QStringLiteral("%1x%2").arg(metadata.videoWidth, metadata.videoHeight));
    }

    if (parts.isEmpty() && !metadata.containerFormat.trimmed().isEmpty()) {
        parts.append(QStringLiteral("%1 container").arg(normalizedDisplayToken(metadata.containerFormat)));
    }

    return parts.isEmpty() ? QString() : QCoreApplication::translate("SpaceLook", "Codec: %1").arg(parts.join(QStringLiteral(", ")));
}

bool isWindowsDefaultVideoCodec(const MediaCodecMetadata& metadata)
{
    const QString fourCc = normalizedCodecToken(metadata.videoFourCc);
    const QString compression = normalizedCodecToken(metadata.videoCompression);
    return fourCc == QStringLiteral("h264") ||
        fourCc == QStringLiteral("avc1") ||
        fourCc == QStringLiteral("avc") ||
        fourCc == QStringLiteral("x264") ||
        fourCc == QStringLiteral("wmv1") ||
        fourCc == QStringLiteral("wmv2") ||
        fourCc == QStringLiteral("wmv3") ||
        fourCc == QStringLiteral("wvc1") ||
        compression.contains(QStringLiteral("h264")) ||
        compression.contains(QStringLiteral("avc")) ||
        compression.contains(QStringLiteral("wmv"));
}

QString playbackPolicyName(MediaPlaybackPolicy policy)
{
    switch (policy) {
    case MediaPlaybackPolicy::WindowsDefault:
        return QStringLiteral("WindowsDefault");
    case MediaPlaybackPolicy::WindowsThenExtensionOrMpv:
        return QStringLiteral("WindowsThenExtensionOrMpv");
    case MediaPlaybackPolicy::MpvPreferred:
        return QStringLiteral("MpvPreferred");
    }
    return QStringLiteral("Unknown");
}

QString windowsFailureAdviceForPolicy(MediaPlaybackPolicy policy)
{
    switch (policy) {
    case MediaPlaybackPolicy::WindowsDefault:
        return QCoreApplication::translate("SpaceLook", "This codec is expected to use the Windows media backend. Check Qt multimedia plugin deployment and Windows media components.");
    case MediaPlaybackPolicy::WindowsThenExtensionOrMpv:
        return QCoreApplication::translate("SpaceLook", "Install the matching Windows media extension, such as HEVC, AV1, or VP9, or install the optional mpv enhanced playback runtime. Download help: %1")
            .arg(QString::fromLatin1(kMpvRuntimeHelpUrl));
    case MediaPlaybackPolicy::MpvPreferred:
        return QCoreApplication::translate("SpaceLook", "This codec is usually better supported by the optional mpv enhanced playback runtime. Download help: %1")
            .arg(QString::fromLatin1(kMpvRuntimeHelpUrl));
    }
    return QCoreApplication::translate("SpaceLook", "Check Windows media components or install the optional mpv enhanced playback runtime. Download help: %1")
        .arg(QString::fromLatin1(kMpvRuntimeHelpUrl));
}

MediaPlaybackPolicy playbackPolicyForMedia(const HoveredItemInfo& info, const MediaCodecMetadata& metadata)
{
    const QString probe = codecProbeText(info, metadata);

    if (isAudioType(info)) {
        if (codecTextContainsAny(probe, {
                QStringLiteral("aac"),
                QStringLiteral("mp3"),
                QStringLiteral("mpeg layer-3"),
                QStringLiteral("mpeg layer 3"),
                QStringLiteral("pcm"),
                QStringLiteral("wave"),
                QStringLiteral("wav"),
                QStringLiteral("aiff"),
                QStringLiteral("aif"),
                QStringLiteral("alac"),
                QStringLiteral("mid"),
                QStringLiteral("midi")
            })) {
            return MediaPlaybackPolicy::WindowsDefault;
        }

        if (codecTextContainsAny(probe, {
                QStringLiteral("ape"),
                QStringLiteral("monkey"),
                QStringLiteral("opus"),
                QStringLiteral("vorbis"),
                QStringLiteral("oga"),
                QStringLiteral("ogg")
            })) {
            return MediaPlaybackPolicy::MpvPreferred;
        }

        return MediaPlaybackPolicy::WindowsThenExtensionOrMpv;
    }

    if (isWindowsDefaultVideoCodec(metadata)) {
        return MediaPlaybackPolicy::WindowsDefault;
    }

    if (codecTextContainsAny(probe, {
            QStringLiteral("h.264"),
            QStringLiteral("h264"),
            QStringLiteral("avc"),
            QStringLiteral("avc1"),
            QStringLiteral("wmv"),
            QStringLiteral("wmv1"),
            QStringLiteral("wmv2"),
            QStringLiteral("wmv3"),
            QStringLiteral("wvc1")
        })) {
        return MediaPlaybackPolicy::WindowsDefault;
    }

    if (codecTextContainsAny(probe, {
            QStringLiteral("hevc"),
            QStringLiteral("h.265"),
            QStringLiteral("h265"),
            QStringLiteral("hev1"),
            QStringLiteral("hvc1"),
            QStringLiteral("av1"),
            QStringLiteral("av01"),
            QStringLiteral("vp9"),
            QStringLiteral("vp09")
        })) {
        return MediaPlaybackPolicy::WindowsThenExtensionOrMpv;
    }

    if (codecTextContainsAny(probe, {
            QStringLiteral("prores"),
            QStringLiteral("apch"),
            QStringLiteral("apcn"),
            QStringLiteral("apcs"),
            QStringLiteral("apco"),
            QStringLiteral("dnxhd"),
            QStringLiteral("dnxhr"),
            QStringLiteral("ffv1"),
            QStringLiteral("huffyuv"),
            QStringLiteral("huff"),
            QStringLiteral("lagarith"),
            QStringLiteral("lags"),
            QStringLiteral("theora"),
            QStringLiteral("realvideo"),
            QStringLiteral("rv40"),
            QStringLiteral("rv30"),
            QStringLiteral("bink"),
            QStringLiteral("smacker"),
            QStringLiteral("cineform"),
            QStringLiteral("dirac"),
            QStringLiteral("flv1"),
            QStringLiteral("sorenson")
        })) {
        return MediaPlaybackPolicy::MpvPreferred;
    }

    return MediaPlaybackPolicy::WindowsThenExtensionOrMpv;
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
            !setOptionString("osd-level", "0", errorMessage) ||
            !setOptionString("osd-bar", "no", errorMessage) ||
            !setOptionString("border", "no", errorMessage) ||
            !setOptionString("keep-open", "yes", errorMessage) ||
            !setOptionString("volume-max", "200", errorMessage) ||
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

    bool loadFile(const QString& filePath, QString* errorMessage, bool startPaused = true)
    {
        if (!m_handle || !m_command) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("libmpv is not initialized.");
            }
            return false;
        }

        if (!setPaused(startPaused)) {
            qDebug().noquote() << QStringLiteral("[SpaceLookRender] libmpv pre-load pause setup failed");
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
        if (!loadFile(filePath, errorMessage, startPaused)) {
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

class WindowsMediaPlayerAudioBackend
{
public:
    ~WindowsMediaPlayerAudioBackend()
    {
        shutdown();
    }

    bool load(const QString& filePath, QString* errorMessage)
    {
        shutdown();

        m_player = new QAxObject(QStringLiteral("WMPlayer.OCX"));
        if (!m_player || m_player->isNull()) {
            shutdown();
            if (errorMessage) {
                *errorMessage = QStringLiteral("Windows Media Player runtime is unavailable.");
            }
            return false;
        }

        m_controls = m_player->querySubObject("controls");
        m_settings = m_player->querySubObject("settings");
        m_currentMedia = m_player->querySubObject("newMedia(const QString&)", QDir::toNativeSeparators(filePath));
        if (!m_controls || !m_settings || !m_currentMedia) {
            shutdown();
            if (errorMessage) {
                *errorMessage = QStringLiteral("Windows Media Player could not prepare this audio file.");
            }
            return false;
        }

        m_player->setProperty("uiMode", QStringLiteral("none"));
        m_player->setProperty("stretchToFit", false);
        m_player->setProperty("enableContextMenu", false);
        m_settings->setProperty("autoStart", false);
        m_settings->setProperty("mute", false);
        m_settings->setProperty("volume", kDefaultMediaVolume);
        m_player->setProperty("currentMedia", m_currentMedia->asVariant());
        m_paused = true;
        return true;
    }

    void shutdown()
    {
        if (m_controls) {
            m_controls->dynamicCall("stop()");
        }
        delete m_currentMedia;
        delete m_settings;
        delete m_controls;
        delete m_player;
        m_currentMedia = nullptr;
        m_settings = nullptr;
        m_controls = nullptr;
        m_player = nullptr;
        m_paused = true;
        m_volume = kDefaultMediaVolume;
        m_muted = false;
    }

    bool isLoaded() const
    {
        return m_player && !m_player->isNull() && m_controls && m_settings;
    }

    bool setPaused(bool paused)
    {
        if (!isLoaded()) {
            return false;
        }

        m_controls->dynamicCall(paused ? "pause()" : "play()");
        m_paused = paused;
        return true;
    }

    bool paused(bool* pausedState) const
    {
        if (!pausedState) {
            return false;
        }
        *pausedState = m_paused;
        return true;
    }

    bool durationSeconds(double* duration) const
    {
        if (!duration || !m_currentMedia) {
            return false;
        }
        bool ok = false;
        const double value = m_currentMedia->property("duration").toDouble(&ok);
        if (!ok || value <= 0.0) {
            return false;
        }
        *duration = value;
        return true;
    }

    bool positionSeconds(double* position) const
    {
        if (!position || !m_controls) {
            return false;
        }
        bool ok = false;
        const double value = m_controls->property("currentPosition").toDouble(&ok);
        if (!ok) {
            return false;
        }
        *position = value;
        return true;
    }

    bool seekSeconds(double seconds)
    {
        if (!m_controls) {
            return false;
        }
        m_controls->setProperty("currentPosition", qMax(0.0, seconds));
        return true;
    }

    bool setMuted(bool muted)
    {
        if (!m_settings) {
            return false;
        }
        m_muted = muted;
        m_settings->setProperty("mute", muted);
        return true;
    }

    bool muted(bool* mutedState) const
    {
        if (!mutedState) {
            return false;
        }
        *mutedState = m_muted;
        return true;
    }

    bool setVolume(int volume)
    {
        if (!m_settings) {
            return false;
        }
        m_volume = qBound(0, volume, kMaxMediaVolume);
        m_settings->setProperty("volume", m_volume);
        return true;
    }

    int volume() const
    {
        return m_volume;
    }

private:
    QAxObject* m_player = nullptr;
    QAxObject* m_controls = nullptr;
    QAxObject* m_settings = nullptr;
    QAxObject* m_currentMedia = nullptr;
    bool m_paused = true;
    bool m_muted = false;
    int m_volume = kDefaultMediaVolume;
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
    , m_statusRow(new QWidget(this))
    , m_statusLabel(new QLabel(m_statusRow))
    , m_mpvHelpButton(new QPushButton(m_statusRow))
    , m_stageCard(new QWidget(this))
    , m_stageLayout(new QVBoxLayout(m_stageCard))
    , m_controlsCard(new QWidget(this))
    , m_mediaViewport(new QWidget(this))
    , m_centerOverlayLabel(new QLabel(m_mediaViewport))
    , m_audioPlaceholder(new QLabel(m_mediaViewport))
    , m_videoPlaceholder(new QLabel(m_mediaViewport))
    , m_videoHost(new QWidget(m_mediaViewport))
    , m_qtVideoWidget(new QVideoWidget(m_mediaViewport))
    , m_playPauseButton(new QPushButton(m_controlsCard))
    , m_volumeButton(new QPushButton(m_controlsCard))
    , m_positionLabel(new QLabel(m_controlsCard))
    , m_durationLabel(new QLabel(m_controlsCard))
    , m_positionSlider(new QSlider(Qt::Horizontal, m_controlsCard))
    , m_volumeValueLabel(new QLabel(m_controlsCard))
    , m_volumeSlider(new QSlider(Qt::Horizontal, m_controlsCard))
    , m_videoPollTimer(new QTimer(this))
    , m_mpvBackend(new InternalMpvVideoBackend())
    , m_windowsAudioBackend(new WindowsMediaPlayerAudioBackend())
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
    m_statusRow->setObjectName(QStringLiteral("MediaStatusRow"));
    m_statusLabel->setObjectName(QStringLiteral("MediaStatus"));
    m_mpvHelpButton->setObjectName(QStringLiteral("MediaMpvHelpButton"));
    m_stageCard->setObjectName(QStringLiteral("MediaStage"));
    m_controlsCard->setObjectName(QStringLiteral("MediaControlsCard"));
    m_mediaViewport->setObjectName(QStringLiteral("MediaViewport"));
    m_centerOverlayLabel->setObjectName(QStringLiteral("MediaCenterOverlay"));
    m_audioPlaceholder->setObjectName(QStringLiteral("MediaAudioPlaceholder"));
    m_videoPlaceholder->setObjectName(QStringLiteral("MediaVideoPlaceholder"));
    m_videoHost->setObjectName(QStringLiteral("MediaVideoHost"));
    m_qtVideoWidget->setObjectName(QStringLiteral("MediaQtVideoWidget"));
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
    rootLayout->addWidget(m_statusRow);
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

    auto* statusLayout = new QHBoxLayout(m_statusRow);
    statusLayout->setContentsMargins(0, 0, 0, 0);
    statusLayout->setSpacing(8);
    statusLayout->addWidget(m_statusLabel, 1);
    statusLayout->addWidget(m_mpvHelpButton);

    m_stageLayout->setContentsMargins(12, 12, 12, 12);
    m_stageLayout->setSpacing(10);
    auto* viewportLayout = new QStackedLayout(m_mediaViewport);
    viewportLayout->setStackingMode(QStackedLayout::StackAll);
    viewportLayout->setContentsMargins(0, 0, 0, 0);
    viewportLayout->addWidget(m_videoPlaceholder);
    viewportLayout->addWidget(m_qtVideoWidget);
    viewportLayout->addWidget(m_videoHost);
    viewportLayout->addWidget(m_audioPlaceholder);
    viewportLayout->addWidget(m_centerOverlayLabel);
    m_stageLayout->addWidget(m_mediaViewport, 1);

    auto* controlsShellLayout = new QVBoxLayout(m_controlsCard);
    controlsShellLayout->setContentsMargins(12, 8, 12, 8);
    controlsShellLayout->setSpacing(0);

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
    m_iconLabel->setScaledContents(false);
    m_iconLabel->setAlignment(Qt::AlignCenter);
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
    m_qtVideoWidget->hide();
    m_qtVideoWidget->installEventFilter(this);
    m_qtVideoWidget->setAspectRatioMode(Qt::KeepAspectRatio);

    m_positionSlider->setRange(0, 0);
    m_positionLabel->setText(QStringLiteral("00:00"));
    m_durationLabel->setText(QStringLiteral("00:00"));
    m_positionLabel->setFixedWidth(48);
    m_durationLabel->setFixedWidth(48);
    m_positionSlider->installEventFilter(this);
    m_volumeSlider->setRange(0, kMaxMediaVolume);
    m_volumeSlider->setValue(m_audioVolume);
    m_volumeSlider->setFixedWidth(76);
    m_playPauseButton->setFixedSize(38, 38);
    m_volumeButton->setFixedSize(34, 34);
    m_playPauseButton->setCursor(Qt::PointingHandCursor);
    m_volumeButton->setCursor(Qt::PointingHandCursor);
    m_positionSlider->setCursor(Qt::PointingHandCursor);
    m_volumeSlider->setCursor(Qt::PointingHandCursor);
    m_playPauseButton->setToolTip(QCoreApplication::translate("SpaceLook", "Play or pause"));
    m_volumeButton->setToolTip(QCoreApplication::translate("SpaceLook", "Mute or restore volume"));
    m_mpvHelpButton->setText(QCoreApplication::translate("SpaceLook", "Get MPV Plugin"));
    m_mpvHelpButton->setCursor(Qt::PointingHandCursor);
    m_mpvHelpButton->setToolTip(QCoreApplication::translate("SpaceLook", "Open SpaceLook MPV plugin download page"));
    PreviewStateVisuals::prepareStatusLabel(m_statusLabel);
    m_statusRow->hide();
    m_statusLabel->hide();
    m_mpvHelpButton->hide();
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

    connect(m_mpvHelpButton, &QPushButton::clicked, this, []() {
        QDesktopServices::openUrl(QUrl(QString::fromLatin1(kMpvRuntimeHelpUrl)));
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
        if (!isCurrentMediaLoad()) {
            m_videoPollTimer->stop();
            return;
        }

        if (m_usingWindowsAudioFallback && m_windowsAudioBackend && m_windowsAudioBackend->isLoaded()) {
            double durationSeconds = 0.0;
            if (m_windowsAudioBackend->durationSeconds(&durationSeconds)) {
                m_positionSlider->setRange(0, qMax(0, static_cast<int>(durationSeconds * 1000.0)));
                m_durationLabel->setText(formatTime(static_cast<qint64>(durationSeconds * 1000.0)));
            }

            if (!m_isSeeking) {
                double positionSeconds = 0.0;
                if (m_windowsAudioBackend->positionSeconds(&positionSeconds)) {
                    const qint64 positionMilliseconds = static_cast<qint64>(positionSeconds * 1000.0);
                    m_positionSlider->setValue(static_cast<int>(positionMilliseconds));
                    m_positionLabel->setText(formatTime(positionMilliseconds));
                    m_videoPreviewReady = true;
                }
            }

            bool paused = true;
            if (m_windowsAudioBackend->paused(&paused)) {
                m_mpvPaused = paused;
            }
            updatePlaybackButton();
            updateCenterOverlay();
            return;
        }

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
                if (!m_mpvPaused) {
                    m_videoStartedPlayback = true;
                }
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
    delete m_windowsAudioBackend;
    m_windowsAudioBackend = nullptr;
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

    if (watched == m_qtVideoWidget && event && isVideoType(m_info) && !m_usingMpvPlayback) {
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

void MediaRenderer::warmUp()
{
    ensureAudioBackend();
    m_videoHost->winId();
    m_qtVideoWidget->winId();
}

void MediaRenderer::load(const HoveredItemInfo& info)
{
    unload();
    m_info = info;
    m_loadToken = m_loadGuard.begin(info.filePath);
    m_mpvPaused = true;
    m_videoPreviewReady = false;
    m_videoStartedPlayback = false;
    m_usingWindowsAudioFallback = false;
    m_triedMpvVideoFallback = false;
    m_triedMpvAudioFallback = false;
    m_allowMpvFallbackForCurrentMedia = false;
    m_videoHost->hide();
    m_qtVideoWidget->hide();
    m_videoPlaceholder->hide();
    m_audioPlaceholder->hide();
    m_centerOverlayLabel->hide();
    qDebug().noquote() << QStringLiteral("[SpaceLookRender] MediaRenderer load path=\"%1\" typeKey=%2")
        .arg(info.filePath, info.typeKey);

    m_titleLabel->setText(displayTitleForMedia(info));
    m_titleLabel->setCopyText(m_titleLabel->text());
    m_iconLabel->setPixmap(FileTypeIconResolver::pixmapForInfo(info, m_iconLabel->contentsRect().size()));
    m_pathValueLabel->setText(info.filePath.trimmed().isEmpty() ? QCoreApplication::translate("SpaceLook", "(Unavailable)") : info.filePath);
    m_openWithButton->setTargetContext(info.filePath, info.typeKey);

    m_positionSlider->setValue(0);
    m_positionSlider->setRange(0, 0);
    m_positionLabel->setText(QStringLiteral("00:00"));
    m_durationLabel->setText(QStringLiteral("00:00"));

    const MediaCodecMetadata codecMetadata = mediaCodecMetadataForFile(info.filePath);
    const MediaPlaybackPolicy playbackPolicy = playbackPolicyForMedia(info, codecMetadata);
    m_currentCodecSummary = friendlyCodecSummaryForMetadata(codecMetadata);
    m_allowMpvFallbackForCurrentMedia = true;
    qDebug().noquote() << QStringLiteral("[SpaceLookRender] Media codec policy=%1 summary=\"%2\" path=\"%3\"")
        .arg(playbackPolicyName(playbackPolicy), m_currentCodecSummary, info.filePath);

    if (isVideoType(info)) {
        destroyAudioBackend();
        if (tryStartMpvVideo()) {
            return;
        }

        destroyVideoBackend();
        ensureAudioBackend();
        m_usingMpvPlayback = false;
        m_usingMpvVideo = false;
        m_videoPreviewReady = false;

        if (!m_player) {
            updateStatusLabel(mpvInstallHint(QCoreApplication::translate("SpaceLook", "Windows media playback backend is unavailable.")));
            return;
        }

        m_player->stop();
        m_player->setVideoOutput(m_qtVideoWidget);
        m_player->setMuted(false);
        m_player->setVolume(qMin(kDefaultMediaVolume, kMaxQtPlayerVolume));
        m_audioVolume = kDefaultMediaVolume;
        m_player->setMedia(QUrl::fromLocalFile(info.filePath));
        updateStatusLabel(QCoreApplication::translate("SpaceLook", "Video ready. Click play to start."));
        updateMediaUi();
        updatePlaybackButton();
        updateVolumeButton();
        updateVolumePopup();
        updateCenterOverlay();
        return;
    }

    destroyAudioBackend();
    if (playbackPolicy == MediaPlaybackPolicy::MpvPreferred && tryStartMpvAudio()) {
        return;
    }

    destroyVideoBackend();
    ensureAudioBackend();
    m_usingMpvPlayback = false;
    if (!m_player) {
        updateStatusLabel(mpvInstallHint(QCoreApplication::translate("SpaceLook", "Audio backend is unavailable.")));
        updateMediaUi();
        updatePlaybackButton();
        updateCenterOverlay();
        return;
    }

    m_player->stop();
    m_player->setMuted(false);
    m_player->setVolume(qMin(m_audioVolume, kMaxQtPlayerVolume));
    m_player->setMedia(QUrl::fromLocalFile(info.filePath));
    updateStatusLabel(QCoreApplication::translate("SpaceLook", "Audio ready. Click play to start."));
    updateMediaUi();
    updatePlaybackButton();
    updateVolumeButton();
    updateVolumePopup();
    updateCenterOverlay();
}

void MediaRenderer::unload()
{
    m_loadGuard.cancel();
    m_loadToken = PreviewLoadGuard::Token();
    destroyAudioBackend();
    destroyVideoBackend();
    if (m_windowsAudioBackend) {
        m_windowsAudioBackend->shutdown();
    }
    m_info = HoveredItemInfo();
    m_mpvPaused = true;
    m_videoPreviewReady = false;
    m_videoStartedPlayback = false;
    m_usingWindowsAudioFallback = false;
    m_triedMpvVideoFallback = false;
    m_triedMpvAudioFallback = false;
    m_allowMpvFallbackForCurrentMedia = false;
    m_currentCodecSummary.clear();
    m_videoMuted = true;
    m_positionSlider->setValue(0);
    m_positionSlider->setRange(0, 0);
    m_positionLabel->setText(QStringLiteral("00:00"));
    m_durationLabel->setText(QStringLiteral("00:00"));
    updateStatusLabel(QString());
    m_pathValueLabel->clear();
    m_openWithButton->setTargetContext(QString(), QString());
    m_audioPlaceholder->clear();
    m_audioPlaceholder->hide();
    m_videoPlaceholder->clear();
    m_videoPlaceholder->hide();
    m_videoHost->hide();
    m_qtVideoWidget->hide();
    m_centerOverlayLabel->hide();
    m_playPauseButton->setText(playGlyph());
    m_volumeButton->setVisible(true);
    m_volumeButton->setText(volumeGlyph());
    const bool sliderWasBlocked = m_volumeSlider->blockSignals(true);
    m_volumeSlider->setVisible(true);
    m_volumeSlider->setValue(m_audioVolume);
    m_volumeSlider->blockSignals(sliderWasBlocked);
    m_volumeValueLabel->setText(QString::number(m_audioVolume));
}

void MediaRenderer::applyChrome()
{
    setStyleSheet(
        "#MediaRendererRoot {"
        "  background: #f3f3f3;"
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
        "  color: #1f1f1f;"
        "}"
        "#MediaMeta {"
        "  color: #616161;"
        "}"
        "#MediaPathTitle {"
        "  color: #16324a;"
        "  font-family: 'Segoe UI Rounded';"
        "}"
        "#MediaPathValue {"
        "  color: #5f5f5f;"
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
        "  color: #3b3a00;"
        "  background: #fff4ce;"
        "  border: 1px solid #f3d77d;"
        "  border-radius: 4px;"
        "  padding: 8px 12px;"
        "}"
        "#MediaMpvHelpButton {"
        "  background: #0078d4;"
        "  color: #ffffff;"
        "  border: 1px solid #0078d4;"
        "  border-radius: 10px;"
        "  padding: 8px 14px;"
        "  min-width: 76px;"
        "  font-family: 'Segoe UI Rounded';"
        "  font-size: 12px;"
        "  font-weight: 700;"
        "}"
        "#MediaMpvHelpButton:hover {"
        "  background: #106ebe;"
        "  border-color: #106ebe;"
        "}"
        "#MediaMpvHelpButton:pressed {"
        "  background: #005a9e;"
        "  border-color: #005a9e;"
        "}"
        "#MediaStage {"
        "  background: #111111;"
        "  border: 1px solid #d0d0d0;"
        "  border-radius: 8px;"
        "}"
        "#MediaControlsCard {"
        "  background: rgba(248, 250, 253, 0.94);"
        "  border: 1px solid rgba(213, 223, 235, 0.92);"
        "  border-radius: 12px;"
        "}"
        "#MediaViewport {"
        "  background: transparent;"
        "}"
        "#MediaAudioPlaceholder, #MediaVideoHost, #MediaVideoPlaceholder {"
        "  background: #1b1b1b;"
        "  color: #f3f3f3;"
        "  border: 1px solid #2b2b2b;"
        "  border-radius: 4px;"
        "  padding: 12px;"
        "}"
        "#MediaCenterOverlay {"
        "  background: transparent;"
        "  color: rgba(255, 255, 255, 0.92);"
        "}"
        "#MediaPlayPause {"
        "  background: rgba(255, 255, 255, 0.72);"
        "  color: #005fb8;"
        "  border: 1px solid rgba(208, 221, 235, 0.95);"
        "  border-radius: 10px;"
        "  min-width: 38px;"
        "  min-height: 38px;"
        "  padding: 0px;"
        "}"
        "#MediaPlayPause:hover {"
        "  background: rgba(235, 246, 255, 1.0);"
        "  border-color: rgba(154, 205, 255, 0.95);"
        "  color: #005a9e;"
        "}"
        "#MediaPlayPause:pressed {"
        "  background: rgba(211, 235, 255, 1.0);"
        "  border-color: rgba(96, 171, 245, 1.0);"
        "  color: #004578;"
        "}"
        "#MediaVolumeButton {"
        "  background: rgba(255, 255, 255, 0.54);"
        "  color: #243548;"
        "  border: 1px solid rgba(208, 221, 235, 0.82);"
        "  border-radius: 9px;"
        "  min-width: 34px;"
        "  min-height: 34px;"
        "  padding: 0px;"
        "}"
        "#MediaVolumeButton:hover {"
        "  background: rgba(235, 246, 255, 1.0);"
        "  border-color: rgba(154, 205, 255, 0.95);"
        "  color: #005a9e;"
        "}"
        "#MediaVolumeButton:pressed {"
        "  background: rgba(211, 235, 255, 1.0);"
        "  border-color: rgba(96, 171, 245, 1.0);"
        "  color: #004578;"
        "}"
        "#MediaVolumeValue {"
        "  color: #586678;"
        "  background: rgba(255, 255, 255, 0.58);"
        "  border: 1px solid rgba(208, 221, 235, 0.72);"
        "  border-radius: 8px;"
        "  font-family: 'Segoe UI Rounded';"
        "  font-size: 12px;"
        "  min-width: 34px;"
        "  padding: 5px 7px;"
        "}"
        "#MediaTimeLabel {"
        "  color: #586678;"
        "  min-width: 48px;"
        "}"
        "#MediaSlider::groove:horizontal {"
        "  height: 4px;"
        "  border-radius: 2px;"
        "  background: rgba(196, 207, 219, 0.92);"
        "}"
        "#MediaSlider::sub-page:horizontal {"
        "  border-radius: 2px;"
        "  background: #0078d4;"
        "}"
        "#MediaSlider::add-page:horizontal {"
        "  border-radius: 2px;"
        "  background: rgba(196, 207, 219, 0.92);"
        "}"
        "#MediaSlider::handle:horizontal {"
        "  background: #0078d4;"
        "  border: 2px solid rgba(255, 255, 255, 0.96);"
        "  width: 12px;"
        "  margin: -6px 0;"
        "  border-radius: 6px;"
        "}"
        "#MediaSlider::handle:horizontal:hover {"
        "  background: #106ebe;"
        "}"
        "#MediaVolumeSlider::groove:horizontal {"
        "  height: 3px;"
        "  border-radius: 2px;"
        "  background: rgba(196, 207, 219, 0.92);"
        "}"
        "#MediaVolumeSlider::sub-page:horizontal {"
        "  border-radius: 2px;"
        "  background: #0078d4;"
        "}"
        "#MediaVolumeSlider::add-page:horizontal {"
        "  border-radius: 2px;"
        "  background: rgba(196, 207, 219, 0.92);"
        "}"
        "#MediaVolumeSlider::handle:horizontal {"
        "  background: #0078d4;"
        "  border: 2px solid rgba(255, 255, 255, 0.96);"
        "  width: 10px;"
        "  margin: -5px 0;"
        "  border-radius: 5px;"
        "}"
    );

    QFont titleFont;
    titleFont.setFamily(QStringLiteral("Segoe UI Rounded"));
    titleFont.setPixelSize(20);
    titleFont.setWeight(QFont::Bold);
    m_titleLabel->setFont(titleFont);
    m_titleLabel->setWordWrap(true);

    QFont metaFont;
    metaFont.setFamily(QStringLiteral("Segoe UI Rounded"));
    metaFont.setPixelSize(11);
    m_metaLabel->setFont(metaFont);
    m_pathTitleLabel->setFont(metaFont);
    m_pathValueLabel->setFont(metaFont);
    m_statusLabel->setFont(metaFont);
    m_metaLabel->setWordWrap(true);
    m_pathValueLabel->setWordWrap(true);

    QFont placeholderFont;
    placeholderFont.setFamily(QStringLiteral("Segoe UI Rounded"));
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
        if (!isCurrentMediaLoad()) {
            return;
        }
        if (!m_isSeeking) {
            m_positionSlider->setValue(static_cast<int>(position));
            m_positionLabel->setText(formatTime(position));
        }
    });

    connect(m_player, &QMediaPlayer::durationChanged, this, [this](qint64 duration) {
        if (!isCurrentMediaLoad()) {
            return;
        }
        m_positionSlider->setRange(0, static_cast<int>(duration));
        m_durationLabel->setText(formatTime(duration));
    });

    connect(m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        if (!isCurrentMediaLoad()) {
            return;
        }
        if (m_usingMpvPlayback) {
            return;
        }

        qDebug().noquote() << QStringLiteral("[SpaceLookRender] Qt media status=%1 path=\"%2\" error=\"%3\"")
            .arg(static_cast<int>(status))
            .arg(m_info.filePath, m_player ? m_player->errorString() : QString());

        if (status == QMediaPlayer::LoadedMedia || status == QMediaPlayer::BufferedMedia) {
            m_videoPreviewReady = true;
            updateStatusLabel(QString());
            updateMediaUi();
            updatePlaybackButton();
            updateVolumeButton();
            updateVolumePopup();
            updateCenterOverlay();
            return;
        }

        if (status == QMediaPlayer::InvalidMedia && isVideoType(m_info)) {
            const QString errorText = m_player && !m_player->errorString().trimmed().isEmpty()
                ? m_player->errorString()
                : QCoreApplication::translate("SpaceLook", "Windows media backend could not parse this video.");
            if (isCodecMissingMediaError(errorText)) {
                updateStatusLabel(mpvInstallHint(errorText));
                updateMediaUi();
                updatePlaybackButton();
                updateVolumeButton();
                updateVolumePopup();
                updateCenterOverlay();
                return;
            }
            if (m_allowMpvFallbackForCurrentMedia) {
                fallbackToMpvVideo(errorText);
                return;
            }
            updateStatusLabel(windowsBackendFailureMessage(errorText));
        }
        if (status == QMediaPlayer::InvalidMedia && isAudioType(m_info)) {
            const QString errorText = m_player && !m_player->errorString().trimmed().isEmpty()
                ? m_player->errorString()
                : QCoreApplication::translate("SpaceLook", "Windows media backend could not parse this audio file.");
            if (isWindowsPlayerFallbackAudioFile(m_info) && tryStartWindowsAudioFallback(errorText)) {
                return;
            }
            fallbackToMpvAudio(errorText);
        }
    });

    connect(m_player, &QMediaPlayer::stateChanged, this, [this](QMediaPlayer::State state) {
        if (!isCurrentMediaLoad()) {
            return;
        }
        if (state == QMediaPlayer::PlayingState) {
            m_videoStartedPlayback = true;
            updateMediaUi();
        }
        updatePlaybackButton();
        updateCenterOverlay();
    });

    connect(m_player, &QMediaPlayer::mutedChanged, this, [this](bool) {
        if (!isCurrentMediaLoad()) {
            return;
        }
        updateVolumeButton();
        updateVolumePopup();
    });

    connect(m_player, &QMediaPlayer::volumeChanged, this, [this](int volume) {
        if (!isCurrentMediaLoad()) {
            return;
        }
        m_audioVolume = volume;
        updateVolumeButton();
        updateVolumePopup();
    });

    connect(m_player, QOverload<QMediaPlayer::Error>::of(&QMediaPlayer::error), this, [this](QMediaPlayer::Error) {
        if (!isCurrentMediaLoad()) {
            return;
        }
        const QString errorText = m_player && !m_player->errorString().trimmed().isEmpty()
            ? m_player->errorString()
            : QCoreApplication::translate("SpaceLook", "Media preview failed to load.");
        qDebug().noquote() << QStringLiteral("[SpaceLookRender] Media backend error path=\"%1\" message=\"%2\"")
            .arg(m_info.filePath, errorText);
        if (isVideoType(m_info) && !m_usingMpvPlayback) {
            if (isCodecMissingMediaError(errorText)) {
                updateStatusLabel(mpvInstallHint(errorText));
                updateMediaUi();
                updatePlaybackButton();
                updateVolumeButton();
                updateVolumePopup();
                updateCenterOverlay();
                return;
            }
            if (m_allowMpvFallbackForCurrentMedia) {
                fallbackToMpvVideo(errorText);
                return;
            }
            updateStatusLabel(windowsBackendFailureMessage(errorText));
            return;
        }
        if (isAudioType(m_info) && !m_usingMpvPlayback && m_allowMpvFallbackForCurrentMedia) {
            if (isWindowsPlayerFallbackAudioFile(m_info) && tryStartWindowsAudioFallback(errorText)) {
                return;
            }
            fallbackToMpvAudio(errorText);
            return;
        }
        if (isAudioType(m_info) && !m_usingMpvPlayback && isCodecMissingMediaError(errorText)) {
            updateStatusLabel(mpvInstallHint(errorText));
            updateMediaUi();
            updatePlaybackButton();
            updateVolumeButton();
            updateVolumePopup();
            updateCenterOverlay();
            return;
        }
        if (isAudioType(m_info) && !m_usingMpvPlayback) {
            updateStatusLabel(windowsBackendFailureMessage(errorText));
            return;
        }
        updateStatusLabel(errorText);
    });
}

void MediaRenderer::destroyAudioBackend()
{
    m_videoPollTimer->stop();
    m_usingMpvPlayback = false;
    m_usingWindowsAudioFallback = false;
    if (m_player) {
        m_player->disconnect(this);
        m_player->stop();
        m_player->setVideoOutput(static_cast<QVideoWidget*>(nullptr));
        m_player->setMedia(QUrl());
        delete m_player;
        m_player = nullptr;
    }
    if (m_windowsAudioBackend) {
        m_windowsAudioBackend->shutdown();
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
    m_usingWindowsAudioFallback = false;
    m_mpvPaused = true;
    m_videoPreviewReady = false;
    m_videoStartedPlayback = false;
    if (m_mpvBackend) {
        m_mpvBackend->shutdown();
    }
    m_videoHost->hide();
    m_videoPlaceholder->hide();
    m_centerOverlayLabel->hide();
}

bool MediaRenderer::tryStartMpvVideo()
{
    if (!isVideoType(m_info)) {
        return false;
    }

    ensureVideoBackend();
    m_usingMpvPlayback = true;
    m_usingMpvVideo = true;

    if (!m_mpvBackend || !m_mpvBackend->isLoaded()) {
        return false;
    }

    QString errorMessage;
    if (!m_mpvBackend->loadFile(m_info.filePath, &errorMessage)) {
        qDebug().noquote() << QStringLiteral("[SpaceLookRender] libmpv video load failed path=\"%1\" message=\"%2\"")
            .arg(m_info.filePath, errorMessage);
        return false;
    }

    m_videoMuted = false;
    m_videoVolume = kDefaultMediaVolume;
    m_mpvBackend->setMuted(false);
    m_mpvBackend->setVolume(static_cast<double>(m_videoVolume) * kMpvVolumeGain);
    m_mpvBackend->setPaused(true);
    m_mpvPaused = true;
    updateStatusLabel(QString());
    m_videoPollTimer->start();
    updateMediaUi();
    updatePlaybackButton();
    updateVolumeButton();
    updateVolumePopup();
    updateCenterOverlay();
    return true;
}

bool MediaRenderer::tryStartMpvAudio()
{
    if (!isAudioType(m_info)) {
        return false;
    }

    ensureVideoBackend();
    m_usingMpvPlayback = true;
    m_usingMpvVideo = false;

    if (!m_mpvBackend || !m_mpvBackend->isLoaded()) {
        return false;
    }

    QString errorMessage;
    if (!m_mpvBackend->loadMedia(m_info.filePath, &errorMessage, true)) {
        qDebug().noquote() << QStringLiteral("[SpaceLookRender] libmpv audio load failed path=\"%1\" message=\"%2\"")
            .arg(m_info.filePath, errorMessage);
        return false;
    }

    m_videoMuted = false;
    m_videoVolume = kDefaultMediaVolume;
    m_mpvBackend->setMuted(false);
    m_mpvBackend->setVolume(static_cast<double>(m_videoVolume) * kMpvVolumeGain);
    m_mpvPaused = true;
    m_videoPollTimer->start();
    updateStatusLabel(QString());
    updateMediaUi();
    updatePlaybackButton();
    updateVolumeButton();
    updateVolumePopup();
    updateCenterOverlay();
    return true;
}

bool MediaRenderer::tryStartWindowsAudioFallback(const QString& reason)
{
    if (!isAudioType(m_info) || !isWindowsPlayerFallbackAudioFile(m_info) || !m_windowsAudioBackend) {
        return false;
    }

    if (m_player) {
        m_player->stop();
        m_player->setMedia(QUrl());
    }

    QString errorMessage;
    if (!m_windowsAudioBackend->load(m_info.filePath, &errorMessage)) {
        qDebug().noquote() << QStringLiteral("[SpaceLookRender] Windows Media Player audio fallback failed path=\"%1\" reason=\"%2\" message=\"%3\"")
            .arg(m_info.filePath, reason, errorMessage);
        return false;
    }

    m_usingMpvPlayback = false;
    m_usingMpvVideo = false;
    m_usingWindowsAudioFallback = true;
    m_mpvPaused = true;
    m_videoPreviewReady = true;
    m_videoStartedPlayback = false;
    m_audioVolume = kDefaultMediaVolume;
    m_windowsAudioBackend->setMuted(false);
    m_windowsAudioBackend->setVolume(m_audioVolume);
    m_videoPollTimer->start();
    updateStatusLabel(QString());
    updateMediaUi();
    updatePlaybackButton();
    updateVolumeButton();
    updateVolumePopup();
    updateCenterOverlay();
    return true;
}

QString MediaRenderer::mpvInstallHint(const QString& reason) const
{
    const QString cleanReason = reason.trimmed().isEmpty()
        ? QCoreApplication::translate("SpaceLook", "Windows media backend could not parse this media file.")
        : reason.trimmed();
    QString codecSummary = m_currentCodecSummary.trimmed();
    if (codecSummary.isEmpty() && !m_info.filePath.trimmed().isEmpty()) {
        codecSummary = friendlyCodecSummaryForMetadata(mediaCodecMetadataForFile(m_info.filePath));
    }
    if (codecSummary.isEmpty()) {
        return QCoreApplication::translate("SpaceLook", "%1\nInstall the optional mpv enhanced playback runtime to preview this media format.")
            .arg(cleanReason);
    }
    return QCoreApplication::translate("SpaceLook", "%1\n%2\nInstall the optional mpv enhanced playback runtime to preview this media format.")
        .arg(cleanReason, codecSummary);
}

QString MediaRenderer::windowsBackendFailureMessage(const QString& reason) const
{
    const QString cleanReason = reason.trimmed().isEmpty()
        ? QCoreApplication::translate("SpaceLook", "Windows media backend could not parse this media file.")
        : reason.trimmed();
    QString codecSummary = m_currentCodecSummary.trimmed();
    if (codecSummary.isEmpty() && !m_info.filePath.trimmed().isEmpty()) {
        codecSummary = friendlyCodecSummaryForMetadata(mediaCodecMetadataForFile(m_info.filePath));
    }
    if (codecSummary.isEmpty()) {
        return QCoreApplication::translate("SpaceLook", "%1\nThis codec is expected to use the Windows media backend, so mpv is not required for this file. Check Qt multimedia plugin deployment and Windows media components.")
            .arg(cleanReason);
    }
    return QCoreApplication::translate("SpaceLook", "%1\n%2\nThis codec is expected to use the Windows media backend, so mpv is not required for this file. Check Qt multimedia plugin deployment and Windows media components.")
        .arg(cleanReason, codecSummary);
}

bool MediaRenderer::isMpvInstallHintMessage(const QString& message) const
{
    return message.contains(QStringLiteral("mpv enhanced playback runtime"), Qt::CaseInsensitive) ||
        isCodecMissingMediaError(message);
}

void MediaRenderer::fallbackToMpvVideo(const QString& reason)
{
    if (!isVideoType(m_info) || m_triedMpvVideoFallback) {
        updateStatusLabel(mpvInstallHint(reason));
        updateMediaUi();
        updatePlaybackButton();
        updateVolumeButton();
        updateVolumePopup();
        updateCenterOverlay();
        return;
    }

    m_triedMpvVideoFallback = true;
    if (m_player) {
        m_player->stop();
        m_player->setMedia(QUrl());
    }
    if (m_windowsAudioBackend) {
        m_windowsAudioBackend->shutdown();
    }
    m_usingWindowsAudioFallback = false;

    ensureVideoBackend();
    m_usingMpvPlayback = true;
    m_usingMpvVideo = true;

    if (!m_mpvBackend || !m_mpvBackend->isLoaded()) {
        updateStatusLabel(mpvInstallHint(reason));
        updateMediaUi();
        updatePlaybackButton();
        updateVolumeButton();
        updateVolumePopup();
        updateCenterOverlay();
        return;
    }

    QString errorMessage;
    if (!m_mpvBackend->loadFile(m_info.filePath, &errorMessage)) {
        updateStatusLabel(mpvInstallHint(errorMessage));
        updateMediaUi();
        updatePlaybackButton();
        updateVolumeButton();
        updateVolumePopup();
        updateCenterOverlay();
        return;
    }

    m_videoMuted = false;
    m_videoVolume = kDefaultMediaVolume;
    m_mpvBackend->setMuted(false);
    m_mpvBackend->setVolume(static_cast<double>(m_videoVolume) * kMpvVolumeGain);
    m_mpvBackend->setPaused(true);
    m_mpvPaused = true;
    updateStatusLabel(QString());
    m_videoPollTimer->start();
    updateMediaUi();
    updatePlaybackButton();
    updateVolumeButton();
    updateVolumePopup();
    updateCenterOverlay();
}

void MediaRenderer::fallbackToMpvAudio(const QString& reason)
{
    if (!isAudioType(m_info) || m_triedMpvAudioFallback) {
        updateStatusLabel(mpvInstallHint(reason));
        updateMediaUi();
        updatePlaybackButton();
        updateVolumeButton();
        updateVolumePopup();
        updateCenterOverlay();
        return;
    }

    m_triedMpvAudioFallback = true;
    if (m_player) {
        m_player->stop();
        m_player->setMedia(QUrl());
    }

    ensureVideoBackend();
    m_usingMpvPlayback = true;
    m_usingMpvVideo = false;

    if (!m_mpvBackend || !m_mpvBackend->isLoaded()) {
        updateStatusLabel(mpvInstallHint(reason));
        updateMediaUi();
        updatePlaybackButton();
        updateVolumeButton();
        updateVolumePopup();
        updateCenterOverlay();
        return;
    }

    QString errorMessage;
    if (!m_mpvBackend->loadMedia(m_info.filePath, &errorMessage, true)) {
        updateStatusLabel(mpvInstallHint(errorMessage));
        updateMediaUi();
        updatePlaybackButton();
        updateVolumeButton();
        updateVolumePopup();
        updateCenterOverlay();
        return;
    }

    m_videoMuted = false;
    m_videoVolume = kDefaultMediaVolume;
    m_mpvBackend->setMuted(false);
    m_mpvBackend->setVolume(static_cast<double>(m_videoVolume) * kMpvVolumeGain);
    m_mpvPaused = true;
    m_videoPollTimer->start();
    updateStatusLabel(QString());
    updateMediaUi();
    updatePlaybackButton();
    updateVolumeButton();
    updateVolumePopup();
    updateCenterOverlay();
}

void MediaRenderer::seekToSliderValue(int value)
{
    if (m_usingWindowsAudioFallback) {
        if (m_windowsAudioBackend && m_windowsAudioBackend->isLoaded()) {
            m_windowsAudioBackend->seekSeconds(static_cast<double>(value) / 1000.0);
        }
        return;
    }

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
    if (m_usingWindowsAudioFallback) {
        if (!m_windowsAudioBackend || !m_windowsAudioBackend->isLoaded()) {
            return;
        }

        bool paused = m_mpvPaused;
        if (m_windowsAudioBackend->paused(&paused)) {
            if (m_windowsAudioBackend->setPaused(!paused)) {
                m_mpvPaused = !paused;
                if (!m_mpvPaused) {
                    m_videoStartedPlayback = true;
                }
                updatePlaybackButton();
                updateCenterOverlay();
            }
        }
        return;
    }

    if (m_usingMpvPlayback) {
        if (!m_mpvBackend || !m_mpvBackend->isLoaded()) {
            return;
        }

        bool paused = m_mpvPaused;
        if (m_mpvBackend->paused(&paused)) {
            if (m_mpvBackend->setPaused(!paused)) {
                m_mpvPaused = !paused;
                if (!m_mpvPaused) {
                    m_videoStartedPlayback = true;
                    updateMediaUi();
                }
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
        m_videoStartedPlayback = true;
        m_player->play();
    }
    updateCenterOverlay();
}

void MediaRenderer::toggleVolumePopup()
{
    setVolumeLevel(isMutedState() ? kDefaultMediaVolume : 0);
}

void MediaRenderer::setVolumeLevel(int value)
{
    const int clampedValue = qBound(0, value, kMaxMediaVolume);

    if (m_usingMpvPlayback) {
        m_videoVolume = clampedValue;
        m_videoMuted = clampedValue == 0;
        if (m_mpvBackend && m_mpvBackend->isLoaded()) {
            m_mpvBackend->setMuted(m_videoMuted);
            m_mpvBackend->setVolume(static_cast<double>(clampedValue) * kMpvVolumeGain);
        }
        updateVolumeButton();
        updateVolumePopup();
        return;
    }

    if (m_usingWindowsAudioFallback) {
        m_audioVolume = clampedValue;
        if (m_windowsAudioBackend && m_windowsAudioBackend->isLoaded()) {
            m_windowsAudioBackend->setMuted(clampedValue == 0);
            m_windowsAudioBackend->setVolume(clampedValue);
        }
        updateVolumeButton();
        updateVolumePopup();
        return;
    }

    m_audioVolume = clampedValue;
    if (m_player) {
        m_player->setMuted(clampedValue == 0);
        m_player->setVolume(qMin(clampedValue, kMaxQtPlayerVolume));
    }
    updateVolumeButton();
    updateVolumePopup();
}

int MediaRenderer::currentVolumeLevel() const
{
    if (m_usingMpvPlayback) {
        return m_videoMuted ? 0 : m_videoVolume;
    }

    if (m_usingWindowsAudioFallback) {
        bool muted = false;
        if (m_windowsAudioBackend && m_windowsAudioBackend->muted(&muted) && muted) {
            return 0;
        }
        return m_windowsAudioBackend ? m_windowsAudioBackend->volume() : m_audioVolume;
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
    if (m_usingWindowsAudioFallback) {
        m_playPauseButton->setText(m_mpvPaused ? playGlyph() : pauseGlyph());
        return;
    }

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
    m_volumeSlider->setVisible(true);
    const bool sliderWasBlocked = m_volumeSlider->blockSignals(true);
    m_volumeSlider->setValue(volume);
    m_volumeSlider->blockSignals(sliderWasBlocked);
    m_volumeValueLabel->setText(QString::number(volume));
}

void MediaRenderer::updateMediaUi()
{
    const bool video = isVideoType(m_info);
    const bool showVideoPlaceholder = video && shouldShowVideoPlaceholder();
    m_videoHost->setVisible(video && m_usingMpvPlayback && !showVideoPlaceholder);
    m_qtVideoWidget->setVisible(video && !m_usingMpvPlayback && !showVideoPlaceholder);
    m_videoPlaceholder->setVisible(showVideoPlaceholder);
    m_audioPlaceholder->setVisible(!video);

    if (!video) {
        m_audioPlaceholder->setPixmap(QPixmap());
        m_audioPlaceholder->setText(displayTitleForMedia(m_info));
    } else {
        m_videoPlaceholder->setPixmap(QPixmap());
        m_videoPlaceholder->setText(QString());
    }

    syncVideoViewportGeometry();
    updateCenterOverlay();
}

void MediaRenderer::syncVideoViewportGeometry()
{
    if (!m_mediaViewport || !m_videoHost || !m_qtVideoWidget || !m_videoPlaceholder) {
        return;
    }

    const QRect viewportRect = m_mediaViewport->contentsRect();
    if (!viewportRect.isValid()) {
        return;
    }

    if (m_videoHost->geometry() != viewportRect) {
        m_videoHost->setGeometry(viewportRect);
    }
    if (m_qtVideoWidget->geometry() != viewportRect) {
        m_qtVideoWidget->setGeometry(viewportRect);
    }
    if (m_videoPlaceholder->geometry() != viewportRect) {
        m_videoPlaceholder->setGeometry(viewportRect);
    }
    if (!isVideoType(m_info)) {
        m_audioPlaceholder->setGeometry(viewportRect);
    }

    m_videoHost->updateGeometry();
    m_videoHost->update();
    m_qtVideoWidget->updateGeometry();
    m_qtVideoWidget->update();
    m_videoPlaceholder->updateGeometry();
    m_videoPlaceholder->update();
    m_audioPlaceholder->updateGeometry();
    m_audioPlaceholder->update();
}

void MediaRenderer::updateStatusLabel(const QString& message)
{
    if (message.trimmed().isEmpty()) {
        PreviewStateVisuals::clearStatus(m_statusLabel);
        if (m_mpvHelpButton) {
            m_mpvHelpButton->hide();
        }
        if (m_statusRow) {
            m_statusRow->hide();
        }
        return;
    }

    QString displayMessage = message;
    if (isCodecMissingMediaError(displayMessage) &&
        !displayMessage.contains(QStringLiteral("Codec:"), Qt::CaseInsensitive)) {
        displayMessage = mpvInstallHint(displayMessage);
    }

    if (m_statusRow) {
        m_statusRow->show();
    }
    if (m_mpvHelpButton) {
        m_mpvHelpButton->setVisible(isMpvInstallHintMessage(displayMessage));
    }
    qDebug().noquote() << QStringLiteral("[SpaceLookRender] Media status message=\"%1\"").arg(displayMessage);
    PreviewStateVisuals::showStatus(m_statusLabel, displayMessage);
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

bool MediaRenderer::isCurrentMediaLoad() const
{
    return m_loadGuard.isCurrent(m_loadToken, m_info.filePath);
}

bool MediaRenderer::isMutedState() const
{
    return currentVolumeLevel() <= 0;
}

bool MediaRenderer::isPlaybackPaused() const
{
    if (m_usingWindowsAudioFallback) {
        return m_mpvPaused;
    }

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
        return !m_videoPreviewReady || !m_videoStartedPlayback;
    }

    return !m_videoPreviewReady || !m_videoStartedPlayback;
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
