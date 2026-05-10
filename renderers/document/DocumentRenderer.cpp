#include "renderers/document/DocumentRenderer.h"

#include <algorithm>
#include <utility>

#include <QCryptographicHash>
#include <QDebug>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMap>
#include <QProcess>
#include <QRegularExpression>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTextBrowser>
#include <QTextStream>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVector>
#include <QXmlStreamReader>
#include <QtConcurrent/QtConcurrent>

#include <QtGui/private/qzipreader_p.h>

#include "renderers/FileTypeIconResolver.h"
#include "renderers/OpenWithButton.h"
#include "renderers/PreviewHeaderBar.h"
#include "renderers/SelectableTitleLabel.h"
#include "renderers/document/PreviewHandlerHost.h"
#include "widgets/SpaceLookWindow.h"

namespace {

struct OfficePreviewResult
{
    QString html;
    QString statusMessage;
    bool success = false;
};

struct WorksheetPreview
{
    QString name;
    QVector<QVector<QString>> rows;
    bool truncated = false;
};

QString attributeValue(const QXmlStreamAttributes& attributes, const QStringList& names)
{
    for (const auto& attribute : attributes) {
        const QString localName = attribute.name().toString();
        const QString qualifiedName = attribute.qualifiedName().toString();
        for (const QString& expectedName : names) {
            if (localName.compare(expectedName, Qt::CaseInsensitive) == 0 ||
                qualifiedName.compare(expectedName, Qt::CaseInsensitive) == 0) {
                return attribute.value().toString();
            }
        }
    }

    return QString();
}

QString normalizedZipPath(const QString& baseDir, const QString& target)
{
    QString normalizedTarget = target;
    normalizedTarget.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (normalizedTarget.startsWith(QLatin1Char('/'))) {
        normalizedTarget.remove(0, 1);
    }

    const QString combined = baseDir.isEmpty()
        ? normalizedTarget
        : QDir::cleanPath(baseDir + QLatin1Char('/') + normalizedTarget);
    return combined;
}

QString htmlPage(const QString& body)
{
    return QStringLiteral(
        "<!DOCTYPE html>"
        "<html>"
        "<head>"
        "<meta charset=\"utf-8\">"
        "<style>"
        "body {"
        "  margin: 0;"
        "  padding: 28px;"
        "  background: linear-gradient(135deg, #f8fbff 0%%, #eef5fb 100%%);"
        "  color: #16324a;"
        "  font-family: 'Segoe UI', sans-serif;"
        "}"
        ".sheet {"
        "  max-width: 1100px;"
        "  margin: 0 auto;"
        "}"
        ".card {"
        "  background: rgba(255, 255, 255, 0.96);"
        "  border: 1px solid #dce6f0;"
        "  border-radius: 18px;"
        "  box-shadow: 0 10px 30px rgba(17, 39, 63, 0.06);"
        "  padding: 22px 24px;"
        "  margin-bottom: 18px;"
        "}"
        "h1, h2, h3 {"
        "  margin: 0 0 14px 0;"
        "  color: #0f2740;"
        "} "
        "p {"
        "  margin: 0 0 12px 0;"
        "  line-height: 1.65;"
        "  white-space: pre-wrap;"
        "} "
        "table {"
        "  width: 100%%;"
        "  border-collapse: collapse;"
        "  margin-top: 12px;"
        "} "
        "th, td {"
        "  border: 1px solid #d8e4ef;"
        "  padding: 8px 10px;"
        "  text-align: left;"
        "  vertical-align: top;"
        "  font-size: 13px;"
        "} "
        "th {"
        "  background: #f3f8fd;"
        "  color: #16324a;"
        "} "
        ".muted {"
        "  color: #60758d;"
        "  font-size: 13px;"
        "} "
        ".empty {"
        "  color: #8aa0b4;"
        "  font-style: italic;"
        "} "
        "</style>"
        "</head>"
        "<body><div class=\"sheet\">%1</div></body>"
        "</html>").arg(body);
}

QString loadingHtmlPage(const QString& title, const QString& message)
{
    return htmlPage(QStringLiteral(
        "<div class=\"card\"><h2>%1</h2><p class=\"muted\">%2</p></div>")
        .arg(title.toHtmlEscaped(), message.toHtmlEscaped()));
}

QString fileTitleForPreview(const HoveredItemInfo& info)
{
    if (!info.fileName.trimmed().isEmpty()) {
        return info.fileName;
    }
    if (!info.title.trimmed().isEmpty()) {
        return info.title;
    }
    return QStringLiteral("Document Preview");
}

QString powerShellLiteral(const QString& value)
{
    QString escaped = QDir::toNativeSeparators(value);
    escaped.replace(QLatin1Char('\''), QStringLiteral("''"));
    return QStringLiteral("'") + escaped + QStringLiteral("'");
}

QString officePreviewCacheRoot()
{
    const QString tempRoot = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    return QDir(tempRoot).filePath(QStringLiteral("SpaceLook/office_cache"));
}

QString legacyOfficeConvertedSuffix(const QString& suffix)
{
    if (suffix == QStringLiteral("doc")) {
        return QStringLiteral("docx");
    }
    if (suffix == QStringLiteral("xls")) {
        return QStringLiteral("xlsx");
    }
    if (suffix == QStringLiteral("ppt")) {
        return QStringLiteral("pptx");
    }
    return QString();
}

QString cacheKeyForFile(const QFileInfo& fileInfo)
{
    const QString payload = QStringLiteral("%1|%2|%3")
        .arg(QDir::cleanPath(fileInfo.absoluteFilePath()))
        .arg(fileInfo.size())
        .arg(fileInfo.lastModified().toMSecsSinceEpoch());
    return QString::fromLatin1(QCryptographicHash::hash(payload.toUtf8(), QCryptographicHash::Sha1).toHex());
}

bool runPowerShellScript(const QString& script, QString* errorMessage)
{
    QProcess process;
    process.setProgram(QStringLiteral("powershell.exe"));
    process.setArguments({
        QStringLiteral("-NoProfile"),
        QStringLiteral("-NonInteractive"),
        QStringLiteral("-ExecutionPolicy"),
        QStringLiteral("Bypass"),
        QStringLiteral("-Command"),
        script
    });
    process.start();
    if (!process.waitForStarted(5000)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to start PowerShell for legacy Office conversion.");
        }
        return false;
    }

