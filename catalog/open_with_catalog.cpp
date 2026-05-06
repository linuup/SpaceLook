#include "catalog/open_with_catalog.h"

#include <QDir>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QSet>

#include <Windows.h>
#include <Shobjidl.h>
#include <Shlwapi.h>

namespace {

QIcon iconFromLocation(const QString& iconPath, int iconIndex)
{
    Q_UNUSED(iconIndex);
    if (iconPath.trimmed().isEmpty()) {
        return QIcon();
    }

    QFileIconProvider iconProvider;
    return iconProvider.icon(QFileInfo(iconPath));
}

QString qStringFromWide(const wchar_t* value)
{
    return value ? QString::fromWCharArray(value).trimmed() : QString();
}

QString cleanDisplayName(const QString& value)
{
    QString normalized = value.trimmed();
    normalized.remove(QLatin1Char('&'));
    return normalized;
}

QVector<OpenWithCatalog::AppEntry> fileAssociationAppsForFile(const QString& filePath)
{
    QVector<OpenWithCatalog::AppEntry> result;
    const QString normalizedPath = QDir::toNativeSeparators(filePath.trimmed());
    if (normalizedPath.isEmpty()) {
        return result;
    }

    const QFileInfo fileInfo(normalizedPath);
    if (!fileInfo.exists()) {
        return result;
    }

    IEnumAssocHandlers* enumHandlers = nullptr;
    const HRESULT enumHr = SHAssocEnumHandlers(reinterpret_cast<LPCWSTR>(normalizedPath.utf16()),
                                               ASSOC_FILTER_NONE,
                                               &enumHandlers);
    if (FAILED(enumHr) || !enumHandlers) {
        return result;
    }

    QSet<QString> seenPaths;
    while (true) {
        IAssocHandler* handler = nullptr;
        const HRESULT nextHr = enumHandlers->Next(1, &handler, nullptr);
        if (nextHr != S_OK || !handler) {
            break;
        }

        LPWSTR uiName = nullptr;
        LPWSTR iconPath = nullptr;
        int iconIndex = 0;
        const HRESULT nameHr = handler->GetUIName(&uiName);
        const HRESULT iconHr = handler->GetIconLocation(&iconPath, &iconIndex);

        QString resolvedName = SUCCEEDED(nameHr) ? cleanDisplayName(qStringFromWide(uiName)) : QString();
        QString resolvedIconPath = SUCCEEDED(iconHr) ? qStringFromWide(iconPath) : QString();
        if (resolvedName.isEmpty()) {
            resolvedName = QStringLiteral("Associated App");
        }

        QString executablePath = resolvedIconPath;
        if (!executablePath.isEmpty()) {
            const int commaIndex = executablePath.indexOf(QLatin1Char(','));
            if (commaIndex >= 0) {
                executablePath = executablePath.left(commaIndex).trimmed();
            }
            if (executablePath.startsWith(QLatin1Char('"')) && executablePath.endsWith(QLatin1Char('"'))) {
                executablePath = executablePath.mid(1, executablePath.size() - 2);
            }
            executablePath = QDir::toNativeSeparators(executablePath);
        }

        if (!executablePath.isEmpty() && QFileInfo(executablePath).exists()) {
            const QString dedupeKey = executablePath.toLower();
            if (!seenPaths.contains(dedupeKey)) {
                OpenWithCatalog::AppEntry entry;
                entry.displayName = resolvedName;
                entry.executablePath = executablePath;
                entry.source = QStringLiteral("AssocHandler");
                entry.icon = iconFromLocation(executablePath, iconIndex);
                result.append(entry);
                seenPaths.insert(dedupeKey);
            }
        }

        if (uiName) {
            CoTaskMemFree(uiName);
        }
        if (iconPath) {
            CoTaskMemFree(iconPath);
        }
        handler->Release();
    }

    enumHandlers->Release();
    return result;
}

}

OpenWithCatalog& OpenWithCatalog::instance()
{
    static OpenWithCatalog catalog;
    return catalog;
}

QVector<OpenWithCatalog::AppEntry> OpenWithCatalog::registryAppsForFile(const QString& filePath) const
{
    return fileAssociationAppsForFile(filePath);
}
