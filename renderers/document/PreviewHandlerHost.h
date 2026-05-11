#pragma once

#include <QString>
#include <QWidget>

struct IPreviewHandler;

struct PreviewHandlerOpenResult
{
    bool success = false;
    QString message;
    QString handlerGuid;
    QString processArchitecture;
    QString registeredArchitecture;
    bool architectureMismatch = false;
};

class PreviewHandlerHost : public QWidget
{
    Q_OBJECT

public:
    explicit PreviewHandlerHost(QWidget* parent = nullptr);
    ~PreviewHandlerHost() override;

    bool openFile(const QString& filePath, QString* errorMessage);
    PreviewHandlerOpenResult openFileDetailed(const QString& filePath);
    void unload();
    void warmUp();
    QString activeHandlerGuid() const;

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    bool ensureComInitialized(QString* errorMessage);
    void updatePreviewRect();

    IPreviewHandler* m_previewHandler = nullptr;
    bool m_comInitializedHere = false;
    QString m_activeHandlerGuid;
};
