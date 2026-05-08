#include "renderers/code/CodeRenderer.h"

#include <QCoreApplication>
#include <QApplication>
#include <QAction>
#include <QClipboard>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QShortcut>
#include <QStackedWidget>
#include <QTimer>
#include <QTextCursor>
#include <QTextDocument>
#include <QTreeWidgetItemIterator>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTextEdit>
#include <QTextBlock>
#include <QToolButton>
#include <QVBoxLayout>
#include <QDebug>
#include <QRegularExpression>
#include <QVariantMap>
#include <QXmlStreamWriter>
#include <QtXml/QDomDocument>
#include <QtConcurrent/QtConcurrent>

#include "core/PreviewFileReader.h"
#include "core/TextEncodingDetector.h"
#include "renderers/CodeThemeManager.h"
#include "renderers/FileTypeIconResolver.h"
#include "renderers/ModeSwitchButton.h"
#include "renderers/OpenWithButton.h"
#include "renderers/PreviewHeaderBar.h"
#include "renderers/PreviewStateVisuals.h"
#include "renderers/SelectableTitleLabel.h"
#include "core/preview_state.h"
#include "widgets/SpaceLookWindow.h"
#include "third_party/ksyntaxhighlighting/src/lib/definition.h"
#include "third_party/ksyntaxhighlighting/src/lib/repository.h"
#include "third_party/ksyntaxhighlighting/src/lib/syntaxhighlighter.h"
#include "third_party/ksyntaxhighlighting/src/lib/theme.h"

namespace {

constexpr qint64 kMaxCodePreviewBytes = 1024 * 1024 * 2;
constexpr int kNodeKindRole = Qt::UserRole + 1;
constexpr int kScalarTextRole = Qt::UserRole + 2;
constexpr int kScalarBoolRole = Qt::UserRole + 3;
constexpr int kXmlTagNameRole = Qt::UserRole + 4;
constexpr int kXmlAttributesRole = Qt::UserRole + 5;

struct CodeLoadResult
{
    QString text;
    QString encodingName;
    QString statusMessage;
    bool success = false;
};

class CodeTextEdit;

class CodeLineNumberArea : public QWidget
{
public:
    explicit CodeLineNumberArea(QWidget* parent, CodeTextEdit* editor)
        : QWidget(parent)
        , m_editor(editor)
    {
    }

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    CodeTextEdit* m_editor = nullptr;
};

class CodeTextEdit : public QPlainTextEdit
{
public:
    explicit CodeTextEdit(QWidget* parent = nullptr)
        : QPlainTextEdit(parent)
        , m_lineNumberArea(new CodeLineNumberArea(this, this))
    {
        connect(this, &QPlainTextEdit::blockCountChanged, this, &CodeTextEdit::updateLineNumberAreaWidth);
        connect(this, &QPlainTextEdit::updateRequest, this, &CodeTextEdit::updateLineNumberArea);
        connect(this, &QPlainTextEdit::cursorPositionChanged, this, &CodeTextEdit::highlightCurrentLine);
        updateLineNumberAreaWidth(0);
        highlightCurrentLine();
    }

    int lineNumberAreaWidth() const
    {
        int digits = 1;
        int max = qMax(1, blockCount());
        while (max >= 10) {
            max /= 10;
            ++digits;
        }

        return 18 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
    }

    void lineNumberAreaPaintEvent(QPaintEvent* event)
    {
        QPainter painter(m_lineNumberArea);
        painter.fillRect(event->rect(), QColor(QStringLiteral("#e8eef6")));
        painter.setPen(QColor(QStringLiteral("#6d8096")));

        QTextBlock block = firstVisibleBlock();
        int blockNumber = block.blockNumber();
        int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
        int bottom = top + qRound(blockBoundingRect(block).height());

        while (block.isValid() && top <= event->rect().bottom()) {
            if (block.isVisible() && bottom >= event->rect().top()) {
                const QString number = QString::number(blockNumber + 1);
                painter.drawText(0, top, m_lineNumberArea->width() - 8, fontMetrics().height(),
                    Qt::AlignRight, number);
            }

            block = block.next();
            top = bottom;
            bottom = top + qRound(blockBoundingRect(block).height());
            ++blockNumber;
        }
    }

protected:
    void changeEvent(QEvent* event) override
    {
        QPlainTextEdit::changeEvent(event);
        if (event && event->type() == QEvent::FontChange) {
            updateLineNumberAreaWidth(0);
        }
    }

    void resizeEvent(QResizeEvent* event) override
    {
        QPlainTextEdit::resizeEvent(event);
        const QRect content = contentsRect();
        m_lineNumberArea->setGeometry(QRect(content.left(), content.top(), lineNumberAreaWidth(), content.height()));
    }

private:
    void highlightCurrentLine()
    {
        QList<QTextEdit::ExtraSelection> extraSelections;

        QTextEdit::ExtraSelection selection;
        selection.format.setBackground(QColor(QStringLiteral("#f6f8fa")));
        selection.format.setProperty(QTextFormat::FullWidthSelection, true);
        selection.cursor = textCursor();
        selection.cursor.clearSelection();
        extraSelections.append(selection);

        setExtraSelections(extraSelections);
    }

    void updateLineNumberAreaWidth(int)
    {
        setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
    }

    void updateLineNumberArea(const QRect& rect, int dy)
    {
        if (dy != 0) {
            m_lineNumberArea->scroll(0, dy);
        } else {
            m_lineNumberArea->update(0, rect.y(), m_lineNumberArea->width(), rect.height());
        }

        if (rect.contains(viewport()->rect())) {
            updateLineNumberAreaWidth(0);
        }
    }

    QWidget* m_lineNumberArea = nullptr;
};

QSize CodeLineNumberArea::sizeHint() const
{
    return QSize(m_editor ? m_editor->lineNumberAreaWidth() : 0, 0);
}

void CodeLineNumberArea::paintEvent(QPaintEvent* event)
{
    if (m_editor) {
        m_editor->lineNumberAreaPaintEvent(event);
    }
}

CodeLoadResult loadCodePreviewContent(const QString& filePath, const PreviewCancellationToken& cancelToken)
{
    CodeLoadResult result;
    if (previewCancellationRequested(cancelToken)) {
        result.statusMessage = QCoreApplication::translate("SpaceLook", "Code preview was canceled.");
        return result;
    }

    QByteArray content;
    bool truncated = false;
    if (!PreviewFileReader::readPrefix(filePath, kMaxCodePreviewBytes, &content, &truncated)) {
        result.text = QCoreApplication::translate("SpaceLook", "Could not read:\n%1").arg(filePath);
        result.statusMessage = QCoreApplication::translate("SpaceLook", "Failed to open the file for preview.");
        return result;
    }

    if (previewCancellationRequested(cancelToken)) {
        content.clear();
        result.statusMessage = QCoreApplication::translate("SpaceLook", "Code preview was canceled.");
        return result;
    }

    if (truncated) {
        result.statusMessage = QCoreApplication::translate("SpaceLook", "Preview is truncated to the first 2 MB.");
    }

    const DetectedTextEncoding decoded = TextEncodingDetector::decode(content);
    result.text = decoded.text;
    result.encodingName = decoded.name;
    result.success = true;
    return result;
}

void appendJsonValue(QTreeWidgetItem* parent, const QString& key, const QJsonValue& value)
{
    if (!parent) {
        return;
    }

    auto* item = new QTreeWidgetItem(parent);
    item->setText(0, key);

    if (value.isObject()) {
        item->setData(0, kNodeKindRole, QStringLiteral("json_object"));
        const QJsonObject object = value.toObject();
        for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
            appendJsonValue(item, it.key(), it.value());
        }
        item->setText(1, object.isEmpty() ? QStringLiteral("{ }") : QString());
        return;
    }

