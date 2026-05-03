#include "catalog/open_with_catalog.h"

#include <QDebug>
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
        qDebug().noquote() << QStringLiteral("[SpaceLookAssoc] file path is empty");
        return result;
    }

    const QFileInfo fileInfo(normalizedPath);
    if (!fileInfo.exists()) {
        qDebug().noquote() << QStringLiteral("[SpaceLookAssoc] file missing path=\"%1\"").arg(normalizedPath);
        return result;
    }

    qDebug().noquote() << QStringLiteral("[SpaceLookAssoc] begin file=\"%1\" suffix=\"%2\"")
        .arg(normalizedPath, fileInfo.suffix().toLower());

    IEnumAssocHandlers* enumHandlers = nullptr;
    const HRESULT enumHr = SHAssocEnumHandlers(reinterpret_cast<LPCWSTR>(normalizedPath.utf16()),
                                               ASSOC_FILTER_NONE,
                                               &enumHandlers);
    if (FAILED(enumHr) || !enumHandlers) {
        qDebug().noquote() << QStringLiteral("[SpaceLookAssoc] SHAssocEnumHandlers failed hr=0x%1 file=\"%2\"")
            .arg(QString::number(static_cast<qulonglong>(enumHr), 16), normalizedPath);
        return result;
    }

    QSet<QString> seenPaths;
    while (true) {
        IAssocHandler* handler = nullptr;
        const HRESULT nextHr = enumHandlers->Next(1, &handler, nullptr);
        if (nextHr != S_OK || !handler) {
            if (nextHr != S_FALSE) {
                qDebug().noquote() << QStringLiteral("[SpaceLookAssoc] enum next stopped hr=0x%1")
                    .arg(QString::number(static_cast<qulonglong>(nextHr), 16));
            }
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

        qDebug().noquote() << QStringLiteral("[SpaceLookAssoc] handler name=\"%1\" iconPath=\"%2\" exeGuess=\"%3\"")
            .arg(resolvedName, resolvedIconPath, executablePath);

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
                qDebug().noquote() << QStringLiteral("[SpaceLookAssoc] accepted name=\"%1\" exe=\"%2\"")
                    .arg(entry.displayName, entry.executablePath);
            } else {
                qDebug().noquote() << QStringLiteral("[SpaceLookAssoc] skipped duplicate exe=\"%1\"")
                    .arg(executablePath);
            }
        } else {
            qDebug().noquote() << QStringLiteral("[SpaceLookAssoc] skipped unresolved handler name=\"%1\" exeGuess=\"%2\"")
                .arg(resolvedName, executablePath);
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
    qDebug().noquote() << QStringLiteral("[SpaceLookAssoc] end file=\"%1\" totalEntries=%2")
        .arg(normalizedPath)
        .arg(result.size());
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
