#include "renderers/image/ImageRenderer.h"

#include <QDebug>
#include <QEvent>
#include <QFrame>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QImageReader>
#include <QLabel>
#include <QMouseEvent>
#include <QMovie>
#include <QPixmap>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <functional>
#include <memory>
#include <vector>
#include <QtConcurrent/QtConcurrent>
#include <QtWinExtras/QtWin>

#include <Windows.h>
#include <objbase.h>
#include <shobjidl.h>

#ifdef SPACELOOK_ENABLE_LIBHEIF
#include <libheif/heif.h>
#endif

#include "renderers/FileTypeIconResolver.h"
#include "renderers/OpenWithButton.h"
#include "renderers/PreviewHeaderBar.h"
#include "renderers/SelectableTitleLabel.h"
#include "widgets/SpaceLookWindow.h"

namespace {

constexpr double kMinZoomFactor = 0.25;
constexpr double kMaxZoomFactor = 8.0;
constexpr double kZoomStepIn = 1.2;
constexpr double kZoomStepOut = 1.0 / 1.2;
constexpr int kShellThumbnailEdgeLength = 2048;
constexpr int kPreviewThumbnailEdgeLength = 768;

bool isAnimatedImageFile(const QString& filePath)
{
    const QString suffix = QFileInfo(filePath).suffix().trimmed().toLower();
    if (suffix == QStringLiteral("gif")) {
        return true;
    }

    if (suffix != QStringLiteral("webp")) {
        return false;
    }

    QImageReader reader(filePath);
    if (!reader.canRead()) {
        return false;
    }

    const int imageCount = reader.imageCount();
    if (imageCount > 1) {
        return true;
    }
    if (imageCount == 1) {
        return false;
    }

    if (!reader.supportsAnimation()) {
        return false;
    }

    const QImage firstFrame = reader.read();
    if (firstFrame.isNull()) {
        return false;
    }

    return reader.jumpToNextImage();
}

struct ImageLoadResult
{
    QImage image;
    QString statusMessage;
    bool success = false;
    bool finalImage = false;
};

#ifdef SPACELOOK_ENABLE_LIBHEIF
QString heifErrorSummary(const heif_error& error)
{
    return QStringLiteral("code=%1 subcode=%2 message=\"%3\"")
        .arg(error.code)
        .arg(error.subcode)
        .arg(QString::fromUtf8(error.message ? error.message : ""));
}

QString heifDecodeTargetSummary(enum heif_colorspace colorspace, enum heif_chroma chroma)
{
    return QStringLiteral("colorspace=%1 chroma=%2")
        .arg(static_cast<int>(colorspace))
        .arg(static_cast<int>(chroma));
}

void ensureLibheifInitialized()
{
    static const bool initialized = []() {
        const heif_error initError = heif_init(nullptr);
        if (initError.code != heif_error_Ok) {
            qDebug().noquote() << QStringLiteral("[SpaceLookRender] libheif init failed %1")
                .arg(heifErrorSummary(initError));
            return false;
        }

        qDebug().noquote() << QStringLiteral("[SpaceLookRender] libheif initialized version=\"%1\" hevcDecoder=%2")
            .arg(QString::fromUtf8(heif_get_version()))
            .arg(heif_have_decoder_for_format(heif_compression_HEVC));
        return true;
    }();

    Q_UNUSED(initialized);
}
#endif

ImageLoadResult loadImagePreviewContent(const QString& filePath, const std::function<QImage(const QString&)>& loader)
{
    ImageLoadResult result;
    result.image = loader(filePath);
    result.success = !result.image.isNull();
    if (!result.success) {
        result.statusMessage = QStringLiteral("Failed to load the image.");
    }
    return result;
}

ImageLoadResult loadThumbnailPreviewContent(const QString& filePath, const std::function<QImage(const QString&)>& loader)
{
    ImageLoadResult result;
    result.image = loader(filePath);
    result.success = !result.image.isNull();
    result.finalImage = false;
    if (!result.success) {
        result.statusMessage = QStringLiteral("Thumbnail preview is unavailable.");
    }
    return result;
}

QImage qImageFromHBitmap(HBITMAP bitmapHandle)
{
    if (!bitmapHandle) {
        return QImage();
    }

    BITMAP bitmapInfo = {};
    if (GetObjectW(bitmapHandle, sizeof(bitmapInfo), &bitmapInfo) == 0 ||
        bitmapInfo.bmWidth <= 0 ||
        bitmapInfo.bmHeight == 0) {
        return QImage();
    }

    BITMAPINFO dibInfo = {};
    dibInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    dibInfo.bmiHeader.biWidth = bitmapInfo.bmWidth;
    dibInfo.bmiHeader.biHeight = -std::abs(bitmapInfo.bmHeight);
    dibInfo.bmiHeader.biPlanes = 1;
    dibInfo.bmiHeader.biBitCount = 32;
    dibInfo.bmiHeader.biCompression = BI_RGB;

    std::vector<uchar> pixels(static_cast<size_t>(bitmapInfo.bmWidth) * static_cast<size_t>(std::abs(bitmapInfo.bmHeight)) * 4u);
    HDC screenDc = GetDC(nullptr);
    if (!screenDc) {
        return QImage();
    }

    const int scanLinesCopied = GetDIBits(
        screenDc,
        bitmapHandle,
        0,
        static_cast<UINT>(std::abs(bitmapInfo.bmHeight)),
        pixels.data(),
        &dibInfo,
        DIB_RGB_COLORS);
    ReleaseDC(nullptr, screenDc);

    if (scanLinesCopied <= 0) {
        return QImage();
    }

    QImage image(bitmapInfo.bmWidth, std::abs(bitmapInfo.bmHeight), QImage::Format_ARGB32_Premultiplied);
    if (image.isNull()) {
        return QImage();
    }

    const int bytesPerLine = bitmapInfo.bmWidth * 4;
    for (int y = 0; y < image.height(); ++y) {
        memcpy(image.scanLine(y), pixels.data() + (static_cast<size_t>(y) * static_cast<size_t>(bytesPerLine)), static_cast<size_t>(bytesPerLine));
    }

    return image;
}

}

