#pragma once

#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include <QFont>
#include <QPixmap>
#include <QString>
#include <QWidget>

#include <litehtml.h>

class QPainter;

class QtLiteHtmlContainer : public litehtml::document_container
{
public:
    struct AnchorNavigation
    {
        QString resolvedUrl;
        QString rawUrl;
    };

    explicit QtLiteHtmlContainer(QWidget* hostWidget);
    ~QtLiteHtmlContainer() override;

    void setBasePath(const QString& filePath);
    void setViewportSize(int width, int height);
    void setAnchorCallback(std::function<void(const AnchorNavigation&)> callback);
    void setCursorCallback(std::function<void(Qt::CursorShape)> callback);

    litehtml::uint_ptr create_font(const litehtml::font_description& descr,
        const litehtml::document* doc,
        litehtml::font_metrics* fm) override;
    void delete_font(litehtml::uint_ptr hFont) override;
    litehtml::pixel_t text_width(const char* text, litehtml::uint_ptr hFont) override;
    void draw_text(litehtml::uint_ptr hdc,
        const char* text,
        litehtml::uint_ptr hFont,
        litehtml::web_color color,
        const litehtml::position& pos) override;
    litehtml::pixel_t pt_to_px(float pt) const override;
    litehtml::pixel_t get_default_font_size() const override;
    const char* get_default_font_name() const override;
    void draw_list_marker(litehtml::uint_ptr hdc, const litehtml::list_marker& marker) override;
    void load_image(const char* src, const char* baseurl, bool redraw_on_ready) override;
    void get_image_size(const char* src, const char* baseurl, litehtml::size& sz) override;
    void draw_image(litehtml::uint_ptr hdc,
        const litehtml::background_layer& layer,
        const std::string& url,
        const std::string& base_url) override;
    void draw_solid_fill(litehtml::uint_ptr hdc,
        const litehtml::background_layer& layer,
        const litehtml::web_color& color) override;
    void draw_linear_gradient(litehtml::uint_ptr hdc,
        const litehtml::background_layer& layer,
        const litehtml::background_layer::linear_gradient& gradient) override;
    void draw_radial_gradient(litehtml::uint_ptr hdc,
        const litehtml::background_layer& layer,
        const litehtml::background_layer::radial_gradient& gradient) override;
    void draw_conic_gradient(litehtml::uint_ptr hdc,
        const litehtml::background_layer& layer,
        const litehtml::background_layer::conic_gradient& gradient) override;
    void draw_borders(litehtml::uint_ptr hdc,
        const litehtml::borders& borders,
        const litehtml::position& draw_pos,
        bool root) override;

    void set_caption(const char* caption) override;
    void set_base_url(const char* base_url) override;
    void link(const std::shared_ptr<litehtml::document>& doc, const litehtml::element::ptr& el) override;
    void on_anchor_click(const char* url, const litehtml::element::ptr& el) override;
    void on_mouse_event(const litehtml::element::ptr& el, litehtml::mouse_event event) override;
    void set_cursor(const char* cursor) override;
    void transform_text(litehtml::string& text, litehtml::text_transform tt) override;
    void import_css(litehtml::string& text, const litehtml::string& url, litehtml::string& baseurl) override;
    void set_clip(const litehtml::position& pos, const litehtml::border_radiuses& bdr_radius) override;
    void del_clip() override;
    void get_viewport(litehtml::position& viewport) const override;
    litehtml::element::ptr create_element(const char* tag_name,
        const litehtml::string_map& attributes,
        const std::shared_ptr<litehtml::document>& doc) override;
    void get_media_features(litehtml::media_features& media) const override;
    void get_language(litehtml::string& language, litehtml::string& culture) const override;

private:
    struct FontHandle
    {
        QFont font;
        std::unique_ptr<class QFontMetrics> metrics;
        bool drawSpaces = true;
    };

    QWidget* m_hostWidget = nullptr;
    QString m_defaultBasePath;
    QString m_documentBaseUrl;
    QString m_caption;
    int m_viewportWidth = 0;
    int m_viewportHeight = 0;
    std::vector<litehtml::position> m_clipStack;
    std::unordered_map<QString, QPixmap> m_images;
    std::function<void(const AnchorNavigation&)> m_anchorCallback;
    std::function<void(Qt::CursorShape)> m_cursorCallback;

    QString resolvePath(const QString& url, const QString& baseUrl) const;
    QString directoryFromBase(const QString& baseUrl) const;
    QPixmap loadPixmap(const QString& resolvedPath);
    const QPixmap* ensurePixmap(const QString& url, const QString& baseUrl);
    void applyClip(QPainter* painter) const;
    QColor toColor(const litehtml::web_color& color) const;
    QRectF toRect(const litehtml::position& pos) const;
    QPainterPath roundedPathFor(const litehtml::position& pos, const litehtml::border_radiuses& radius) const;
    QString orderedMarkerText(litehtml::list_style_type type, int index) const;
    QString alphaMarkerText(int index, bool upper) const;
    QString romanMarkerText(int index, bool upper) const;
    void drawBorderSide(QPainter* painter,
        const litehtml::border& border,
        qreal x1,
        qreal y1,
        qreal x2,
        qreal y2) const;
};
