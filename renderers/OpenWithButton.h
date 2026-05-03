#pragma once

#include <functional>

#include <QIcon>
#include <QStringList>
#include <QVector>
#include <QWidget>

class QAction;
class QMenu;
class QToolButton;

class OpenWithButton : public QWidget
{
public:
    explicit OpenWithButton(QWidget* parent = nullptr);
    ~OpenWithButton() override;

    bool hasAvailableHandlers() const;
    void setTargetPath(const QString& filePath);
    void setTargetContext(const QString& filePath, const QString& typeKey);
    void setStatusCallback(std::function<void(const QString&)> callback);
    void setLaunchSuccessCallback(std::function<void()> callback);
    void showMenuAtGlobalPos(const QPoint& globalPos);

private:
    enum class EntryKind
    {
        DirectCommand,
        ExplorerLocation
    };

    struct HandlerEntry
    {
        QString displayName;
        QIcon icon;
        bool recommended = false;
        EntryKind kind = EntryKind::DirectCommand;
        QString executablePath;
        QStringList arguments;
    };

    void clearHandlers();
    void refreshHandlers();
    bool containsHandlerName(const QString& displayName) const;
    QString entryKindName(EntryKind kind) const;
    void logEntry(const QString& prefix, const HandlerEntry& entry) const;
    void rebuildMenu();
    void updatePrimaryButton();
    void selectHandler(int index, bool launchAfterSelect);
    void launchCurrentHandler();
    bool launchHandler(int index);
    bool launchDirectEntry(const HandlerEntry& entry);
    QString preferenceScopeKey() const;
    QString handlerPreferenceKey() const;
    QString handlerIdentity(const HandlerEntry& entry) const;
    int preferredHandlerIndex() const;
    void persistSelectedHandler(const HandlerEntry& entry) const;
    void reportStatus(const QString& message) const;

    QString m_targetPath;
    QString m_targetTypeKey;
    QVector<HandlerEntry> m_handlers;
    int m_currentHandlerIndex = -1;
    std::function<void(const QString&)> m_statusCallback;
    std::function<void()> m_launchSuccessCallback;
    QToolButton* m_primaryButton = nullptr;
    QToolButton* m_expandButton = nullptr;
    QMenu* m_menu = nullptr;
};