    if (!process.waitForFinished(60000)) {
        process.kill();
        process.waitForFinished(3000);
        if (errorMessage) {
            *errorMessage = QStringLiteral("Legacy Office conversion timed out.");
        }
        return false;
    }

    const QString stdOut = QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
    const QString stdErr = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (errorMessage) {
            QString details = stdErr;
            if (details.isEmpty()) {
                details = stdOut;
            }
            if (details.isEmpty()) {
                details = QStringLiteral("Unknown conversion error.");
            }
            *errorMessage = details;
        }
        return false;
    }

    return true;
}

QString legacyOfficeConversionScript(const QString& inputPath,
                                     const QString& outputPath,
                                     const QString& inputSuffix)
{
    const QString inputLiteral = powerShellLiteral(inputPath);
    const QString outputLiteral = powerShellLiteral(outputPath);
    const QString outputDirLiteral = powerShellLiteral(QFileInfo(outputPath).absolutePath());

    if (inputSuffix == QStringLiteral("doc")) {
        return QStringLiteral(
            "$ErrorActionPreference='Stop';"
            "$inputPath=%1;"
            "$outputPath=%2;"
            "$outputDir=%3;"
            "New-Item -ItemType Directory -Force -Path $outputDir | Out-Null;"
            "$word=$null;"
            "$document=$null;"
            "try {"
            "  $word=New-Object -ComObject Word.Application;"
            "  $word.Visible=$false;"
            "  $word.DisplayAlerts=0;"
            "  $document=$word.Documents.Open($inputPath,$false,$true);"
            "  $document.SaveAs2($outputPath,16);"
            "} finally {"
            "  if ($document -ne $null) { $document.Close($false) | Out-Null; [void][Runtime.InteropServices.Marshal]::ReleaseComObject($document) }"
            "  if ($word -ne $null) { $word.Quit() | Out-Null; [void][Runtime.InteropServices.Marshal]::ReleaseComObject($word) }"
            "  [GC]::Collect();"
            "  [GC]::WaitForPendingFinalizers();"
            "}")
            .arg(inputLiteral, outputLiteral, outputDirLiteral);
    }

    if (inputSuffix == QStringLiteral("xls")) {
        return QStringLiteral(
            "$ErrorActionPreference='Stop';"
            "$inputPath=%1;"
            "$outputPath=%2;"
            "$outputDir=%3;"
            "New-Item -ItemType Directory -Force -Path $outputDir | Out-Null;"
            "$excel=$null;"
            "$workbook=$null;"
            "try {"
            "  $excel=New-Object -ComObject Excel.Application;"
            "  $excel.Visible=$false;"
            "  $excel.DisplayAlerts=$false;"
            "  $workbook=$excel.Workbooks.Open($inputPath,0,$true);"
            "  $workbook.SaveAs($outputPath,51);"
            "} finally {"
            "  if ($workbook -ne $null) { $workbook.Close($false) | Out-Null; [void][Runtime.InteropServices.Marshal]::ReleaseComObject($workbook) }"
            "  if ($excel -ne $null) { $excel.Quit() | Out-Null; [void][Runtime.InteropServices.Marshal]::ReleaseComObject($excel) }"
            "  [GC]::Collect();"
            "  [GC]::WaitForPendingFinalizers();"
            "}")
            .arg(inputLiteral, outputLiteral, outputDirLiteral);
    }

    if (inputSuffix == QStringLiteral("ppt")) {
        return QStringLiteral(
            "$ErrorActionPreference='Stop';"
            "$inputPath=%1;"
            "$outputPath=%2;"
            "$outputDir=%3;"
            "New-Item -ItemType Directory -Force -Path $outputDir | Out-Null;"
            "$powerPoint=$null;"
            "$presentation=$null;"
            "try {"
            "  $powerPoint=New-Object -ComObject PowerPoint.Application;"
            "  $presentation=$powerPoint.Presentations.Open($inputPath,$false,$true,$false);"
            "  $presentation.SaveAs($outputPath,24);"
            "} finally {"
            "  if ($presentation -ne $null) { $presentation.Close() | Out-Null; [void][Runtime.InteropServices.Marshal]::ReleaseComObject($presentation) }"
            "  if ($powerPoint -ne $null) { $powerPoint.Quit() | Out-Null; [void][Runtime.InteropServices.Marshal]::ReleaseComObject($powerPoint) }"
            "  [GC]::Collect();"
            "  [GC]::WaitForPendingFinalizers();"
            "}")
            .arg(inputLiteral, outputLiteral, outputDirLiteral);
    }

    return QString();
}

