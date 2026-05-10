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
    void unload();
    QString activeHandlerGuid() const;

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    bool ensureComInitialized(QString* errorMessage);
    void updatePreviewRect();

    IPreviewHandler* m_previewHandler = nullptr;
    bool m_comInitialized = false;
    QString m_activeHandlerGuid;
};
