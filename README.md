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
2. `document`
3. `code`
4. `text`
5. `image`
6. `media`
7. `summary`

The first renderer whose `canHandle()` returns `true` is used.

## Supported Preview Types

| Category | Renderer | Supported formats or behavior |
| --- | --- | --- |
| PDF | `PdfRenderer` | `pdf` |
| Office Documents | `DocumentRenderer` | `doc`, `docx`, `xls`, `xlsx`, `ppt`, `pptx` |
| Code Files | `CodeRenderer` | `c`, `cc`, `cpp`, `cxx`, `h`, `hpp`, `hh`, `hxx`, `cs`, `java`, `kt`, `kts`, `go`, `rs`, `swift`, `m`, `mm`, `py`, `rb`, `php`, `js`, `jsx`, `ts`, `tsx`, `qml`, `vue`, `svelte`, `css`, `scss`, `sass`, `less`, `html`, `htm`, `sql`, `sh`, `bash`, `zsh`, `fish`, `dockerfile`, `makefile`, `mk` |
| Text Files | `TextRenderer` | `txt`, `md`, `log`, `json`, `ini`, `xml`, `yaml`, `yml`, `toml`, `conf`, `config`, `cfg`, `props`, `targets`, `bat`, `cmd`, `ps1`, `reg`, `env`, `gitignore`, `gitattributes`, `editorconfig`, `gradle`, `cmake`, `qrc`, `qss`, `ui`, `pri`, `pro` |
| Images | `ImageRenderer` | `png`, `jpg`, `jpeg`, `bmp`, `gif`, `webp`, `heic`, `heif`, `svg`, `ico`, `psd` |
| Audio | `MediaRenderer` | `mp3`, `wav`, `flac`, `aac`, `m4a`, `ogg`, `wma`, `opus` |
| Video | `MediaRenderer` | `mp4`, `mkv`, `avi`, `mov`, `wmv`, `webm`, `m4v`, `mpg`, `mpeg`, `ts` |
| Archives | `SummaryRenderer` | `zip`, `7z`, `rar`, `tar`, `gz`, `tgz`, `bz2`, `tbz`, `tbz2`, `xz`, `txz`, `cab` |
| Shortcuts and Shell Items | `SummaryRenderer` | `lnk`, `url`, `appref-ms` |
| Folders and Generic Files | `SummaryRenderer` | Folders, shell folders, desktop items, unknown file types, and generic fallback preview |

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
3. Shows read only code preview.
4. Truncates very large files at 2 MB for responsiveness.

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