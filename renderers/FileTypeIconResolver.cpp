#include "renderers/FileTypeIconResolver.h"

#include <QDir>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QUrl>

namespace {

QString bySuffix(const QString& suffix)
{
    const QString lower = suffix.toLower();
    if (lower == QStringLiteral("pdf")) {
        return QStringLiteral(":/SPACELOOK/file-icon/pdf.svg");
    }
    if (lower == QStringLiteral("doc") || lower == QStringLiteral("docx")) {
        return QStringLiteral(":/SPACELOOK/file-icon/office-doc.svg");
    }
    if (lower == QStringLiteral("xls") || lower == QStringLiteral("xlsx")) {
        return QStringLiteral(":/SPACELOOK/file-icon/office-els.svg");
    }
    if (lower == QStringLiteral("ppt") || lower == QStringLiteral("pptx")) {
        return QStringLiteral(":/SPACELOOK/file-icon/office-ppt.svg");
    }
    if (lower == QStringLiteral("txt") || lower == QStringLiteral("log")) {
        return QStringLiteral(":/SPACELOOK/file-icon/txt.svg");
    }
    if (lower == QStringLiteral("md") || lower == QStringLiteral("json") || lower == QStringLiteral("xml") ||
        lower == QStringLiteral("yaml") || lower == QStringLiteral("yml") || lower == QStringLiteral("toml") ||
        lower == QStringLiteral("ini") || lower == QStringLiteral("conf") || lower == QStringLiteral("cfg")) {
        return QStringLiteral(":/SPACELOOK/file-icon/office-txt.svg");
    }
    if (lower == QStringLiteral("js") || lower == QStringLiteral("jsx")) {
        return QStringLiteral(":/SPACELOOK/file-icon/js.svg");
    }
    if (lower == QStringLiteral("css") || lower == QStringLiteral("scss") || lower == QStringLiteral("sass") ||
        lower == QStringLiteral("less")) {
        return QStringLiteral(":/SPACELOOK/file-icon/css.svg");
    }
    if (lower == QStringLiteral("mp3") || lower == QStringLiteral("wav") || lower == QStringLiteral("flac") ||
        lower == QStringLiteral("aac") || lower == QStringLiteral("m4a") || lower == QStringLiteral("ogg") ||
        lower == QStringLiteral("wma") || lower == QStringLiteral("opus")) {
        return QStringLiteral(":/SPACELOOK/file-icon/mp3.svg");
    }
    if (lower == QStringLiteral("mp4") || lower == QStringLiteral("mkv") || lower == QStringLiteral("avi") ||
        lower == QStringLiteral("mov") || lower == QStringLiteral("wmv") || lower == QStringLiteral("webm") ||
        lower == QStringLiteral("m4v") || lower == QStringLiteral("mpg") || lower == QStringLiteral("mpeg") ||
        lower == QStringLiteral("ts")) {
        return QStringLiteral(":/SPACELOOK/file-icon/mp4.svg");
    }
    if (lower == QStringLiteral("png")) {
        return QStringLiteral(":/SPACELOOK/file-icon/image-PNG.svg");
    }
    if (lower == QStringLiteral("jpg") || lower == QStringLiteral("jpeg")) {
        return QStringLiteral(":/SPACELOOK/file-icon/image-jpeg.svg");
    }
    if (lower == QStringLiteral("gif")) {
        return QStringLiteral(":/SPACELOOK/file-icon/image-gif.svg");
    }
    if (lower == QStringLiteral("bmp") || lower == QStringLiteral("webp") || lower == QStringLiteral("svg") ||
        lower == QStringLiteral("ico")) {
        return QStringLiteral(":/SPACELOOK/file-icon/image-pic.svg");
    }
    if (lower == QStringLiteral("zip") || lower == QStringLiteral("7z") ||
        lower == QStringLiteral("tgz") || lower == QStringLiteral("cab")) {
        return QStringLiteral(":/SPACELOOK/file-icon/zip.svg");
    }
    if (lower == QStringLiteral("rar") || lower == QStringLiteral("tar") || lower == QStringLiteral("gz") ||
        lower == QStringLiteral("bz2") || lower == QStringLiteral("tbz") || lower == QStringLiteral("tbz2") ||
        lower == QStringLiteral("xz") || lower == QStringLiteral("txz")) {
        return QStringLiteral(":/SPACELOOK/file-icon/rar.svg");
    }
    if (lower == QStringLiteral("exe")) {
        return QStringLiteral(":/SPACELOOK/file-icon/exe.svg");
    }
    if (lower == QStringLiteral("apk")) {
        return QStringLiteral(":/SPACELOOK/file-icon/apk.svg");
    }
    if (lower == QStringLiteral("ipa")) {
        return QStringLiteral(":/SPACELOOK/file-icon/ipa.svg");
    }
    return QString();
}

bool isShortcutLikeType(const HoveredItemInfo& info)
{
    return info.typeKey == QStringLiteral("shortcut") ||
        info.typeKey == QStringLiteral("shell_item");
}

bool shouldPreferResolvedSystemIcon(const HoveredItemInfo& info)
{
    return isShortcutLikeType(info);
}

QString localResolvedPath(const HoveredItemInfo& info)
{
    const QString resolvedPath = info.resolvedPath.trimmed();
    if (resolvedPath.isEmpty()) {
        return QString();
    }

    if (resolvedPath.startsWith(QStringLiteral("file:///"), Qt::CaseInsensitive)) {
        const QString localFile = QUrl(resolvedPath).toLocalFile();
        return localFile.isEmpty() ? QString() : QDir::cleanPath(localFile);
    }

    if (resolvedPath.contains(QStringLiteral("://"))) {
        return QString();
    }

    return QDir::cleanPath(resolvedPath);
}

HoveredItemInfo effectiveIconInfo(const HoveredItemInfo& info)
{
    HoveredItemInfo effectiveInfo = info;
    const QString resolvedLocalPath = localResolvedPath(info);
    if (isShortcutLikeType(info) && !resolvedLocalPath.isEmpty()) {
        const QFileInfo resolvedFileInfo(resolvedLocalPath);
        effectiveInfo.filePath = resolvedLocalPath;
        effectiveInfo.fileName = resolvedFileInfo.fileName();
        effectiveInfo.isDirectory = resolvedFileInfo.isDir();
        if (resolvedFileInfo.isDir()) {
            effectiveInfo.typeKey = QStringLiteral("folder");
        }
    }
    return effectiveInfo;
}

QString specificIconResourcePath(const HoveredItemInfo& info)
{
    if (info.isDirectory || info.typeKey == QStringLiteral("folder") || info.typeKey == QStringLiteral("shell_folder")) {
        return QStringLiteral(":/SPACELOOK/file-icon/folder.svg");
    }

    const QString suffix = QFileInfo(info.filePath).suffix();
    const QString suffixIcon = bySuffix(suffix);
    if (!suffixIcon.isEmpty()) {
        return suffixIcon;
    }

    if (info.typeKey == QStringLiteral("pdf")) {
        return QStringLiteral(":/SPACELOOK/file-icon/pdf.svg");
    }
    if (info.typeKey == QStringLiteral("office")) {
        return QStringLiteral(":/SPACELOOK/file-icon/office-doc.svg");
    }
    if (info.typeKey == QStringLiteral("code")) {
        return QStringLiteral(":/SPACELOOK/file-icon/code.svg");
    }
    if (info.typeKey == QStringLiteral("text")) {
        return QStringLiteral(":/SPACELOOK/file-icon/office-txt.svg");
    }
    if (info.typeKey == QStringLiteral("image")) {
        return QStringLiteral(":/SPACELOOK/file-icon/image.svg");
    }
    if (info.typeKey == QStringLiteral("audio")) {
        return QStringLiteral(":/SPACELOOK/file-icon/music.svg");
    }
    if (info.typeKey == QStringLiteral("video")) {
        return QStringLiteral(":/SPACELOOK/file-icon/video.svg");
    }
    if (info.typeKey == QStringLiteral("archive")) {
        return QStringLiteral(":/SPACELOOK/file-icon/zip.svg");
    }

    return QString();
}

}

