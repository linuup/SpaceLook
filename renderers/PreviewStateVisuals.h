#pragma once

#include <QString>

class QLabel;
class QProgressBar;
class QWidget;

namespace PreviewStateVisuals
{
enum class Kind
{
    Automatic,
    Loading,
    Error,
    Empty,
    Info,
    Success
};

Kind inferKind(const QString& message);
void prepareStatusLabel(QLabel* label);
void showStatus(QLabel* label, const QString& message, Kind kind = Kind::Automatic);
void clearStatus(QLabel* label);
void prepareStateCard(QWidget* card,
                      QLabel* titleLabel,
                      QLabel* messageLabel,
                      QProgressBar* progressBar,
                      Kind kind);
void setStateCard(QWidget* card,
                  QLabel* titleLabel,
                  QLabel* messageLabel,
                  const QString& title,
                  const QString& message,
                  Kind kind);
QString htmlStatePage(const QString& title, const QString& message, Kind kind);
}