    if (value.isArray()) {
        item->setData(0, kNodeKindRole, QStringLiteral("json_array"));
        const QJsonArray array = value.toArray();
        for (int index = 0; index < array.size(); ++index) {
            appendJsonValue(item, QStringLiteral("[%1]").arg(index), array.at(index));
        }
        item->setText(1, array.isEmpty() ? QStringLiteral("[ ]") : QStringLiteral("[%1]").arg(array.size()));
        return;
    }

    if (value.isString()) {
        item->setData(0, kNodeKindRole, QStringLiteral("json_string"));
        item->setData(0, kScalarTextRole, value.toString());
        item->setText(1, value.toString());
        return;
    }

    if (value.isDouble()) {
        const QString numberText = QString::number(value.toDouble());
        item->setData(0, kNodeKindRole, QStringLiteral("json_number"));
        item->setData(0, kScalarTextRole, numberText);
        item->setText(1, numberText);
        return;
    }

    if (value.isBool()) {
        item->setData(0, kNodeKindRole, QStringLiteral("json_bool"));
        item->setData(0, kScalarBoolRole, value.toBool());
        item->setText(1, value.toBool() ? QStringLiteral("true") : QStringLiteral("false"));
        return;
    }

    if (value.isNull()) {
        item->setData(0, kNodeKindRole, QStringLiteral("json_null"));
        item->setText(1, QStringLiteral("null"));
        return;
    }

    item->setText(1, QCoreApplication::translate("SpaceLook", "(unsupported)"));
}

void appendXmlNode(QTreeWidgetItem* parent, const QDomNode& node)
{
    if (!parent || node.isNull()) {
        return;
    }

    if (node.isText()) {
        const QString text = node.nodeValue().trimmed();
        if (text.isEmpty()) {
            return;
        }
        auto* textItem = new QTreeWidgetItem(parent);
        textItem->setData(0, kNodeKindRole, QStringLiteral("xml_text"));
        textItem->setData(0, kScalarTextRole, text);
        textItem->setText(0, QStringLiteral("#text"));
        textItem->setText(1, text);
        return;
    }

    if (!node.isElement()) {
        return;
    }

    const QDomElement element = node.toElement();
    auto* item = new QTreeWidgetItem(parent);
    item->setData(0, kNodeKindRole, QStringLiteral("xml_element"));
    item->setData(0, kXmlTagNameRole, element.tagName());
    item->setText(0, element.tagName());

    QStringList attributes;
    QVariantMap attributeValues;
    const QDomNamedNodeMap attributeMap = element.attributes();
    for (int index = 0; index < attributeMap.count(); ++index) {
        const QDomNode attribute = attributeMap.item(index);
        attributes.append(QStringLiteral("%1=\"%2\"").arg(attribute.nodeName(), attribute.nodeValue()));
        attributeValues.insert(attribute.nodeName(), attribute.nodeValue());
    }
    item->setData(0, kXmlAttributesRole, attributeValues);
    if (!attributes.isEmpty()) {
        item->setText(1, attributes.join(QStringLiteral(" ")));
    }

    for (QDomNode child = node.firstChild(); !child.isNull(); child = child.nextSibling()) {
        appendXmlNode(item, child);
    }
}

QString yamlScalarText(const QString& valueText)
{
    const QString trimmed = valueText.trimmed();
    if (trimmed.isEmpty()) {
        return QString();
    }

    if ((trimmed.startsWith(QLatin1Char('"')) && trimmed.endsWith(QLatin1Char('"')))
        || (trimmed.startsWith(QLatin1Char('\'')) && trimmed.endsWith(QLatin1Char('\'')))) {
        return trimmed.mid(1, trimmed.size() - 2);
    }

    return trimmed;
}

int yamlIndentLevel(const QString& line)
{
    int count = 0;
    while (count < line.size() && line.at(count) == QLatin1Char(' ')) {
        ++count;
    }
    return count;
}

bool isStructuredOverrideSuffix(const QString& filePath)
{
    const QString suffix = QFileInfo(filePath).suffix().trimmed().toLower();
    return suffix == QStringLiteral("json")
        || suffix == QStringLiteral("xml")
        || suffix == QStringLiteral("yaml")
        || suffix == QStringLiteral("yml");
}

QString jsonStringLiteral(const QString& text)
{
    const QByteArray arrayText = QJsonDocument(QJsonArray{ QJsonValue(text) }).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(arrayText.mid(1, arrayText.size() - 2));
}

QJsonValue jsonValueFromTreeItem(QTreeWidgetItem* item)
{
    if (!item) {
        return QJsonValue();
    }

    const QString nodeKind = item->data(0, kNodeKindRole).toString();
    if (nodeKind == QStringLiteral("json_object")) {
        QJsonObject object;
        for (int index = 0; index < item->childCount(); ++index) {
            QTreeWidgetItem* child = item->child(index);
            object.insert(child->text(0), jsonValueFromTreeItem(child));
        }
        return object;
    }

    if (nodeKind == QStringLiteral("json_array")) {
        QJsonArray array;
        for (int index = 0; index < item->childCount(); ++index) {
            array.append(jsonValueFromTreeItem(item->child(index)));
        }
        return array;
    }

    if (nodeKind == QStringLiteral("json_string")) {
        return QJsonValue(item->data(0, kScalarTextRole).toString());
    }

    if (nodeKind == QStringLiteral("json_number")) {
        return QJsonValue(item->data(0, kScalarTextRole).toString().toDouble());
    }

    if (nodeKind == QStringLiteral("json_bool")) {
        return QJsonValue(item->data(0, kScalarBoolRole).toBool());
    }

    if (nodeKind == QStringLiteral("json_null")) {
        return QJsonValue(QJsonValue::Null);
    }

    return QJsonValue(item->text(1));
}

QString jsonValueToText(const QJsonValue& value)
{
    if (value.isObject()) {
        return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Indented)).trimmed();
    }

    if (value.isArray()) {
        return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Indented)).trimmed();
    }

    if (value.isString()) {
        return jsonStringLiteral(value.toString());
    }

    if (value.isDouble()) {
        return QString::number(value.toDouble());
    }

    if (value.isBool()) {
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    }

    if (value.isNull()) {
        return QStringLiteral("null");
    }

    return QString();
}

void writeXmlTreeItem(QXmlStreamWriter& writer, QTreeWidgetItem* item)
{
    if (!item) {
        return;
    }

    const QString nodeKind = item->data(0, kNodeKindRole).toString();
    if (nodeKind == QStringLiteral("xml_text")) {
        writer.writeCharacters(item->data(0, kScalarTextRole).toString());
        return;
    }

    if (nodeKind != QStringLiteral("xml_element")) {
        return;
    }

    writer.writeStartElement(item->data(0, kXmlTagNameRole).toString());
    const QVariantMap attributes = item->data(0, kXmlAttributesRole).toMap();
    for (auto it = attributes.constBegin(); it != attributes.constEnd(); ++it) {
        writer.writeAttribute(it.key(), it.value().toString());
    }

    for (int index = 0; index < item->childCount(); ++index) {
        writeXmlTreeItem(writer, item->child(index));
    }

    writer.writeEndElement();
}

QString xmlSubtreeText(QTreeWidgetItem* item)
{
    QString output;
    QXmlStreamWriter writer(&output);
    writer.setAutoFormatting(true);
    writeXmlTreeItem(writer, item);
    return output.trimmed();
}

