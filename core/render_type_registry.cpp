#include "core/render_type_registry.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QPair>

namespace {

QString runtimeConfigPath()
{
    const QString applicationDir = QCoreApplication::applicationDirPath().trimmed();
    if (!applicationDir.isEmpty()) {
        return QDir(applicationDir).filePath(QStringLiteral("RenderType.json"));
    }

    return QDir(QDir::currentPath()).filePath(QStringLiteral("RenderType.json"));
}

QString bundledConfigPath()
{
#ifdef SPACELOOK_PROJECT_DIR
    return QDir(QStringLiteral(SPACELOOK_PROJECT_DIR)).filePath(QStringLiteral("core/RenderType.json"));
#else
    return QDir(QDir::currentPath()).filePath(QStringLiteral("core/RenderType.json"));
#endif
}

QJsonObject defaultRenderTypeConfig()
{
    return QJsonObject{
        {QStringLiteral("shortcut"), QJsonObject{
            {QStringLiteral("name"), QStringLiteral("SummaryRenderer")},
            {QStringLiteral("typeKey"), QStringLiteral("shortcut")},
            {QStringLiteral("typeDetails"), QStringLiteral("Shortcut or link file.")},
            {QStringLiteral("suffixes"), QJsonArray{
                QStringLiteral("lnk"),
                QStringLiteral("url"),
                QStringLiteral("appref-ms")
            }}
        }},
        {QStringLiteral("image"), QJsonObject{
            {QStringLiteral("name"), QStringLiteral("ImageRenderer")},
            {QStringLiteral("typeKey"), QStringLiteral("image")},
            {QStringLiteral("typeDetails"), QStringLiteral("Image file preview category.")},
            {QStringLiteral("suffixes"), QJsonArray{
                QStringLiteral("png"),
                QStringLiteral("jpg"),
                QStringLiteral("jpeg"),
                QStringLiteral("jpe"),
                QStringLiteral("bmp"),
                QStringLiteral("dib"),
                QStringLiteral("gif"),
                QStringLiteral("webp"),
                QStringLiteral("heic"),
                QStringLiteral("heif"),
                QStringLiteral("avif"),
                QStringLiteral("tif"),
                QStringLiteral("tiff"),
                QStringLiteral("svg"),
                QStringLiteral("ico"),
                QStringLiteral("dds"),
                QStringLiteral("tga")
            }}
        }},
        {QStringLiteral("design"), QJsonObject{
            {QStringLiteral("name"), QStringLiteral("SummaryRenderer")},
            {QStringLiteral("typeKey"), QStringLiteral("design")},
            {QStringLiteral("typeDetails"), QStringLiteral("Design source file.")},
            {QStringLiteral("suffixes"), QJsonArray{
                QStringLiteral("psd")
            }}
        }},
        {QStringLiteral("media_video"), QJsonObject{
            {QStringLiteral("name"), QStringLiteral("MediaRenderer")},
            {QStringLiteral("typeKey"), QStringLiteral("video")},
            {QStringLiteral("typeDetails"), QStringLiteral("Video media file.")},
            {QStringLiteral("suffixes"), QJsonArray{
                QStringLiteral("mp4"),
                QStringLiteral("mkv"),
                QStringLiteral("avi"),
                QStringLiteral("mov"),
                QStringLiteral("wmv"),
                QStringLiteral("webm"),
                QStringLiteral("m4v"),
                QStringLiteral("mpg"),
                QStringLiteral("mpeg"),
                QStringLiteral("mts"),
                QStringLiteral("m2ts"),
                QStringLiteral("3gp"),
                QStringLiteral("3g2"),
                QStringLiteral("asf"),
                QStringLiteral("f4v"),
                QStringLiteral("flv"),
                QStringLiteral("hevc"),
                QStringLiteral("m2v"),
                QStringLiteral("mxf"),
                QStringLiteral("ogv"),
                QStringLiteral("rm"),
                QStringLiteral("rmvb"),
                QStringLiteral("vob"),
                QStringLiteral("wtv")
            }}
        }},
        {QStringLiteral("media_audio"), QJsonObject{
            {QStringLiteral("name"), QStringLiteral("MediaRenderer")},
            {QStringLiteral("typeKey"), QStringLiteral("audio")},
            {QStringLiteral("typeDetails"), QStringLiteral("Audio media file.")},
            {QStringLiteral("suffixes"), QJsonArray{
                QStringLiteral("mp3"),
                QStringLiteral("wav"),
                QStringLiteral("flac"),
                QStringLiteral("aac"),
                QStringLiteral("m4a"),
                QStringLiteral("ogg"),
                QStringLiteral("oga"),
                QStringLiteral("wma"),
                QStringLiteral("opus"),
                QStringLiteral("aiff"),
                QStringLiteral("aif"),
                QStringLiteral("alac"),
                QStringLiteral("ape"),
                QStringLiteral("mid"),
                QStringLiteral("midi")
            }}
        }},
        {QStringLiteral("pdf"), QJsonObject{
            {QStringLiteral("name"), QStringLiteral("PdfRenderer")},
            {QStringLiteral("typeKey"), QStringLiteral("pdf")},
            {QStringLiteral("typeDetails"), QStringLiteral("Portable document format file.")},
            {QStringLiteral("suffixes"), QJsonArray{
                QStringLiteral("pdf"),
                QStringLiteral("xps"),
                QStringLiteral("oxps")
            }}
        }},
        {QStringLiteral("markdown"), QJsonObject{
            {QStringLiteral("name"), QStringLiteral("RenderedPageRenderer")},
            {QStringLiteral("typeKey"), QStringLiteral("markdown")},
            {QStringLiteral("typeDetails"), QStringLiteral("Markdown document preview category.")},
            {QStringLiteral("suffixes"), QJsonArray{
                QStringLiteral("md"),
                QStringLiteral("markdown"),
                QStringLiteral("mdown"),
                QStringLiteral("mkd")
            }}
        }},
        {QStringLiteral("text"), QJsonObject{
            {QStringLiteral("name"), QStringLiteral("TextRenderer")},
            {QStringLiteral("typeKey"), QStringLiteral("text")},
            {QStringLiteral("typeDetails"), QStringLiteral("Text based document.")},
            {QStringLiteral("suffixes"), QJsonArray{
                QStringLiteral("txt"),
                QStringLiteral("log"),
                QStringLiteral("ini"),
                QStringLiteral("toml"),
                QStringLiteral("conf"),
                QStringLiteral("config"),
                QStringLiteral("cfg"),
                QStringLiteral("props"),
                QStringLiteral("targets"),
                QStringLiteral("reg"),
                QStringLiteral("env"),
                QStringLiteral("gitignore"),
                QStringLiteral("gitattributes"),
                QStringLiteral("editorconfig"),
                QStringLiteral("cmake"),
                QStringLiteral("qrc"),
                QStringLiteral("qss"),
                QStringLiteral("ui"),
                QStringLiteral("pri"),
                QStringLiteral("pro"),
                QStringLiteral("tsbuildinfo"),
                QStringLiteral("json"),
                QStringLiteral("jsonc"),
                QStringLiteral("xml"),
                QStringLiteral("yaml"),
                QStringLiteral("yml"),
                QStringLiteral("csv"),
                QStringLiteral("tsv"),
                QStringLiteral("properties")
            }}
        }},
        {QStringLiteral("code"), QJsonObject{
            {QStringLiteral("name"), QStringLiteral("CodeRenderer")},
            {QStringLiteral("typeKey"), QStringLiteral("code")},
            {QStringLiteral("typeDetails"), QStringLiteral("Source code document.")},
            {QStringLiteral("suffixes"), QJsonArray{
                QStringLiteral("c"),
                QStringLiteral("cc"),
                QStringLiteral("cpp"),
                QStringLiteral("cxx"),
                QStringLiteral("h"),
                QStringLiteral("hpp"),
                QStringLiteral("hh"),
                QStringLiteral("hxx"),
                QStringLiteral("cs"),
                QStringLiteral("java"),
                QStringLiteral("kt"),
                QStringLiteral("kts"),
                QStringLiteral("go"),
                QStringLiteral("rs"),
                QStringLiteral("swift"),
                QStringLiteral("m"),
                QStringLiteral("mm"),
                QStringLiteral("py"),
                QStringLiteral("pyw"),
                QStringLiteral("ipynb"),
                QStringLiteral("rb"),
                QStringLiteral("php"),
                QStringLiteral("js"),
                QStringLiteral("mjs"),
                QStringLiteral("cjs"),
                QStringLiteral("jsx"),
                QStringLiteral("ts"),
                QStringLiteral("tsx"),
                QStringLiteral("qml"),
                QStringLiteral("vue"),
                QStringLiteral("svelte"),
                QStringLiteral("astro"),
                QStringLiteral("css"),
                QStringLiteral("scss"),
                QStringLiteral("sass"),
                QStringLiteral("less"),
                QStringLiteral("sql"),
                QStringLiteral("r"),
                QStringLiteral("rmd"),
                QStringLiteral("jl"),
                QStringLiteral("do"),
                QStringLiteral("ado"),
                QStringLiteral("sas"),
                QStringLiteral("sh"),
                QStringLiteral("bash"),
                QStringLiteral("zsh"),
                QStringLiteral("fish"),
                QStringLiteral("ps1"),
                QStringLiteral("psm1"),
                QStringLiteral("psd1"),
                QStringLiteral("bat"),
                QStringLiteral("cmd"),
                QStringLiteral("lua"),
                QStringLiteral("dart"),
                QStringLiteral("pl"),
                QStringLiteral("pm"),
                QStringLiteral("t"),
                QStringLiteral("scala"),
                QStringLiteral("sc"),
                QStringLiteral("groovy"),
                QStringLiteral("gradle"),
                QStringLiteral("ex"),
                QStringLiteral("exs"),
                QStringLiteral("erl"),
                QStringLiteral("hrl"),
                QStringLiteral("clj"),
                QStringLiteral("cljs"),
                QStringLiteral("cljc"),
                QStringLiteral("zig"),
                QStringLiteral("nim"),
                QStringLiteral("v"),
                QStringLiteral("asm"),
                QStringLiteral("s"),
                QStringLiteral("glsl"),
                QStringLiteral("vert"),
                QStringLiteral("frag"),
                QStringLiteral("hlsl"),
                QStringLiteral("fx"),
                QStringLiteral("wgsl"),
                QStringLiteral("metal"),
                QStringLiteral("dockerfile"),
                QStringLiteral("containerfile"),
                QStringLiteral("makefile"),
                QStringLiteral("mk"),
                QStringLiteral("ninja"),
                QStringLiteral("bazel"),
                QStringLiteral("bzl"),
                QStringLiteral("build"),
                QStringLiteral("sln"),
                QStringLiteral("vcxproj"),
                QStringLiteral("csproj"),
                QStringLiteral("fsproj")
            }}
        }},
        {QStringLiteral("html"), QJsonObject{
            {QStringLiteral("name"), QStringLiteral("RenderedPageRenderer")},
            {QStringLiteral("typeKey"), QStringLiteral("html")},
            {QStringLiteral("typeDetails"), QStringLiteral("HTML document preview category.")},
            {QStringLiteral("suffixes"), QJsonArray{
                QStringLiteral("html"),
                QStringLiteral("htm"),
                QStringLiteral("xhtml"),
                QStringLiteral("mhtml")
            }}
        }},
        {QStringLiteral("document"), QJsonObject{
            {QStringLiteral("name"), QStringLiteral("DocumentRenderer")},
            {QStringLiteral("typeKey"), QStringLiteral("office")},
            {QStringLiteral("typeDetails"), QStringLiteral("Office document.")},
            {QStringLiteral("suffixes"), QJsonArray{
                QStringLiteral("doc"),
                QStringLiteral("docx"),
                QStringLiteral("rtf"),
                QStringLiteral("xls"),
                QStringLiteral("xlsx"),
                QStringLiteral("ppt"),
                QStringLiteral("pptx")
            }}
        }},
        {QStringLiteral("certificate"), QJsonObject{
            {QStringLiteral("name"), QStringLiteral("CertificateRenderer")},
            {QStringLiteral("typeKey"), QStringLiteral("certificate")},
            {QStringLiteral("typeDetails"), QStringLiteral("Certificate and key file.")},
            {QStringLiteral("suffixes"), QJsonArray{
                QStringLiteral("cer"),
                QStringLiteral("crt"),
                QStringLiteral("pem"),
                QStringLiteral("der"),
                QStringLiteral("pfx"),
                QStringLiteral("p12"),
                QStringLiteral("key"),
                QStringLiteral("pub"),
                QStringLiteral("asc"),
                QStringLiteral("gpg")
            }}
        }},
        {QStringLiteral("executable"), QJsonObject{
            {QStringLiteral("name"), QStringLiteral("SummaryRenderer")},
            {QStringLiteral("typeKey"), QStringLiteral("executable")},
            {QStringLiteral("typeDetails"), QStringLiteral("Windows executable or installer file.")},
            {QStringLiteral("suffixes"), QJsonArray{
                QStringLiteral("exe"),
                QStringLiteral("dll"),
                QStringLiteral("msi"),
                QStringLiteral("com"),
                QStringLiteral("scr"),
                QStringLiteral("sys")
            }}
        }},
        {QStringLiteral("archive"), QJsonObject{
            {QStringLiteral("name"), QStringLiteral("ArchiveRenderer")},
            {QStringLiteral("typeKey"), QStringLiteral("archive")},
            {QStringLiteral("typeDetails"), QStringLiteral("Compressed archive file.")},
            {QStringLiteral("suffixes"), QJsonArray{
                QStringLiteral("zip"),
                QStringLiteral("7z"),
                QStringLiteral("rar"),
                QStringLiteral("tar"),
                QStringLiteral("gz"),
                QStringLiteral("tgz"),
                QStringLiteral("bz2"),
                QStringLiteral("tbz"),
                QStringLiteral("tbz2"),
                QStringLiteral("xz"),
                QStringLiteral("txz"),
                QStringLiteral("cab")
            }}
        }}
    };
}

QList<QPair<QString, DetectedTypeInfo>> parseMappings(const QJsonObject& root)
{
    QList<QPair<QString, DetectedTypeInfo>> mappings;
    const QStringList rendererKeys = root.keys();
    for (const QString& rendererKey : rendererKeys) {
        const QJsonObject entry = root.value(rendererKey).toObject();
        const QString rendererName = entry.value(QStringLiteral("name")).toString().trimmed().toLower();
        const QString typeKey = entry.value(QStringLiteral("typeKey")).toString().trimmed();
        const QString typeDetails = entry.value(QStringLiteral("typeDetails")).toString().trimmed();
        const QJsonArray suffixes = entry.value(QStringLiteral("suffixes")).toArray();

        if (rendererName.isEmpty() || typeKey.isEmpty() || suffixes.isEmpty()) {
            continue;
        }

        DetectedTypeInfo info;
        info.typeKey = typeKey;
        info.typeDetails = typeDetails;
        info.rendererName = rendererName;

        for (const QJsonValue& suffixValue : suffixes) {
            const QString suffix = suffixValue.toString().trimmed().toLower();
            if (suffix.isEmpty()) {
                continue;
            }
            mappings.append(qMakePair(suffix, info));
        }
    }
    return mappings;
}

bool mergeMissingDefaultSuffixes(QJsonObject* rootObject)
{
    if (!rootObject) {
        return false;
    }

    bool changed = false;
    const QJsonObject defaultRoot = defaultRenderTypeConfig();
    const QStringList rendererKeys = defaultRoot.keys();

    for (const QString& rendererKey : rendererKeys) {
        const QJsonObject defaultEntry = defaultRoot.value(rendererKey).toObject();
        if (!rootObject->contains(rendererKey) || !rootObject->value(rendererKey).isObject()) {
            rootObject->insert(rendererKey, defaultEntry);
            changed = true;
            continue;
        }

        QJsonObject entry = rootObject->value(rendererKey).toObject();
        QJsonArray suffixes = entry.value(QStringLiteral("suffixes")).toArray();
        QStringList existingSuffixes;
        for (const QJsonValue& suffixValue : suffixes) {
            const QString suffix = suffixValue.toString().trimmed().toLower();
            if (!suffix.isEmpty() && !existingSuffixes.contains(suffix)) {
                existingSuffixes.append(suffix);
            }
        }

        const QJsonArray defaultSuffixes = defaultEntry.value(QStringLiteral("suffixes")).toArray();
        bool entryChanged = false;
        for (const QJsonValue& suffixValue : defaultSuffixes) {
            const QString suffix = suffixValue.toString().trimmed().toLower();
            if (suffix.isEmpty() || existingSuffixes.contains(suffix)) {
                continue;
            }

            suffixes.append(suffix);
            existingSuffixes.append(suffix);
            entryChanged = true;
        }

        if (entryChanged) {
            entry.insert(QStringLiteral("suffixes"), suffixes);
            rootObject->insert(rendererKey, entry);
            changed = true;
        }
    }

    return changed;
}

}

