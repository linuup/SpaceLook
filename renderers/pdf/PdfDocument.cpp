#include "renderers/pdf/PdfDocument.h"

#include <QColor>
#include <QFile>
#include <QSize>
#include <QVector>

#include "core/PreviewFileReader.h"
#include "renderers/pdf/PdfiumBridge.h"
#include "third_party/pdfium/include/fpdf_text.h"

namespace {

constexpr int kPdfiumBitmapFormatBgra = 4;
constexpr int kRenderFlags = FPDF_ANNOT | FPDF_LCD_TEXT;

void releaseByteArrayStorage(QByteArray* data)
{
    if (!data) {
        return;
    }

    QByteArray empty;
    data->swap(empty);
}

}

PdfDocument::PdfDocument()
{
    PdfiumBridge::instance().retain();
}

PdfDocument::~PdfDocument()
{
    unload();
    PdfiumBridge::instance().release();
}

bool PdfDocument::load(const QString& filePath, QString* errorMessage)
{
    unload();

    QString readError;
    if (!PreviewFileReader::readAll(filePath, &m_fileData, &readError)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to open the PDF file for reading: %1").arg(readError);
        }
        return false;
    }

    if (m_fileData.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("The PDF file is empty or could not be read.");
        }
        releaseByteArrayStorage(&m_fileData);
        return false;
    }

    m_document = FPDF_LoadMemDocument64(
        m_fileData.constData(),
        static_cast<size_t>(m_fileData.size()),
        nullptr);
    if (!m_document) {
        const unsigned long errorCode = FPDF_GetLastError();
        if (errorMessage) {
            *errorMessage = PdfiumBridge::instance().errorString(errorCode);
        }
        releaseByteArrayStorage(&m_fileData);
        return false;
    }

    m_filePath = filePath;
    m_pageCount = FPDF_GetPageCount(m_document);
    if (m_pageCount <= 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("The PDF does not contain any readable pages.");
        }
        unload();
        return false;
    }

    return true;
}

void PdfDocument::unload()
{
    if (m_document) {
        FPDF_CloseDocument(m_document);
    }
    m_document = nullptr;
    m_filePath.clear();
    releaseByteArrayStorage(&m_fileData);
    m_pageCount = 0;
}

bool PdfDocument::isLoaded() const
{
    return m_document != nullptr;
}

int PdfDocument::pageCount() const
{
    return m_pageCount;
}

QSizeF PdfDocument::pageSizePoints(int pageIndex) const
{
    if (!m_document || pageIndex < 0 || pageIndex >= m_pageCount) {
        return QSizeF();
    }

    FPDF_PAGE page = FPDF_LoadPage(m_document, pageIndex);
    if (!page) {
        return QSizeF();
    }

    const float width = FPDF_GetPageWidthF(page);
    const float height = FPDF_GetPageHeightF(page);
    FPDF_ClosePage(page);
    return QSizeF(width, height);
}

QImage PdfDocument::renderPage(int pageIndex, double scale, QColor background, QString* errorMessage) const
{
    if (!m_document || pageIndex < 0 || pageIndex >= m_pageCount) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("The requested PDF page is out of range.");
        }
        return QImage();
    }

    FPDF_PAGE page = FPDF_LoadPage(m_document, pageIndex);
    if (!page) {
        const unsigned long errorCode = FPDF_GetLastError();
        if (errorMessage) {
            *errorMessage = PdfiumBridge::instance().errorString(errorCode);
        }
        return QImage();
    }

    const int width = qMax(1, qRound(FPDF_GetPageWidthF(page) * scale));
    const int height = qMax(1, qRound(FPDF_GetPageHeightF(page) * scale));
    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    if (image.isNull()) {
        FPDF_ClosePage(page);
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to allocate the PDF page image buffer.");
        }
        return QImage();
    }

    FPDF_BITMAP bitmap = FPDFBitmap_CreateEx(
        image.width(),
        image.height(),
        kPdfiumBitmapFormatBgra,
        image.bits(),
        image.bytesPerLine());
    if (!bitmap) {
        FPDF_ClosePage(page);
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to create the PDFium bitmap.");
        }
        return QImage();
    }

    const uint bgra = (static_cast<uint>(background.alpha()) << 24) |
        (static_cast<uint>(background.red()) << 16) |
        (static_cast<uint>(background.green()) << 8) |
        static_cast<uint>(background.blue());
    FPDFBitmap_FillRect(bitmap, 0, 0, image.width(), image.height(), bgra);
    FPDF_RenderPageBitmap(bitmap, page, 0, 0, image.width(), image.height(), 0, kRenderFlags);

    FPDFBitmap_Destroy(bitmap);
    FPDF_ClosePage(page);
    return image;
}

QString PdfDocument::pageText(int pageIndex) const
{
    if (!m_document || pageIndex < 0 || pageIndex >= m_pageCount) {
        return QString();
    }

    FPDF_PAGE page = FPDF_LoadPage(m_document, pageIndex);
    if (!page) {
        return QString();
    }

    FPDF_TEXTPAGE textPage = FPDFText_LoadPage(page);
    if (!textPage) {
        FPDF_ClosePage(page);
        return QString();
    }

    const int charCount = FPDFText_CountChars(textPage);
    if (charCount <= 0) {
        FPDFText_ClosePage(textPage);
        FPDF_ClosePage(page);
        return QString();
    }

    QVector<ushort> buffer(charCount + 1);
    const int written = FPDFText_GetText(textPage, 0, charCount + 1, buffer.data());
    FPDFText_ClosePage(textPage);
    FPDF_ClosePage(page);
    if (written <= 0) {
        return QString();
    }

    return QString::fromUtf16(buffer.constData(), qMax(0, written - 1));
}
