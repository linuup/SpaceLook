#include "renderers/markup/QtLiteHtmlContainer.h"

#include <algorithm>
#include <cmath>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontMetrics>
#include <QConicalGradient>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QRadialGradient>
#include <QRegularExpression>
#include <QScreen>
#include <QTextBoundaryFinder>
#include <QUrl>

#include "core/PreviewFileReader.h"

namespace {

QString fromUtf8(const std::string& value)
{
    return QString::fromUtf8(value.c_str());
}

std::string toUtf8(const QString& value)
{
    const QByteArray bytes = value.toUtf8();
    return std::string(bytes.constData(), static_cast<size_t>(bytes.size()));
}

QGradientStops gradientStops(const std::vector<litehtml::background_layer::color_point>& points)
{
    QGradientStops stops;
    stops.reserve(static_cast<int>(points.size()));
    for (const auto& point : points) {
        stops.append(QGradientStop(point.offset, QColor(point.color.red, point.color.green, point.color.blue, point.color.alpha)));
    }
    return stops;
}

}

QtLiteHtmlContainer::QtLiteHtmlContainer(QWidget* hostWidget)
    : m_hostWidget(hostWidget)
{
}

QtLiteHtmlContainer::~QtLiteHtmlContainer() = default;

void QtLiteHtmlContainer::setBasePath(const QString& filePath)
{
    const QFileInfo fileInfo(filePath);
    m_defaultBasePath = fileInfo.absoluteFilePath();
    m_documentBaseUrl = m_defaultBasePath;
}

void QtLiteHtmlContainer::setViewportSize(int width, int height)
{
    m_viewportWidth = std::max(0, width);
    m_viewportHeight = std::max(0, height);
}

void QtLiteHtmlContainer::setAnchorCallback(std::function<void(const AnchorNavigation&)> callback)
{
    m_anchorCallback = std::move(callback);
}

void QtLiteHtmlContainer::setCursorCallback(std::function<void(Qt::CursorShape)> callback)
{
    m_cursorCallback = std::move(callback);
}

litehtml::uint_ptr QtLiteHtmlContainer::create_font(const litehtml::font_description& descr,
    const litehtml::document*,
    litehtml::font_metrics* fm)
{
    auto* handle = new FontHandle();
    QString family = fromUtf8(descr.family);
    const QStringList families = family.split(',', Qt::SkipEmptyParts);
    if (!families.isEmpty()) {
        family = families.first().trimmed();
        if ((family.startsWith('"') && family.endsWith('"')) || (family.startsWith('\'') && family.endsWith('\''))) {
            family = family.mid(1, family.size() - 2);
        }
    }
    if (family.isEmpty()) {
        family = QStringLiteral("Segoe UI Rounded");
    }

    handle->font = QFont(family);
    handle->font.setPixelSize(static_cast<int>(std::round(descr.size)));
    handle->font.setWeight(descr.weight);
    handle->font.setItalic(descr.style == litehtml::font_style_italic);
    handle->font.setUnderline((descr.decoration_line & litehtml::text_decoration_line_underline) != 0);
    handle->font.setStrikeOut((descr.decoration_line & litehtml::text_decoration_line_line_through) != 0);
    handle->metrics = std::make_unique<QFontMetrics>(handle->font);
    handle->drawSpaces = handle->font.italic() || descr.decoration_line != litehtml::text_decoration_line_none;

    if (fm) {
        fm->font_size = descr.size;
        fm->ascent = handle->metrics->ascent();
        fm->descent = handle->metrics->descent();
        fm->height = handle->metrics->height();
        fm->x_height = handle->metrics->xHeight();
        fm->ch_width = handle->metrics->horizontalAdvance(QStringLiteral("0"));
        fm->draw_spaces = handle->drawSpaces;
        fm->sub_shift = descr.size / 5.0f;
        fm->super_shift = descr.size / 3.0f;
    }

    return reinterpret_cast<litehtml::uint_ptr>(handle);
}

void QtLiteHtmlContainer::delete_font(litehtml::uint_ptr hFont)
{
    delete reinterpret_cast<FontHandle*>(hFont);
}