QString FileTypeIconResolver::iconResourcePath(const HoveredItemInfo& info)
{
    const HoveredItemInfo effectiveInfo = effectiveIconInfo(info);
    const QString specificPath = specificIconResourcePath(effectiveInfo);
    if (!specificPath.isEmpty()) {
        return specificPath;
    }

    if (info.typeKey == QStringLiteral("shortcut")) {
        return QStringLiteral(":/SPACELOOK/file-icon/file.svg");
    }

    return QStringLiteral(":/SPACELOOK/file-icon/file.svg");
}

QIcon FileTypeIconResolver::iconForInfo(const HoveredItemInfo& info)
{
    static QFileIconProvider iconProvider;
    const HoveredItemInfo effectiveInfo = effectiveIconInfo(info);

    if (shouldPreferResolvedSystemIcon(info) && !effectiveInfo.filePath.trimmed().isEmpty()) {
        const QFileInfo resolvedFileInfo(effectiveInfo.filePath);
        if (resolvedFileInfo.exists()) {
            const QIcon resolvedSystemIcon = iconProvider.icon(resolvedFileInfo);
            if (!resolvedSystemIcon.isNull()) {
                return resolvedSystemIcon;
            }
        }
    }

    const QString specificResourcePath = specificIconResourcePath(effectiveInfo);
    if (!specificResourcePath.isEmpty()) {
        return QIcon(specificResourcePath);
    }

    if (!effectiveInfo.filePath.trimmed().isEmpty()) {
        const QFileInfo fileInfo(effectiveInfo.filePath);
        if (fileInfo.exists()) {
            const QIcon systemIcon = iconProvider.icon(fileInfo);
            if (!systemIcon.isNull()) {
                return systemIcon;
            }
        }
    }

    const QString resourcePath = iconResourcePath(info);
    if (!resourcePath.isEmpty()) {
        return QIcon(resourcePath);
    }

    return QIcon();
}
