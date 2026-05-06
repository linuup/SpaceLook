# SpaceLook

## Overview

SpaceLook inspects the item under the mouse cursor on the Windows desktop or in File Explorer, converts that result into a shared data model, and routes it to the correct preview renderer.

The current implementation is optimized for fast local preview with native Qt widgets and lightweight parsing where possible.

## How to Use
1. Hover over a file, folder, shortcut, or shell item on the desktop or in File Explorer.
2. Press the `Space` key to toggle the preview window for the currently hovered item.

## Current Renderer Order

The registry currently loads renderers in this order:

1. `pdf`
2. `welcome`
3. `document`
4. `archive`
5. `certificate`
6. `folder`
7. `rendered_page`
8. `code`
9. `text`
10. `image`
11. `media`
12. `summary`

If `core/RenderType.json` provides a renderer name, the registry resolves that renderer first. Otherwise, the first renderer whose `canHandle()` returns `true` is used.

## Renderer Directory

The current renderer modules under `renderers/` are:

| Path | Main renderer or helper | Responsibility |
| --- | --- | --- |
| `renderers/certificate/` | `CertificateRenderer` | Previews certificate and key metadata, including password unlock flow for PKCS#12 containers. |
| `renderers/code/` | `CodeRenderer` | Shows syntax highlighted source code and structured code files with line numbers. |
| `renderers/document/` | `DocumentRenderer`, `PreviewHandlerHost` | Previews Office documents and hosts Windows Preview Handler based document views. |
| `renderers/folder/` | `FolderRenderer` | Shows folder contents, shell folder entries, item actions, and inline rename UI. |
| `renderers/image/` | `ImageRenderer` | Previews raster and vector images with zoom, pan, animated image handling, and special decoders. |
| `renderers/markup/` | `RenderedPageRenderer`, `WebView2HtmlView`, `LiteHtmlView` | Previews Markdown and HTML through rendered page views, with WebView2 preferred for HTML. |
| `renderers/media/` | `MediaRenderer` | Previews audio and video with modern playback controls, mpv first, Qt media fallback. |
| `renderers/pdf/` | `PdfRenderer`, `PdfDocument`, `PdfViewWidget` | Previews PDF through PDFium and routes XPS or OXPS through Windows Preview Handler fallback. |
| `renderers/summary/` | `SummaryRenderer`, `ArchiveRenderer` | Shows fallback metadata and archive contents. |
| `renderers/text/` | `TextRenderer` | Shows plain text and structured text formats with formatting, wrapping, and line numbers. |
| `renderers/welcome/` | `WelcomeRenderer` | Shows the welcome screen and supported preview entry points. |
| `renderers/RendererRegistry.*` | `RendererRegistry` | Registers renderers, resolves configured renderer names, and selects a fallback renderer. |
| `renderers/PreviewHost.*` | `PreviewHost` | Hosts renderer widgets, switches active renderer, and applies summary fallback requests. |
| `renderers/FileTypeIconResolver.*` | `FileTypeIconResolver` | Resolves file type, shell, shortcut, and welcome icon assets. |
| `renderers/OpenWithButton.*` | `OpenWithButton` | Provides the shared Open with control and handler menu. |
| `renderers/PreviewHeaderBar.*` | `PreviewHeaderBar` | Provides shared title, path, action, and close chrome used by renderers. |
| `renderers/SelectableTitleLabel.*` | `SelectableTitleLabel` | Provides title copy interactions used in renderer headers. |
| `renderers/ModeSwitchButton.*` | `ModeSwitchButton` | Provides shared mode selection UI for text and code views. |
| `renderers/FluentIconFont.*` | `FluentIconFont` | Loads the embedded Segoe Fluent Icons font and exposes glyph helpers. |
| `renderers/CodeThemeManager.*` | `CodeThemeManager` | Provides syntax highlighting theme selection for code preview. |
| `renderers/QmlShellRenderer.*` | `QmlShellRenderer` | Hosts QML based renderer surfaces where needed. |
| `renderers/IPreviewRenderer.h` | `IPreviewRenderer` | Defines the common renderer interface. |
| `renderers/PreviewLoadGuard.h` | `PreviewLoadGuard` | Helps guard async renderer loads from stale preview state. |

## Preview Type Coverage Matrix

`✅` marks formats currently routed by `core/RenderType.json` and active renderer `canHandle()` checks. Entries without `✅` are target coverage.

