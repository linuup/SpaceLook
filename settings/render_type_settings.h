#pragma once

#include <QObject>
#include <QVariantList>

class RenderTypeSettings : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString configFilePath READ configFilePath NOTIFY changed)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY changed)
    Q_PROPERTY(QVariantList entries READ entries NOTIFY changed)

public:
    static RenderTypeSettings& instance();

    QString configFilePath() const;
    QString statusMessage() const;
    QVariantList entries() const;

    Q_INVOKABLE void load();
    Q_INVOKABLE bool saveEntries(const QVariantList& entries);

signals:
    void changed();

private:
    explicit RenderTypeSettings(QObject* parent = nullptr);

    void setStatusMessage(const QString& message);

    QString m_configFilePath;
    QString m_statusMessage;
    QVariantList m_entries;
};