litehtml::pixel_t QtLiteHtmlContainer::text_width(const char* text, litehtml::uint_ptr hFont)
{
    auto* handle = reinterpret_cast<FontHandle*>(hFont);
    if (!handle || !handle->metrics) {
        return 0;
    }
    return handle->metrics->horizontalAdvance(QString::fromUtf8(text));
}

void QtLiteHtmlContainer::draw_text(litehtml::uint_ptr hdc,
    const char* text,
    litehtml::uint_ptr hFont,
    litehtml::web_color color,
    const litehtml::position& pos)
{
    auto* painter = reinterpret_cast<QPainter*>(hdc);
    auto* handle = reinterpret_cast<FontHandle*>(hFont);
    if (!painter || !handle || !handle->metrics) {
        return;
    }

    painter->save();
    applyClip(painter);
    painter->setFont(handle->font);
    painter->setPen(toColor(color));
    const qreal baselineY = pos.y + handle->metrics->ascent();
    painter->drawText(QPointF(pos.x, baselineY), QString::fromUtf8(text));
    painter->restore();
}

litehtml::pixel_t QtLiteHtmlContainer::pt_to_px(float pt) const
{
    const qreal dpi = m_hostWidget ? m_hostWidget->logicalDpiY() : qApp->primaryScreen()->logicalDotsPerInchY();
    return static_cast<litehtml::pixel_t>(pt * dpi / 72.0);
}

litehtml::pixel_t QtLiteHtmlContainer::get_default_font_size() const
{
    return 16;
}

const char* QtLiteHtmlContainer::get_default_font_name() const
{
    return "Segoe UI Rounded";
}

void QtLiteHtmlContainer::draw_list_marker(litehtml::uint_ptr hdc, const litehtml::list_marker& marker)
{
    auto* painter = reinterpret_cast<QPainter*>(hdc);
    if (!painter) {
        return;
    }

    painter->save();
    applyClip(painter);
    painter->setPen(toColor(marker.color));
    painter->setBrush(toColor(marker.color));

    const QRectF rect = toRect(marker.pos);
    switch (marker.marker_type) {
    case litehtml::list_style_type_circle:
        painter->setBrush(Qt::NoBrush);
        painter->drawEllipse(rect);
        break;
    case litehtml::list_style_type_disc:
        painter->drawEllipse(rect);
        break;
    case litehtml::list_style_type_square:
        painter->drawRect(rect);
        break;
    default: {
        const QString markerText = orderedMarkerText(marker.marker_type, marker.index);
        auto* handle = reinterpret_cast<FontHandle*>(marker.font);
        if (handle) {
            painter->setFont(handle->font);
        }
        painter->setBrush(Qt::NoBrush);
        painter->drawText(rect, Qt::AlignLeft | Qt::AlignVCenter, markerText);
        break;
    }
    }

    painter->restore();
}

void QtLiteHtmlContainer::load_image(const char* src, const char* baseurl, bool)
{
    const QString key = resolvePath(QString::fromUtf8(src), QString::fromUtf8(baseurl ? baseurl : ""));
    if (key.isEmpty() || m_images.find(key) != m_images.end()) {
        return;
    }
    m_images.insert_or_assign(key, loadPixmap(key));
}

void QtLiteHtmlContainer::get_image_size(const char* src, const char* baseurl, litehtml::size& sz)
{
    sz.width = 0;
    sz.height = 0;
    const QPixmap* pixmap = ensurePixmap(QString::fromUtf8(src), QString::fromUtf8(baseurl ? baseurl : ""));
    if (!pixmap || pixmap->isNull()) {
        return;
    }
    sz.width = pixmap->width();
    sz.height = pixmap->height();
}

