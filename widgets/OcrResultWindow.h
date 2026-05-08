#pragma once

#include <QImage>
#include <QLabel>
#include <QWidget>

#include "renderers/image/OcrTypes.h"

class QPlainTextEdit;

class OcrResultImageView : public QLabel
{
    Q_OBJECT

public:
    explicit OcrResultImageView(QWidget* parent = nullptr);
    void setImage(const QImage& image);
    void setBoxes(const QVector<OcrTextBox>& boxes);

signals:
    void textCopied(const QString& text);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    QRectF mappedRect(const QRectF& imageRect) const;

    QImage m_image;
    QVector<OcrTextBox> m_boxes;
};

class OcrResultWindow : public QWidget
{
    Q_OBJECT

public:
    explicit OcrResultWindow(const QImage& image, QWidget* parent = nullptr);

    void setLoading();
    void setResult(const OcrResult& result);
    void setError(const QString& message);

private:
    void showFeedback(const QString& message);

    OcrResultImageView* m_imageView = nullptr;
    QPlainTextEdit* m_textEdit = nullptr;
    QLabel* m_statusLabel = nullptr;
    QLabel* m_textMetaLabel = nullptr;
};