ImageRenderer::ImageRenderer(QWidget* parent)
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
    , m_imageLabel(new QLabel(this))
    , m_scrollArea(new QScrollArea(this))
{
    setAttribute(Qt::WA_StyledBackground, true);
    setObjectName(QStringLiteral("ImageRendererRoot"));
    m_headerRow->setObjectName(QStringLiteral("ImageHeaderRow"));
    m_iconLabel->setObjectName(QStringLiteral("ImageTypeIcon"));
    m_titleLabel->setObjectName(QStringLiteral("ImageTitle"));
    m_metaLabel->setObjectName(QStringLiteral("ImageMeta"));
    m_pathRow->setObjectName(QStringLiteral("ImagePathRow"));
    m_pathTitleLabel->setObjectName(QStringLiteral("ImagePathTitle"));
    m_pathValueLabel->setObjectName(QStringLiteral("ImagePathValue"));
    m_openWithButton->setObjectName(QStringLiteral("ImageOpenWithButton"));
    m_statusLabel->setObjectName(QStringLiteral("ImageStatus"));
    m_scrollArea->setObjectName(QStringLiteral("ImageStage"));
    m_imageLabel->setObjectName(QStringLiteral("ImageCanvas"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 0, 12, 12);
    layout->setSpacing(14);
    layout->addWidget(m_headerRow);
    layout->addWidget(m_statusLabel);
    layout->addWidget(m_scrollArea, 1);

    auto* headerLayout = new QHBoxLayout(m_headerRow);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(12);
    auto* titleBlock = new PreviewHeaderBar(m_iconLabel, m_titleLabel, m_pathRow, m_openWithButton, m_headerRow);
    headerLayout->addWidget(titleBlock->contentWidget(), 1);

    auto* pathLayout = new QHBoxLayout(m_pathRow);
    pathLayout->setContentsMargins(0, 0, 0, 0);
    pathLayout->setSpacing(8);
    pathLayout->addWidget(m_pathValueLabel, 1);

    m_imageLabel->setAlignment(Qt::AlignCenter);
    m_scrollArea->setWidget(m_imageLabel);
    m_scrollArea->setWidgetResizable(false);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->viewport()->setAutoFillBackground(false);
    m_scrollArea->setAlignment(Qt::AlignCenter);
    m_scrollArea->viewport()->installEventFilter(this);
    m_imageLabel->installEventFilter(this);
    m_iconLabel->setFixedSize(72, 72);
    m_iconLabel->setScaledContents(true);
    m_titleLabel->setWordWrap(true);
    m_pathTitleLabel->hide();
    m_pathValueLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_pathValueLabel->setWordWrap(true);
    m_pathValueLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_pathRow->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_openWithButton->setStatusCallback([this](const QString& message) {
        showStatusMessage(message);
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
    m_statusLabel->hide();
    updateDragCursor();

    connect(m_titleLabel, &SelectableTitleLabel::copyFeedbackRequested, this, [this](const QString& message) {
        showStatusMessage(message);
    });

    applyChrome();
}

QString ImageRenderer::rendererId() const
{
    return QStringLiteral("image");
}

bool ImageRenderer::canHandle(const HoveredItemInfo& info) const
{
    return info.typeKey == QStringLiteral("image");
}

QWidget* ImageRenderer::widget()
{
    return this;
}

void ImageRenderer::load(const HoveredItemInfo& info)
{
    m_info = info;
    ++m_loadRequestId;
    const quint64 requestId = m_loadRequestId;
    qDebug().noquote() << QStringLiteral("[SpaceLookRender] ImageRenderer load path=\"%1\"").arg(info.filePath);
    m_titleLabel->setText(info.title.isEmpty() ? QStringLiteral("Image Preview") : info.title);
    m_titleLabel->setCopyText(m_titleLabel->text());
    const QIcon typeIcon(FileTypeIconResolver::iconForInfo(info));
    m_iconLabel->setPixmap(typeIcon.pixmap(128, 128));
    m_pathValueLabel->setText(info.filePath.trimmed().isEmpty() ? QStringLiteral("(Unavailable)") : info.filePath);
    m_openWithButton->setTargetContext(info.filePath, info.typeKey);
    clearAnimatedImage();
    m_originalPixmap = QPixmap();
    m_hasHighResolutionImage = false;
    m_zoomFactor = 1.0;
    m_isDragging = false;
    m_movieFrameSize = QSize();
    m_imageLabel->setText(QStringLiteral("Loading image preview..."));
    m_statusLabel->setText(QStringLiteral("Loading image preview..."));
    m_statusLabel->show();
    updateDragCursor();

    if (isAnimatedImageFile(info.filePath) && tryLoadAnimatedImage(info.filePath)) {
        return;
    }

    auto* thumbnailWatcher = new QFutureWatcher<ImageLoadResult>(this);
    connect(thumbnailWatcher, &QFutureWatcher<ImageLoadResult>::finished, this, [this, thumbnailWatcher, requestId, filePath = info.filePath]() {
        const ImageLoadResult result = thumbnailWatcher->result();
        thumbnailWatcher->deleteLater();

        if (requestId != m_loadRequestId || m_info.filePath != filePath) {
            qDebug().noquote() << QStringLiteral("[SpaceLookRender] ImageRenderer discarded stale thumbnail result path=\"%1\"")
                .arg(filePath);
            return;
        }

        if (!result.success || result.image.isNull() || m_hasHighResolutionImage) {
            return;
        }

        m_originalPixmap = QPixmap::fromImage(result.image);
        if (m_originalPixmap.isNull()) {
            return;
        }

        m_statusLabel->setText(QStringLiteral("Thumbnail ready. Loading full image..."));
        m_statusLabel->show();
        updatePixmapView();
        qDebug().noquote() << QStringLiteral("[SpaceLookRender] ImageRenderer thumbnail loaded size=%1x%2 path=\"%3\"")
            .arg(m_originalPixmap.width())
            .arg(m_originalPixmap.height())
            .arg(filePath);
    });
    thumbnailWatcher->setFuture(QtConcurrent::run([this, filePath = info.filePath]() {
        return loadThumbnailPreviewContent(filePath, [this](const QString& path) {
            return loadThumbnailImageForPath(path);
        });
    }));

    auto* watcher = new QFutureWatcher<ImageLoadResult>(this);
    connect(watcher, &QFutureWatcher<ImageLoadResult>::finished, this, [this, watcher, requestId, filePath = info.filePath]() {
        const ImageLoadResult result = watcher->result();
        watcher->deleteLater();

        if (requestId != m_loadRequestId || m_info.filePath != filePath) {
            qDebug().noquote() << QStringLiteral("[SpaceLookRender] ImageRenderer discarded stale async result path=\"%1\"")
                .arg(filePath);
            return;
        }

        if (!result.success) {
            qDebug().noquote() << QStringLiteral("[SpaceLookRender] ImageRenderer failed to load: %1").arg(filePath);
            if (m_originalPixmap.isNull()) {
                m_statusLabel->setText(result.statusMessage);
                m_statusLabel->show();
                m_imageLabel->setText(QStringLiteral("Image preview is unavailable."));
            } else {
                m_statusLabel->setText(result.statusMessage);
                m_statusLabel->show();
            }
            return;
        }

        m_originalPixmap = QPixmap::fromImage(result.image);
        if (m_originalPixmap.isNull()) {
            if (m_originalPixmap.isNull()) {
                m_statusLabel->setText(QStringLiteral("Failed to load the image."));
                m_statusLabel->show();
                m_imageLabel->setText(QStringLiteral("Image preview is unavailable."));
            }
            return;
        }

        m_hasHighResolutionImage = true;
        m_statusLabel->clear();
        m_statusLabel->hide();
        updatePixmapView();
        qDebug().noquote() << QStringLiteral("[SpaceLookRender] ImageRenderer full image loaded size=%1x%2 path=\"%3\"")
            .arg(m_originalPixmap.width())
            .arg(m_originalPixmap.height())
            .arg(filePath);
    });
    watcher->setFuture(QtConcurrent::run([this, filePath = info.filePath]() {
        ImageLoadResult result = loadImagePreviewContent(filePath, [this](const QString& path) {
            return loadImageForPath(path);
        });
        result.finalImage = true;
        return result;
    }));
}

QImage ImageRenderer::loadImageForPath(const QString& filePath) const
{
    const QString suffix = QFileInfo(filePath).suffix().trimmed().toLower();
    if (suffix == QStringLiteral("heic") || suffix == QStringLiteral("heif")) {
        const QImage heifImage = loadHeifImageForPath(filePath);
        if (!heifImage.isNull()) {
            return heifImage;
        }
    }

    const QImage directImage(filePath);
    if (!directImage.isNull()) {
        return directImage;
    }

    qDebug().noquote() << QStringLiteral("[SpaceLookRender] Direct image decode failed, trying shell thumbnail: %1")
        .arg(filePath);
    return loadShellThumbnailForPath(filePath, kShellThumbnailEdgeLength);
}

QImage ImageRenderer::loadThumbnailImageForPath(const QString& filePath) const
{
    return loadShellThumbnailForPath(filePath, kPreviewThumbnailEdgeLength);
}

QImage ImageRenderer::loadHeifImageForPath(const QString& filePath) const
{
#ifndef SPACELOOK_ENABLE_LIBHEIF
    qDebug().noquote() << QStringLiteral("[SpaceLookRender] libheif is disabled in this build. HEIC full decode unavailable: %1")
        .arg(filePath);
    return QImage();
#else
    const QString trimmedPath = filePath.trimmed();
    if (trimmedPath.isEmpty()) {
        return QImage();
    }

    ensureLibheifInitialized();

    QFile sourceFile(trimmedPath);
    if (!sourceFile.open(QIODevice::ReadOnly)) {
        qDebug().noquote() << QStringLiteral("[SpaceLookRender] HEIF source open failed for: %1 error=\"%2\"")
            .arg(trimmedPath, sourceFile.errorString());
        return QImage();
    }

    const QByteArray fileBytes = sourceFile.readAll();
    if (fileBytes.isEmpty()) {
        qDebug().noquote() << QStringLiteral("[SpaceLookRender] HEIF source read returned no bytes for: %1")
            .arg(trimmedPath);
        return QImage();
    }

    const char* mimeType = heif_get_file_mime_type(
        reinterpret_cast<const uint8_t*>(fileBytes.constData()),
        static_cast<int>(fileBytes.size()));
    qDebug().noquote() << QStringLiteral("[SpaceLookRender] HEIF source bytes=%1 mime=\"%2\" path=\"%3\"")
        .arg(fileBytes.size())
        .arg(QString::fromUtf8(mimeType ? mimeType : ""))
        .arg(trimmedPath);

    struct heif_context* context = heif_context_alloc();
    if (!context) {
        qDebug().noquote() << QStringLiteral("[SpaceLookRender] libheif context allocation failed for: %1")
            .arg(trimmedPath);
        return QImage();
    }

    const heif_error readError = heif_context_read_from_memory_without_copy(
        context,
        fileBytes.constData(),
        static_cast<size_t>(fileBytes.size()),
        nullptr);
    if (readError.code != heif_error_Ok) {
        qDebug().noquote() << QStringLiteral("[SpaceLookRender] libheif memory read failed for: %1 %2")
            .arg(trimmedPath, heifErrorSummary(readError));
        heif_context_free(context);
        return QImage();
    }

    heif_image_handle* imageHandle = nullptr;
    const heif_error handleError = heif_context_get_primary_image_handle(context, &imageHandle);
    if (handleError.code != heif_error_Ok || !imageHandle) {
        qDebug().noquote() << QStringLiteral("[SpaceLookRender] libheif primary handle failed for: %1 %2")
            .arg(trimmedPath, heifErrorSummary(handleError));
        heif_context_free(context);
        return QImage();
    }

    enum heif_colorspace preferredColorspace = heif_colorspace_undefined;
    enum heif_chroma preferredChroma = heif_chroma_undefined;
    const heif_error preferredError = heif_image_handle_get_preferred_decoding_colorspace(
        imageHandle,
        &preferredColorspace,
        &preferredChroma);
    if (preferredError.code == heif_error_Ok) {
        qDebug().noquote() << QStringLiteral("[SpaceLookRender] libheif preferred decode target for %1: %2")
            .arg(trimmedPath, heifDecodeTargetSummary(preferredColorspace, preferredChroma));
    } else {
        qDebug().noquote() << QStringLiteral("[SpaceLookRender] libheif preferred decode target lookup failed for %1 %2")
            .arg(trimmedPath, heifErrorSummary(preferredError));
    }

    heif_decoding_options* decodingOptions = heif_decoding_options_alloc();
    if (decodingOptions) {
        decodingOptions->convert_hdr_to_8bit = 1;
        decodingOptions->strict_decoding = 0;
    }

    const std::initializer_list<std::pair<enum heif_colorspace, enum heif_chroma>> decodeTargets = {
        { heif_colorspace_RGB, heif_chroma_interleaved_RGBA },
        { heif_colorspace_RGB, heif_chroma_interleaved_RGB },
        { preferredColorspace, preferredChroma }
    };

    QImage decodedImage;
    for (const auto& target : decodeTargets) {
        if (target.first == heif_colorspace_undefined || target.second == heif_chroma_undefined) {
            continue;
        }

        heif_image* image = nullptr;
        const heif_error decodeError = heif_decode_image(
            imageHandle,
            &image,
            target.first,
            target.second,
            decodingOptions);
        if (decodeError.code != heif_error_Ok || !image) {
            qDebug().noquote() << QStringLiteral("[SpaceLookRender] libheif decode attempt failed for: %1 target={%2} %3")
                .arg(trimmedPath, heifDecodeTargetSummary(target.first, target.second), heifErrorSummary(decodeError));
            continue;
        }

        decodedImage = imageFromHeifImage(image);
        heif_image_release(image);
        if (!decodedImage.isNull()) {
            qDebug().noquote() << QStringLiteral("[SpaceLookRender] libheif decode succeeded for: %1 target={%2}")
                .arg(trimmedPath, heifDecodeTargetSummary(target.first, target.second));
            break;
        }

        qDebug().noquote() << QStringLiteral("[SpaceLookRender] libheif decode produced unsupported pixel layout for: %1 target={%2}")
            .arg(trimmedPath, heifDecodeTargetSummary(target.first, target.second));
    }

    if (decodingOptions) {
        heif_decoding_options_free(decodingOptions);
    }

    heif_image_handle_release(imageHandle);
    heif_context_free(context);

    return decodedImage;
#endif
}

QImage ImageRenderer::imageFromHeifImage(struct heif_image* image) const
{
#ifndef SPACELOOK_ENABLE_LIBHEIF
    Q_UNUSED(image);
    return QImage();
#else
    if (!image) {
        return QImage();
    }

    const heif_chroma chroma = heif_image_get_chroma_format(image);
    int stride = 0;
    const uint8_t* pixels = heif_image_get_plane_readonly(image, heif_channel_interleaved, &stride);
    const int width = heif_image_get_width(image, heif_channel_interleaved);
    const int height = heif_image_get_height(image, heif_channel_interleaved);
    if (!pixels || width <= 0 || height <= 0 || stride <= 0) {
        return QImage();
    }

    if (chroma == heif_chroma_interleaved_RGBA) {
        QImage decodedImage(width, height, QImage::Format_RGBA8888);
        if (decodedImage.isNull()) {
            return QImage();
        }

        for (int y = 0; y < height; ++y) {
            memcpy(decodedImage.scanLine(y), pixels + y * stride, static_cast<size_t>(width) * 4);
        }
        return decodedImage;
    }

    if (chroma == heif_chroma_interleaved_RGB) {
        QImage decodedImage(width, height, QImage::Format_RGB888);
        if (decodedImage.isNull()) {
            return QImage();
        }

        for (int y = 0; y < height; ++y) {
            memcpy(decodedImage.scanLine(y), pixels + y * stride, static_cast<size_t>(width) * 3);
        }
        return decodedImage;
    }

    qDebug().noquote() << QStringLiteral("[SpaceLookRender] libheif returned unsupported chroma layout: %1")
        .arg(static_cast<int>(chroma));
    return QImage();
#endif
}

QImage ImageRenderer::loadShellThumbnailForPath(const QString& filePath, int edgeLength) const
{
    const QString trimmedPath = filePath.trimmed();
    if (trimmedPath.isEmpty()) {
        return QImage();
    }

    IShellItemImageFactory* imageFactory = nullptr;
    const std::wstring nativePath = trimmedPath.toStdWString();
    const HRESULT createHr = SHCreateItemFromParsingName(
        nativePath.c_str(),
        nullptr,
        IID_PPV_ARGS(&imageFactory));
    if (FAILED(createHr) || !imageFactory) {
        qDebug().noquote() << QStringLiteral("[SpaceLookRender] Shell thumbnail factory creation failed for: %1")
            .arg(trimmedPath);
        return QImage();
    }

    HBITMAP bitmapHandle = nullptr;
    const SIZE requestedSize = { edgeLength, edgeLength };
    const HRESULT imageHr = imageFactory->GetImage(
        requestedSize,
        SIIGBF_BIGGERSIZEOK | SIIGBF_THUMBNAILONLY,
        &bitmapHandle);
    imageFactory->Release();
    if (FAILED(imageHr) || !bitmapHandle) {
        qDebug().noquote() << QStringLiteral("[SpaceLookRender] Shell thumbnail retrieval failed for: %1")
            .arg(trimmedPath);
        return QImage();
    }

    const QImage thumbnail = qImageFromHBitmap(bitmapHandle);
    DeleteObject(bitmapHandle);
    return thumbnail;
}

bool ImageRenderer::tryLoadAnimatedImage(const QString& filePath)
{
    std::unique_ptr<QMovie> movie = std::make_unique<QMovie>(filePath);
    if (!movie || !movie->isValid()) {
        qDebug().noquote() << QStringLiteral("[SpaceLookRender] Animated image load failed path=\"%1\"")
            .arg(filePath);
        return false;
    }

    movie->setCacheMode(QMovie::CacheAll);
    connect(movie.get(), &QMovie::frameChanged, this, [this]() {
        if (!m_movie) {
            return;
        }
        m_movieFrameSize = m_movie->currentPixmap().size();
        updateMovieView();
    });
    connect(movie.get(), &QMovie::resized, this, [this](const QSize& size) {
        m_movieFrameSize = size;
        updateMovieView();
    });
    connect(movie.get(), &QMovie::stateChanged, this, [this](QMovie::MovieState state) {
        if (state == QMovie::Running) {
            m_statusLabel->clear();
            m_statusLabel->hide();
        }
    });

    m_movie = movie.release();
    m_movieFrameSize = m_movie->currentPixmap().size();
    m_imageLabel->clear();
    m_imageLabel->setMovie(m_movie);
    m_hasHighResolutionImage = true;
    m_statusLabel->setText(QStringLiteral("Animated image ready"));
    m_statusLabel->show();
    m_movie->start();
    updateMovieView();

    qDebug().noquote() << QStringLiteral("[SpaceLookRender] Animated image playback started path=\"%1\" format=\"%2\"")
        .arg(filePath, QString::fromLatin1(m_movie->format()));
    return true;
}

void ImageRenderer::clearAnimatedImage()
{
    if (!m_movie) {
        return;
    }

    m_imageLabel->setMovie(nullptr);
    m_movie->stop();
    m_movie->deleteLater();
    m_movie = nullptr;
    m_movieFrameSize = QSize();
}

void ImageRenderer::unload()
{
    ++m_loadRequestId;
    clearAnimatedImage();
    m_originalPixmap = QPixmap();
    m_hasHighResolutionImage = false;
    m_zoomFactor = 1.0;
    m_isDragging = false;
    m_movieFrameSize = QSize();
    m_pathValueLabel->clear();
    m_openWithButton->setTargetContext(QString(), QString());
    m_imageLabel->clear();
    m_statusLabel->clear();
    m_statusLabel->hide();
    m_info = HoveredItemInfo();
    updateDragCursor();
}

void ImageRenderer::showStatusMessage(const QString& message)
{
    if (message.trimmed().isEmpty()) {
        m_statusLabel->clear();
        m_statusLabel->hide();
        return;
    }

    m_statusLabel->setText(message);
    m_statusLabel->show();
    QTimer::singleShot(1400, m_statusLabel, [label = m_statusLabel]() {
        if (label) {
            label->clear();
            label->hide();
        }
    });
}

bool ImageRenderer::eventFilter(QObject* watched, QEvent* event)
{
    if ((watched == m_scrollArea->viewport() || watched == m_imageLabel) && event) {
        if (event->type() == QEvent::Wheel) {
            auto* wheelEvent = static_cast<QWheelEvent*>(event);
            if (wheelEvent->modifiers().testFlag(Qt::ControlModifier) && !m_originalPixmap.isNull()) {
                const QPoint viewportPos = m_scrollArea->viewport()->mapFromGlobal(wheelEvent->globalPos());
                adjustZoom(wheelEvent->angleDelta().y() > 0 ? kZoomStepIn : kZoomStepOut, viewportPos);
                wheelEvent->accept();
                return true;
            }
        }

        if (event->type() == QEvent::MouseButtonPress) {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton && canPanImage()) {
                m_isDragging = true;
                m_lastDragGlobalPos = mouseEvent->globalPos();
                updateDragCursor();
                mouseEvent->accept();
                return true;
            }
        }

        if (event->type() == QEvent::MouseMove) {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (m_isDragging && canPanImage()) {
                const QPoint delta = mouseEvent->globalPos() - m_lastDragGlobalPos;
                m_lastDragGlobalPos = mouseEvent->globalPos();
                m_scrollArea->horizontalScrollBar()->setValue(m_scrollArea->horizontalScrollBar()->value() - delta.x());
                m_scrollArea->verticalScrollBar()->setValue(m_scrollArea->verticalScrollBar()->value() - delta.y());
                mouseEvent->accept();
                return true;
            }

            updateDragCursor();
        }

        if (event->type() == QEvent::MouseButtonRelease) {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton && m_isDragging) {
                m_isDragging = false;
                updateDragCursor();
                mouseEvent->accept();
                return true;
            }
        }

        if (event->type() == QEvent::MouseButtonDblClick) {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton && !m_originalPixmap.isNull()) {
                m_isDragging = false;
                resetZoom();
                mouseEvent->accept();
                return true;
            }
        }
    }

    return QWidget::eventFilter(watched, event);
}