void QtLiteHtmlContainer::draw_image(litehtml::uint_ptr hdc,
    const litehtml::background_layer& layer,
    const std::string& url,
    const std::string& base_url)
{
    auto* painter = reinterpret_cast<QPainter*>(hdc);
    const QPixmap* pixmap = ensurePixmap(fromUtf8(url), fromUtf8(base_url));
    if (!painter || !pixmap || pixmap->isNull()) {
        return;
    }

    painter->save();
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
    applyClip(painter);
    painter->setClipRect(toRect(layer.clip_box), Qt::IntersectClip);

    const QRectF originRect = toRect(layer.origin_box);
    switch (layer.repeat) {
    case litehtml::background_repeat_no_repeat:
        painter->drawPixmap(originRect, *pixmap, pixmap->rect());
        break;
    case litehtml::background_repeat_repeat_x: {
        qreal x = originRect.x();
        while (x > layer.clip_box.left()) {
            x -= originRect.width();
        }
        for (; x < layer.clip_box.right(); x += originRect.width()) {
            painter->drawPixmap(QRectF(x, originRect.y(), originRect.width(), originRect.height()), *pixmap, pixmap->rect());
        }
        break;
    }
    case litehtml::background_repeat_repeat_y: {
        qreal y = originRect.y();
        while (y > layer.clip_box.top()) {
            y -= originRect.height();
        }
        for (; y < layer.clip_box.bottom(); y += originRect.height()) {
            painter->drawPixmap(QRectF(originRect.x(), y, originRect.width(), originRect.height()), *pixmap, pixmap->rect());
        }
        break;
    }
    case litehtml::background_repeat_repeat: {
        qreal x = originRect.x();
        while (x > layer.clip_box.left()) {
            x -= originRect.width();
        }
        qreal y0 = originRect.y();
        while (y0 > layer.clip_box.top()) {
            y0 -= originRect.height();
        }
        for (; x < layer.clip_box.right(); x += originRect.width()) {
            for (qreal y = y0; y < layer.clip_box.bottom(); y += originRect.height()) {
                painter->drawPixmap(QRectF(x, y, originRect.width(), originRect.height()), *pixmap, pixmap->rect());
            }
        }
        break;
    }
    }

    painter->restore();
}

void QtLiteHtmlContainer::draw_solid_fill(litehtml::uint_ptr hdc,
    const litehtml::background_layer& layer,
    const litehtml::web_color& color)
{
    auto* painter = reinterpret_cast<QPainter*>(hdc);
    if (!painter) {
        return;
    }

    painter->save();
    applyClip(painter);
    painter->fillPath(roundedPathFor(layer.border_box, layer.border_radius), toColor(color));
    painter->restore();
}

void QtLiteHtmlContainer::draw_linear_gradient(litehtml::uint_ptr hdc,
    const litehtml::background_layer& layer,
    const litehtml::background_layer::linear_gradient& gradient)
{
    auto* painter = reinterpret_cast<QPainter*>(hdc);
    if (!painter) {
        return;
    }

    QLinearGradient brush(QPointF(gradient.start.x, gradient.start.y), QPointF(gradient.end.x, gradient.end.y));
    brush.setStops(gradientStops(gradient.color_points));

    painter->save();
    applyClip(painter);
    painter->fillPath(roundedPathFor(layer.border_box, layer.border_radius), brush);
    painter->restore();
}

void QtLiteHtmlContainer::draw_radial_gradient(litehtml::uint_ptr hdc,
    const litehtml::background_layer& layer,
    const litehtml::background_layer::radial_gradient& gradient)
{
    auto* painter = reinterpret_cast<QPainter*>(hdc);
    if (!painter) {
        return;
    }

    QRadialGradient brush(QPointF(gradient.position.x, gradient.position.y), gradient.radius.x, QPointF(gradient.position.x, gradient.position.y));
    brush.setStops(gradientStops(gradient.color_points));

    painter->save();
    applyClip(painter);
    painter->fillPath(roundedPathFor(layer.border_box, layer.border_radius), brush);
    painter->restore();
}

void QtLiteHtmlContainer::draw_conic_gradient(litehtml::uint_ptr hdc,
    const litehtml::background_layer& layer,
    const litehtml::background_layer::conic_gradient& gradient)
{
    auto* painter = reinterpret_cast<QPainter*>(hdc);
    if (!painter) {
        return;
    }

    QConicalGradient brush(QPointF(gradient.position.x, gradient.position.y), 90.0 - gradient.angle);
    brush.setStops(gradientStops(gradient.color_points));

    painter->save();
    applyClip(painter);
    painter->fillPath(roundedPathFor(layer.border_box, layer.border_radius), brush);
    painter->restore();
}

