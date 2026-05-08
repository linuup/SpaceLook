#include "renderers/image/BaiduOcrService.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDateTime>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QSize>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QVariant>

namespace {

constexpr int kNetworkTimeoutMs = 20000;
constexpr int kMaxOcrImageSide = 3000;
constexpr int kMaxBase64ImageChars = 3500 * 1024;

struct NetworkResponse
{
    bool success = false;
    int httpStatus = 0;
    QByteArray body;
    QString errorMessage;
};

struct TokenResponse
{
    bool success = false;
    QString accessToken;
    qint64 expiresInSeconds = 0;
    QString errorMessage;
};

struct PreparedOcrImage
{
    QByteArray bytes;
    double scaleX = 1.0;
    double scaleY = 1.0;
};

QMutex g_tokenMutex;
QString g_cachedAccessToken;
QString g_cachedCredentialKey;
QDateTime g_cachedTokenExpiryUtc;

QString credentialsKey(const QString& apiKey, const QString& secretKey)
{
    return apiKey + QChar::LineFeed + secretKey;
}

NetworkResponse postFormBody(QNetworkAccessManager& manager, const QUrl& url, const QByteArray& body)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);

    QNetworkReply* reply = manager.post(request, body);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, [&loop, reply]() {
        if (reply) {
            reply->abort();
        }
        loop.quit();
    });

    timer.start(kNetworkTimeoutMs);
    loop.exec();

    NetworkResponse response;
    response.httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    response.body = reply->readAll();
    if (reply->error() == QNetworkReply::NoError) {
        response.success = true;
    } else if (timer.isActive()) {
        response.errorMessage = reply->errorString();
    } else {
        response.errorMessage = QCoreApplication::translate("SpaceLook", "Baidu OCR request timed out.");
    }
    reply->deleteLater();
    return response;
}

NetworkResponse postForm(QNetworkAccessManager& manager, const QUrl& url, const QUrlQuery& form)
{
    return postFormBody(manager, url, form.toString(QUrl::FullyEncoded).toUtf8());
}

QString jsonErrorMessage(const QJsonObject& object)
{
    const int errorCode = object.value(QStringLiteral("error_code")).toInt();
    const QString errorDescription = object.value(QStringLiteral("error_description")).toString().trimmed();
    if (!errorDescription.isEmpty()) {
        if (errorCode != 0) {
            return QCoreApplication::translate("SpaceLook", "Baidu OCR error %1: %2").arg(errorCode).arg(errorDescription);
        }
        return errorDescription;
    }

    const QString errorMessage = object.value(QStringLiteral("error_msg")).toString().trimmed();
    if (!errorMessage.isEmpty()) {
        if (errorCode != 0) {
            return QCoreApplication::translate("SpaceLook", "Baidu OCR error %1: %2").arg(errorCode).arg(errorMessage);
        }
        return errorMessage;
    }

    const QString error = object.value(QStringLiteral("error")).toString().trimmed();
    if (!error.isEmpty()) {
        if (errorCode != 0) {
            return QCoreApplication::translate("SpaceLook", "Baidu OCR error %1: %2").arg(errorCode).arg(error);
        }
        return error;
    }

    if (errorCode != 0) {
        return QCoreApplication::translate("SpaceLook", "Baidu OCR error %1.").arg(errorCode);
    }

    return QString();
}

