#include "renderers/QmlShellRenderer.h"

#include <QDebug>
#include <QVariantMap>
#include <QQuickWidget>
#include <QQmlContext>
#include <QUrl>
#include <QVBoxLayout>

#include "core/preview_state.h"
#include "widgets/SpaceLookWindow.h"

namespace {

QVariantMap previewDataForInfo(const HoveredItemInfo& info)
{
    QVariantMap data;
    data.insert(QStringLiteral("title"), info.title);
    data.insert(QStringLiteral("typeKey"), info.typeKey);
    data.insert(QStringLiteral("typeDetails"), info.typeDetails);
    data.insert(QStringLiteral("rendererName"), info.rendererName);
    data.insert(QStringLiteral("sourceKind"), info.sourceKind);
    data.insert(QStringLiteral("filePath"), info.filePath);
    data.insert(QStringLiteral("fileName"), info.fileName);
    data.insert(QStringLiteral("folderPath"), info.folderPath);
    data.insert(QStringLiteral("resolvedPath"), info.resolvedPath);
    data.insert(QStringLiteral("sourceLabel"), info.sourceLabel);
    data.insert(QStringLiteral("windowClassName"), info.windowClassName);
    data.insert(QStringLiteral("statusMessage"), info.statusMessage);
    data.insert(QStringLiteral("hasItem"), info.valid);
    data.insert(QStringLiteral("exists"), info.exists);
    data.insert(QStringLiteral("isDirectory"), info.isDirectory);
    return data;
}

}

QmlShellRenderer::QmlShellRenderer(PreviewState* previewState, QWidget* parent)
    : QWidget(parent)
    , m_previewState(previewState)
    , m_quickWidget(new QQuickWidget(this))
{
    setAttribute(Qt::WA_StyledBackground, true);
    setObjectName(QStringLiteral("QmlShellRendererRoot"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_quickWidget);

    m_quickWidget->setObjectName(QStringLiteral("QmlShellRendererView"));
    m_quickWidget->setResizeMode(QQuickWidget::SizeRootObjectToView);
    m_quickWidget->setClearColor(Qt::transparent);
    m_quickWidget->rootContext()->setContextProperty(QStringLiteral("previewData"), previewDataForInfo(HoveredItemInfo()));
    m_quickWidget->rootContext()->setContextProperty(QStringLiteral("qmlShellBridge"), this);
    m_quickWidget->setSource(QUrl(QStringLiteral("qrc:/SPACELOOK/viewers/QuickPageViewer.qml")));

    connect(m_quickWidget, &QQuickWidget::statusChanged, this, [this](QQuickWidget::Status status) {
        qDebug().noquote() << QStringLiteral("[SpaceLookQml] shell status=%1").arg(static_cast<int>(status));
    });

    setStyleSheet(
        "#QmlShellRendererRoot {"
        "  background: transparent;"
        "}"
        "#QmlShellRendererView {"
        "  background: transparent;"
        "}"
    );
}

QString QmlShellRenderer::rendererId() const
{
    return QStringLiteral("qml_shell");
}

bool QmlShellRenderer::canHandle(const HoveredItemInfo& info) const
{
    return info.typeKey == QStringLiteral("qml_shell");
}

QWidget* QmlShellRenderer::widget()
{
    return this;
}

void QmlShellRenderer::load(const HoveredItemInfo& info)
{
    m_quickWidget->rootContext()->setContextProperty(QStringLiteral("previewData"), previewDataForInfo(info));
    if (!m_quickWidget->source().isValid()) {
        m_quickWidget->setSource(QUrl(QStringLiteral("qrc:/SPACELOOK/viewers/QuickPageViewer.qml")));
    }
}

void QmlShellRenderer::unload()
{
}

void QmlShellRenderer::openSettingsWindow()
{
    if (SpaceLookWindow* previewWindow = qobject_cast<SpaceLookWindow*>(window())) {
        previewWindow->showSettingsWindow();
    }
}

void QmlShellRenderer::openSettingsPage()
{
    m_quickWidget->setSource(QUrl(QStringLiteral("qrc:/SPACELOOK/viewers/SettingsPage.qml")));
}