void appendYamlLines(QTreeWidgetItem* item, int indent, QStringList& lines)
{
    if (!item) {
        return;
    }

    const QString indentText(indent, QLatin1Char(' '));
    const QString key = item->text(0);
    const QString value = item->text(1).trimmed();

    if (key == QStringLiteral("-")) {
        if (item->childCount() <= 0) {
            lines.append(indentText + QStringLiteral("- ") + value);
            return;
        }

        lines.append(indentText + QStringLiteral("-"));
        for (int index = 0; index < item->childCount(); ++index) {
            appendYamlLines(item->child(index), indent + 2, lines);
        }
        return;
    }

    if (item->childCount() <= 0) {
        lines.append(value.isEmpty()
            ? indentText + key + QStringLiteral(":")
            : indentText + key + QStringLiteral(": ") + value);
        return;
    }

    lines.append(indentText + key + QStringLiteral(":"));
    for (int index = 0; index < item->childCount(); ++index) {
        appendYamlLines(item->child(index), indent + 2, lines);
    }
}

QString yamlSubtreeText(QTreeWidgetItem* item)
{
    QStringList lines;
    appendYamlLines(item, 0, lines);
    return lines.join(QStringLiteral("\n")).trimmed();
}

QString structuredFormatForPath(const QString& filePath)
{
    const QString suffix = QFileInfo(filePath).suffix().trimmed().toLower();
    if (suffix == QStringLiteral("xml")) {
        return QStringLiteral("xml");
    }
    if (suffix == QStringLiteral("yaml") || suffix == QStringLiteral("yml")) {
        return QStringLiteral("yaml");
    }
    return QStringLiteral("json");
}

QString formattedJsonPreviewText(const QString& text)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || document.isNull()) {
        return text;
    }

    return QString::fromUtf8(document.toJson(QJsonDocument::Indented));
}

QString formattedXmlPreviewText(const QString& text)
{
    QDomDocument document;
    QString errorMessage;
    int errorLine = 0;
    int errorColumn = 0;
    if (!document.setContent(text, &errorMessage, &errorLine, &errorColumn)) {
        Q_UNUSED(errorMessage);
        Q_UNUSED(errorLine);
        Q_UNUSED(errorColumn);
        return text;
    }

    return document.toString(2);
}

QString formattedStructuredPreviewText(const QString& filePath, const QString& text)
{
    const QString suffix = QFileInfo(filePath).suffix().trimmed().toLower();
    if (suffix == QStringLiteral("json")) {
        return formattedJsonPreviewText(text);
    }
    if (suffix == QStringLiteral("xml")) {
        return formattedXmlPreviewText(text);
    }
    return text;
}

QString treeItemRowText(QTreeWidgetItem* item, int columnCount)
{
    if (!item) {
        return QString();
    }

    QStringList parts;
    for (int column = 0; column < columnCount; ++column) {
        const QString text = item->text(column).trimmed();
        if (!text.isEmpty()) {
            parts.append(text);
        }
    }
    return parts.join(QStringLiteral("\t"));
}

}

