#include "renderers/pdf/PdfiumBridge.h"

PdfiumBridge& PdfiumBridge::instance()
{
    static PdfiumBridge bridge;
    return bridge;
}

void PdfiumBridge::retain()
{
    QMutexLocker locker(&m_mutex);
    if (m_refCount == 0) {
        FPDF_InitLibrary();
    }
    ++m_refCount;
}

void PdfiumBridge::release()
{
    QMutexLocker locker(&m_mutex);
    if (m_refCount <= 0) {
        return;
    }

    --m_refCount;
    if (m_refCount == 0) {
        FPDF_DestroyLibrary();
    }
}

QString PdfiumBridge::errorString(unsigned long errorCode) const
{
    switch (errorCode) {
    case FPDF_ERR_SUCCESS:
        return QStringLiteral("No error");
    case FPDF_ERR_UNKNOWN:
        return QStringLiteral("Unknown PDFium error");
    case FPDF_ERR_FILE:
        return QStringLiteral("PDF file access failed");
    case FPDF_ERR_FORMAT:
        return QStringLiteral("Invalid or unsupported PDF format");
    case FPDF_ERR_PASSWORD:
        return QStringLiteral("PDF password is required");
    case FPDF_ERR_SECURITY:
        return QStringLiteral("PDF security restriction blocked access");
    case FPDF_ERR_PAGE:
        return QStringLiteral("Failed to load the requested PDF page");
    default:
        return QStringLiteral("PDFium error code %1").arg(errorCode);
    }
}
