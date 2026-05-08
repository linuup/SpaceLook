#include "renderers/certificate/CertificateRenderer.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <QCoreApplication>
#include <wincrypt.h>

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QInputDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "core/PreviewFileReader.h"
#include "renderers/FileTypeIconResolver.h"
#include "renderers/OpenWithButton.h"
#include "renderers/PreviewHeaderBar.h"
#include "renderers/PreviewStateVisuals.h"
#include "renderers/SelectableTitleLabel.h"
#include "widgets/SpaceLookWindow.h"

namespace {

QString displayValue(const QString& value, const QString& fallback = QStringLiteral("Unavailable"))
{
    const QString trimmed = value.trimmed();
    return trimmed.isEmpty() ? fallback : trimmed;
}

QString formatBytes(const QByteArray& bytes, bool reverse = false)
{
    QStringList parts;
    if (reverse) {
        for (int index = bytes.size() - 1; index >= 0; --index) {
            parts.append(QStringLiteral("%1").arg(static_cast<unsigned char>(bytes.at(index)), 2, 16, QLatin1Char('0')).toUpper());
        }
    } else {
        for (char byte : bytes) {
            parts.append(QStringLiteral("%1").arg(static_cast<unsigned char>(byte), 2, 16, QLatin1Char('0')).toUpper());
        }
    }
    return parts.join(QLatin1Char(' '));
}

QString formatSystemTime(const FILETIME& fileTime)
{
    SYSTEMTIME utcTime = {};
    SYSTEMTIME localTime = {};
    if (!FileTimeToSystemTime(&fileTime, &utcTime) ||
        !SystemTimeToTzSpecificLocalTime(nullptr, &utcTime, &localTime)) {
        return QCoreApplication::translate("SpaceLook", "Unavailable");
    }

    const QDate date(localTime.wYear, localTime.wMonth, localTime.wDay);
    const QTime time(localTime.wHour, localTime.wMinute, localTime.wSecond);
    return QDateTime(date, time).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

QString certName(PCCERT_CONTEXT context, DWORD flags)
{
    const DWORD length = CertGetNameStringW(
        context,
        CERT_NAME_SIMPLE_DISPLAY_TYPE,
        flags,
        nullptr,
        nullptr,
        0);
    if (length <= 1) {
        return QString();
    }

    QVector<wchar_t> buffer(static_cast<int>(length));
    CertGetNameStringW(
        context,
        CERT_NAME_SIMPLE_DISPLAY_TYPE,
        flags,
        nullptr,
        buffer.data(),
        length);
    return QString::fromWCharArray(buffer.data()).trimmed();
}

QString oidName(const char* oid)
{
    if (!oid) {
        return QString();
    }

    PCCRYPT_OID_INFO oidInfo = CryptFindOIDInfo(CRYPT_OID_INFO_OID_KEY, const_cast<char*>(oid), 0);
    if (oidInfo && oidInfo->pwszName) {
        return QString::fromWCharArray(oidInfo->pwszName);
    }

    return QString::fromLatin1(oid);
}

QString lastWindowsError()
{
    const DWORD errorCode = GetLastError();
    if (!errorCode) {
        return QCoreApplication::translate("SpaceLook", "Unknown error.");
    }

    wchar_t* messageBuffer = nullptr;
    const DWORD size = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        errorCode,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&messageBuffer),
        0,
        nullptr);

    QString message = size > 0 && messageBuffer
        ? QString::fromWCharArray(messageBuffer).trimmed()
        : QCoreApplication::translate("SpaceLook", "Windows error %1").arg(errorCode);
    if (messageBuffer) {
        LocalFree(messageBuffer);
    }
    return message;
}

QString firstNonEmptyLine(const QByteArray& data)
{
    const QList<QByteArray> lines = data.split('\n');
    for (const QByteArray& line : lines) {
        const QString text = QString::fromUtf8(line).trimmed();
        if (!text.isEmpty()) {
            return text;
        }
    }
    return QString();
}

}