CodeRenderer::CodeRenderer(PreviewState* previewState, QWidget* parent)
    : QWidget(parent)
    , m_headerRow(new QWidget(this))
    , m_iconLabel(new QLabel(this))
    , m_titleLabel(new SelectableTitleLabel(this))
    , m_metaLabel(new QLabel(this))
    , m_pathRow(new QWidget(this))
    , m_pathTitleLabel(new QLabel(this))
    , m_pathValueLabel(new QLabel(this))
    , m_openWithButton(new OpenWithButton(this))
    , m_contentSection(new QWidget(this))
    , m_statusRow(new QWidget(this))
    , m_searchRow(new QWidget(this))
    , m_searchEdit(new QLineEdit(this))
    , m_searchPreviousButton(new QToolButton(this))
    , m_searchNextButton(new QToolButton(this))
    , m_searchCountLabel(new QLabel(this))
    , m_statusLabel(new QLabel(this))
    , m_modeSwitchButton(new ModeSwitchButton(this))
    , m_contentStack(new QStackedWidget(this))
    , m_loadingCard(new QWidget(this))
    , m_loadingTitleLabel(new QLabel(this))
    , m_loadingMessageLabel(new QLabel(this))
    , m_textEdit(new CodeTextEdit(this))
    , m_treeView(new QTreeWidget(this))
    , m_previewState(previewState)
{
    setAttribute(Qt::WA_StyledBackground, true);
    setObjectName(QStringLiteral("CodeRendererRoot"));
    m_headerRow->setObjectName(QStringLiteral("CodeHeaderRow"));
    m_iconLabel->setObjectName(QStringLiteral("CodeTypeIcon"));
    m_titleLabel->setObjectName(QStringLiteral("CodeTitle"));
    m_metaLabel->setObjectName(QStringLiteral("CodeMeta"));
    m_pathRow->setObjectName(QStringLiteral("CodePathRow"));
    m_pathTitleLabel->setObjectName(QStringLiteral("CodePathTitle"));
    m_pathValueLabel->setObjectName(QStringLiteral("CodePathValue"));
    m_openWithButton->setObjectName(QStringLiteral("CodeOpenWithButton"));
    m_contentSection->setObjectName(QStringLiteral("CodeContentSection"));
    m_statusRow->setObjectName(QStringLiteral("CodeStatusRow"));
    m_searchRow->setObjectName(QStringLiteral("CodeSearchRow"));
    m_searchEdit->setObjectName(QStringLiteral("CodeSearchEdit"));
    m_searchPreviousButton->setObjectName(QStringLiteral("CodeSearchPreviousButton"));
    m_searchNextButton->setObjectName(QStringLiteral("CodeSearchNextButton"));
    m_searchCountLabel->setObjectName(QStringLiteral("CodeSearchCount"));
    m_statusLabel->setObjectName(QStringLiteral("CodeStatus"));
    m_modeSwitchButton->setObjectName(QStringLiteral("CodeModeSwitchButton"));
    m_contentStack->setObjectName(QStringLiteral("CodeContentStack"));
    m_loadingCard->setObjectName(QStringLiteral("CodeLoadingCard"));
    m_loadingTitleLabel->setObjectName(QStringLiteral("CodeLoadingTitle"));
    m_loadingMessageLabel->setObjectName(QStringLiteral("CodeLoadingMessage"));
    m_textEdit->setObjectName(QStringLiteral("CodeContent"));
    m_treeView->setObjectName(QStringLiteral("CodeTreeContent"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 0, 12, 12);
    layout->setSpacing(14);
    layout->addWidget(m_headerRow);
    layout->addWidget(m_contentSection, 1);

    auto* headerLayout = new QHBoxLayout(m_headerRow);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(12);
    auto* trailingActions = new QWidget(m_headerRow);
    auto* trailingActionsLayout = new QHBoxLayout(trailingActions);
    trailingActionsLayout->setContentsMargins(0, 0, 0, 0);
    trailingActionsLayout->setSpacing(12);
    trailingActionsLayout->addWidget(m_openWithButton, 0, Qt::AlignVCenter);

    auto* titleBlock = new PreviewHeaderBar(m_iconLabel, m_titleLabel, m_pathRow, trailingActions, m_headerRow);
    headerLayout->addWidget(titleBlock->contentWidget(), 1);

    auto* pathLayout = new QHBoxLayout(m_pathRow);
    pathLayout->setContentsMargins(0, 0, 0, 0);
    pathLayout->setSpacing(8);
    pathLayout->addWidget(m_pathValueLabel, 1);

    auto* contentSectionLayout = new QVBoxLayout(m_contentSection);
    contentSectionLayout->setContentsMargins(0, 0, 0, 0);
    contentSectionLayout->setSpacing(12);
    contentSectionLayout->addWidget(m_searchRow, 0);
    contentSectionLayout->addWidget(m_contentStack, 1);
    contentSectionLayout->addWidget(m_statusRow, 0);

    auto* statusLayout = new QHBoxLayout(m_statusRow);
    statusLayout->setContentsMargins(0, 0, 0, 0);
    statusLayout->setSpacing(12);
    statusLayout->addWidget(m_statusLabel, 1);
    statusLayout->addWidget(m_modeSwitchButton, 0, Qt::AlignRight | Qt::AlignVCenter);

    auto* searchLayout = new QHBoxLayout(m_searchRow);
    searchLayout->setContentsMargins(8, 4, 8, 4);
    searchLayout->setSpacing(6);
    m_searchEdit->setPlaceholderText(QCoreApplication::translate("SpaceLook", "Search"));
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setFixedWidth(160);
    m_searchPreviousButton->setText(QStringLiteral("<"));
    m_searchNextButton->setText(QStringLiteral(">"));
    m_searchPreviousButton->setFixedSize(24, 24);
    m_searchNextButton->setFixedSize(24, 24);
    m_searchCountLabel->setMinimumWidth(46);
    searchLayout->addWidget(m_searchEdit);
    searchLayout->addWidget(m_searchPreviousButton);
    searchLayout->addWidget(m_searchNextButton);
    searchLayout->addWidget(m_searchCountLabel);
    searchLayout->addStretch(1);
    m_searchRow->hide();

    m_textEdit->setReadOnly(true);
    m_textEdit->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_textEdit->setFrameShape(QFrame::NoFrame);
    m_treeView->setRootIsDecorated(true);
    m_treeView->setAlternatingRowColors(true);
    m_treeView->setUniformRowHeights(false);
    m_treeView->setExpandsOnDoubleClick(true);
    m_treeView->setAnimated(true);
    m_treeView->setAllColumnsShowFocus(true);
    m_treeView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_treeView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_treeView->setTextElideMode(Qt::ElideNone);
    m_treeView->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_treeView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_treeView->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_treeView->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_treeView->setWordWrap(false);
    m_treeView->setFocusPolicy(Qt::StrongFocus);
    m_treeView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_treeView->setHeaderLabels({ QCoreApplication::translate("SpaceLook", "Node"), QCoreApplication::translate("SpaceLook", "Value") });
    m_treeView->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_treeView->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_treeView->header()->hide();
    m_treeView->setFrameShape(QFrame::NoFrame);
    m_modeSwitchButton->hide();
    QFont modeFont;
    modeFont.setFamily(QStringLiteral("Segoe UI Rounded"));
    modeFont.setPixelSize(12);
    modeFont.setWeight(QFont::DemiBold);
    m_modeSwitchButton->setMenuFont(modeFont);
    auto* loadingLayout = new QVBoxLayout(m_loadingCard);
    loadingLayout->setContentsMargins(24, 24, 24, 24);
    loadingLayout->setSpacing(10);
    loadingLayout->addWidget(m_loadingTitleLabel);
    loadingLayout->addWidget(m_loadingMessageLabel);
    loadingLayout->addStretch(1);
    m_contentStack->addWidget(m_loadingCard);
    m_contentStack->addWidget(m_textEdit);
    m_contentStack->addWidget(m_treeView);
    m_iconLabel->setFixedSize(72, 72);
    m_iconLabel->setScaledContents(false);
    m_iconLabel->setAlignment(Qt::AlignCenter);
    m_titleLabel->setWordWrap(true);
    m_pathTitleLabel->hide();
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
    PreviewStateVisuals::prepareStatusLabel(m_statusLabel);
    m_statusLabel->hide();
    m_loadingTitleLabel->setText(QCoreApplication::translate("SpaceLook", "Preparing code preview"));
    m_loadingMessageLabel->setText(QCoreApplication::translate("SpaceLook", "Header details are ready. Source content is loading in the background."));
    m_loadingMessageLabel->setWordWrap(true);
    PreviewStateVisuals::prepareStateCard(
        m_loadingCard,
        m_loadingTitleLabel,
        m_loadingMessageLabel,
        nullptr,
        PreviewStateVisuals::Kind::Loading);

    connect(m_titleLabel, &SelectableTitleLabel::copyFeedbackRequested, this, [this](const QString& message) {
        showStatusMessage(message);
    });
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this]() {
        findSearchMatch(false);
    });
    connect(m_searchEdit, &QLineEdit::returnPressed, this, [this]() {
        findSearchMatch(false);
    });
    connect(m_searchPreviousButton, &QToolButton::clicked, this, [this]() {
        findSearchMatch(true);
    });
    connect(m_searchNextButton, &QToolButton::clicked, this, [this]() {
        findSearchMatch(false);
    });
    auto* findShortcut = new QShortcut(QKeySequence::Find, this);
    findShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(findShortcut, &QShortcut::activated, this, &CodeRenderer::showSearchRow);
    auto* hideSearchShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), m_searchRow);
    hideSearchShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(hideSearchShortcut, &QShortcut::activated, this, [this]() {
        hideSearchRow(false);
    });
    connect(m_treeView, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item, int) {
        toggleStructuredItem(item);
    });
    connect(m_treeView, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int) {
        toggleStructuredItem(item);
    });
    connect(m_treeView, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint& position) {
        showStructuredContextMenu(position);
    });
    connect(m_treeView, &QTreeWidget::itemSelectionChanged, this, [this]() {
        const QList<QTreeWidgetItem*> items = m_treeView->selectedItems();
        if (items.isEmpty()) {
            return;
        }

        const QString rowText = treeItemRowText(items.constFirst(), m_treeView->columnCount());
        if (!rowText.isEmpty()) {
            QApplication::clipboard()->setText(rowText);
        }
    });
    m_modeSwitchButton->setModeChangedCallback([this](const QString& rendererId) {
        if (!m_previewState) {
            return;
        }

        const QString filePath = m_info.filePath.trimmed();
        if (!isStructuredOverrideSuffix(filePath)) {
            return;
        }
        m_previewState->setRendererOverride(rendererId);
        if (SpaceLookWindow* previewWindow = qobject_cast<SpaceLookWindow*>(window())) {
            previewWindow->refreshCurrentPreview();
        }
    });

    applyChrome();
}

CodeRenderer::~CodeRenderer() = default;

QString CodeRenderer::rendererId() const
{
    return QStringLiteral("code");
}

bool CodeRenderer::canHandle(const HoveredItemInfo& info) const
{
    return info.typeKey == QStringLiteral("code");
}

QWidget* CodeRenderer::widget()
{
    return this;
}

bool CodeRenderer::reportsLoadingState() const
{
    return true;
}

void CodeRenderer::setLoadingStateCallback(std::function<void(bool)> callback)
{
    m_loadingStateCallback = std::move(callback);
}

