#include "settings/render_type_settings.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QVariantMap>

#include "core/render_type_registry.h"

RenderTypeSettings& RenderTypeSettings::instance()
{
    static RenderTypeSettings settings;
    return settings;
}

RenderTypeSettings::RenderTypeSettings(QObject* parent)
    : QObject(parent)
{
}

QString RenderTypeSettings::configFilePath() const
{
    return m_configFilePath;
}

QString RenderTypeSettings::statusMessage() const
{
    return m_statusMessage;
}

QVariantList RenderTypeSettings::entries() const
{
    return m_entries;
}

void RenderTypeSettings::load()
{
    RenderTypeRegistry::instance().load();
    m_configFilePath = RenderTypeRegistry::instance().configFilePath();
    m_entries.clear();

    QFile file(m_configFilePath);
    if (!file.open(QIODevice::ReadOnly)) {
        setStatusMessage(QStringLiteral("Failed to open RenderType.json."));
        emit changed();
        return;
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!document.isObject()) {
        setStatusMessage(QStringLiteral("RenderType.json is not a JSON object."));
        emit changed();
        return;
    }

    const QJsonObject root = document.object();
    const QStringList keys = root.keys();
    for (const QString& key : keys) {
        const QJsonObject object = root.value(key).toObject();
        QVariantMap entry;
        entry.insert(QStringLiteral("key"), key);
        entry.insert(QStringLiteral("name"), object.value(QStringLiteral("name")).toString());
        entry.insert(QStringLiteral("typeKey"), object.value(QStringLiteral("typeKey")).toString());
        entry.insert(QStringLiteral("typeDetails"), object.value(QStringLiteral("typeDetails")).toString());

        QStringList suffixes;
        const QJsonArray suffixArray = object.value(QStringLiteral("suffixes")).toArray();
        for (const QJsonValue& suffixValue : suffixArray) {
            const QString suffix = suffixValue.toString().trimmed();
            if (!suffix.isEmpty()) {
                suffixes.append(suffix);
            }
        }
        entry.insert(QStringLiteral("suffixes"), suffixes.join(QStringLiteral(", ")));
        m_entries.append(entry);
    }

    setStatusMessage(QStringLiteral("Loaded RenderType.json."));
    emit changed();
}

bool RenderTypeSettings::saveEntries(const QVariantList& entries)
{
    if (m_configFilePath.trimmed().isEmpty()) {
        m_configFilePath = RenderTypeRegistry::instance().configFilePath();
    }

    QJsonObject root;
    for (const QVariant& entryValue : entries) {
        const QVariantMap entry = entryValue.toMap();
        const QString key = entry.value(QStringLiteral("key")).toString().trimmed();
        const QString name = entry.value(QStringLiteral("name")).toString().trimmed();
        const QString typeKey = entry.value(QStringLiteral("typeKey")).toString().trimmed();
        const QString typeDetails = entry.value(QStringLiteral("typeDetails")).toString().trimmed();
        const QString suffixText = entry.value(QStringLiteral("suffixes")).toString();
        if (key.isEmpty() || name.isEmpty() || typeKey.isEmpty()) {
            setStatusMessage(QStringLiteral("Each row needs a key, renderer name, and type key."));
            emit changed();
            return false;
        }

        QJsonArray suffixes;
        const QStringList suffixParts = suffixText.split(QRegularExpression(QStringLiteral("[,\\s]+")), Qt::SkipEmptyParts);
        for (const QString& suffixPart : suffixParts) {
            QString suffix = suffixPart.trimmed().toLower();
            while (suffix.startsWith(QLatin1Char('.'))) {
                suffix.remove(0, 1);
            }
            if (!suffix.isEmpty()) {
                suffixes.append(suffix);
            }
        }
        if (suffixes.isEmpty()) {
            setStatusMessage(QStringLiteral("Each row needs at least one suffix."));
            emit changed();
            return false;
        }

        root.insert(key, QJsonObject{
            {QStringLiteral("name"), name},
            {QStringLiteral("typeKey"), typeKey},
            {QStringLiteral("typeDetails"), typeDetails},
            {QStringLiteral("suffixes"), suffixes}
        });
    }

    QFile file(m_configFilePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setStatusMessage(QStringLiteral("Failed to save RenderType.json."));
        emit changed();
        return false;
    }

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();
    RenderTypeRegistry::instance().load();
    load();
    setStatusMessage(QStringLiteral("Saved and reloaded RenderType.json."));
    emit changed();
    return true;
}

void RenderTypeSettings::setStatusMessage(const QString& message)
{
    m_statusMessage = message;
}
