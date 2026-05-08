#pragma once

#include <QObject>
#include <QString>
#include <QTranslator>

class AppTranslator : public QObject
{
    Q_OBJECT

public:
    static AppTranslator& instance();

    void initialize();
    QString currentLanguage() const;

signals:
    void languageChanged();

private:
    explicit AppTranslator(QObject* parent = nullptr);
    void applyLanguage(const QString& language);
    bool loadTranslator(const QString& language);

    QString m_currentLanguage = QStringLiteral("en");
    QTranslator m_translator;
};