void CodeRenderer::load(const HoveredItemInfo& info)
{
    cancelPreviewTask(m_cancelToken);
    const PreviewCancellationToken cancelToken = makePreviewCancellationToken();
    m_cancelToken = cancelToken;
    m_info = info;
    const PreviewLoadGuard::Token loadToken = m_loadGuard.begin(info.filePath);
    notifyLoadingState(true);
    qDebug().noquote() << QStringLiteral("[SpaceLookRender] CodeRenderer load path=\"%1\"").arg(info.filePath);

    m_titleLabel->setText(info.title.isEmpty() ? QCoreApplication::translate("SpaceLook", "Code Preview") : info.title);
    m_titleLabel->setCopyText(m_titleLabel->text());
    m_iconLabel->setPixmap(FileTypeIconResolver::pixmapForInfo(info, m_iconLabel->contentsRect().size()));
    m_pathValueLabel->setText(info.filePath.trimmed().isEmpty() ? QCoreApplication::translate("SpaceLook", "(Unavailable)") : info.filePath);
    m_openWithButton->setTargetContext(info.filePath, info.typeKey);
    updateModeSelector(info.filePath);
    hideSearchRow(true);
    m_loadingTitleLabel->setText(QCoreApplication::translate("SpaceLook", "Preparing code preview"));
    m_loadingMessageLabel->setText(QCoreApplication::translate("SpaceLook", "Header details are ready. Source content is loading in the background."));
    PreviewStateVisuals::prepareStateCard(
        m_loadingCard,
        m_loadingTitleLabel,
        m_loadingMessageLabel,
        nullptr,
        PreviewStateVisuals::Kind::Loading);
    m_contentStack->setCurrentWidget(m_loadingCard);
    m_textEdit->clear();
    PreviewStateVisuals::showStatus(m_statusLabel, QCoreApplication::translate("SpaceLook", "Loading code preview..."), PreviewStateVisuals::Kind::Loading);

    auto* watcher = new QFutureWatcher<CodeLoadResult>(this);
    connect(watcher, &QFutureWatcher<CodeLoadResult>::finished, this, [this, watcher, loadToken, cancelToken]() {
        watcher->deleteLater();

        if (previewCancellationRequested(cancelToken) || !m_loadGuard.isCurrent(loadToken, m_info.filePath)) {
            qDebug().noquote() << QStringLiteral("[SpaceLookRender] CodeRenderer discarded stale async result path=\"%1\"")
                .arg(loadToken.path);
            return;
        }

        const CodeLoadResult result = watcher->result();
        if (!result.success) {
            PreviewStateVisuals::showStatus(m_statusLabel, result.statusMessage, PreviewStateVisuals::Kind::Error);
            m_textEdit->setPlainText(result.text);
            m_contentStack->setCurrentWidget(m_textEdit);
            resetSearch();
            notifyLoadingState(false);
            return;
        }

        ensureRepository();
        const QString previewText = formattedStructuredPreviewText(loadToken.path, result.text);
        showStructuredPreview(loadToken.path, previewText);
        resetSearch();
        if (result.statusMessage.trimmed().isEmpty()) {
            PreviewStateVisuals::showStatus(m_statusLabel, QCoreApplication::translate("SpaceLook", "Encoding: %1").arg(result.encodingName));
        } else {
            PreviewStateVisuals::showStatus(m_statusLabel, QCoreApplication::translate("SpaceLook", "Encoding: %1. %2").arg(result.encodingName, result.statusMessage));
        }

        qDebug().noquote() << QStringLiteral("[SpaceLookRender] CodeRenderer loaded async chars=%1 path=\"%2\"")
            .arg(result.text.size())
            .arg(loadToken.path);
        notifyLoadingState(false);
    });
    watcher->setFuture(QtConcurrent::run([filePath = info.filePath, cancelToken]() {
        return loadCodePreviewContent(filePath, cancelToken);
    }));
}

void CodeRenderer::unload()
{
    cancelPreviewTask(m_cancelToken);
    m_loadGuard.cancel();
    notifyLoadingState(false);
    m_textEdit->clear();
    clearStructuredPreview();
    hideSearchRow(true);
    m_contentStack->setCurrentWidget(m_loadingCard);
    m_pathValueLabel->clear();
    m_openWithButton->setTargetContext(QString(), QString());
    PreviewStateVisuals::clearStatus(m_statusLabel);
    if (m_highlighter) {
        m_highlighter->setDocument(nullptr);
        delete m_highlighter;
        m_highlighter = nullptr;
    }
    m_info = HoveredItemInfo();
}

void CodeRenderer::notifyLoadingState(bool loading)
{
    if (m_loadingStateCallback) {
        m_loadingStateCallback(loading);
    }
}

void CodeRenderer::findSearchMatch(bool backwards)
{
    if (m_contentStack && m_contentStack->currentWidget() == m_treeView) {
        rebuildStructuredSearchMatches();
        if (m_searchMatches.isEmpty()) {
            updateSearchSummary();
            return;
        }

        const int nextIndex = m_searchMatchIndex < 0
            ? 0
            : (backwards
                ? (m_searchMatchIndex + m_searchMatches.size() - 1) % m_searchMatches.size()
                : (m_searchMatchIndex + 1) % m_searchMatches.size());
        selectStructuredSearchMatch(nextIndex);
        updateSearchSummary();
        return;
    }

    findTextSearchMatch(backwards);
}

void CodeRenderer::findTextSearchMatch(bool backwards)
{
    const QString query = m_searchEdit ? m_searchEdit->text() : QString();
    if (query.trimmed().isEmpty() || !m_textEdit || !m_textEdit->document()) {
        updateSearchSummary();
        return;
    }

    QTextDocument::FindFlags flags;
    if (backwards) {
        flags |= QTextDocument::FindBackward;
    }

    QTextCursor found = m_textEdit->document()->find(query, m_textEdit->textCursor(), flags);
    if (found.isNull()) {
        QTextCursor wrapCursor(m_textEdit->document());
        wrapCursor.movePosition(backwards ? QTextCursor::End : QTextCursor::Start);
        found = m_textEdit->document()->find(query, wrapCursor, flags);
    }
    if (!found.isNull()) {
        m_textEdit->setTextCursor(found);
        m_textEdit->ensureCursorVisible();
    }
    updateSearchSummary();
}

void CodeRenderer::rebuildStructuredSearchMatches()
{
    const QString query = m_searchEdit ? m_searchEdit->text().trimmed() : QString();
    if (query == m_lastSearchQuery && !m_searchMatches.isEmpty()) {
        return;
    }

    m_lastSearchQuery = query;
    m_searchMatches.clear();
    m_searchMatchIndex = -1;
    if (query.isEmpty() || !m_treeView) {
        return;
    }

    QTreeWidgetItemIterator iterator(m_treeView);
    while (*iterator) {
        QTreeWidgetItem* item = *iterator;
        if (item->text(0).contains(query, Qt::CaseInsensitive) ||
            item->text(1).contains(query, Qt::CaseInsensitive)) {
            m_searchMatches.append(item);
        }
        ++iterator;
    }
}

void CodeRenderer::selectStructuredSearchMatch(int index)
{
    if (!m_treeView || index < 0 || index >= m_searchMatches.size()) {
        return;
    }

    QTreeWidgetItem* item = m_searchMatches.at(index);
    for (QTreeWidgetItem* parent = item ? item->parent() : nullptr; parent; parent = parent->parent()) {
        parent->setExpanded(true);
    }
    m_searchMatchIndex = index;
    m_treeView->setCurrentItem(item);
    m_treeView->scrollToItem(item, QAbstractItemView::PositionAtCenter);
}

void CodeRenderer::updateSearchSummary()
{
    if (!m_searchCountLabel) {
        return;
    }

    const QString query = m_searchEdit ? m_searchEdit->text() : QString();
    if (query.trimmed().isEmpty()) {
        m_searchCountLabel->clear();
        return;
    }

    if (m_contentStack && m_contentStack->currentWidget() == m_treeView) {
        rebuildStructuredSearchMatches();
        m_searchCountLabel->setText(m_searchMatches.isEmpty()
            ? QStringLiteral("0/0")
            : QStringLiteral("%1/%2").arg(m_searchMatchIndex >= 0 ? m_searchMatchIndex + 1 : 1).arg(m_searchMatches.size()));
        return;
    }

    if (!m_textEdit || !m_textEdit->document()) {
        m_searchCountLabel->setText(QStringLiteral("0/0"));
        return;
    }

    int total = 0;
    int current = 0;
    const int selectionStart = m_textEdit->textCursor().selectionStart();
    QTextCursor cursor(m_textEdit->document());
    while (true) {
        cursor = m_textEdit->document()->find(query, cursor);
        if (cursor.isNull()) {
            break;
        }
        ++total;
        if (cursor.selectionStart() == selectionStart) {
            current = total;
        }
    }
    m_searchCountLabel->setText(total > 0
        ? QStringLiteral("%1/%2").arg(current > 0 ? current : 1).arg(total)
        : QStringLiteral("0/0"));
}

void CodeRenderer::resetSearch()
{
    m_searchMatches.clear();
    m_lastSearchQuery.clear();
    m_searchMatchIndex = -1;
    if (m_searchEdit && !m_searchEdit->text().trimmed().isEmpty()) {
        findSearchMatch(false);
        return;
    }
    if (m_searchCountLabel) {
        m_searchCountLabel->clear();
    }
}