CertificateRenderer::CertificateRenderer(QWidget* parent)
    : QWidget(parent)
    , m_headerRow(new QWidget(this))
    , m_iconLabel(new QLabel(this))
    , m_titleLabel(new SelectableTitleLabel(this))
    , m_pathRow(new QWidget(this))
    , m_pathValueLabel(new QLabel(this))
    , m_openWithButton(new OpenWithButton(this))
    , m_statusLabel(new QLabel(this))
    , m_unlockButton(new QPushButton(this))
    , m_scrollArea(new QScrollArea(this))
    , m_detailContent(new QWidget(this))
    , m_detailLayout(new QVBoxLayout(m_detailContent))
{
    setAttribute(Qt::WA_StyledBackground, true);
    setObjectName(QStringLiteral("CertificateRendererRoot"));
    m_headerRow->setObjectName(QStringLiteral("CertificateHeaderRow"));
    m_iconLabel->setObjectName(QStringLiteral("CertificateTypeIcon"));
    m_titleLabel->setObjectName(QStringLiteral("CertificateTitle"));
    m_pathRow->setObjectName(QStringLiteral("CertificatePathRow"));
    m_pathValueLabel->setObjectName(QStringLiteral("CertificatePathValue"));
    m_openWithButton->setObjectName(QStringLiteral("CertificateOpenWithButton"));
    m_statusLabel->setObjectName(QStringLiteral("CertificateStatus"));
    m_unlockButton->setObjectName(QStringLiteral("CertificateUnlockButton"));
    m_scrollArea->setObjectName(QStringLiteral("CertificateScrollArea"));
    m_detailContent->setObjectName(QStringLiteral("CertificateDetailContent"));

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(12, 0, 12, 12);
    rootLayout->setSpacing(14);
    rootLayout->addWidget(m_headerRow);
    rootLayout->addWidget(m_statusLabel);
    rootLayout->addWidget(m_unlockButton, 0, Qt::AlignLeft);
    rootLayout->addWidget(m_scrollArea, 1);

    auto* headerLayout = new QHBoxLayout(m_headerRow);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(12);
    auto* titleBlock = new PreviewHeaderBar(m_iconLabel, m_titleLabel, m_pathRow, m_openWithButton, m_headerRow);
    headerLayout->addWidget(titleBlock->contentWidget(), 1);

    auto* pathLayout = new QHBoxLayout(m_pathRow);
    pathLayout->setContentsMargins(0, 0, 0, 0);
    pathLayout->setSpacing(8);
    pathLayout->addWidget(m_pathValueLabel, 1);

    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setWidget(m_detailContent);
    m_detailLayout->setContentsMargins(18, 14, 18, 14);
    m_detailLayout->setSpacing(12);

    m_iconLabel->setFixedSize(72, 72);
    m_iconLabel->setScaledContents(false);
    m_iconLabel->setAlignment(Qt::AlignCenter);
    m_titleLabel->setWordWrap(true);
    m_pathValueLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_pathValueLabel->setWordWrap(true);
    m_pathValueLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    PreviewStateVisuals::prepareStatusLabel(m_statusLabel);
    m_statusLabel->hide();
    m_unlockButton->setText(QCoreApplication::translate("SpaceLook", "Unlock with password"));
    m_unlockButton->setCursor(Qt::PointingHandCursor);
    m_unlockButton->hide();

    m_openWithButton->setStatusCallback([this](const QString& message) {
        showStatusMessage(message);
    });
    m_openWithButton->setLaunchSuccessCallback([this]() {
        if (SpaceLookWindow* previewWindow = qobject_cast<SpaceLookWindow*>(window())) {
            previewWindow->hidePreview();
        }
    });
    connect(titleBlock->closeButton(), &QToolButton::clicked, this, [this]() {
        if (SpaceLookWindow* previewWindow = qobject_cast<SpaceLookWindow*>(window())) {
            previewWindow->hidePreview();
        }
    });
    connect(m_titleLabel, &SelectableTitleLabel::copyFeedbackRequested, this, [this](const QString& message) {
        showStatusMessage(message);
    });
    connect(m_unlockButton, &QPushButton::clicked, this, [this]() {
        unlockCurrentFile();
    });

    applyChrome();
}