| Category | Renderer | Supported formats or target behavior |
| --- | --- | --- |
| PDF and Page Documents | `PdfRenderer` | ✅ `pdf`, ✅ `xps`, ✅ `oxps` |
| Office Documents | `DocumentRenderer` | ✅ `doc`, ✅ `docx`, `docm`, `dot`, `dotx`, ✅ `rtf`, ✅ `xls`, ✅ `xlsx`, `xlsm`, `xlsb`, `xlt`, `xltx`, ✅ `ppt`, ✅ `pptx`, `pptm`, `pps`, `ppsx`, `vsd`, `vsdx` |
| OpenDocument Files | `DocumentRenderer` | `odt`, `ott`, `ods`, `ots`, `odp`, `otp`, `odg`, `otg`, `odf` |
| Markup Documents | `RenderedPageRenderer` | ✅ `md`, ✅ `markdown`, ✅ `mdown`, ✅ `mkd`, ✅ `html`, ✅ `htm`, ✅ `xhtml`, ✅ `mhtml` |
| Code Files, C Family | `CodeRenderer` | ✅ `c`, ✅ `cc`, ✅ `cpp`, ✅ `cxx`, ✅ `h`, ✅ `hpp`, ✅ `hh`, ✅ `hxx`, ✅ `m`, ✅ `mm`, ✅ `cs`, ✅ `java`, ✅ `kt`, ✅ `kts`, ✅ `swift` |
| Code Files, Web and UI | `CodeRenderer` | ✅ `js`, ✅ `mjs`, ✅ `cjs`, ✅ `jsx`, ✅ `ts`, ✅ `tsx`, ✅ `qml`, ✅ `vue`, ✅ `svelte`, ✅ `astro`, ✅ `css`, ✅ `scss`, ✅ `sass`, ✅ `less` |
| Code Files, Scripting | `CodeRenderer` | ✅ `py`, ✅ `pyw`, ✅ `ipynb`, ✅ `rb`, ✅ `php`, ✅ `sh`, ✅ `bash`, ✅ `zsh`, ✅ `fish`, ✅ `ps1`, ✅ `psm1`, ✅ `psd1`, ✅ `bat`, ✅ `cmd`, ✅ `lua`, ✅ `dart`, ✅ `pl`, ✅ `pm`, ✅ `t` |
| Code Files, Data and Query | `CodeRenderer` | ✅ `sql`, ✅ `r`, ✅ `rmd`, ✅ `jl`, ✅ `do`, ✅ `ado`, ✅ `sas` |
| Code Files, JVM and BEAM | `CodeRenderer` | ✅ `scala`, ✅ `sc`, ✅ `groovy`, ✅ `gradle`, ✅ `ex`, ✅ `exs`, ✅ `erl`, ✅ `hrl`, ✅ `clj`, ✅ `cljs`, ✅ `cljc` |
| Code Files, Systems and Shaders | `CodeRenderer` | ✅ `go`, ✅ `rs`, ✅ `zig`, ✅ `nim`, ✅ `v`, ✅ `asm`, ✅ `s`, ✅ `glsl`, ✅ `vert`, ✅ `frag`, ✅ `hlsl`, ✅ `fx`, ✅ `wgsl`, ✅ `metal` |
| Code Files, Build and Project | `CodeRenderer` | ✅ `dockerfile`, ✅ `containerfile`, ✅ `makefile`, ✅ `mk`, ✅ `ninja`, ✅ `bazel`, ✅ `bzl`, ✅ `BUILD`, ✅ `sln`, ✅ `vcxproj`, ✅ `csproj`, ✅ `fsproj` |
| Text and Structured Data | `TextRenderer` | ✅ `txt`, ✅ `log`, ✅ `json`, ✅ `jsonc`, ✅ `xml`, ✅ `yaml`, ✅ `yml`, ✅ `toml`, ✅ `ini`, ✅ `conf`, ✅ `config`, ✅ `cfg`, ✅ `env`, ✅ `csv`, ✅ `tsv`, ✅ `properties`, ✅ `editorconfig`, ✅ `gitignore`, ✅ `gitattributes`, ✅ `reg`, ✅ `props`, ✅ `targets`, ✅ `cmake`, ✅ `qrc`, ✅ `qss`, ✅ `ui`, ✅ `pri`, ✅ `pro`, ✅ `tsbuildinfo` |
| Images, Raster and Vector | `ImageRenderer` | ✅ `png`, ✅ `jpg`, ✅ `jpeg`, ✅ `jpe`, ✅ `bmp`, ✅ `dib`, ✅ `gif`, ✅ `webp`, ✅ `heic`, ✅ `heif`, ✅ `avif`, ✅ `tif`, ✅ `tiff`, ✅ `svg`, ✅ `ico`, ✅ `dds`, ✅ `tga` |
| Camera RAW Images | `ImageRenderer` | `raw`, `dng`, `cr2`, `cr3`, `nef`, `arw`, `orf`, `rw2`, `raf`, `pef`, `srw`  |
| Audio | `MediaRenderer` | ✅ `mp3`, ✅ `wav`, ✅ `flac`, ✅ `aac`, ✅ `m4a`, ✅ `ogg`, ✅ `oga`, ✅ `wma`, ✅ `opus`, ✅ `aiff`, ✅ `aif`, ✅ `alac`, ✅ `ape`, ✅ `mid`, ✅ `midi` |
| Video | `MediaRenderer` | ✅ `mp4`, ✅ `mkv`, ✅ `avi`, ✅ `mov`, ✅ `wmv`, ✅ `webm`, ✅ `m4v`, ✅ `mpg`, ✅ `mpeg`, ✅ `mts`, ✅ `m2ts`, `3gp`, `flv`, `ogv`, `ts` |
| Subtitles and Captions | `TextRenderer` | `srt`, `vtt`, `ass`, `ssa`, `sub`, `idx` |
| Archives and Packages | `ArchiveRenderer` | ✅ `zip`, ✅ `7z`, ✅ `rar`, ✅ `tar`, ✅ `gz`, ✅ `tgz`, ✅ `bz2`, ✅ `tbz`, ✅ `tbz2`, ✅ `xz`, ✅ `txz`, ✅ `cab`, `iso`, `jar`, `war`, `ear`, `apk`, `ipa`, `nupkg`, `vsix`, `crx`, `appx`, `msix` |
| Design Files | `SummaryRenderer` | ✅ `psd`, `ai`, `eps`, `sketch`, `fig`, `xd`, `indd`, `idml`, `cdr`, `afdesign`, `afphoto`, `aseprite`  |
| CAD and Engineering | `SummaryRenderer` | `dwg`, `dxf`, `step`, `stp`, `iges`, `igs`, `stl`, `sat`, `sldprt`, `sldasm`, `ipt`, `iam`, `f3d`, `fcstd`  |
| 3D Models | `SummaryRenderer` | `obj`, `fbx`, `glb`, `gltf`, `dae`, `3ds`, `ply`, `usd`, `usdz`, `blend`, `abc`, `ifc`  |
| GIS Files | `SummaryRenderer` | `shp`, `shx`, `dbf`, `prj`, `geojson`, `kml`, `kmz`, `tif`, `tiff`, `geotiff`, `gpkg`, `gdb`, `mbtiles`, `osm`, `pbf`  |
| eBook and Comic Files | `SummaryRenderer` | `epub`, `mobi`, `azw`, `azw3`, `azw4`, `fb2`, `djvu`, `djv`, `cbz`, `cbr`, `cb7`, `cbt`  |
| Medical Imaging | `SummaryRenderer` | `dcm`, `dicom`, `nii`, `nii.gz`, `nrrd`, `mha`, `mhd`, `img`, `hdr`  |
| Scientific and Analytics Data | `SummaryRenderer` | `h5`, `hdf5`, `nc`, `netcdf`, `mat`, `parquet`, `feather`, `arrow`, `fits`, `fit`, `sav`, `dta`, `por`  |
| Finance, Calendar, and Contacts | `SummaryRenderer` | `ofx`, `qif`, `qfx`, `xbrl`, `ixbrl`, `ics`, `ical`, `vcf`, `vcard`  |
| Database Files | `SummaryRenderer` | `sqlite`, `sqlite3`, `db`, `db3`, `mdb`, `accdb`, `frm`, `ibd`, `bak`, `dump`, `sqlitedb`  |
| Fonts | `SummaryRenderer` | `ttf`, `otf`, `woff`, `woff2`, `eot`, `ttc`, `fon`  |
| Certificates and Keys | `CertificateRenderer` | ✅ `cer`, ✅ `crt`, ✅ `pem`, ✅ `der`, ✅ `pfx`, ✅ `p12`, ✅ `key`, ✅ `pub`, ✅ `asc`, ✅ `gpg`  |
| Executables and Installers | `SummaryRenderer` | ✅ `exe`, ✅ `dll`, ✅ `msi`, `msix`, `appx`, ✅ `com`, ✅ `scr`, ✅ `sys` |
| Shortcuts and Shell Items | `SummaryRenderer` | ✅ `lnk`, ✅ `url`, ✅ `appref-ms` |
| Folders and Generic Files | `FolderRenderer`, `SummaryRenderer` | ✅ File system folders, ✅ shell folders, ✅ desktop items, ✅ unknown file types, ✅ generic fallback preview |