void CodeRenderer::showSearchRow()
{
    if (!m_searchRow) {
        return;
    }

    m_searchRow->show();
    if (m_searchEdit) {
        m_searchEdit->setFocus(Qt::ShortcutFocusReason);
        m_searchEdit->selectAll();
    }
}

void CodeRenderer::hideSearchRow(bool clearQuery)
{
    if (clearQuery && m_searchEdit) {
        m_searchEdit->clear();
    }
    m_searchMatches.clear();
    m_lastSearchQuery.clear();
    m_searchMatchIndex = -1;
    if (m_searchCountLabel) {
        m_searchCountLabel->clear();
    }
    if (m_searchRow) {
        m_searchRow->hide();
    }
}

void CodeRenderer::showStatusMessage(const QString& message)
{
    if (message.trimmed().isEmpty()) {
        PreviewStateVisuals::clearStatus(m_statusLabel);
        return;
    }

    PreviewStateVisuals::showStatus(m_statusLabel, message);
    QTimer::singleShot(1400, m_statusLabel, [label = m_statusLabel]() {
        if (label) {
            PreviewStateVisuals::clearStatus(label);
        }
    });
}

void CodeRenderer::applyChrome()
{
    setStyleSheet(
        "#CodeRendererRoot {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:1,"
        "      stop:0 #fcfdff,"
        "      stop:1 #f4f8fc);"
        "  border-radius: 0px;"
        "}"
        "QLabel {"
        "  color: #18324a;"
        "}"
        "#CodeTypeIcon {"
        "  background: transparent;"
        "  border: none;"
        "  padding: 0px;"
        "}"
        "#CodeTitle {"
        "  color: #0f2740;"
        "}"
        "#CodeMeta {"
        "  color: #5c738b;"
        "}"
        "#CodePathTitle {"
        "  color: #16324a;"
        "  font-family: 'Segoe UI Rounded';"
        "}"
        "#CodePathValue {"
        "  color: #445d76;"
        "}"
        "#CodeOpenWithButton QToolButton {"
        "  background: rgba(238, 244, 252, 0.96);"
        "  color: #17324b;"
        "  border: 1px solid rgba(193, 208, 224, 0.95);"
        "  padding: 3px 8px;"
        "  min-height: 24px;"
        "}"
        "#CodeOpenWithButton QToolButton:hover {"
        "  background: rgba(245, 249, 255, 1.0);"
        "}"
        "#CodeOpenWithButton QToolButton:pressed {"
        "  background: rgba(224, 234, 246, 1.0);"
        "}"
        "#CodeOpenWithButton #OpenWithPrimaryButton {"
        "  border-top-left-radius: 10px;"
        "  border-bottom-left-radius: 10px;"
        "  border-top-right-radius: 0px;"
        "  border-bottom-right-radius: 0px;"
        "  min-width: 28px;"
        "}"
        "#CodeOpenWithButton #OpenWithExpandButton {"
        "  border-left: none;"
        "  border-top-left-radius: 0px;"
        "  border-bottom-left-radius: 0px;"
        "  border-top-right-radius: 10px;"
        "  border-bottom-right-radius: 10px;"
        "  min-width: 22px;"
        "  padding-left: 5px;"
        "  padding-right: 5px;"
        "}"
        "#CodeModeSwitchButton QToolButton {"
        "  background: rgba(238, 244, 252, 0.96);"
        "  color: #17324b;"
        "  border: 1px solid rgba(193, 208, 224, 0.95);"
        "  padding: 3px 8px;"
        "  min-height: 24px;"
        "}"
        "#CodeModeSwitchButton QToolButton:hover {"
        "  background: rgba(245, 249, 255, 1.0);"
        "}"
        "#CodeModeSwitchButton QToolButton:pressed {"
        "  background: rgba(224, 234, 246, 1.0);"
        "}"
        "#CodeModeSwitchButton #ModeSwitchPrimaryButton {"
        "  border-top-left-radius: 10px;"
        "  border-bottom-left-radius: 10px;"
        "  border-top-right-radius: 0px;"
        "  border-bottom-right-radius: 0px;"
        "  min-width: 46px;"
        "}"
        "#CodeModeSwitchButton #ModeSwitchExpandButton {"
        "  border-left: none;"
        "  border-top-left-radius: 0px;"
        "  border-bottom-left-radius: 0px;"
        "  border-top-right-radius: 10px;"
        "  border-bottom-right-radius: 10px;"
        "  min-width: 20px;"
        "  padding-left: 5px;"
        "  padding-right: 5px;"
        "}"
        "#CodeStatusRow {"
        "  background: transparent;"
        "}"
        "#CodeContentSection {"
        "  background: transparent;"
        "}"
        "#CodeStatus {"
        "  color: #6e4f12;"
        "  background: rgba(255, 239, 206, 0.92);"
        "  border: 1px solid rgba(232, 201, 131, 0.95);"
        "  border-radius: 12px;"
        "  padding: 10px 14px;"
        "}"
        "#CodeSearchRow {"
        "  background: rgba(238, 244, 252, 0.96);"
        "  border: 1px solid rgba(193, 208, 224, 0.95);"
        "  border-radius: 12px;"
        "}"
        "#CodeSearchEdit {"
        "  color: #17324b;"
        "  background: transparent;"
        "  border: none;"
        "  selection-background-color: #cfe3ff;"
        "}"
        "#CodeSearchPreviousButton, #CodeSearchNextButton {"
        "  color: #17324b;"
        "  background: transparent;"
        "  border: none;"
        "  border-radius: 8px;"
        "}"
        "#CodeSearchPreviousButton:hover, #CodeSearchNextButton:hover {"
        "  background: rgba(215, 229, 246, 0.95);"
        "}"
        "#CodeSearchCount {"
        "  color: #526b85;"
        "}"
        "#CodeContentStack {"
        "  background: transparent;"
        "}"
        "#CodeLoadingCard {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:1,"
        "      stop:0 rgba(248, 250, 253, 0.98),"
        "      stop:1 rgba(240, 244, 249, 0.98));"
        "  border: 1px solid rgba(206, 216, 227, 0.96);"
        "  border-radius: 18px;"
        "}"
        "#CodeLoadingTitle {"
        "  color: #10273f;"
        "}"
        "#CodeLoadingMessage {"
        "  color: #5c7187;"
        "}"
        "#CodeContent {"
        "  background: #f4f7fb;"
        "  border: 1px solid #ccd6e2;"
        "  border-radius: 18px;"
        "  color: #24292e;"
        "  selection-background-color: #dee6fc;"
        "  selection-color: #24292e;"
        "}"
        "#CodeTreeContent {"
        "  background: #f4f7fb;"
        "  border: 1px solid #ccd6e2;"
        "  border-radius: 18px;"
        "  color: #1f2937;"
        "  alternate-background-color: #eef3f9;"
        "  outline: none;"
        "}"
        "#CodeTreeContent::item {"
        "  padding: 4px 6px;"
        "}"
        "#CodeTreeContent QHeaderView::section {"
        "  background: #e9eff7;"
        "  color: #29415a;"
        "  border: none;"
        "  border-bottom: 1px solid #ccd6e2;"
        "  padding: 8px 10px;"
        "  font-family: 'Segoe UI Rounded';"
        "}"
        "#CodeContent QScrollBar:vertical,"
        "#CodeTreeContent QScrollBar:vertical {"
        "  background: rgba(232, 238, 245, 0.8);"
        "  border: none;"
        "  width: 8px;"
        "  margin: 10px 0 10px 0;"
        "  border-radius: 4px;"
        "}"
        "#CodeContent QScrollBar::handle:vertical,"
        "#CodeTreeContent QScrollBar::handle:vertical {"
        "  background: #7c8fa8;"
        "  min-height: 52px;"
        "  border-radius: 4px;"
        "}"
        "#CodeContent QScrollBar::handle:vertical:hover,"
        "#CodeTreeContent QScrollBar::handle:vertical:hover {"
        "  background: #6d829d;"
        "}"
        "#CodeContent QScrollBar::handle:vertical:pressed,"
        "#CodeTreeContent QScrollBar::handle:vertical:pressed {"
        "  background: #61768f;"
        "}"
        "#CodeContent QScrollBar:horizontal,"
        "#CodeTreeContent QScrollBar:horizontal {"
        "  background: rgba(232, 238, 245, 0.8);"
        "  border: none;"
        "  height: 8px;"
        "  margin: 0 10px 0 10px;"
        "  border-radius: 4px;"
        "}"
        "#CodeContent QScrollBar::handle:horizontal,"
        "#CodeTreeContent QScrollBar::handle:horizontal {"
        "  background: #7c8fa8;"
        "  min-width: 52px;"
        "  border-radius: 4px;"
        "}"
        "#CodeContent QScrollBar::handle:horizontal:hover,"
        "#CodeTreeContent QScrollBar::handle:horizontal:hover {"
        "  background: #6d829d;"
        "}"
        "#CodeContent QScrollBar::handle:horizontal:pressed,"
        "#CodeTreeContent QScrollBar::handle:horizontal:pressed {"
        "  background: #61768f;"
        "}"
        "#CodeContent QScrollBar::add-line:vertical, #CodeContent QScrollBar::sub-line:vertical,"
        "#CodeContent QScrollBar::add-line:horizontal, #CodeContent QScrollBar::sub-line:horizontal,"
        "#CodeTreeContent QScrollBar::add-line:vertical, #CodeTreeContent QScrollBar::sub-line:vertical,"
        "#CodeTreeContent QScrollBar::add-line:horizontal, #CodeTreeContent QScrollBar::sub-line:horizontal {"
        "  width: 0px;"
        "  height: 0px;"
        "}"
        "#CodeContent QScrollBar::add-page:vertical, #CodeContent QScrollBar::sub-page:vertical,"
        "#CodeContent QScrollBar::add-page:horizontal, #CodeContent QScrollBar::sub-page:horizontal,"
        "#CodeTreeContent QScrollBar::add-page:vertical, #CodeTreeContent QScrollBar::sub-page:vertical,"
        "#CodeTreeContent QScrollBar::add-page:horizontal, #CodeTreeContent QScrollBar::sub-page:horizontal {"
        "  background: transparent;"
        "}"
    );

    QFont titleFont;
    titleFont.setFamily(QStringLiteral("Segoe UI Rounded"));
    titleFont.setPixelSize(20);
    titleFont.setWeight(QFont::Bold);
    m_titleLabel->setFont(titleFont);
    m_titleLabel->setWordWrap(true);

    QFont metaFont;
    metaFont.setFamily(QStringLiteral("Segoe UI Rounded"));
    metaFont.setPixelSize(13);
    m_metaLabel->setFont(metaFont);
    m_pathTitleLabel->setFont(metaFont);
    m_pathValueLabel->setFont(metaFont);
    m_statusLabel->setFont(metaFont);
    m_metaLabel->setWordWrap(true);
    m_pathValueLabel->setWordWrap(true);
    m_loadingMessageLabel->setFont(metaFont);

    QFont loadingTitleFont;
    loadingTitleFont.setFamily(QStringLiteral("Segoe UI Rounded"));
    loadingTitleFont.setPixelSize(18);
    m_loadingTitleLabel->setFont(loadingTitleFont);

    QFont textFont;
    textFont.setFamily(QStringLiteral("Cascadia Mono"));
    textFont.setPixelSize(14);
    m_textEdit->setFont(textFont);
    m_treeView->setFont(metaFont);
}

