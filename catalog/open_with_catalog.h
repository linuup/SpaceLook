#pragma once

#include <QIcon>
#include <QString>
#include <QVector>

class OpenWithCatalog
{
public:
    struct AppEntry
    {
        QString displayName;
        QString executablePath;
        QString source;
        QIcon icon;
    };

    static OpenWithCatalog& instance();

    QVector<AppEntry> registryAppsForFile(const QString& filePath) const;

private:
    OpenWithCatalog() = default;
};