void QtLiteHtmlContainer::draw_borders(litehtml::uint_ptr hdc,
    const litehtml::borders& borders,
    const litehtml::position& draw_pos,
    bool)
{
    auto* painter = reinterpret_cast<QPainter*>(hdc);
    if (!painter) {
        return;
    }

    painter->save();
    applyClip(painter);

    if (borders.top.width > 0) {
        drawBorderSide(painter, borders.top, draw_pos.left(), draw_pos.top() + borders.top.width / 2.0, draw_pos.right(), draw_pos.top() + borders.top.width / 2.0);
    }
    if (borders.bottom.width > 0) {
        drawBorderSide(painter, borders.bottom, draw_pos.left(), draw_pos.bottom() - borders.bottom.width / 2.0, draw_pos.right(), draw_pos.bottom() - borders.bottom.width / 2.0);
    }
    if (borders.left.width > 0) {
        drawBorderSide(painter, borders.left, draw_pos.left() + borders.left.width / 2.0, draw_pos.top(), draw_pos.left() + borders.left.width / 2.0, draw_pos.bottom());
    }
    if (borders.right.width > 0) {
        drawBorderSide(painter, borders.right, draw_pos.right() - borders.right.width / 2.0, draw_pos.top(), draw_pos.right() - borders.right.width / 2.0, draw_pos.bottom());
    }

    painter->restore();
}

void QtLiteHtmlContainer::set_caption(const char* caption)
{
    m_caption = QString::fromUtf8(caption ? caption : "");
}

void QtLiteHtmlContainer::set_base_url(const char* base_url)
{
    m_documentBaseUrl = QString::fromUtf8(base_url ? base_url : "");
}

void QtLiteHtmlContainer::link(const std::shared_ptr<litehtml::document>&, const litehtml::element::ptr&)
{
}

void QtLiteHtmlContainer::on_anchor_click(const char* url, const litehtml::element::ptr&)
{
    if (!m_anchorCallback) {
        return;
    }
    AnchorNavigation navigation;
    navigation.rawUrl = QString::fromUtf8(url ? url : "");
    navigation.resolvedUrl = resolvePath(navigation.rawUrl, m_documentBaseUrl);
    m_anchorCallback(navigation);
}

void QtLiteHtmlContainer::on_mouse_event(const litehtml::element::ptr&, litehtml::mouse_event)
{
}

void QtLiteHtmlContainer::set_cursor(const char* cursor)
{
    if (!m_cursorCallback) {
        return;
    }

    const QString value = QString::fromUtf8(cursor ? cursor : "");
    Qt::CursorShape shape = Qt::ArrowCursor;
    if (value.compare(QStringLiteral("pointer"), Qt::CaseInsensitive) == 0) {
        shape = Qt::PointingHandCursor;
    } else if (value.compare(QStringLiteral("text"), Qt::CaseInsensitive) == 0) {
        shape = Qt::IBeamCursor;
    }
    m_cursorCallback(shape);
}

void QtLiteHtmlContainer::transform_text(litehtml::string& text, litehtml::text_transform tt)
{
    QString value = QString::fromUtf8(text.c_str());
    switch (tt) {
    case litehtml::text_transform_capitalize: {
        QTextBoundaryFinder finder(QTextBoundaryFinder::Word, value);
        int position = finder.toNextBoundary();
        while (position != -1) {
            const int start = finder.toPreviousBoundary();
            if (start >= 0 && start < value.size() && value.at(start).isLetter()) {
                value[start] = value.at(start).toUpper();
            }
            finder.setPosition(position);
            position = finder.toNextBoundary();
        }
        break;
    }
    case litehtml::text_transform_uppercase:
        value = value.toUpper();
        break;
    case litehtml::text_transform_lowercase:
        value = value.toLower();
        break;
    default:
        break;
    }
    text = toUtf8(value);
}

void QtLiteHtmlContainer::import_css(litehtml::string& text, const litehtml::string& url, litehtml::string& baseurl)
{
    const QString resolvedPath = resolvePath(fromUtf8(url), fromUtf8(baseurl));
    QByteArray cssBytes;
    if (!PreviewFileReader::readAll(resolvedPath, &cssBytes)) {
        text.clear();
        return;
    }
    text = toUtf8(QString::fromUtf8(cssBytes));
    baseurl = toUtf8(resolvedPath);
}