void CodeRenderer::ensureRepository()
{
    if (!m_repository) {
        m_repository = std::make_unique<KSyntaxHighlighting::Repository>();
        const QString searchRoot = CodeThemeManager::themeSearchRoot();
        if (!searchRoot.trimmed().isEmpty()) {
            m_repository->addCustomSearchPath(searchRoot);
        }
    }

    if (!m_highlighter) {
        m_highlighter = new KSyntaxHighlighting::SyntaxHighlighter(m_textEdit->document());
        m_highlighter->setTheme(CodeThemeManager::resolveTheme(*m_repository));
    }
}

void CodeRenderer::applyDefinitionForPath(const QString& filePath)
{
    if (!m_repository || !m_highlighter) {
        return;
    }

    const QString suffix = QFileInfo(filePath).suffix().trimmed().toLower();
    KSyntaxHighlighting::Definition definition;
    if (suffix == QStringLiteral("vue")) {
        definition = m_repository->definitionForName(QStringLiteral("HTML"));
    } else if (suffix == QStringLiteral("astro")) {
        definition = m_repository->definitionForName(QStringLiteral("HTML"));
    } else if (suffix == QStringLiteral("ipynb")) {
        definition = m_repository->definitionForName(QStringLiteral("JSON"));
    } else if (suffix == QStringLiteral("php")) {
        definition = m_repository->definitionForName(QStringLiteral("PHP/PHP"));
    } else if (suffix == QStringLiteral("sc")) {
        definition = m_repository->definitionForName(QStringLiteral("Scala"));
    } else if (suffix == QStringLiteral("hrl")) {
        definition = m_repository->definitionForName(QStringLiteral("Erlang"));
    } else if (suffix == QStringLiteral("t")) {
        definition = m_repository->definitionForName(QStringLiteral("Perl"));
    } else if (suffix == QStringLiteral("asm") || suffix == QStringLiteral("s")) {
        definition = m_repository->definitionForName(QStringLiteral("GNU Assembler"));
    } else if (suffix == QStringLiteral("hlsl")
        || suffix == QStringLiteral("fx")
        || suffix == QStringLiteral("wgsl")
        || suffix == QStringLiteral("metal")) {
        definition = m_repository->definitionForName(QStringLiteral("GLSL"));
    } else if (suffix == QStringLiteral("containerfile")) {
        definition = m_repository->definitionForName(QStringLiteral("Dockerfile"));
    } else if (suffix == QStringLiteral("bazel") || suffix == QStringLiteral("bzl") || suffix == QStringLiteral("build")) {
        definition = m_repository->definitionForName(QStringLiteral("Python"));
    } else if (suffix == QStringLiteral("vcxproj")
        || suffix == QStringLiteral("csproj")
        || suffix == QStringLiteral("fsproj")) {
        definition = m_repository->definitionForName(QStringLiteral("XML"));
    } else {
        definition = m_repository->definitionForFileName(filePath);
    }
    if (!definition.isValid()) {
        qDebug().noquote() << QStringLiteral("[SpaceLookRender] CodeRenderer no syntax definition for: %1").arg(filePath);
        return;
    }

    qDebug().noquote() << QStringLiteral("[SpaceLookRender] CodeRenderer definition=\"%1\" path=\"%2\"")
        .arg(definition.name(), filePath);
    m_highlighter->setDefinition(definition);
    Q_UNUSED(definition);
}

void CodeRenderer::toggleStructuredItem(QTreeWidgetItem* item)
{
    if (!item || item->childCount() <= 0) {
        return;
    }

    item->setExpanded(!item->isExpanded());
}

