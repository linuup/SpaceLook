#include "renderers/PreviewStateVisuals.h"

#include <QEvent>
#include <QLabel>
#include <QProgressBar>
#include <QStyle>
#include <QVariant>
#include <QWidget>

namespace {

QString kindName(PreviewStateVisuals::Kind kind)
{
    switch (kind) {
    case PreviewStateVisuals::Kind::Loading:
        return QStringLiteral("loading");
    case PreviewStateVisuals::Kind::Error:
        return QStringLiteral("error");
    case PreviewStateVisuals::Kind::Empty:
        return QStringLiteral("empty");
    case PreviewStateVisuals::Kind::Success:
        return QStringLiteral("success");
    case PreviewStateVisuals::Kind::Info:
    case PreviewStateVisuals::Kind::Automatic:
    default:
        return QStringLiteral("info");
    }
}

struct StatePalette
{
    QString background;
    QString border;
    QString title;
    QString body;
    QString accent;
};

StatePalette paletteForKind(PreviewStateVisuals::Kind kind)
{
    switch (kind) {
    case PreviewStateVisuals::Kind::Loading:
        return {
            QStringLiteral("rgba(236, 246, 255, 0.96)"),
            QStringLiteral("rgba(160, 201, 242, 0.95)"),
            QStringLiteral("#174a7c"),
            QStringLiteral("#43627f"),
            QStringLiteral("#0078d4")
        };
    case PreviewStateVisuals::Kind::Error:
        return {
            QStringLiteral("rgba(255, 244, 242, 0.98)"),
            QStringLiteral("rgba(242, 178, 174, 0.96)"),
            QStringLiteral("#8f1d1d"),
            QStringLiteral("#6f3b38"),
            QStringLiteral("#d13438")
        };
    case PreviewStateVisuals::Kind::Empty:
        return {
            QStringLiteral("rgba(247, 250, 253, 0.98)"),
            QStringLiteral("rgba(211, 223, 235, 0.98)"),
            QStringLiteral("#36536f"),
            QStringLiteral("#63788e"),
            QStringLiteral("#6b7f95")
        };
    case PreviewStateVisuals::Kind::Success:
        return {
            QStringLiteral("rgba(241, 250, 244, 0.98)"),
            QStringLiteral("rgba(170, 218, 184, 0.95)"),
            QStringLiteral("#0f5f2b"),
            QStringLiteral("#496a53"),
            QStringLiteral("#107c10")
        };
    case PreviewStateVisuals::Kind::Info:
    case PreviewStateVisuals::Kind::Automatic:
    default:
        return {
            QStringLiteral("rgba(239, 246, 253, 0.98)"),
            QStringLiteral("rgba(188, 211, 234, 0.96)"),
            QStringLiteral("#1f4f7a"),
            QStringLiteral("#526b85"),
            QStringLiteral("#0f6cbd")
        };
    }
}

void polish(QWidget* widget)
{
    if (!widget) {
        return;
    }

    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

QString statusStyle(PreviewStateVisuals::Kind kind)
{
    const StatePalette palette = paletteForKind(kind);
    return QStringLiteral(
        "QLabel {"
        "  color: %1;"
        "  background: %2;"
        "  border: 1px solid %3;"
        "  border-left: 4px solid %4;"
        "  border-radius: 12px;"
        "  padding: 9px 13px;"
        "  font-family: 'Segoe UI Rounded';"
        "  font-size: 13px;"
        "  selection-background-color: rgba(0, 120, 212, 0.24);"
        "}")
        .arg(palette.title, palette.background, palette.border, palette.accent);
}

QString cardStyle(PreviewStateVisuals::Kind kind)
{
    const StatePalette palette = paletteForKind(kind);
    return QStringLiteral(
        "QWidget {"
        "  background: %1;"
        "  border: 1px solid %2;"
        "  border-radius: 18px;"
        "}"
        "QLabel {"
        "  background: transparent;"
        "  border: none;"
        "}")
        .arg(palette.background, palette.border);
}

QString progressStyle(PreviewStateVisuals::Kind kind)
{
    const StatePalette palette = paletteForKind(kind);
    return QStringLiteral(
        "QProgressBar {"
        "  background: rgba(214, 226, 238, 0.86);"
        "  border: none;"
        "  border-radius: 2px;"
        "}"
        "QProgressBar::chunk {"
        "  background: %1;"
        "  border-radius: 2px;"
        "}")
        .arg(palette.accent);
}

class StatusAutoStyler : public QObject
{
public:
    explicit StatusAutoStyler(QLabel* label)
        : QObject(label)
    {
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event && event->type() == QEvent::Show) {
            if (auto* label = qobject_cast<QLabel*>(watched)) {
                const auto kind = PreviewStateVisuals::inferKind(label->text());
                label->setProperty("previewStateKind", kindName(kind));
                label->setStyleSheet(statusStyle(kind));
                polish(label);
            }
        }
        return QObject::eventFilter(watched, event);
    }
};

QString htmlColorForKind(PreviewStateVisuals::Kind kind, bool title)
{
    const StatePalette palette = paletteForKind(kind);
    return title ? palette.title : palette.body;
}

}