QString CertificateRenderer::rendererId() const
{
    return QStringLiteral("certificate");
}

bool CertificateRenderer::canHandle(const HoveredItemInfo& info) const
{
    return info.typeKey == QStringLiteral("certificate");
}

QWidget* CertificateRenderer::widget()
{
    return this;
}

void CertificateRenderer::load(const HoveredItemInfo& info)
{
    m_info = info;
    qDebug().noquote() << QStringLiteral("[SpaceLookRender] CertificateRenderer load path=\"%1\"").arg(info.filePath);

    showStatusMessage(QString());
    m_unlockButton->hide();
    populateDetails({});

    m_titleLabel->setText(info.fileName.trimmed().isEmpty() ? QCoreApplication::translate("SpaceLook", "Certificate Preview") : info.fileName);
    m_titleLabel->setCopyText(m_titleLabel->text());
    m_iconLabel->setPixmap(FileTypeIconResolver::pixmapForInfo(info, m_iconLabel->contentsRect().size()));
    m_pathValueLabel->setText(info.filePath.trimmed().isEmpty() ? QCoreApplication::translate("SpaceLook", "(Unavailable)") : info.filePath);
    m_openWithButton->setTargetContext(info.filePath, info.typeKey);
    updateUnlockButtonVisibility();

    QString statusMessage;
    populateDetails(inspectCertificateFile(info, &statusMessage));
    showStatusMessage(statusMessage);
}

void CertificateRenderer::unload()
{
    m_info = HoveredItemInfo();
    m_pathValueLabel->clear();
    m_openWithButton->setTargetContext(QString(), QString());
    showStatusMessage(QString());
    m_unlockButton->hide();
    populateDetails({});
}

void CertificateRenderer::populateDetails(const QVector<Detail>& details)
{
    while (QLayoutItem* item = m_detailLayout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }

    for (const Detail& detail : details) {
        auto* row = new QWidget(m_detailContent);
        row->setObjectName(QStringLiteral("CertificateDetailRow"));
        auto* layout = new QVBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(5);

        auto* title = new QLabel(detail.title, row);
        title->setObjectName(QStringLiteral("CertificateDetailTitle"));

        auto* value = new QLabel(displayValue(detail.value), row);
        value->setObjectName(QStringLiteral("CertificateDetailValue"));
        value->setWordWrap(true);
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        value->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

        layout->addWidget(title);
        layout->addWidget(value);
        m_detailLayout->addWidget(row);
    }

    m_detailLayout->addStretch(1);
}

