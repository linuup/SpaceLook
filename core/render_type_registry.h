#pragma once

#include <optional>

#include <QString>
#include <QStringList>

#include "core/file_type_detector.h"

class RenderTypeRegistry
{
public:
    static RenderTypeRegistry& instance();

    void load();
    std::optional<DetectedTypeInfo> detectTypeInfoForSuffixCandidates(const QStringList& suffixCandidates) const;
    QString configFilePath() const;

private:
    RenderTypeRegistry() = default;

    void ensureLoaded() const;

    bool m_loaded = false;
    QString m_configFilePath;
    QList<QPair<QString, DetectedTypeInfo>> m_suffixMappings;
};
