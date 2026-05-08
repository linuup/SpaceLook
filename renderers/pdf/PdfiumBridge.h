#pragma once

#include <QMutex>
#include <QString>

#include "third_party/pdfium/include/fpdfview.h"

class PdfiumBridge
{
public:
    static PdfiumBridge& instance();

    void retain();
    void release();

    QString errorString(unsigned long errorCode) const;

private:
    PdfiumBridge() = default;
    ~PdfiumBridge() = default;

    QMutex m_mutex;
    int m_refCount = 0;
};