namespace PreviewStateVisuals
{

Kind inferKind(const QString& message)
{
    const QString lower = message.trimmed().toLower();
    if (lower.isEmpty()) {
        return Kind::Info;
    }

    if (lower.contains(QStringLiteral("loading")) ||
        lower.contains(QStringLiteral("opening")) ||
        lower.contains(QStringLiteral("preparing")) ||
        lower.contains(QStringLiteral("rendering")) ||
        lower.contains(QStringLiteral("parsing"))) {
        return Kind::Loading;
    }

    if (lower.contains(QStringLiteral("empty")) ||
        lower.contains(QStringLiteral("no readable")) ||
        lower.contains(QStringLiteral("does not contain readable"))) {
        return Kind::Empty;
    }

    if (lower.contains(QStringLiteral("failed")) ||
        lower.contains(QStringLiteral("unavailable")) ||
        lower.contains(QStringLiteral("could not")) ||
        lower.contains(QStringLiteral("error")) ||
        lower.contains(QStringLiteral("timed out")) ||
        lower.contains(QStringLiteral("unsupported")) ||
        lower.contains(QStringLiteral("missing"))) {
        return Kind::Error;
    }

    if (lower.contains(QStringLiteral("loaded")) ||
        lower.contains(QStringLiteral("ready")) ||
        lower.contains(QStringLiteral("success"))) {
        return Kind::Success;
    }

    return Kind::Info;
}

void prepareStatusLabel(QLabel* label)
{
    if (!label) {
        return;
    }

    if (!label->property("previewStateVisualsPrepared").toBool()) {
        label->installEventFilter(new StatusAutoStyler(label));
        label->setProperty("previewStateVisualsPrepared", true);
    }

    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setStyleSheet(statusStyle(inferKind(label->text())));
}

void showStatus(QLabel* label, const QString& message, Kind kind)
{
    if (!label) {
        return;
    }

    if (message.trimmed().isEmpty()) {
        clearStatus(label);
        return;
    }

    const Kind resolvedKind = kind == Kind::Automatic ? inferKind(message) : kind;
    prepareStatusLabel(label);
    label->setProperty("previewStateKind", kindName(resolvedKind));
    label->setText(message);
    label->setStyleSheet(statusStyle(resolvedKind));
    polish(label);
    label->show();
}

void clearStatus(QLabel* label)
{
    if (!label) {
        return;
    }

    label->clear();
    label->hide();
}

void prepareStateCard(QWidget* card,
                      QLabel* titleLabel,
                      QLabel* messageLabel,
                      QProgressBar* progressBar,
                      Kind kind)
{
    if (card) {
        card->setProperty("previewStateKind", kindName(kind));
        card->setStyleSheet(cardStyle(kind));
        polish(card);
    }

    const StatePalette palette = paletteForKind(kind);
    if (titleLabel) {
        titleLabel->setStyleSheet(QStringLiteral(
            "QLabel {"
            "  color: %1;"
            "  background: transparent;"
            "  border: none;"
            "  font-family: 'Segoe UI Rounded';"
            "  font-size: 18px;"
            "  font-weight: 700;"
            "}").arg(palette.title));
    }

    if (messageLabel) {
        messageLabel->setWordWrap(true);
        messageLabel->setStyleSheet(QStringLiteral(
            "QLabel {"
            "  color: %1;"
            "  background: transparent;"
            "  border: none;"
            "  font-family: 'Segoe UI Rounded';"
            "  font-size: 13px;"
            "}").arg(palette.body));
    }

    if (progressBar) {
        progressBar->setStyleSheet(progressStyle(kind));
    }
}

void setStateCard(QWidget* card,
                  QLabel* titleLabel,
                  QLabel* messageLabel,
                  const QString& title,
                  const QString& message,
                  Kind kind)
{
    prepareStateCard(card, titleLabel, messageLabel, nullptr, kind);
    if (titleLabel) {
        titleLabel->setText(title);
    }
    if (messageLabel) {
        messageLabel->setText(message);
    }
}

QString htmlStatePage(const QString& title, const QString& message, Kind kind)
{
    const StatePalette palette = paletteForKind(kind);
    return QStringLiteral(
        "<!doctype html><html><head><meta charset=\"utf-8\">"
        "<style>"
        "html,body{margin:0;width:100%%;height:100%%;font-family:'Segoe UI Rounded','Segoe UI',sans-serif;background:#f6f9fc;color:%4;}"
        ".wrap{box-sizing:border-box;min-height:100%%;display:flex;align-items:center;justify-content:center;padding:28px;}"
        ".card{max-width:560px;width:100%%;box-sizing:border-box;background:%1;border:1px solid %2;border-left:4px solid %5;border-radius:18px;padding:24px 26px;box-shadow:0 18px 42px rgba(31,79,122,.10);}"
        "h1{margin:0 0 10px 0;font-size:20px;line-height:1.28;color:%3;font-weight:700;}"
        "p{margin:0;font-size:14px;line-height:1.65;color:%4;white-space:pre-wrap;overflow-wrap:anywhere;}"
        "</style></head><body><div class=\"wrap\"><div class=\"card\"><h1>%6</h1><p>%7</p></div></div></body></html>")
        .arg(palette.background,
             palette.border,
             htmlColorForKind(kind, true),
             htmlColorForKind(kind, false),
             palette.accent,
             title.toHtmlEscaped(),
             message.toHtmlEscaped());
}

}