void CodeRenderer::showStructuredContextMenu(const QPoint& position)
{
    QTreeWidgetItem* item = m_treeView->itemAt(position);
    if (!item) {
        return;
    }

    m_treeView->setCurrentItem(item);

    const QString keyText = item->text(0).trimmed();
    const QString valueText = item->text(1).trimmed();
    const QString rowText = treeItemRowText(item, m_treeView->columnCount());

    QString subtreeText;
    const QString format = structuredFormatForPath(m_info.filePath);
    if (format == QStringLiteral("xml")) {
        subtreeText = xmlSubtreeText(item);
    } else if (format == QStringLiteral("yaml")) {
        subtreeText = yamlSubtreeText(item);
    } else {
        subtreeText = jsonValueToText(jsonValueFromTreeItem(item));
    }

    QMenu menu(this);
    QAction* copyKeyAction = menu.addAction(QCoreApplication::translate("SpaceLook", "Copy Key"));
    QAction* copyValueAction = menu.addAction(QCoreApplication::translate("SpaceLook", "Copy Value"));
    QAction* copyRowAction = menu.addAction(QCoreApplication::translate("SpaceLook", "Copy Row"));
    QAction* copySubtreeAction = menu.addAction(QCoreApplication::translate("SpaceLook", "Copy Subtree"));

    copyKeyAction->setEnabled(!keyText.isEmpty());
    copyValueAction->setEnabled(!valueText.isEmpty());
    copyRowAction->setEnabled(!rowText.isEmpty());
    copySubtreeAction->setEnabled(!subtreeText.isEmpty());

    QAction* selectedAction = menu.exec(m_treeView->viewport()->mapToGlobal(position));
    if (!selectedAction) {
        return;
    }

    if (selectedAction == copyKeyAction) {
        QApplication::clipboard()->setText(keyText);
        showStatusMessage(QCoreApplication::translate("SpaceLook", "Copied key"));
        return;
    }

    if (selectedAction == copyValueAction) {
        QApplication::clipboard()->setText(valueText);
        showStatusMessage(QCoreApplication::translate("SpaceLook", "Copied value"));
        return;
    }

    if (selectedAction == copyRowAction) {
        QApplication::clipboard()->setText(rowText);
        showStatusMessage(QCoreApplication::translate("SpaceLook", "Copied row"));
        return;
    }

    if (selectedAction == copySubtreeAction) {
        QApplication::clipboard()->setText(subtreeText);
        showStatusMessage(QCoreApplication::translate("SpaceLook", "Copied subtree"));
        return;
    }
}

void CodeRenderer::updateModeSelector(const QString& filePath)
{
    const bool supportsStructuredToggle = isStructuredOverrideSuffix(filePath);
    m_modeSwitchButton->setVisible(supportsStructuredToggle);
    if (!supportsStructuredToggle) {
        return;
    }

    const QString currentRenderer = m_previewState && !m_previewState->rendererOverride().trimmed().isEmpty()
        ? m_previewState->rendererOverride()
        : QStringLiteral("code");
    m_modeSwitchButton->setCurrentModeId(currentRenderer);
}

void CodeRenderer::showStructuredPreview(const QString& filePath, const QString& text)
{
    if (isStructuredPreviewPath(filePath)) {
        if (tryLoadJsonPreview(text) || tryLoadXmlPreview(text) || tryLoadYamlPreview(text)) {
            m_contentStack->setCurrentWidget(m_treeView);
            resetSearch();
            return;
        }
    }

    clearStructuredPreview();
    m_textEdit->setPlainText(text);
    m_contentStack->setCurrentWidget(m_textEdit);
    applyDefinitionForPath(filePath);
    resetSearch();
}

void CodeRenderer::clearStructuredPreview()
{
    m_treeView->clear();
}

bool CodeRenderer::tryLoadJsonPreview(const QString& text)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || document.isNull()) {
        return false;
    }

    m_treeView->clear();
    if (document.isObject()) {
        const QJsonObject object = document.object();
        for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
            appendJsonValue(m_treeView->invisibleRootItem(), it.key(), it.value());
        }
    } else if (document.isArray()) {
        const QJsonArray array = document.array();
        for (int index = 0; index < array.size(); ++index) {
            appendJsonValue(m_treeView->invisibleRootItem(), QStringLiteral("[%1]").arg(index), array.at(index));
        }
    } else {
        return false;
    }

    m_treeView->expandToDepth(0);
    return true;
}

bool CodeRenderer::tryLoadXmlPreview(const QString& text)
{
    QDomDocument document;
    QString errorMessage;
    int errorLine = 0;
    int errorColumn = 0;
    if (!document.setContent(text, &errorMessage, &errorLine, &errorColumn)) {
        Q_UNUSED(errorMessage);
        Q_UNUSED(errorLine);
        Q_UNUSED(errorColumn);
        return false;
    }

    m_treeView->clear();
    const QDomElement rootElement = document.documentElement();
    if (rootElement.isNull()) {
        return false;
    }

    appendXmlNode(m_treeView->invisibleRootItem(), rootElement);

    m_treeView->expandToDepth(0);
    return true;
}

bool CodeRenderer::tryLoadYamlPreview(const QString& text)
{
    const QStringList lines = text.split(QChar::fromLatin1('\n'));
    struct StackEntry
    {
        int indent = -1;
        QTreeWidgetItem* item = nullptr;
    };

    m_treeView->clear();
    QList<StackEntry> stack;
    stack.append({ -1, m_treeView->invisibleRootItem() });

    bool hasContent = false;
    for (QString rawLine : lines) {
        rawLine.remove(QRegularExpression(QStringLiteral("[\\r\\n]+$")));
        if (rawLine.trimmed().isEmpty()) {
            continue;
        }

        const QString trimmed = rawLine.trimmed();
        if (trimmed.startsWith(QLatin1Char('#'))) {
            continue;
        }

        const int indent = yamlIndentLevel(rawLine);
        while (stack.size() > 1 && indent <= stack.constLast().indent) {
            stack.removeLast();
        }

        QTreeWidgetItem* parent = stack.constLast().item;
        if (!parent) {
            parent = m_treeView->invisibleRootItem();
        }

        if (trimmed.startsWith(QStringLiteral("- "))) {
            const QString itemBody = trimmed.mid(2).trimmed();
            auto* item = new QTreeWidgetItem(parent);
            item->setText(0, QStringLiteral("-"));
            if (itemBody.contains(QLatin1Char(':'))) {
                const int separator = itemBody.indexOf(QLatin1Char(':'));
                const QString key = itemBody.left(separator).trimmed();
                const QString value = yamlScalarText(itemBody.mid(separator + 1));
                item->setData(0, kNodeKindRole, value.isEmpty() ? QStringLiteral("yaml_object") : QStringLiteral("yaml_scalar"));
                item->setText(0, key.isEmpty() ? QStringLiteral("-") : key);
                item->setText(1, value);
            } else {
                item->setData(0, kNodeKindRole, QStringLiteral("yaml_scalar"));
                item->setText(1, yamlScalarText(itemBody));
            }
            stack.append({ indent, item });
            hasContent = true;
            continue;
        }

        const int separator = trimmed.indexOf(QLatin1Char(':'));
        if (separator < 0) {
            auto* item = new QTreeWidgetItem(parent);
            item->setData(0, kNodeKindRole, QStringLiteral("yaml_scalar"));
            item->setText(0, trimmed);
            hasContent = true;
            continue;
        }

        const QString key = trimmed.left(separator).trimmed();
        const QString value = yamlScalarText(trimmed.mid(separator + 1));
        auto* item = new QTreeWidgetItem(parent);
        item->setData(0, kNodeKindRole, value.isEmpty() ? QStringLiteral("yaml_object") : QStringLiteral("yaml_scalar"));
        item->setText(0, key);
        item->setText(1, value);
        stack.append({ indent, item });
        hasContent = true;
    }

    if (!hasContent) {
        m_treeView->clear();
        return false;
    }

    m_treeView->expandToDepth(0);
    return true;
}

bool CodeRenderer::isStructuredPreviewPath(const QString& filePath)
{
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    return suffix == QStringLiteral("json")
        || suffix == QStringLiteral("xml")
        || suffix == QStringLiteral("yaml")
        || suffix == QStringLiteral("yml");
}