void QtLiteHtmlContainer::set_clip(const litehtml::position& pos, const litehtml::border_radiuses&)
{
    m_clipStack.push_back(pos);
}

void QtLiteHtmlContainer::del_clip()
{
    if (!m_clipStack.empty()) {
        m_clipStack.pop_back();
    }
}

void QtLiteHtmlContainer::get_viewport(litehtml::position& viewport) const
{
    viewport = litehtml::position(0, 0, static_cast<litehtml::pixel_t>(m_viewportWidth), static_cast<litehtml::pixel_t>(m_viewportHeight));
}

litehtml::element::ptr QtLiteHtmlContainer::create_element(const char*,
    const litehtml::string_map&,
    const std::shared_ptr<litehtml::document>&)
{
    return nullptr;
}

void QtLiteHtmlContainer::get_media_features(litehtml::media_features& media) const
{
    litehtml::position viewport;
    get_viewport(viewport);
    media.type = litehtml::media_type_screen;
    media.width = viewport.width;
    media.height = viewport.height;
    media.color = 8;
    media.monochrome = 0;
    media.color_index = 0;
    media.resolution = m_hostWidget ? m_hostWidget->logicalDpiX() : 96;
    media.device_width = viewport.width;
    media.device_height = viewport.height;
}

void QtLiteHtmlContainer::get_language(litehtml::string& language, litehtml::string& culture) const
{
    language = "en";
    culture.clear();
}

QString QtLiteHtmlContainer::resolvePath(const QString& url, const QString& baseUrl) const
{
    const QString trimmed = url.trimmed();
    if (trimmed.isEmpty()) {
        return QString();
    }

    if (trimmed.startsWith(QLatin1Char('#'))) {
        return trimmed;
    }

    const QUrl parsed(trimmed);
    if (parsed.isLocalFile()) {
        return QDir::cleanPath(parsed.toLocalFile());
    }
    if (parsed.isRelative() && !parsed.fragment().isEmpty() && parsed.path().trimmed().isEmpty()) {
        return trimmed;
    }
    if (parsed.isValid() && !parsed.scheme().isEmpty()) {
        return trimmed;
    }
    if (QDir::isAbsolutePath(trimmed)) {
        return QDir::cleanPath(trimmed);
    }

    const QString baseDir = directoryFromBase(baseUrl);
    if (baseDir.isEmpty()) {
        return QDir::cleanPath(trimmed);
    }
    return QDir(baseDir).absoluteFilePath(trimmed);
}

QString QtLiteHtmlContainer::directoryFromBase(const QString& baseUrl) const
{
    QString candidate = baseUrl.trimmed();
    if (candidate.isEmpty()) {
        candidate = m_documentBaseUrl.trimmed();
    }
    if (candidate.isEmpty()) {
        candidate = m_defaultBasePath.trimmed();
    }
    if (candidate.startsWith(QStringLiteral("file:///"), Qt::CaseInsensitive)) {
        candidate = QUrl(candidate).toLocalFile();
    }
    QFileInfo info(candidate);
    if (info.isDir()) {
        return info.absoluteFilePath();
    }
    return info.absolutePath();
}

QPixmap QtLiteHtmlContainer::loadPixmap(const QString& resolvedPath)
{
    if (resolvedPath.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive) ||
        resolvedPath.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)) {
        return QPixmap();
    }
    return QPixmap(resolvedPath);
}

const QPixmap* QtLiteHtmlContainer::ensurePixmap(const QString& url, const QString& baseUrl)
{
    const QString key = resolvePath(url, baseUrl);
    if (key.isEmpty()) {
        return nullptr;
    }
    auto it = m_images.find(key);
    if (it == m_images.end()) {
        it = m_images.emplace(key, loadPixmap(key)).first;
    }
    return &it->second;
}

void QtLiteHtmlContainer::applyClip(QPainter* painter) const
{
    if (!painter || m_clipStack.empty()) {
        return;
    }
    painter->setClipRect(toRect(m_clipStack.back()), Qt::IntersectClip);
}

QColor QtLiteHtmlContainer::toColor(const litehtml::web_color& color) const
{
    return QColor(color.red, color.green, color.blue, color.alpha);
}

QRectF QtLiteHtmlContainer::toRect(const litehtml::position& pos) const
{
    return QRectF(pos.x, pos.y, pos.width, pos.height);
}

