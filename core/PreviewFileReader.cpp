#include "core/PreviewFileReader.h"

#include <QtGlobal>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <QDir>
#include <QFile>

#include <string>

namespace {

QString systemErrorMessage(
#ifdef Q_OS_WIN
    DWORD errorCode
#else
    const QString& fallback
#endif
)
{
#ifdef Q_OS_WIN
    wchar_t* messageBuffer = nullptr;
    const DWORD size = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        errorCode,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&messageBuffer),
        0,
        nullptr);

    QString message = size > 0 && messageBuffer
        ? QString::fromWCharArray(messageBuffer).trimmed()
        : QStringLiteral("Windows error %1").arg(errorCode);
    if (messageBuffer) {
        LocalFree(messageBuffer);
    }
    if (message.isEmpty()) {
        message = QStringLiteral("Windows error %1").arg(errorCode);
    }
    return message;
#else
    return fallback;
#endif
}

#ifdef Q_OS_WIN
bool readWithSharedHandle(const QString& filePath, qint64 maxBytes, QByteArray* data, bool* truncated, QString* errorMessage)
{
    if (data) {
        data->clear();
    }
    if (truncated) {
        *truncated = false;
    }
    if (!data) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("No output buffer was provided.");
        }
        return false;
    }

    const QString nativePath = QDir::toNativeSeparators(filePath);
    const std::wstring widePath = nativePath.toStdWString();
    HANDLE fileHandle = CreateFileW(
        widePath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr);
    if (fileHandle == INVALID_HANDLE_VALUE) {
        if (errorMessage) {
            *errorMessage = systemErrorMessage(GetLastError());
        }
        return false;
    }

    const qint64 targetBytes = maxBytes >= 0 ? maxBytes + 1 : -1;
    QByteArray buffer;
    constexpr DWORD kChunkSize = 64 * 1024;
    char chunk[kChunkSize];

    bool success = true;
    while (true) {
        DWORD requested = kChunkSize;
        if (targetBytes >= 0) {
            const qint64 remaining = targetBytes - buffer.size();
            if (remaining <= 0) {
                break;
            }
            requested = static_cast<DWORD>(qMin<qint64>(remaining, kChunkSize));
        }

        DWORD bytesRead = 0;
        if (!ReadFile(fileHandle, chunk, requested, &bytesRead, nullptr)) {
            if (errorMessage) {
                *errorMessage = systemErrorMessage(GetLastError());
            }
            success = false;
            break;
        }
        if (bytesRead == 0) {
            break;
        }
        buffer.append(chunk, static_cast<int>(bytesRead));
    }

    CloseHandle(fileHandle);
    if (!success) {
        data->clear();
        return false;
    }

    if (maxBytes >= 0 && buffer.size() > maxBytes) {
        if (truncated) {
            *truncated = true;
        }
        buffer.truncate(static_cast<int>(maxBytes));
    }

    *data = buffer;
    return true;
}
#endif

bool readWithQtFile(const QString& filePath, qint64 maxBytes, QByteArray* data, bool* truncated, QString* errorMessage)
{
    if (data) {
        data->clear();
    }
    if (truncated) {
        *truncated = false;
    }
    if (!data) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("No output buffer was provided.");
        }
        return false;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage) {
            *errorMessage = file.errorString();
        }
        return false;
    }

    QByteArray content = maxBytes >= 0 ? file.read(maxBytes + 1) : file.readAll();
    file.close();
    if (maxBytes >= 0 && content.size() > maxBytes) {
        if (truncated) {
            *truncated = true;
        }
        content.truncate(static_cast<int>(maxBytes));
    }

    *data = content;
    return true;
}

}

namespace PreviewFileReader {

bool readAll(const QString& filePath, QByteArray* data, QString* errorMessage)
{
#ifdef Q_OS_WIN
    return readWithSharedHandle(filePath, -1, data, nullptr, errorMessage);
#else
    return readWithQtFile(filePath, -1, data, nullptr, errorMessage);
#endif
}

bool readPrefix(const QString& filePath, qint64 maxBytes, QByteArray* data, bool* truncated, QString* errorMessage)
{
    if (maxBytes < 0) {
        return readAll(filePath, data, errorMessage);
    }
#ifdef Q_OS_WIN
    return readWithSharedHandle(filePath, maxBytes, data, truncated, errorMessage);
#else
    return readWithQtFile(filePath, maxBytes, data, truncated, errorMessage);
#endif
}

}