QVector<CertificateRenderer::Detail> CertificateRenderer::inspectCertificateFile(const HoveredItemInfo& info, QString* statusMessage) const
{
    QVector<Detail> details;
    const QFileInfo fileInfo(info.filePath);
    const QString suffix = fileInfo.suffix().toLower();
    details.append({QCoreApplication::translate("SpaceLook", "Format"), suffix.isEmpty() ? QCoreApplication::translate("SpaceLook", "Certificate or key file") : suffix.toUpper()});
    details.append({QCoreApplication::translate("SpaceLook", "Size"), QCoreApplication::translate("SpaceLook", "%1 bytes").arg(fileInfo.size())});

    QByteArray data;
    if (!PreviewFileReader::readAll(info.filePath, &data)) {
        if (statusMessage) {
            *statusMessage = QCoreApplication::translate("SpaceLook", "Failed to open certificate file for reading.");
        }
        return details;
    }

    if (suffix == QStringLiteral("pfx") || suffix == QStringLiteral("p12")) {
        CRYPT_DATA_BLOB blob;
        blob.cbData = static_cast<DWORD>(data.size());
        blob.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(data.constData()));
        details.append({QCoreApplication::translate("SpaceLook", "Container"), QCoreApplication::translate("SpaceLook", "PKCS#12 certificate bundle")});
        details.append({QCoreApplication::translate("SpaceLook", "Password status"), PFXIsPFXBlob(&blob)
            ? (PFXVerifyPassword(&blob, L"", 0) ? QCoreApplication::translate("SpaceLook", "No password or empty password") : QCoreApplication::translate("SpaceLook", "Password protected"))
            : QCoreApplication::translate("SpaceLook", "Invalid PKCS#12 data")});
        if (statusMessage) {
            *statusMessage = QCoreApplication::translate("SpaceLook", "PKCS#12 files can contain private keys. SpaceLook shows container metadata only.");
        }
        return details;
    }

    if (suffix == QStringLiteral("key") ||
        suffix == QStringLiteral("pub") ||
        suffix == QStringLiteral("asc") ||
        suffix == QStringLiteral("gpg")) {
        const QString firstLine = firstNonEmptyLine(data.left(8192));
        const QString lowerText = QString::fromUtf8(data.left(8192)).toLower();
        QString kind = QCoreApplication::translate("SpaceLook", "Key or signature material");
        if (lowerText.contains(QStringLiteral("private key"))) {
            kind = QCoreApplication::translate("SpaceLook", "Private key");
        } else if (lowerText.contains(QStringLiteral("public key")) || suffix == QStringLiteral("pub")) {
            kind = QCoreApplication::translate("SpaceLook", "Public key");
        } else if (lowerText.contains(QStringLiteral("pgp"))) {
            kind = QCoreApplication::translate("SpaceLook", "OpenPGP armored data");
        } else if (suffix == QStringLiteral("gpg")) {
            kind = QCoreApplication::translate("SpaceLook", "OpenPGP binary data");
        }
        details.append({QCoreApplication::translate("SpaceLook", "Detected kind"), kind});
        details.append({QCoreApplication::translate("SpaceLook", "Header"), displayValue(firstLine)});
        if (statusMessage) {
            *statusMessage = QCoreApplication::translate("SpaceLook", "Sensitive key contents are not rendered directly. Use Open with to inspect the full file.");
        }
        return details;
    }

    HCERTSTORE store = nullptr;
    HCRYPTMSG message = nullptr;
    const void* queryContext = nullptr;
    DWORD encodingType = 0;
    DWORD contentType = 0;
    DWORD formatType = 0;
    const QString nativePath = QDir::toNativeSeparators(fileInfo.absoluteFilePath());

    const BOOL queryOk = CryptQueryObject(
        CERT_QUERY_OBJECT_FILE,
        reinterpret_cast<const void*>(nativePath.utf16()),
        CERT_QUERY_CONTENT_FLAG_CERT |
            CERT_QUERY_CONTENT_FLAG_SERIALIZED_CERT |
            CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED |
            CERT_QUERY_CONTENT_FLAG_PKCS7_UNSIGNED |
            CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED,
        CERT_QUERY_FORMAT_FLAG_ALL,
        0,
        &encodingType,
        &contentType,
        &formatType,
        &store,
        &message,
        &queryContext);

    PCCERT_CONTEXT certContext = nullptr;
    if (queryOk) {
        if (queryContext && (contentType == CERT_QUERY_CONTENT_CERT || contentType == CERT_QUERY_CONTENT_SERIALIZED_CERT)) {
            certContext = CertDuplicateCertificateContext(reinterpret_cast<PCCERT_CONTEXT>(queryContext));
        } else if (store) {
            PCCERT_CONTEXT storeContext = CertEnumCertificatesInStore(store, nullptr);
            if (storeContext) {
                certContext = CertDuplicateCertificateContext(storeContext);
                CertFreeCertificateContext(storeContext);
            }
        }
    }

    if (!certContext) {
        if (queryContext && (contentType == CERT_QUERY_CONTENT_CERT || contentType == CERT_QUERY_CONTENT_SERIALIZED_CERT)) {
            CertFreeCertificateContext(reinterpret_cast<PCCERT_CONTEXT>(const_cast<void*>(queryContext)));
        }
        if (message) {
            CryptMsgClose(message);
        }
        if (store) {
            CertCloseStore(store, 0);
        }
        if (statusMessage) {
            *statusMessage = QCoreApplication::translate("SpaceLook", "Certificate metadata could not be parsed: %1").arg(lastWindowsError());
        }
        return details;
    }

    appendCertificateContextDetails(&details, certContext, QString());

    CertFreeCertificateContext(certContext);
    if (queryContext && (contentType == CERT_QUERY_CONTENT_CERT || contentType == CERT_QUERY_CONTENT_SERIALIZED_CERT)) {
        CertFreeCertificateContext(reinterpret_cast<PCCERT_CONTEXT>(const_cast<void*>(queryContext)));
    }
    if (message) {
        CryptMsgClose(message);
    }
    if (store) {
        CertCloseStore(store, 0);
    }

    if (statusMessage) {
        *statusMessage = QCoreApplication::translate("SpaceLook", "Certificate metadata loaded.");
    }
    return details;
}