QPainterPath QtLiteHtmlContainer::roundedPathFor(const litehtml::position& pos, const litehtml::border_radiuses& radius) const
{
    QPainterPath path;
    const QRectF rect = toRect(pos);
    const qreal radiusValue = std::max({ radius.top_left_x, radius.top_right_x, radius.bottom_left_x, radius.bottom_right_x, radius.top_left_y, radius.top_right_y, radius.bottom_left_y, radius.bottom_right_y });
    if (radiusValue <= 0.0) {
        path.addRect(rect);
        return path;
    }
    path.addRoundedRect(rect, radiusValue, radiusValue);
    return path;
}

QString QtLiteHtmlContainer::orderedMarkerText(litehtml::list_style_type type, int index) const
{
    const int value = std::max(1, index);
    switch (type) {
    case litehtml::list_style_type_decimal:
        return QStringLiteral("%1.").arg(value);
    case litehtml::list_style_type_decimal_leading_zero:
        return QStringLiteral("%1.").arg(value, 2, 10, QLatin1Char('0'));
    case litehtml::list_style_type_lower_alpha:
    case litehtml::list_style_type_lower_latin:
        return alphaMarkerText(value, false) + QLatin1Char('.');
    case litehtml::list_style_type_upper_alpha:
    case litehtml::list_style_type_upper_latin:
        return alphaMarkerText(value, true) + QLatin1Char('.');
    case litehtml::list_style_type_lower_roman:
        return romanMarkerText(value, false) + QLatin1Char('.');
    case litehtml::list_style_type_upper_roman:
        return romanMarkerText(value, true) + QLatin1Char('.');
    default:
        return QStringLiteral("%1.").arg(value);
    }
}

QString QtLiteHtmlContainer::alphaMarkerText(int index, bool upper) const
{
    QString text;
    int value = std::max(1, index);
    while (value > 0) {
        value -= 1;
        const QChar ch = QChar((upper ? 'A' : 'a') + (value % 26));
        text.prepend(ch);
        value /= 26;
    }
    return text;
}

QString QtLiteHtmlContainer::romanMarkerText(int index, bool upper) const
{
    struct RomanPart { int value; const char* numeral; };
    static const RomanPart parts[] = {
        {1000, "M"}, {900, "CM"}, {500, "D"}, {400, "CD"},
        {100, "C"}, {90, "XC"}, {50, "L"}, {40, "XL"},
        {10, "X"}, {9, "IX"}, {5, "V"}, {4, "IV"}, {1, "I"}
    };

    QString result;
    int value = std::max(1, index);
    for (const RomanPart& part : parts) {
        while (value >= part.value) {
            result += QString::fromLatin1(part.numeral);
            value -= part.value;
        }
    }
    return upper ? result : result.toLower();
}

void QtLiteHtmlContainer::drawBorderSide(QPainter* painter,
    const litehtml::border& border,
    qreal x1,
    qreal y1,
    qreal x2,
    qreal y2) const
{
    if (!painter || border.width <= 0 || border.style == litehtml::border_style_none || border.style == litehtml::border_style_hidden) {
        return;
    }

    QPen pen(toColor(border.color), border.width);
    pen.setCapStyle(Qt::SquareCap);
    if (border.style == litehtml::border_style_dotted) {
        pen.setStyle(Qt::DotLine);
    } else if (border.style == litehtml::border_style_dashed) {
        pen.setStyle(Qt::DashLine);
    }
    painter->setPen(pen);
    painter->drawLine(QPointF(x1, y1), QPointF(x2, y2));

    if (border.style == litehtml::border_style_double && border.width >= 3.0) {
        const qreal offset = border.width / 3.0;
        if (std::abs(y1 - y2) < 0.01) {
            painter->drawLine(QPointF(x1, y1 - offset), QPointF(x2, y2 - offset));
            painter->drawLine(QPointF(x1, y1 + offset), QPointF(x2, y2 + offset));
        } else {
            painter->drawLine(QPointF(x1 - offset, y1), QPointF(x2 - offset, y2));
            painter->drawLine(QPointF(x1 + offset, y1), QPointF(x2 + offset, y2));
        }
    }
}