OfficePreviewResult buildLegacyOfficePreview(const QString& filePath);

QString readTextNodeBlock(QXmlStreamReader& xml, const QString& paragraphTag)
{
    QString text;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isEndElement() && xml.name() == paragraphTag) {
            break;
        }

        if (!xml.isStartElement()) {
            continue;
        }

        const QString tagName = xml.name().toString();
        if (tagName == QStringLiteral("t")) {
            text += xml.readElementText(QXmlStreamReader::IncludeChildElements);
        } else if (tagName == QStringLiteral("tab")) {
            text += QLatin1Char('\t');
        } else if (tagName == QStringLiteral("br") || tagName == QStringLiteral("cr")) {
            text += QLatin1Char('\n');
        }
    }
    return text;
}

OfficePreviewResult buildDocxPreview(QZipReader& zip)
{
    OfficePreviewResult result;
    const QByteArray documentXml = zip.fileData(QStringLiteral("word/document.xml"));
    if (documentXml.isEmpty()) {
        result.statusMessage = QStringLiteral("The DOCX file does not contain word/document.xml.");
        return result;
    }

    QXmlStreamReader xml(documentXml);
    QStringList paragraphs;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement() && xml.name() == QStringLiteral("p")) {
            paragraphs.append(readTextNodeBlock(xml, QStringLiteral("p")));
        }
    }

    if (xml.hasError()) {
        result.statusMessage = QStringLiteral("Failed to parse the DOCX XML content.");
        return result;
    }

    QString body;
    QTextStream stream(&body);
    stream << "<div class=\"card\"><h2>Document</h2>";
    if (paragraphs.isEmpty()) {
        stream << "<p class=\"empty\">This DOCX file does not contain readable paragraph text.</p>";
    } else {
        for (const QString& paragraph : paragraphs) {
            const QString trimmedParagraph = paragraph.trimmed();
            if (trimmedParagraph.isEmpty()) {
                stream << "<p>&nbsp;</p>";
            } else {
                stream << "<p>" << trimmedParagraph.toHtmlEscaped() << "</p>";
            }
        }
    }
    stream << "</div>";

    result.html = htmlPage(body);
    result.success = true;
    return result;
}

QVector<QString> parseSharedStrings(const QByteArray& xmlData)
{
    QVector<QString> strings;
    if (xmlData.isEmpty()) {
        return strings;
    }

    QXmlStreamReader xml(xmlData);
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement() || xml.name() != QStringLiteral("si")) {
            continue;
        }

        QString text;
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isEndElement() && xml.name() == QStringLiteral("si")) {
                break;
            }
            if (xml.isStartElement() && xml.name() == QStringLiteral("t")) {
                text += xml.readElementText(QXmlStreamReader::IncludeChildElements);
            }
        }
        strings.append(text);
    }

    return strings;
}

int columnIndexFromCellRef(const QString& cellRef)
{
    int value = 0;
    for (const QChar ch : cellRef) {
        if (!ch.isLetter()) {
            break;
        }
        value = (value * 26) + (ch.toUpper().unicode() - QLatin1Char('A').unicode() + 1);
    }
    return qMax(0, value - 1);
}

QMap<QString, QString> parseWorkbookRelationships(const QByteArray& xmlData)
{
    QMap<QString, QString> relationships;
    QXmlStreamReader xml(xmlData);
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement() || xml.name() != QStringLiteral("Relationship")) {
            continue;
        }

        const QString id = attributeValue(xml.attributes(), { QStringLiteral("Id") });
        const QString target = attributeValue(xml.attributes(), { QStringLiteral("Target") });
        if (!id.isEmpty() && !target.isEmpty()) {
            relationships.insert(id, normalizedZipPath(QStringLiteral("xl"), target));
        }
    }
    return relationships;
}

QVector<QPair<QString, QString>> parseWorkbookSheets(const QByteArray& workbookXml, const QByteArray& relsXml)
{
    const QMap<QString, QString> relationships = parseWorkbookRelationships(relsXml);
    QVector<QPair<QString, QString>> sheets;

    QXmlStreamReader xml(workbookXml);
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement() || xml.name() != QStringLiteral("sheet")) {
            continue;
        }

        const QString name = attributeValue(xml.attributes(), { QStringLiteral("name") });
        const QString relId = attributeValue(xml.attributes(), { QStringLiteral("id"), QStringLiteral("r:id") });
        const QString path = relationships.value(relId);
        if (!name.isEmpty() && !path.isEmpty()) {
            sheets.append(qMakePair(name, path));
        }
    }

    return sheets;
}