### PDF

Renderer: `PdfRenderer`

Behavior:

1. Uses PDFium through a dynamic DLL.
2. Uses a native PDF preview widget.
3. Supports scrolling.
4. Supports `Ctrl + mouse wheel` zoom.
5. Uses a larger default preview window size for document style content.

Current note:

1. The current native PDF preview path is focused on speed and stability.

### Office Documents

Renderer: `DocumentRenderer`

Behavior:

1. `docx`, `xlsx`, and `pptx` are parsed with Qt side logic and rendered into lightweight HTML.
2. Legacy binary formats `doc`, `xls`, and `ppt` are converted through local Microsoft Office automation into modern Open XML formats when Microsoft Office is installed.
3. The converted result is cached under the local temp directory and then previewed through the existing Qt parser path.

### Code Files

Renderer: `CodeRenderer`

Behavior:

1. Uses `KSyntaxHighlighting`.
2. Uses the current GitHub style theme path.
3. Shows read only code preview with line numbers.
4. Truncates very large files at 2 MB for responsiveness.

Planned format coverage:

1. C family: `c`, `cc`, `cpp`, `cxx`, `h`, `hpp`, `hh`, `hxx`, `m`, `mm`, `cs`, `java`, `kt`, `kts`, `swift`.
2. Web and UI: `js`, `mjs`, `cjs`, `jsx`, `ts`, `tsx`, `qml`, `vue`, `svelte`, `astro`, `css`, `scss`, `sass`, `less`, `html`, `htm`.
3. Scripting: `py`, `pyw`, `ipynb`, `rb`, `php`, `sh`, `bash`, `zsh`, `fish`, `ps1`, `psm1`, `psd1`, `bat`, `cmd`, `lua`, `pl`, `pm`, `t`.
4. Data and query code: `sql`, `r`, `rmd`, `jl`.
5. JVM and BEAM: `scala`, `sc`, `groovy`, `gradle`, `ex`, `exs`, `erl`, `hrl`, `clj`, `cljs`, `cljc`.
6. .NET and legacy languages: `fs`, `fsi`, `fsx`, `vb`, `vbs`, `pas`, `pp`.
7. Systems and emerging languages: `go`, `rs`, `zig`, `nim`, `v`, `asm`, `s`.
8. Build and project files: `dockerfile`, `makefile`, `mk`.