void ImageRenderer::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (m_movie) {
        updateMovieView();
    } else {
        updatePixmapView();
    }
}

void ImageRenderer::applyChrome()
{
    setStyleSheet(
        "#ImageRendererRoot {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:1,"
        "      stop:0 #fcfdff,"
        "      stop:1 #f4f8fc);"
        "  border-radius: 0px;"
        "}"
        "QLabel {"
        "  color: #18324a;"
        "}"
        "#ImageTypeIcon {"
        "  background: transparent;"
        "  border: none;"
        "  padding: 0px;"
        "}"
        "#ImageTitle {"
        "  color: #0f2740;"
        "}"
        "#ImageMeta {"
        "  color: #5c738b;"
        "}"
        "#ImagePathTitle {"
        "  color: #16324a;"
        "  font-family: 'Segoe UI Semibold';"
        "}"
        "#ImagePathValue {"
        "  color: #445d76;"
        "}"
        "#ImageOpenWithButton QToolButton {"
        "  background: rgba(238, 244, 252, 0.96);"
        "  color: #17324b;"
        "  border: 1px solid rgba(193, 208, 224, 0.95);"
        "  padding: 3px 8px;"
        "  min-height: 24px;"
        "}"
        "#ImageOpenWithButton QToolButton:hover {"
        "  background: rgba(245, 249, 255, 1.0);"
        "}"
        "#ImageOpenWithButton QToolButton:pressed {"
        "  background: rgba(224, 234, 246, 1.0);"
        "}"
        "#ImageOpenWithButton #OpenWithPrimaryButton {"
        "  border-top-left-radius: 10px;"
        "  border-bottom-left-radius: 10px;"
        "  border-top-right-radius: 0px;"
        "  border-bottom-right-radius: 0px;"
        "  min-width: 28px;"
        "}"
        "#ImageOpenWithButton #OpenWithExpandButton {"
        "  border-left: none;"
        "  border-top-left-radius: 0px;"
        "  border-bottom-left-radius: 0px;"
        "  border-top-right-radius: 10px;"
        "  border-bottom-right-radius: 10px;"
        "  min-width: 22px;"
        "  padding-left: 5px;"
        "  padding-right: 5px;"
        "}"
        "#ImageStatus {"
        "  color: #6e4f12;"
        "  background: rgba(255, 239, 206, 0.92);"
        "  border: 1px solid rgba(232, 201, 131, 0.95);"
        "  border-radius: 12px;"
        "  padding: 10px 14px;"
        "}"
        "#ImageStage {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:1,"
        "      stop:0 #16202c,"
        "      stop:1 #233246);"
        "  border: 1px solid rgba(49, 69, 92, 0.95);"
        "  border-radius: 22px;"
        "}"
        "#ImageStage QScrollBar:vertical {"
        "  background: rgba(232, 238, 245, 0.8);"
        "  border: none;"
        "  width: 8px;"
        "  margin: 10px 0 10px 0;"
        "  border-radius: 4px;"
        "}"
        "#ImageStage QScrollBar::handle:vertical {"
        "  background: #7c8fa8;"
        "  min-height: 52px;"
        "  border-radius: 4px;"
        "}"
        "#ImageStage QScrollBar::handle:vertical:hover {"
        "  background: #6d829d;"
        "}"
        "#ImageStage QScrollBar::handle:vertical:pressed {"
        "  background: #61768f;"
        "}"
        "#ImageStage QScrollBar:horizontal {"
        "  background: rgba(232, 238, 245, 0.8);"
        "  border: none;"
        "  height: 8px;"
        "  margin: 0 10px 0 10px;"
        "  border-radius: 4px;"
        "}"
        "#ImageStage QScrollBar::handle:horizontal {"
        "  background: #7c8fa8;"
        "  min-width: 52px;"
        "  border-radius: 4px;"
        "}"
        "#ImageStage QScrollBar::handle:horizontal:hover {"
        "  background: #6d829d;"
        "}"
        "#ImageStage QScrollBar::handle:horizontal:pressed {"
        "  background: #61768f;"
        "}"
        "#ImageStage QScrollBar::add-line:vertical, #ImageStage QScrollBar::sub-line:vertical,"
        "#ImageStage QScrollBar::add-line:horizontal, #ImageStage QScrollBar::sub-line:horizontal {"
        "  width: 0px;"
        "  height: 0px;"
        "}"
        "#ImageStage QScrollBar::add-page:vertical, #ImageStage QScrollBar::sub-page:vertical,"
        "#ImageStage QScrollBar::add-page:horizontal, #ImageStage QScrollBar::sub-page:horizontal {"
        "  background: transparent;"
        "}"
        "#ImageStage > QWidget > QWidget {"
        "  background: transparent;"
        "}"
        "#ImageCanvas {"
        "  background: transparent;"
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
    metaFont.setPixelSize(13);
    m_metaLabel->setFont(metaFont);
    m_pathTitleLabel->setFont(metaFont);
    m_pathValueLabel->setFont(metaFont);
    m_statusLabel->setFont(metaFont);
    m_metaLabel->setWordWrap(true);
    m_pathValueLabel->setWordWrap(true);
}

void ImageRenderer::updatePixmapView()
{
    if (m_originalPixmap.isNull() || !m_scrollArea) {
        return;
    }

    const QSize targetSize = scaledVisualSize(m_originalPixmap.size(), m_zoomFactor);
    const QPixmap scaledPixmap = m_originalPixmap.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_imageLabel->setPixmap(scaledPixmap);
    m_imageLabel->resize(scaledPixmap.size());
    updateDragCursor();
}

void ImageRenderer::updateMovieView()
{
    if (!m_movie || !m_scrollArea) {
        return;
    }

    QSize sourceSize = m_movieFrameSize;
    if (!sourceSize.isValid()) {
        sourceSize = m_movie->currentPixmap().size();
    }
    if (!sourceSize.isValid()) {
        return;
    }

    const QSize scaledSize = scaledVisualSize(sourceSize, m_zoomFactor);
    m_movie->setScaledSize(scaledSize);
    m_imageLabel->resize(scaledSize);
    updateDragCursor();
}

void ImageRenderer::adjustZoom(double zoomStep, const QPoint& viewportPos)
{
    if ((!m_movie && m_originalPixmap.isNull()) || !m_scrollArea) {
        return;
    }

    const QSize oldSize = scaledVisualSize(currentSourceSize(), m_zoomFactor);
    const double nextZoom = qBound(kMinZoomFactor, m_zoomFactor * zoomStep, kMaxZoomFactor);
    if (qFuzzyCompare(nextZoom, m_zoomFactor)) {
        return;
    }

    const int oldHorizontal = m_scrollArea->horizontalScrollBar()->value();
    const int oldVertical = m_scrollArea->verticalScrollBar()->value();
    const QPoint imageAnchor(oldHorizontal + viewportPos.x(), oldVertical + viewportPos.y());

    m_zoomFactor = nextZoom;
    if (m_movie) {
        updateMovieView();
    } else {
        updatePixmapView();
    }

    const QSize newSize = scaledVisualSize(currentSourceSize(), m_zoomFactor);
    if (oldSize.width() > 0 && oldSize.height() > 0) {
        const double widthRatio = static_cast<double>(newSize.width()) / static_cast<double>(oldSize.width());
        const double heightRatio = static_cast<double>(newSize.height()) / static_cast<double>(oldSize.height());
        m_scrollArea->horizontalScrollBar()->setValue(qRound(imageAnchor.x() * widthRatio - viewportPos.x()));
        m_scrollArea->verticalScrollBar()->setValue(qRound(imageAnchor.y() * heightRatio - viewportPos.y()));
    }

}

void ImageRenderer::resetZoom()
{
    if (m_originalPixmap.isNull() && !m_movie) {
        return;
    }

    m_zoomFactor = 1.0;
    if (m_movie) {
        updateMovieView();
    } else {
        updatePixmapView();
    }
}

QSize ImageRenderer::currentSourceSize() const
{
    if (m_movie) {
        if (m_movieFrameSize.isValid()) {
            return m_movieFrameSize;
        }
        return m_movie->currentPixmap().size();
    }

    return m_originalPixmap.size();
}

QSize ImageRenderer::scaledVisualSize(const QSize& sourceSize, double zoomFactor) const
{
    if (!sourceSize.isValid() || !m_scrollArea) {
        return QSize();
    }

    const QSize viewportSize = m_scrollArea->viewport()->size();
    if (!viewportSize.isValid()) {
        return sourceSize;
    }

    QSize fitSize = sourceSize;
    fitSize.scale(viewportSize, Qt::KeepAspectRatio);
    if (!fitSize.isValid()) {
        return sourceSize;
    }

    return QSize(
        qMax(1, qRound(fitSize.width() * zoomFactor)),
        qMax(1, qRound(fitSize.height() * zoomFactor)));
}

bool ImageRenderer::canPanImage() const
{
    if ((m_originalPixmap.isNull() && !m_movie) || !m_scrollArea) {
        return false;
    }

    const QSize viewportSize = m_scrollArea->viewport()->size();
    const QSize currentSize = scaledVisualSize(currentSourceSize(), m_zoomFactor);
    return currentSize.width() > viewportSize.width() || currentSize.height() > viewportSize.height();
}

void ImageRenderer::updateDragCursor()
{
    if (!m_scrollArea || !m_imageLabel) {
        return;
    }

    const Qt::CursorShape cursorShape = m_isDragging
        ? Qt::ClosedHandCursor
        : (canPanImage() ? Qt::OpenHandCursor : Qt::ArrowCursor);
    m_imageLabel->setCursor(cursorShape);
    m_scrollArea->viewport()->setCursor(cursorShape);
}
