#pragma once

#include <QString>
#include <QWidget>

struct IPreviewHandler;

class PreviewHandlerHost : public QWidget
{
    Q_OBJECT

public:
    explicit PreviewHandlerHost(QWidget* parent = nullptr);
    ~PreviewHandlerHost() override;

    bool openFile(const QString& filePath, QString* errorMessage);
    bool openFileWithHandler(const QString& filePath, const QString& handlerGuid, QString* errorMessage);
    void unload();
    void warmUp();
    QString activeHandlerGuid() const;

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    bool ensureComInitialized(QString* errorMessage);
    bool openFileWithHandlerGuid(const QString& filePath, const QString& handlerGuid, const QString& handlerLabel, bool requireRegistration, QString* errorMessage);
    void updatePreviewRect();

    IPreviewHandler* m_previewHandler = nullptr;
    bool m_comInitialized = false;
    QString m_activeHandlerGuid;
};