### Text Files

Renderer: `TextRenderer`

Behavior:

1. Uses `QPlainTextEdit`.
2. Shows line numbers.
3. Uses custom modern scroll bar styling.
4. Truncates very large files at 2 MB for responsiveness.

### Images

Renderer: `ImageRenderer`

Behavior:

1. Supports zoom with `Ctrl + mouse wheel`.
2. Supports drag to pan when zoomed in.
3. Supports double click to reset zoom.
4. Supports copying the image to the clipboard.

### Audio and Video

Renderer: `MediaRenderer`

Behavior:

1. Prefers dynamic `libmpv` playback when available.
2. Opens preview in paused state.
3. Uses default volume `50`.
4. Supports click to play or pause.
5. Supports click seek on the progress slider.
6. Supports mute toggle and direct volume slider control.
7. Falls back to `QMediaPlayer` for audio when `libmpv` is unavailable.

### Summary and Fallback

Renderer: `SummaryRenderer`

Common routed types:

1. `welcome`
2. `folder`
3. `shortcut`
4. `shell_folder`
5. `archive`
6. generic file types
7. unsupported objects

Behavior:

1. Shows normalized item metadata.
2. Shows path, resolved target, and basic file system flags.
3. Shows status text for missing, unsupported, or unresolved objects.
## Core Modules

1. `file_type_detector.*`
   Detects the hovered object, resolves its real path, and assigns a normalized `typeKey`.

2. `preview_manager.*`
   Controls preview show and hide flow and handles the `Space` toggle behavior.

3. `preview_state.*`
   Stores the current preview item state used by the rendering layer.

4. `renderers/RendererRegistry.*`
   Registers all renderers and selects the first one that can handle the current item.

5. `renderers/PreviewHost.*`
   Hosts renderer widgets and switches between them.

6. `widgets/SpaceLookWindow.*`
   Provides the frameless preview window, close button, and manual resize from window edges and corners.