void CertificateRenderer::appendCertificateContextDetails(QVector<Detail>* details,
                                                          const _CERT_CONTEXT* certContext,
                                                          const QString& prefix) const
{
    if (!details || !certContext) {
        return;
    }

    const QString labelPrefix = prefix.trimmed().isEmpty() ? QString() : prefix.trimmed() + QStringLiteral(" ");
    details->append({labelPrefix + QCoreApplication::translate("SpaceLook", "Detected kind"), QCoreApplication::translate("SpaceLook", "X.509 certificate")});
    details->append({labelPrefix + QCoreApplication::translate("SpaceLook", "Subject"), certName(certContext, 0)});
    details->append({labelPrefix + QCoreApplication::translate("SpaceLook", "Issuer"), certName(certContext, CERT_NAME_ISSUER_FLAG)});
    details->append({labelPrefix + QCoreApplication::translate("SpaceLook", "Valid from"), formatSystemTime(certContext->pCertInfo->NotBefore)});
    details->append({labelPrefix + QCoreApplication::translate("SpaceLook", "Valid until"), formatSystemTime(certContext->pCertInfo->NotAfter)});
    details->append({labelPrefix + QCoreApplication::translate("SpaceLook", "Serial number"), formatBytes(QByteArray(
        reinterpret_cast<const char*>(certContext->pCertInfo->SerialNumber.pbData),
        static_cast<int>(certContext->pCertInfo->SerialNumber.cbData)), true)});
    details->append({labelPrefix + QCoreApplication::translate("SpaceLook", "Signature algorithm"), oidName(certContext->pCertInfo->SignatureAlgorithm.pszObjId)});
    details->append({labelPrefix + QCoreApplication::translate("SpaceLook", "Public key algorithm"), oidName(certContext->pCertInfo->SubjectPublicKeyInfo.Algorithm.pszObjId)});

    DWORD hashSize = 0;
    if (CertGetCertificateContextProperty(certContext, CERT_HASH_PROP_ID, nullptr, &hashSize) && hashSize > 0) {
        QByteArray hash;
        hash.resize(static_cast<int>(hashSize));
        if (CertGetCertificateContextProperty(certContext, CERT_HASH_PROP_ID, hash.data(), &hashSize)) {
            details->append({labelPrefix + QCoreApplication::translate("SpaceLook", "SHA1 thumbprint"), formatBytes(hash)});
        }
    }

    DWORD keyInfoSize = 0;
    const bool hasPrivateKeyInfo = CertGetCertificateContextProperty(
        certContext,
        CERT_KEY_PROV_INFO_PROP_ID,
        nullptr,
        &keyInfoSize);
    details->append({labelPrefix + QCoreApplication::translate("SpaceLook", "Private key"), hasPrivateKeyInfo
        ? QCoreApplication::translate("SpaceLook", "Present in unlocked container")
        : QCoreApplication::translate("SpaceLook", "Not exposed by this certificate context")});
}