TokenResponse requestAccessToken(QNetworkAccessManager& manager, const QString& apiKey, const QString& secretKey)
{
    {
        QMutexLocker locker(&g_tokenMutex);
        if (!g_cachedAccessToken.isEmpty() &&
            g_cachedCredentialKey == credentialsKey(apiKey, secretKey) &&
            g_cachedTokenExpiryUtc > QDateTime::currentDateTimeUtc().addSecs(60)) {
            return { true, g_cachedAccessToken, g_cachedTokenExpiryUtc.toSecsSinceEpoch(), QString() };
        }
    }

    QUrlQuery form;
    form.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("client_credentials"));
    form.addQueryItem(QStringLiteral("client_id"), apiKey);
    form.addQueryItem(QStringLiteral("client_secret"), secretKey);

    const NetworkResponse response = postForm(manager, QUrl(QStringLiteral("https://aip.baidubce.com/oauth/2.0/token")), form);
    if (!response.success) {
        return { false, QString(), 0, response.errorMessage };
    }

    const QJsonDocument document = QJsonDocument::fromJson(response.body);
    if (!document.isObject()) {
        return { false, QString(), 0, QCoreApplication::translate("SpaceLook", "Baidu token response is invalid.") };
    }

    const QJsonObject object = document.object();
    const QString token = object.value(QStringLiteral("access_token")).toString().trimmed();
    if (token.isEmpty()) {
        const QString error = jsonErrorMessage(object);
        return { false, QString(), 0, error.isEmpty()
            ? QCoreApplication::translate("SpaceLook", "Baidu token request failed.")
            : error };
    }

    const qint64 expiresIn = qMax<qint64>(60, object.value(QStringLiteral("expires_in")).toVariant().toLongLong());
    {
        QMutexLocker locker(&g_tokenMutex);
        g_cachedAccessToken = token;
        g_cachedCredentialKey = credentialsKey(apiKey, secretKey);
        g_cachedTokenExpiryUtc = QDateTime::currentDateTimeUtc().addSecs(expiresIn);
    }

    return { true, token, expiresIn, QString() };
}

QByteArray imageToJpegBytes(const QImage& sourceImage, int quality)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly)) {
        return QByteArray();
    }

    sourceImage.save(&buffer, "JPEG", quality);
    return bytes;
}

int base64CharCount(int byteCount)
{
    return ((byteCount + 2) / 3) * 4;
}

QByteArray percentEncodedBase64(const QByteArray& bytes)
{
    return QUrl::toPercentEncoding(QString::fromLatin1(bytes.toBase64()));
}

bool fitsBaiduImagePayloadLimit(const QByteArray& bytes)
{
    return !bytes.isEmpty() &&
        base64CharCount(bytes.size()) <= kMaxBase64ImageChars &&
        percentEncodedBase64(bytes).size() <= kMaxBase64ImageChars;
}

QByteArray baiduImageRequestBody(const PreparedOcrImage& preparedImage)
{
    QByteArray body = QByteArrayLiteral("image=");
    body += percentEncodedBase64(preparedImage.bytes);
    body += QByteArrayLiteral("&vertexes_location=true");
    return body;
}

QImage flattenedRgbImage(const QImage& sourceImage)
{
    QImage image(sourceImage.size(), QImage::Format_RGB888);
    image.fill(Qt::white);

    QPainter painter(&image);
    painter.drawImage(QPoint(0, 0), sourceImage);
    painter.end();
    return image;
}

PreparedOcrImage prepareOcrImage(const QImage& sourceImage)
{
    PreparedOcrImage prepared;
    if (sourceImage.isNull()) {
        return prepared;
    }

    QImage image = flattenedRgbImage(sourceImage);
    const QSize originalSize = image.size();
    if (qMax(image.width(), image.height()) > kMaxOcrImageSide) {
        image = image.scaled(QSize(kMaxOcrImageSide, kMaxOcrImageSide),
                             Qt::KeepAspectRatio,
                             Qt::SmoothTransformation);
    }

    const int qualities[] = { 92, 85, 75, 65, 55 };
    for (int quality : qualities) {
        prepared.bytes = imageToJpegBytes(image, quality);
        if (fitsBaiduImagePayloadLimit(prepared.bytes)) {
            prepared.scaleX = static_cast<double>(image.width()) / static_cast<double>(originalSize.width());
            prepared.scaleY = static_cast<double>(image.height()) / static_cast<double>(originalSize.height());
            return prepared;
        }
    }

    while (!image.isNull() &&
           !fitsBaiduImagePayloadLimit(prepared.bytes) &&
           image.width() > 64 &&
           image.height() > 64) {
        image = image.scaled(QSize(qMax(64, qRound(image.width() * 0.85)),
                                   qMax(64, qRound(image.height() * 0.85))),
                             Qt::KeepAspectRatio,
                             Qt::SmoothTransformation);
        prepared.bytes = imageToJpegBytes(image, 65);
    }

    if (fitsBaiduImagePayloadLimit(prepared.bytes)) {
        prepared.scaleX = static_cast<double>(image.width()) / static_cast<double>(originalSize.width());
        prepared.scaleY = static_cast<double>(image.height()) / static_cast<double>(originalSize.height());
    } else {
        prepared.bytes.clear();
    }
    return prepared;
}

