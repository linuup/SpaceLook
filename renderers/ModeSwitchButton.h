#pragma once

#include <functional>

#include <QFont>
#include <QVector>
#include <QWidget>

class QMenu;
class QToolButton;

class ModeSwitchButton : public QWidget
{
public:
    explicit ModeSwitchButton(QWidget* parent = nullptr);
    ~ModeSwitchButton() override;

    void setCurrentModeId(const QString& modeId);
    QString currentModeId() const;
    void setModeChangedCallback(std::function<void(const QString&)> callback);
    void setMenuFont(const QFont& font);

private:
    struct ModeEntry
    {
        QString modeId;
        QString displayText;
    };

    void rebuildMenu();
    void updatePrimaryButton();
    void selectMode(int index, bool emitChange);
    void showMenu();

    QVector<ModeEntry> m_modes;
    int m_currentModeIndex = 0;
    std::function<void(const QString&)> m_modeChangedCallback;
    QToolButton* m_primaryButton = nullptr;
    QToolButton* m_expandButton = nullptr;
    QMenu* m_menu = nullptr;
};