QVector<CertificateRenderer::Detail> CertificateRenderer::inspectPkcs12File(const QString& filePath,
                                                                             const QString& password,
                                                                             QString* statusMessage) const
{
    QVector<Detail> details;
    const QFileInfo fileInfo(filePath);
    details.append({QCoreApplication::translate("SpaceLook", "Format"), fileInfo.suffix().toUpper()});
    details.append({QCoreApplication::translate("SpaceLook", "Container"), QCoreApplication::translate("SpaceLook", "PKCS#12 certificate bundle")});

    QByteArray data;
    if (!PreviewFileReader::readAll(filePath, &data)) {
        if (statusMessage) {
            *statusMessage = QCoreApplication::translate("SpaceLook", "Failed to open PKCS#12 file for reading.");
        }
        return details;
    }

    CRYPT_DATA_BLOB blob;
    blob.cbData = static_cast<DWORD>(data.size());
    blob.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(data.constData()));
    if (!PFXIsPFXBlob(&blob)) {
        if (statusMessage) {
            *statusMessage = QCoreApplication::translate("SpaceLook", "This file is not a valid PKCS#12 container.");
        }
        return details;
    }

    const std::wstring nativePassword = password.toStdWString();
    if (!PFXVerifyPassword(&blob, nativePassword.c_str(), 0)) {
        if (statusMessage) {
            *statusMessage = QCoreApplication::translate("SpaceLook", "PKCS#12 password is incorrect or unsupported: %1").arg(lastWindowsError());
        }
        return details;
    }

    HCERTSTORE store = PFXImportCertStore(
        &blob,
        nativePassword.c_str(),
        PKCS12_NO_PERSIST_KEY | PKCS12_PREFER_CNG_KSP);
    if (!store) {
        if (statusMessage) {
            *statusMessage = QCoreApplication::translate("SpaceLook", "Failed to unlock PKCS#12 container: %1").arg(lastWindowsError());
        }
        return details;
    }

    int certificateIndex = 0;
    PCCERT_CONTEXT certContext = nullptr;
    while ((certContext = CertEnumCertificatesInStore(store, certContext)) != nullptr) {
        ++certificateIndex;
        appendCertificateContextDetails(
            &details,
            certContext,
            QCoreApplication::translate("SpaceLook", "Certificate %1").arg(certificateIndex));
    }

    CertCloseStore(store, 0);
    if (statusMessage) {
        *statusMessage = certificateIndex > 0
            ? QCoreApplication::translate("SpaceLook", "PKCS#12 container unlocked in memory. No certificate or key was written to the system store.")
            : QCoreApplication::translate("SpaceLook", "PKCS#12 container unlocked, but no certificate entry was found.");
    }
    return details;
}

void CertificateRenderer::updateUnlockButtonVisibility()
{
    const QString suffix = QFileInfo(m_info.filePath).suffix().toLower();
    const bool canUnlock = suffix == QStringLiteral("pfx") || suffix == QStringLiteral("p12");
    m_unlockButton->setVisible(canUnlock);
}

void CertificateRenderer::unlockCurrentFile()
{
    const QString filePath = m_info.filePath.trimmed();
    if (filePath.isEmpty()) {
        showStatusMessage(QCoreApplication::translate("SpaceLook", "No PKCS#12 file is loaded."));
        return;
    }

    bool accepted = false;
    const QString password = QInputDialog::getText(
        this,
        QCoreApplication::translate("SpaceLook", "Unlock PKCS#12"),
        QCoreApplication::translate("SpaceLook", "Enter certificate password"),
        QLineEdit::Password,
        QString(),
        &accepted);
    if (!accepted) {
        return;
    }

    QString statusMessage;
    populateDetails(inspectPkcs12File(filePath, password, &statusMessage));
    showStatusMessage(statusMessage);
}