WorksheetPreview parseWorksheet(const QString& sheetName, const QByteArray& sheetXml, const QVector<QString>& sharedStrings)
{
    constexpr int kMaxRows = 80;
    constexpr int kMaxColumns = 12;

    WorksheetPreview preview;
    preview.name = sheetName;

    QXmlStreamReader xml(sheetXml);
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement() || xml.name() != QStringLiteral("row")) {
            continue;
        }

        if (preview.rows.size() >= kMaxRows) {
            preview.truncated = true;
            break;
        }

        QMap<int, QString> rowValues;
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isEndElement() && xml.name() == QStringLiteral("row")) {
                break;
            }

            if (!xml.isStartElement() || xml.name() != QStringLiteral("c")) {
                continue;
            }

            const QString cellRef = attributeValue(xml.attributes(), { QStringLiteral("r") });
            const QString cellType = attributeValue(xml.attributes(), { QStringLiteral("t") });
            const int columnIndex = columnIndexFromCellRef(cellRef);
            if (columnIndex >= kMaxColumns) {
                preview.truncated = true;
            }

            QString rawValue;
            QString inlineText;
            while (!xml.atEnd()) {
                xml.readNext();
                if (xml.isEndElement() && xml.name() == QStringLiteral("c")) {
                    break;
                }

                if (!xml.isStartElement()) {
                    continue;
                }

                const QString tagName = xml.name().toString();
                if (tagName == QStringLiteral("v")) {
                    rawValue = xml.readElementText(QXmlStreamReader::IncludeChildElements);
                } else if (tagName == QStringLiteral("t")) {
                    inlineText += xml.readElementText(QXmlStreamReader::IncludeChildElements);
                }
            }

            QString finalValue = rawValue;
            if (cellType == QStringLiteral("s")) {
                const int stringIndex = rawValue.toInt();
                if (stringIndex >= 0 && stringIndex < sharedStrings.size()) {
                    finalValue = sharedStrings.at(stringIndex);
                }
            } else if (cellType == QStringLiteral("inlineStr")) {
                finalValue = inlineText;
            } else if (cellType == QStringLiteral("b")) {
                finalValue = rawValue == QStringLiteral("1") ? QStringLiteral("TRUE") : QStringLiteral("FALSE");
            }

            if (columnIndex < kMaxColumns) {
                rowValues.insert(columnIndex, finalValue);
            }
        }

        int maxColumn = -1;
        for (auto it = rowValues.constBegin(); it != rowValues.constEnd(); ++it) {
            maxColumn = qMax(maxColumn, it.key());
        }
        QVector<QString> row(qMax(maxColumn + 1, 0));
        for (auto it = rowValues.constBegin(); it != rowValues.constEnd(); ++it) {
            row[it.key()] = it.value();
        }
        preview.rows.append(row);
    }

    return preview;
}

OfficePreviewResult buildXlsxPreview(QZipReader& zip)
{
    OfficePreviewResult result;
    const QByteArray workbookXml = zip.fileData(QStringLiteral("xl/workbook.xml"));
    const QByteArray relsXml = zip.fileData(QStringLiteral("xl/_rels/workbook.xml.rels"));
    if (workbookXml.isEmpty() || relsXml.isEmpty()) {
        result.statusMessage = QStringLiteral("The XLSX file is missing workbook metadata.");
        return result;
    }

    const QVector<QString> sharedStrings = parseSharedStrings(zip.fileData(QStringLiteral("xl/sharedStrings.xml")));
    const QVector<QPair<QString, QString>> sheets = parseWorkbookSheets(workbookXml, relsXml);
    if (sheets.isEmpty()) {
        result.statusMessage = QStringLiteral("No worksheet data was found in this XLSX file.");
        return result;
    }

    QString body;
    QTextStream stream(&body);
    bool truncated = false;

    for (const auto& sheet : sheets) {
        const WorksheetPreview preview = parseWorksheet(sheet.first, zip.fileData(sheet.second), sharedStrings);
        truncated = truncated || preview.truncated;

        stream << "<div class=\"card\">";
        stream << "<h2>" << preview.name.toHtmlEscaped() << "</h2>";
        if (preview.rows.isEmpty()) {
            stream << "<p class=\"empty\">This worksheet does not contain readable cell data.</p>";
        } else {
            int maxColumns = 0;
            for (const QVector<QString>& row : preview.rows) {
                maxColumns = qMax(maxColumns, row.size());
            }

            stream << "<table><tbody>";
            for (const QVector<QString>& row : preview.rows) {
                stream << "<tr>";
                if (row.isEmpty()) {
                    stream << "<td class=\"empty\"></td>";
                } else {
                    for (int column = 0; column < maxColumns; ++column) {
                        const QString cellValue = column < row.size() ? row.at(column) : QString();
                        stream << "<td>";
                        if (cellValue.trimmed().isEmpty()) {
                            stream << "&nbsp;";
                        } else {
                            stream << cellValue.toHtmlEscaped();
                        }
                        stream << "</td>";
                    }
                }
                stream << "</tr>";
            }
            stream << "</tbody></table>";
        }
        stream << "</div>";
    }

    if (truncated) {
        result.statusMessage = QStringLiteral("Spreadsheet preview is limited to the first 80 rows and 12 columns per sheet.");
    }

    result.html = htmlPage(body);
    result.success = true;
    return result;
}