QRectF sourceImageRectFromPreparedRect(const QRectF& preparedRect, const PreparedOcrImage& prepared)
{
    if (prepared.scaleX <= 0.0 || prepared.scaleY <= 0.0) {
        return preparedRect;
    }

    return QRectF(preparedRect.left() / prepared.scaleX,
                  preparedRect.top() / prepared.scaleY,
                  preparedRect.width() / prepared.scaleX,
                  preparedRect.height() / prepared.scaleY);
}

}

OcrResult BaiduOcrService::recognizeText(const QImage& sourceImage, const QString& apiKey, const QString& secretKey)
{
    OcrResult result;
    if (sourceImage.isNull()) {
        result.errorMessage = QCoreApplication::translate("SpaceLook", "Image is unavailable for OCR.");
        return result;
    }

    if (apiKey.trimmed().isEmpty() || secretKey.trimmed().isEmpty()) {
        result.errorMessage = QCoreApplication::translate("SpaceLook", "Baidu OCR requires API_KEY and SECRET_KEY.");
        return result;
    }

    const PreparedOcrImage preparedImage = prepareOcrImage(sourceImage);
    if (preparedImage.bytes.isEmpty()) {
        result.errorMessage = QCoreApplication::translate("SpaceLook", "Failed to prepare image for OCR.");
        return result;
    }

    QNetworkAccessManager manager;
    const TokenResponse token = requestAccessToken(manager, apiKey.trimmed(), secretKey.trimmed());
    if (!token.success) {
        result.errorMessage = token.errorMessage.trimmed().isEmpty()
            ? QCoreApplication::translate("SpaceLook", "Baidu token request failed.")
            : token.errorMessage;
        return result;
    }

    QUrl url(QStringLiteral("https://aip.baidubce.com/rest/2.0/ocr/v1/general"));
    QUrlQuery urlQuery;
    urlQuery.addQueryItem(QStringLiteral("access_token"), token.accessToken);
    url.setQuery(urlQuery);

    const NetworkResponse response = postFormBody(manager, url, baiduImageRequestBody(preparedImage));
    if (!response.success) {
        result.errorMessage = response.errorMessage;
        return result;
    }

    const QJsonDocument document = QJsonDocument::fromJson(response.body);
    if (!document.isObject()) {
        result.errorMessage = QCoreApplication::translate("SpaceLook", "Baidu OCR response is invalid.");
        return result;
    }

    const QJsonObject object = document.object();
    const QString error = jsonErrorMessage(object);
    if (!error.isEmpty()) {
        result.errorMessage = error;
        return result;
    }

    QStringList lines;
    const QJsonArray wordsResult = object.value(QStringLiteral("words_result")).toArray();
    for (const QJsonValue& value : wordsResult) {
        const QJsonObject wordObject = value.toObject();
        const QString text = wordObject.value(QStringLiteral("words")).toString().trimmed();
        if (text.isEmpty()) {
            continue;
        }

        lines.push_back(text);
        const QJsonObject location = wordObject.value(QStringLiteral("location")).toObject();
        const double left = location.value(QStringLiteral("left")).toDouble();
        const double top = location.value(QStringLiteral("top")).toDouble();
        const double width = location.value(QStringLiteral("width")).toDouble();
        const double height = location.value(QStringLiteral("height")).toDouble();
        if (width > 0.0 && height > 0.0) {
            result.boxes.push_back({ text, sourceImageRectFromPreparedRect(QRectF(left, top, width, height), preparedImage) });
        }
    }

    result.text = lines.join(QChar::LineFeed).trimmed();
    result.success = true;
    return result;
}
