#pragma once

#include <QWidget>

#include "core/hovered_item_info.h"
#include "renderers/IPreviewRenderer.h"

class QLabel;
class OpenWithButton;
class QPushButton;
class QScrollArea;
class QVBoxLayout;
class SelectableTitleLabel;
struct _CERT_CONTEXT;

class CertificateRenderer : public QWidget, public IPreviewRenderer
{
    Q_OBJECT

public:
    explicit CertificateRenderer(QWidget* parent = nullptr);

    QString rendererId() const override;
    bool canHandle(const HoveredItemInfo& info) const override;
    QWidget* widget() override;
    void load(const HoveredItemInfo& info) override;
    void unload() override;

private:
    struct Detail
    {
        QString title;
        QString value;
    };

    void applyChrome();
    void showStatusMessage(const QString& message);
    void populateDetails(const QVector<Detail>& details);
    QVector<Detail> inspectCertificateFile(const HoveredItemInfo& info, QString* statusMessage) const;
    QVector<Detail> inspectPkcs12File(const QString& filePath, const QString& password, QString* statusMessage) const;
    void appendCertificateContextDetails(QVector<Detail>* details, const _CERT_CONTEXT* certContext, const QString& prefix) const;
    void updateUnlockButtonVisibility();
    void unlockCurrentFile();

    HoveredItemInfo m_info;
    QWidget* m_headerRow = nullptr;
    QLabel* m_iconLabel = nullptr;
    SelectableTitleLabel* m_titleLabel = nullptr;
    QWidget* m_pathRow = nullptr;
    QLabel* m_pathValueLabel = nullptr;
    OpenWithButton* m_openWithButton = nullptr;
    QLabel* m_statusLabel = nullptr;
    QPushButton* m_unlockButton = nullptr;
    QScrollArea* m_scrollArea = nullptr;
    QWidget* m_detailContent = nullptr;
    QVBoxLayout* m_detailLayout = nullptr;
};