OfficePreviewResult buildPptxPreview(QZipReader& zip)
{
    OfficePreviewResult result;
    QVector<QString> slidePaths;
    for (const QZipReader::FileInfo& fileInfo : zip.fileInfoList()) {
        if (fileInfo.isFile &&
            fileInfo.filePath.startsWith(QStringLiteral("ppt/slides/slide")) &&
            fileInfo.filePath.endsWith(QStringLiteral(".xml"))) {
            slidePaths.append(fileInfo.filePath);
        }
    }

    std::sort(slidePaths.begin(), slidePaths.end(), [](const QString& left, const QString& right) {
        static const QRegularExpression numberPattern(QStringLiteral("(\\d+)"));
        const QRegularExpressionMatch leftMatch = numberPattern.match(left);
        const QRegularExpressionMatch rightMatch = numberPattern.match(right);
        return leftMatch.captured(1).toInt() < rightMatch.captured(1).toInt();
    });

    if (slidePaths.isEmpty()) {
        result.statusMessage = QStringLiteral("No slide XML files were found in this PPTX document.");
        return result;
    }

    QString body;
    QTextStream stream(&body);

    for (int slideIndex = 0; slideIndex < slidePaths.size(); ++slideIndex) {
        const QByteArray slideXml = zip.fileData(slidePaths.at(slideIndex));
        QXmlStreamReader xml(slideXml);
        QStringList paragraphs;
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement() && xml.name() == QStringLiteral("p")) {
                paragraphs.append(readTextNodeBlock(xml, QStringLiteral("p")));
            }
        }

        stream << "<div class=\"card\">";
        stream << "<h2>Slide " << slideIndex + 1 << "</h2>";
        if (paragraphs.isEmpty()) {
            stream << "<p class=\"empty\">This slide does not contain readable text.</p>";
        } else {
            for (const QString& paragraph : paragraphs) {
                const QString trimmedParagraph = paragraph.trimmed();
                if (trimmedParagraph.isEmpty()) {
                    continue;
                }
                stream << "<p>" << trimmedParagraph.toHtmlEscaped() << "</p>";
            }
        }
        stream << "</div>";
    }

    result.html = htmlPage(body);
    result.success = true;
    return result;
}

OfficePreviewResult buildOfficePreview(const QString& filePath)
{
    OfficePreviewResult result;
    const QFileInfo fileInfo(filePath);
    const QString suffix = fileInfo.suffix().toLower();

    if (suffix == QStringLiteral("doc") ||
        suffix == QStringLiteral("xls") ||
        suffix == QStringLiteral("ppt")) {
        return buildLegacyOfficePreview(filePath);
    }

    QZipReader zip(filePath);
    if (!zip.exists() || !zip.isReadable()) {
        result.statusMessage = QStringLiteral("Failed to read the Office container.");
        return result;
    }

    if (suffix == QStringLiteral("docx")) {
        return buildDocxPreview(zip);
    }
    if (suffix == QStringLiteral("xlsx")) {
        return buildXlsxPreview(zip);
    }
    if (suffix == QStringLiteral("pptx")) {
        return buildPptxPreview(zip);
    }

    result.statusMessage = QStringLiteral("This Office format is not supported by the current Qt parser.");
    return result;
}

OfficePreviewResult buildLegacyOfficePreview(const QString& filePath)
{
    OfficePreviewResult result;
    const QFileInfo sourceInfo(filePath);
    const QString suffix = sourceInfo.suffix().toLower();
    const QString convertedSuffix = legacyOfficeConvertedSuffix(suffix);
    if (convertedSuffix.isEmpty()) {
        result.statusMessage = QStringLiteral("This legacy Office format is not supported by the conversion pipeline.");
        return result;
    }

    const QString cacheRoot = officePreviewCacheRoot();
    QDir().mkpath(cacheRoot);

    const QString exportBaseDir = QDir(cacheRoot).filePath(cacheKeyForFile(sourceInfo));
    QDir().mkpath(exportBaseDir);
    const QString convertedPath = QDir(exportBaseDir).filePath(
        QStringLiteral("preview.%1").arg(convertedSuffix));

    if (!QFileInfo::exists(convertedPath)) {
        const QString script = legacyOfficeConversionScript(filePath, convertedPath, suffix);
        if (script.trimmed().isEmpty()) {
            result.statusMessage = QStringLiteral("Legacy Office conversion is unavailable for this format.");
            return result;
        }

        QString conversionError;
        if (!runPowerShellScript(script, &conversionError) || !QFileInfo::exists(convertedPath)) {
            result.statusMessage = conversionError.trimmed().isEmpty()
                ? QStringLiteral("Legacy Office conversion failed. Microsoft Office may be unavailable on this system.")
                : QStringLiteral("Legacy Office conversion failed: %1").arg(conversionError.trimmed());
            result.html = htmlPage(QStringLiteral(
                "<div class=\"card\"><h2>Legacy Office Preview Unavailable</h2>"
                "<p>The selected file uses an older Office binary format.</p>"
                "<p class=\"muted\">SpaceLook tried to convert it through local Microsoft Office automation, and that conversion did not complete.</p></div>"));
            result.success = true;
            return result;
        }
    }

    result = buildOfficePreview(convertedPath);
    if (result.success) {
        result.statusMessage = QStringLiteral("Converted legacy %1 to %2 for preview.")
            .arg(suffix.toUpper(), convertedSuffix.toUpper());
    }
    return result;
}

}