RenderTypeRegistry& RenderTypeRegistry::instance()
{
    static RenderTypeRegistry registry;
    return registry;
}

void RenderTypeRegistry::load()
{
    m_configFilePath = runtimeConfigPath();
    QFile file(m_configFilePath);
    QFile bundledFile(bundledConfigPath());

    if (!file.exists()) {
        QJsonObject rootObject = defaultRenderTypeConfig();
        if (bundledFile.exists() && bundledFile.open(QIODevice::ReadOnly)) {
            const QJsonDocument bundledDocument = QJsonDocument::fromJson(bundledFile.readAll());
            if (bundledDocument.isObject()) {
                rootObject = bundledDocument.object();
            }
            bundledFile.close();
        }

        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            const QJsonDocument document(rootObject);
            file.write(document.toJson(QJsonDocument::Indented));
            file.close();
        }
    }

    QJsonObject rootObject = defaultRenderTypeConfig();
    if (file.open(QIODevice::ReadOnly)) {
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
        if (document.isObject()) {
            rootObject = document.object();
        }
        file.close();
    }

    if (mergeMissingDefaultSuffixes(&rootObject)) {
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            const QJsonDocument document(rootObject);
            file.write(document.toJson(QJsonDocument::Indented));
            file.close();
        }
    }

    m_suffixMappings = parseMappings(rootObject);
    m_loaded = true;
}

std::optional<DetectedTypeInfo> RenderTypeRegistry::detectTypeInfoForSuffixCandidates(const QStringList& suffixCandidates) const
{
    ensureLoaded();

    for (const QString& candidate : suffixCandidates) {
        const QString normalizedCandidate = candidate.trimmed().toLower();
        for (const auto& mapping : m_suffixMappings) {
            if (mapping.first == normalizedCandidate) {
                return mapping.second;
            }
        }
    }

    return std::nullopt;
}

QString RenderTypeRegistry::configFilePath() const
{
    ensureLoaded();
    return m_configFilePath;
}

void RenderTypeRegistry::ensureLoaded() const
{
    if (!m_loaded) {
        const_cast<RenderTypeRegistry*>(this)->load();
    }
}