void CertificateRenderer::showStatusMessage(const QString& message)
{
    if (message.trimmed().isEmpty()) {
        PreviewStateVisuals::clearStatus(m_statusLabel);
        return;
    }

    PreviewStateVisuals::showStatus(m_statusLabel, message);
    QTimer::singleShot(2200, m_statusLabel, [label = m_statusLabel]() {
        if (label) {
            PreviewStateVisuals::clearStatus(label);
        }
    });
}

void CertificateRenderer::applyChrome()
{
    setStyleSheet(
        "#CertificateRendererRoot {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:1,"
        "      stop:0 #fbfdff,"
        "      stop:1 #f2f7fb);"
        "  border-radius: 0px;"
        "}"
        "QLabel {"
        "  color: #18324a;"
        "}"
        "#CertificateTypeIcon {"
        "  background: transparent;"
        "  border: none;"
        "}"
        "#CertificateTitle {"
        "  color: #0f2740;"
        "}"
        "#CertificatePathValue {"
        "  color: #445d76;"
        "}"
        "#CertificateOpenWithButton QToolButton {"
        "  background: rgba(238, 244, 252, 0.96);"
        "  color: #17324b;"
        "  border: 1px solid rgba(193, 208, 224, 0.95);"
        "  padding: 3px 8px;"
        "  min-height: 24px;"
        "}"
        "#CertificateOpenWithButton #OpenWithPrimaryButton {"
        "  border-top-left-radius: 10px;"
        "  border-bottom-left-radius: 10px;"
        "  border-top-right-radius: 0px;"
        "  border-bottom-right-radius: 0px;"
        "}"
        "#CertificateOpenWithButton #OpenWithExpandButton {"
        "  border-left: none;"
        "  border-top-left-radius: 0px;"
        "  border-bottom-left-radius: 0px;"
        "  border-top-right-radius: 10px;"
        "  border-bottom-right-radius: 10px;"
        "}"
        "#CertificateStatus {"
        "  color: #27568b;"
        "  background: rgba(220, 235, 255, 0.92);"
        "  border: 1px solid rgba(164, 193, 229, 0.95);"
        "  border-radius: 12px;"
        "  padding: 10px 14px;"
        "}"
        "#CertificateUnlockButton {"
        "  background: #0f6cbd;"
        "  color: #ffffff;"
        "  border: 1px solid #0f6cbd;"
        "  border-radius: 10px;"
        "  padding: 8px 14px;"
        "  font-family: 'Segoe UI Rounded';"
        "  font-size: 13px;"
        "}"
        "#CertificateUnlockButton:hover {"
        "  background: #115ea3;"
        "  border-color: #115ea3;"
        "}"
        "#CertificateUnlockButton:pressed {"
        "  background: #0f548c;"
        "  border-color: #0f548c;"
        "}"
        "#CertificateScrollArea {"
        "  background: #ffffff;"
        "  border: 1px solid #d9e1ea;"
        "  border-radius: 22px;"
        "}"
        "#CertificateDetailContent {"
        "  background: transparent;"
        "}"
        "#CertificateDetailRow {"
        "  background: transparent;"
        "}"
        "#CertificateDetailTitle {"
        "  color: #577199;"
        "  font-family: 'Segoe UI Rounded';"
        "  font-size: 13px;"
        "  font-weight: 700;"
        "}"
        "#CertificateDetailValue {"
        "  color: #1e2c3b;"
        "  font-family: 'Segoe UI Rounded';"
        "  font-size: 15px;"
        "  selection-background-color: #cfe3ff;"
        "}"
    );

    QFont titleFont;
    titleFont.setFamily(QStringLiteral("Segoe UI Rounded"));
    titleFont.setPixelSize(20);
    titleFont.setWeight(QFont::Bold);
    m_titleLabel->setFont(titleFont);

    QFont textFont;
    textFont.setFamily(QStringLiteral("Segoe UI Rounded"));
    textFont.setPixelSize(13);
    m_pathValueLabel->setFont(textFont);
    m_statusLabel->setFont(textFont);
}