DocumentRenderer::DocumentRenderer(QWidget* parent)
    : QWidget(parent)
    , m_headerRow(new QWidget(this))
    , m_iconLabel(new QLabel(this))
    , m_titleLabel(new SelectableTitleLabel(this))
    , m_metaLabel(new QLabel(this))
    , m_pathRow(new QWidget(this))
    , m_pathTitleLabel(new QLabel(this))
    , m_pathValueLabel(new QLabel(this))
    , m_openWithButton(new OpenWithButton(this))
    , m_statusLabel(new QLabel(this))
    , m_contentStack(new QStackedWidget(this))
    , m_previewHandlerHost(new PreviewHandlerHost(this))
    , m_textBrowser(new QTextBrowser(this))
{
    setAttribute(Qt::WA_StyledBackground, true);
    setObjectName(QStringLiteral("DocumentRendererRoot"));
    m_headerRow->setObjectName(QStringLiteral("DocumentHeaderRow"));
    m_iconLabel->setObjectName(QStringLiteral("DocumentTypeIcon"));
    m_titleLabel->setObjectName(QStringLiteral("DocumentTitle"));
    m_metaLabel->setObjectName(QStringLiteral("DocumentMeta"));
    m_pathRow->setObjectName(QStringLiteral("DocumentPathRow"));
    m_pathTitleLabel->setObjectName(QStringLiteral("DocumentPathTitle"));
    m_pathValueLabel->setObjectName(QStringLiteral("DocumentPathValue"));
    m_openWithButton->setObjectName(QStringLiteral("DocumentOpenWithButton"));
    m_statusLabel->setObjectName(QStringLiteral("DocumentStatus"));
    m_contentStack->setObjectName(QStringLiteral("DocumentContentStack"));
    m_textBrowser->setObjectName(QStringLiteral("DocumentBrowser"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 0, 12, 12);
    layout->setSpacing(14);
    layout->addWidget(m_headerRow);
    layout->addWidget(m_statusLabel);
    layout->addWidget(m_contentStack, 1);

    auto* headerLayout = new QHBoxLayout(m_headerRow);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(12);
    auto* titleBlock = new PreviewHeaderBar(m_iconLabel, m_titleLabel, m_pathRow, m_openWithButton, m_headerRow);
    headerLayout->addWidget(titleBlock->contentWidget(), 1);

    auto* pathLayout = new QHBoxLayout(m_pathRow);
    pathLayout->setContentsMargins(0, 0, 0, 0);
    pathLayout->setSpacing(8);
    pathLayout->addWidget(m_pathValueLabel, 1);

    m_pathTitleLabel->hide();
    m_iconLabel->setFixedSize(72, 72);
    m_iconLabel->setScaledContents(true);
    m_titleLabel->setWordWrap(true);
    m_pathValueLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_pathValueLabel->setWordWrap(true);
    m_pathValueLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_pathRow->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
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
    m_textBrowser->setOpenExternalLinks(false);
    m_statusLabel->hide();
    m_contentStack->addWidget(m_previewHandlerHost);
    m_contentStack->addWidget(m_textBrowser);
    m_contentStack->setCurrentWidget(m_textBrowser);

    connect(m_titleLabel, &SelectableTitleLabel::copyFeedbackRequested, this, [this](const QString& message) {
        showStatusMessage(message);
    });

    applyChrome();
}

QString DocumentRenderer::rendererId() const
{
    return QStringLiteral("document");
}

bool DocumentRenderer::canHandle(const HoveredItemInfo& info) const
{
    return info.typeKey == QStringLiteral("office");
}

QWidget* DocumentRenderer::widget()
{
    return this;
}

bool DocumentRenderer::reportsLoadingState() const
{
    return true;
}

void DocumentRenderer::setLoadingStateCallback(std::function<void(bool)> callback)
{
    m_loadingStateCallback = std::move(callback);
}

void DocumentRenderer::load(const HoveredItemInfo& info)
{
    m_info = info;
    const PreviewLoadGuard::Token loadToken = m_loadGuard.begin(info.filePath);
    notifyLoadingState(true);
    qDebug().noquote() << QStringLiteral("[SpaceLookRender] DocumentRenderer load path=\"%1\" typeKey=%2")
        .arg(info.filePath, info.typeKey);

    m_titleLabel->setText(fileTitleForPreview(info));
    m_titleLabel->setCopyText(m_titleLabel->text());
    const QIcon typeIcon(FileTypeIconResolver::iconForInfo(info));
    m_iconLabel->setPixmap(typeIcon.pixmap(128, 128));
    m_pathValueLabel->setText(info.filePath.trimmed().isEmpty() ? QStringLiteral("(Unavailable)") : info.filePath);
    m_openWithButton->setTargetContext(info.filePath, info.typeKey);
    m_textBrowser->clear();
    m_previewHandlerHost->unload();

    m_contentStack->setCurrentWidget(m_previewHandlerHost);
    QString handlerError;
    if (m_previewHandlerHost->openFile(info.filePath, &handlerError)) {
        m_statusLabel->clear();
        m_statusLabel->hide();
        notifyLoadingState(false);
        return;
    }

    qDebug().noquote() << QStringLiteral("[SpaceLookRender] Windows Preview Handler unavailable, falling back to Qt parser path=\"%1\" reason=\"%2\"")
        .arg(info.filePath, handlerError);
    loadWithQtParser(info, loadToken, handlerError);
}

void DocumentRenderer::loadWithQtParser(const HoveredItemInfo& info, const PreviewLoadGuard::Token& loadToken, const QString& handlerError)
{
    m_contentStack->setCurrentWidget(m_textBrowser);
    const QString loadingMessage = handlerError.trimmed().isEmpty()
        ? QStringLiteral("Parsing Office document...")
        : QStringLiteral("Windows Preview Handler unavailable. Falling back to built in parser...");
    m_statusLabel->setText(loadingMessage);
    m_statusLabel->show();
    m_textBrowser->setHtml(loadingHtmlPage(fileTitleForPreview(info), loadingMessage));

    auto* watcher = new QFutureWatcher<OfficePreviewResult>(this);
    connect(watcher, &QFutureWatcher<OfficePreviewResult>::finished, this, [this, watcher, loadToken, handlerError]() {
        const OfficePreviewResult preview = watcher->result();
        watcher->deleteLater();

        if (!m_loadGuard.isCurrent(loadToken, m_info.filePath)) {
            qDebug().noquote() << QStringLiteral("[SpaceLookRender] DocumentRenderer discarded stale async result path=\"%1\"")
                .arg(loadToken.path);
            return;
        }

        if (!preview.statusMessage.trimmed().isEmpty()) {
            const QString statusText = handlerError.trimmed().isEmpty()
                ? preview.statusMessage
                : QStringLiteral("%1 %2").arg(handlerError.trimmed(), preview.statusMessage);
            m_statusLabel->setText(statusText.trimmed());
            m_statusLabel->show();
        } else {
            if (handlerError.trimmed().isEmpty()) {
                m_statusLabel->clear();
                m_statusLabel->hide();
            } else {
                m_statusLabel->setText(handlerError.trimmed());
                m_statusLabel->show();
            }
        }

        if (preview.success) {
            m_textBrowser->setHtml(preview.html);
        } else {
            m_textBrowser->setHtml(htmlPage(QStringLiteral(
                "<div class=\"card\"><h2>Preview Unavailable</h2>"
                "<p>The Office document could not be parsed by the current Qt based preview pipeline.</p></div>")));
        }
        notifyLoadingState(false);
    });
    watcher->setFuture(QtConcurrent::run([filePath = info.filePath]() {
        return buildOfficePreview(filePath);
    }));
}

void DocumentRenderer::unload()
{
    m_loadGuard.cancel();
    notifyLoadingState(false);
    m_previewHandlerHost->unload();
    m_pathValueLabel->clear();
    m_openWithButton->setTargetContext(QString(), QString());
    showStatusMessage(QString());
    m_info = HoveredItemInfo();
    m_textBrowser->clear();
}

void DocumentRenderer::notifyLoadingState(bool loading)
{
    if (m_loadingStateCallback) {
        m_loadingStateCallback(loading);
    }
}

void DocumentRenderer::applyChrome()
{
    setStyleSheet(
        "#DocumentRendererRoot {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:1,"
        "      stop:0 #fcfdff,"
        "      stop:1 #f4f8fc);"
        "  border-radius: 0px;"
        "}"
        "QLabel {"
        "  color: #18324a;"
        "}"
        "#DocumentTypeIcon {"
        "  background: transparent;"
        "  border: none;"
        "  padding: 0px;"
        "}"
        "#DocumentTitle {"
        "  color: #0f2740;"
        "}"
        "#DocumentMeta {"
        "  color: #5c738b;"
        "}"
        "#DocumentPathTitle {"
        "  color: #16324a;"
        "  font-family: 'Segoe UI Semibold';"
        "}"
        "#DocumentPathValue {"
        "  color: #445d76;"
        "}"
        "#DocumentOpenWithButton QToolButton {"
        "  background: rgba(238, 244, 252, 0.96);"
        "  color: #17324b;"
        "  border: 1px solid rgba(193, 208, 224, 0.95);"
        "  padding: 3px 8px;"
        "  min-height: 24px;"
        "}"
        "#DocumentOpenWithButton QToolButton:hover {"
        "  background: rgba(245, 249, 255, 1.0);"
        "}"
        "#DocumentOpenWithButton QToolButton:pressed {"
        "  background: rgba(224, 234, 246, 1.0);"
        "}"
        "#DocumentOpenWithButton #OpenWithPrimaryButton {"
        "  border-top-left-radius: 10px;"
        "  border-bottom-left-radius: 10px;"
        "  border-top-right-radius: 0px;"
        "  border-bottom-right-radius: 0px;"
        "  min-width: 28px;"
        "}"
        "#DocumentOpenWithButton #OpenWithExpandButton {"
        "  border-left: none;"
        "  border-top-left-radius: 0px;"
        "  border-bottom-left-radius: 0px;"
        "  border-top-right-radius: 10px;"
        "  border-bottom-right-radius: 10px;"
        "  min-width: 22px;"
        "  padding-left: 5px;"
        "  padding-right: 5px;"
        "}"
        "#DocumentStatus {"
        "  color: #27568b;"
        "  background: rgba(220, 235, 255, 0.92);"
        "  border: 1px solid rgba(164, 193, 229, 0.95);"
        "  border-radius: 12px;"
        "  padding: 10px 14px;"
        "}"
        "#DocumentContentStack {"
        "  background: transparent;"
        "  border: none;"
        "}"
        "#DocumentBrowser {"
        "  background: #f4f7fb;"
        "  border: 1px solid #ccd6e2;"
        "  border-radius: 18px;"
        "  padding: 0px;"
        "}"
        "#DocumentBrowser QScrollBar:vertical {"
        "  background: rgba(232, 238, 245, 0.8);"
        "  border: none;"
        "  width: 8px;"
        "  margin: 10px 0 10px 0;"
        "  border-radius: 4px;"
        "}"
        "#DocumentBrowser QScrollBar::handle:vertical {"
        "  background: #7c8fa8;"
        "  min-height: 52px;"
        "  border-radius: 4px;"
        "}"
        "#DocumentBrowser QScrollBar::handle:vertical:hover {"
        "  background: #6d829d;"
        "}"
        "#DocumentBrowser QScrollBar::handle:vertical:pressed {"
        "  background: #61768f;"
        "}"
        "#DocumentBrowser QScrollBar:horizontal {"
        "  background: rgba(232, 238, 245, 0.8);"
        "  border: none;"
        "  height: 8px;"
        "  margin: 0 10px 0 10px;"
        "  border-radius: 4px;"
        "}"
        "#DocumentBrowser QScrollBar::handle:horizontal {"
        "  background: #7c8fa8;"
        "  min-width: 52px;"
        "  border-radius: 4px;"
        "}"
        "#DocumentBrowser QScrollBar::handle:horizontal:hover {"
        "  background: #6d829d;"
        "}"
        "#DocumentBrowser QScrollBar::handle:horizontal:pressed {"
        "  background: #61768f;"
        "}"
        "#DocumentBrowser QScrollBar::add-line:vertical, #DocumentBrowser QScrollBar::sub-line:vertical,"
        "#DocumentBrowser QScrollBar::add-line:horizontal, #DocumentBrowser QScrollBar::sub-line:horizontal {"
        "  width: 0px;"
        "  height: 0px;"
        "}"
        "#DocumentBrowser QScrollBar::add-page:vertical, #DocumentBrowser QScrollBar::sub-page:vertical,"
        "#DocumentBrowser QScrollBar::add-page:horizontal, #DocumentBrowser QScrollBar::sub-page:horizontal {"
        "  background: transparent;"
        "}"
    );

    QFont titleFont;
    titleFont.setFamily(QStringLiteral("Microsoft YaHei UI"));
    titleFont.setPixelSize(20);
    titleFont.setWeight(QFont::Bold);
    m_titleLabel->setFont(titleFont);
    m_titleLabel->setWordWrap(true);

    QFont metaFont;
    metaFont.setFamily(QStringLiteral("Segoe UI"));
    metaFont.setPixelSize(13);
    m_metaLabel->setFont(metaFont);
    m_pathTitleLabel->setFont(metaFont);
    m_pathValueLabel->setFont(metaFont);
    m_statusLabel->setFont(metaFont);
    m_metaLabel->setWordWrap(true);
    m_pathValueLabel->setWordWrap(true);
}

void DocumentRenderer::showStatusMessage(const QString& message)
{
    if (message.trimmed().isEmpty()) {
        m_statusLabel->clear();
        m_statusLabel->hide();
        return;
    }

    m_statusLabel->setText(message);
    m_statusLabel->show();
    QTimer::singleShot(1400, m_statusLabel, [label = m_statusLabel]() {
        if (label) {
            label->clear();
            label->hide();
        }
    });
}
