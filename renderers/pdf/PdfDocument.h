#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>

#include "third_party/pdfium/include/fpdfview.h"

class PdfDocument
{
public:
    PdfDocument();
    ~PdfDocument();

    bool load(const QString& filePath, QString* errorMessage);
    void unload();

    bool isLoaded() const;
    int pageCount() const;
    QSizeF pageSizePoints(int pageIndex) const;
    QImage renderPage(int pageIndex, double scale, QColor background, QString* errorMessage) const;
    QString pageText(int pageIndex) const;

private:
    QString renderErrorMessage(QString* errorMessage) const;

    QString m_filePath;
    QByteArray m_fileData;
    FPDF_DOCUMENT m_document = nullptr;
    int m_pageCount = 0;
};
