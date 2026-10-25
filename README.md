# FLEX Download Manager 1.0

A personal, **non-commercial** segmented download manager for Windows x64,
written from scratch in C++17 with **Qt 6 Widgets** for the user interface and
the WinHTTP transport stack.

> This project is a private learning exercise. It is **not affiliated with,
> endorsed by, or derived from** Tonec Inc. or Internet Download Manager (IDM).
> No third-party binary or decompiled source is redistributed here. The original
> installer is used only as a *behavioral* reference for feature research.

---

## Table of contents

- [What it does](#what-it-does)
- [Evidence provenance](#evidence-provenance)
- [Requirements](#requirements)
- [Building](#building)
- [Installer](#installer)
- [Running](#running)
- [Chrome extension integration](#chrome-extension-integration)
- [Source layout](#source-layout)
- [Download engine behavior](#download-engine-behavior)
- [Headless test harness (`flextest`)](#headless-test-harness-flextest)
- [Local test server](#local-test-server)
- [Credits](#credits)
- [Known limitations](#known-limitations)

---

## What it does

- Adds downloads by direct URL, from the clipboard, or by drag-and-drop.
- Probes the server for byte-range support, then splits the transfer across up
  to *N* parallel connections, one per part file.
- Resumes interrupted or paused downloads, including across process restarts, by
  persisting part state to a sidecar file.
- Falls back gracefully to a single sequential stream when the server ignores
  `Range` (HTTP 200) or uses `Transfer-Encoding: chunked`.
- Verifies/repairs connections that end early and retries transient failures.
- Adopts a server-suggested name from `Content-Disposition` when the name was
  guessed from the URL.
- Minimizing or closing the window hides it to the notification area; downloads
  keep running in the background. The tray icon restores the window; use its
  **Exit** action (or `File ▸ Exit` / `Ctrl+Q`) to quit fully. Exiting with
  active downloads asks for confirmation first.
- Optional Chrome/Chromium integration: a browser download opens the IDM-style
  **Download File Info** dialog. If the desktop app is not running, Chrome keeps
  the download instead of losing it.
- Per-user settings under `HKCU\Software\FLEXDownloadManager` (connections,
  retries, timeouts, user agent, temp/save folders, proxy, speed limit, minimum
  part, remembered category folders and custom categories). On first run the
  app copies any existing settings from the old `HKCU\Software\IDMClone` key.

### UI surface

The main window follows the classic IDM layout and is built entirely with Qt
Widgets:

- **Menu bar** - `Tasks`, `File`, `Downloads`, `View`, `Help`, `Registration`.
  The `Downloads` menu mirrors IDM: Pause All, Stop All, Delete All Completed,
  Find (Ctrl+F), Find Next (F3), Scheduler, Start queue / Stop queue submenus,
  Speed Limiter and Options. Right-clicking a row opens the per-item commands
  (Download Now, Stop Download, Redownload, Remove, Open file/folder, Copy URL).
- **Toolbar** - icon + caption buttons for Add URL, Resume, Stop, Stop All,
  Delete, Delete Completed, Options, Scheduler, Start Queue, Stop Queue and
  Tell a Friend, grouped with separators. Icons come from the embedded
  **Fluent UI System Icons** font (see [Credits](#credits)).
- **Categories pane** on the left — `All Downloads` (Compressed, Documents,
  Music, Programs, Video), `Unfinished`, `Finished`, `Grabber projects`,
  `Queues`. Selecting a category filters the list by file type or state; the
  pane can be hidden from its ✕ button or from `View ▸ Categories`.
- **Download list** — `File Name`, `Q`, `Size`, `Status`, `Time left`,
  `Transfer rate`, `Last Try Date`, `Description`.
- **Status bar** — item counters, aggregate speed and a progress bar.

A dark Qt stylesheet and palette reproduce the reference dark theme, including
the dark window frame on Windows 10/11.

| Shortcut | Command |
| --- | --- |
| `Ctrl+N` | Add URL… |
| `Ctrl+V` | Add from clipboard |
| `Ctrl+F` | Find |
| `F3`     | Find next |
| `Ctrl+A` | Select all |
| `Del`    | Remove from list |
| `F5`     | Start |
| `F9`     | Stop |

Dialogs: **Add download**, **Download File Info** (from Chrome), **Options**,
**About**. The tray icon shows a balloon and a context menu; double-click
restores the window. Minimizing or closing hides the window to the notification
area while the app and its downloads keep running.
`View ▸ Language` switches between English and العربية at runtime, including
the RTL layout; `View ▸ Arrange files` exposes the table sorting options.

---

## Evidence provenance

Feature research was performed with
[REA](https://github.com/morluto/rea) (`rea-agents` 6.7.0) driving **Ghidra
12.1.4** as the native analysis provider.

Target binary (personal copy, kept outside version control):

| Property | Value |
| --- | --- |
| File | `idman642build61.exe` |
| Format | PE32 (x86), ~12.4 MB |
| SHA-256 | `b180ad3f4f6069331a98040de640fd762cfd44a048fe938513d4e8973d7f4bd6` |

The installer is a self-extracting archive of ~192 zlib streams starting at
`0x17420`. The embedded `IDMan.exe` (image base `0x400000`, ~18.8k procedures)
is MFC-based; class names such as `CFilePart`, `ChunksList`, `CQueueTab`,
`HTTPConnection`, `FTPSession` and `CM3UParser` were used to confirm which
capabilities to reproduce. Extracted strings (`analysis/strings_all.txt`,
~40k entries) corroborate the HTTP/range/timeout surface.

All clone code in `src/` is original. Nothing was copied from the target.

---

## Requirements

- Windows 10/11 x64
- Visual Studio 2022+ (or VS 2026) with the **Desktop development with C++**
  workload
- **Qt 6.5 or newer** with the **Widgets** module (tested with Qt 6.8.3,
  `win64_msvc2022_64`)
- CMake ≥ 3.21 and Ninja (both ship with the VS CMake component)
- Optional: Chrome/Chromium 102+ for the browser extension (the desktop app
  works without it)

`build.bat` assumes the VS install at
`C:\Program Files\Microsoft Visual Studio\18\Community` and Qt at
`C:\Qt\6.8.3\msvc2022_64`; edit `VSROOT` or set `QTROOT` if yours differs.

At runtime the Qt DLLs and the `platforms\qwindows.dll` plugin are required.
`build.bat` runs `windeployqt` so the `build` folder is self-contained.

The download engine itself still uses only inbox Windows libraries (`winhttp`,
`shlwapi`, `shell32`, `ole32`, `user32`, `gdi32`, `advapi32`).

---

## Building

```bat
cd idm-clone
cmd /c build.bat            :: Release (default)
cmd /c build.bat Debug      :: Debug
```

Outputs:

- `build\FLEX Download Manager.exe` — the GUI application
- `build\flextest.exe` — console engine harness (not shipped)

The build is warning-clean under `/W4 /permissive- /utf-8 /EHsc`.

The app and Chrome-extension icons are generated from `FlexLOGO.svg`.
Regenerate both after changing the logo (requires Chrome/Chromium and
Pillow; the normal build uses the checked-in assets):

```bat
python tools\make_icon.py
```

Use `--no-extension-icons` to update only `src\app.ico`.

---

## Installer

`installer/` contains a small, dependency-free Win32 setup program (static CRT,
no Qt, no Inno Setup/NSIS) published under the **ProSoft** name. It embeds the
whole application as one `RCDATA` payload, so the result is a single
self-contained `.exe`.

```bat
cmd /c installer\build_setup.bat
```

The script expects `build\FLEX Download Manager.exe` (run `build.bat` first) and
produces:

- `dist\FLEX Download Manager 1.0 Setup.exe` — setup + uninstaller + payload

Behaviour:

- **Per-user install**, no UAC: `%LOCALAPPDATA%\Programs\FLEX Download Manager`,
  with an uninstall entry under
  `HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\{7F1C2A54-9D3E-4B7A-9E64-2B9F1D6C0A31}`.
  Everything is removed again by the uninstaller, including the shortcuts and
  the per-user files the setup created.
- **Theme-matched UI** using the application's colours (`#1e252f`, `#252d39`,
  `#386ea8`, ...), owner-drawn controls, dark DWM title bar.
- **English and Arabic** (RTL) interface, auto-detected from the Windows display
  language and switchable from the header.
- **Desktop and Start menu shortcuts** in a `ProSoft` program group.
- **Direct streaming extraction** from the embedded payload with real progress,
  no temporary archive and no external tools.
- **Browser extension guide** on the finish page: the extension is installed to
  `browser-extension\` next to the application, and the page detects Chrome,
  Brave and Edge and walks the user through *Load unpacked*.
- **Uninstaller** (`unins000.exe`) built from the same sources. The UI uninstall
  copies itself to `%TEMP%` first so the application folder can be deleted
  completely.

Command line:

| Switch | Meaning |
| --- | --- |
| `/S` | Silent install (no UI, no prompts) |
| `/DIR=<path>` | Install folder (default: per-user `Programs` folder) |
| `/NOLAUNCH` | Do not start the app after a silent install |
| `/LANG=en` / `/LANG=ar` | Force the interface language |
| `unins000.exe /S` | Silent uninstall |
| `unins000.exe /S /DELSETTINGS` | Silent uninstall and delete `HKCU\Software\FLEXDownloadManager` |

Code signing is optional; set `PROSOFT_CERT`, `PROSOFT_CERT_ARGS` and
`PROSOFT_SIGNTOOL` before running the build script to sign the application,
the uninstaller and the setup executable.

### Why the extension is a guided step

Chrome, Brave and Edge refuse to install an extension that is not published in
their stores: since Chrome 33 the registry "external extension" method only
accepts Chrome Web Store entries, and `ExtensionInstallForcelist` only works for
store-hosted extensions (or domain/Azure-AD managed devices). Elevating the
installer therefore does **not** make a store-less extension install silently.

The setup copies the unpacked extension next to the application and shows the
four-step guide instead, which works on any machine without publishing anything.
If the extension is later published to the Chrome Web Store and Edge Add-ons,
the installer can be extended with a policy-based silent install by writing
`ExtensionInstallForcelist` under `HKLM\SOFTWARE\Policies\...` — a step that does
require administrator rights.

---

## Running

```bat
"build\FLEX Download Manager.exe"
```

Add a URL with `Ctrl+N` (or `File ▸ Add URL…`), pick a folder, and start it.
**Minimizing** or **closing** the window hides it to the tray; double-click the
icon to restore it, or right-click for **Show main window** / **Exit**. The app
keeps running and downloads continue in the background. **Exit** (or `Ctrl+Q`)
quits completely; if downloads are still running it asks for confirmation
first.

---

## Chrome extension integration

The optional MV3 extension in `chrome-extension/` forwards Chrome downloads to
the running desktop app. When Chrome starts a download, the extension sends its
URL, suggested file name, MIME type, total size and referrer to the app, and the
app opens the IDM-style **Download File Info** dialog:

- **URL** (editable), **Category** with `+` for a custom category, **Save As**
  with `...` for a folder picker, a remembered-path group for the selected
  category, **Description**, the file icon and size, and **Download Later** /
  **Start Download** / **Cancel**.
- The layout mirrors for Arabic (RTL) and follows the app's dark theme.
- Category folders remembered with the checkbox are stored per category under
  `HKCU\Software\FLEXDownloadManager` and applied to later downloads of that
  category.
- Custom categories added with `+` are persisted and appear in later dialogs.
- The size comes from Chrome's `totalBytes` when available; otherwise the app
  probes `Content-Length` with a Qt `HEAD` request.
- Browser-triggered downloads surface **only** that dialog: the main window is
  not shown or raised, and the dialog is placed on the screen with the pointer,
  kept above the browser and given the foreground so it is ready to type in.
  Closing it leaves the app in the notification area.

Automatic capture stops the browser's copy as soon as Chrome creates the
download item and holds filename determination until the app answers. Chrome
cannot reach the step that opens its own **Save As** dialog while that callback
is pending, so the browser dialog no longer appears next to **Download File
Info** even when *Ask where to save each file before downloading* is enabled.
If the app is offline or rejects the URL, the paused download is resumed
untouched and Chrome behaves normally. A ten-second safety timer resumes the
download if the app never answers.

### Install (development)

1. Start `"build\FLEX Download Manager.exe"`.
2. Open `chrome://extensions` in Chrome/Chromium.
3. Enable **Developer mode**, choose **Load unpacked**, and select the
   `chrome-extension` folder (or the extracted
   `dist\FLEXDownloadManager-extension.zip`).
4. Pin **FLEX Download Manager** to the toolbar. The popup shows whether the
   desktop app is reachable and lets you turn automatic capture and the media
   panel on or off.

The setup program copies the extension to `browser-extension\` inside the
installation folder and repeats these steps on its finish page, with a button
for every detected browser.

Branded Chrome 137+ removed the `--load-extension` command-line switch. Use
Chrome for Testing/Chromium for scripted testing, or load the folder manually as
above. The extension also adds a **Download with FLEX Download Manager**
context-menu item for links, images, video and audio.

### Media download panel

On pages with a visible `<video>` or `<audio>` element, the extension shows a
small **Download this video** / **Download this audio** toolbar glued to the
player's top edge and aligned with its right edge, with no gap between the strip
and the frame (it drops just below the frame when the page top leaves no room).
The panel appears when the media is detected, on hover, and on playback; it is
rendered in a shadow root, so it does not inherit or alter the page's media
controls. It offers:

- A single panel button. When the page exposes **one** video/audio file, clicking
  it sends that file to the desktop app and opens **Download File Info**.
- When **more than one rendition** is available, clicking the panel opens a
  drop-down list with the video name, every resolution, and the file size
  (when the server reports it). Clicking a resolution sends that exact stream to
  the desktop app and opens **Download File Info** for it.
- Qualities are detected from `<source>` elements, media attributes, URL
  resolution hints (`1080p`, `1920x1080`, `itag=137`), and the media requests the
  service worker sees. Network capture is what makes the panel work on players
  that use MSE (`blob:` URLs): the `<video>` element has no downloadable URL, but
  the streams it fetched can still be offered. The list is sorted from highest to
  lowest resolution.
- On **YouTube, Vimeo, TikTok, Instagram, X/Twitter, Facebook, Reddit, Twitch,
  SoundCloud** and the other sites listed in `content.js` (`YTDLP_SITES`), the
  panel asks the desktop app for a quality list from **yt-dlp** as soon as the
  video is detected/played, and caches it per video. Every resolution the site
  publishes is listed with its approximate size, including 1080p/1440p/4K.
- `?` help and `X` close buttons. Closing hides the panel for that media element
  until the page is reloaded.
- English and Arabic labels, following the extension UI language.

For those social sites the download runs through the bundled `yt-dlp.exe` instead
of the HTTP engine: the app merges the separate video and audio tracks with
ffmpeg when the chosen resolution needs it. ffmpeg is fetched once, on the first
merged download (`~115 MB`, gyan.dev build), unpacked with Windows' `tar.exe`, and
kept in `%LOCALAPPDATA%\FLEXDownloadManager\ffmpeg\ffmpeg.exe`. yt-dlp is looked up
next to the executable, in its `tools` folder, in that per-user folder, or on
`PATH`, so dropping in a newer build is enough to update it.

The panel works independently of the **Capture Chrome downloads** setting: it
is an explicit user action. The popup has a separate **Show media download
panel** toggle, on by default. The panel ignores tiny/hidden media, follows the
frame on scroll and resize, and hides completely while the player is in
fullscreen, coming back when the user leaves it.

> **Adaptive streams:** HLS (`.m3u8`) and DASH (`.mpd`) manifests are detected
> but are not offered as direct downloads. The desktop engine downloads a single
> HTTP file; it does not fetch, decrypt or mux adaptive segments itself. On the
> supported social sites the panel delegates to yt-dlp, which handles those
> manifests; elsewhere, use the page's own player or a direct-file source.

> **Note:** If the original Internet Download Manager is installed, its browser
> integration can capture a link before Chrome creates a download item. Disable
> IDM's browser integration (or close IDM) while testing this extension,
> otherwise both tools can compete for the same download.

### Chrome's own Save As prompt

If Chrome is set to **Ask where to save each file before downloading**
(`chrome://settings/downloads`), Chrome opens its own Save As dialog before the
extension can cancel the download, so both dialogs appear. Turn that option off
to see only the FLEX Download Manager dialog. The extension popup has a shortcut
to the Chrome download settings. As a one-off, **Download with FLEX Download
Manager** in the context menu bypasses Chrome's download flow entirely.

Chrome does not expose this preference to extensions, and `downloads.cancel()`
cannot close the already-open native Save As dialog. The setting is therefore a
one-time browser configuration, not something the extension can toggle.

### Behavior and safety

- The bridge binds only to `127.0.0.1`, tries ports `48765`–`48769`, and accepts
  requests only from `chrome-extension://` origins. No native-messaging host or
  registry entry is installed.
- The extension asks for `webRequest` and all-site host access so the media panel
  can list streams that players fetch through MSE. Captured URLs stay in the
  service worker's memory per tab, are dropped when the tab closes or navigates,
  and are never sent anywhere except the local desktop app when you click one.
- The extension forwards the download first. Chrome's copy is canceled and
  erased only after the app acknowledges the request. If the app is not running,
  the Chrome download is left untouched.
- If a manual context-menu download cannot reach the app, the extension shows a
  notification instead of losing the link.
- The media panel sends explicit panel clicks straight to the local bridge; it
  never starts a Chrome download item, so it works even when automatic capture is
  off. HLS/DASH manifests are intentionally not offered.
- The popup's **Capture Chrome downloads** toggle is on by default; turning it
  off leaves all downloads in Chrome. **Show media download panel** is also on by
  default and can hide the overlay without disabling capture.
- The popup is localized in English and Arabic.

### Packaging a CRX

```bat
powershell -ExecutionPolicy Bypass -File tools\pack_chrome_extension.ps1
```

Outputs:

- `dist\FLEXDownloadManager.crx` — packed extension
- `dist\FLEXDownloadManager-extension.zip` — unpacked-folder zip for **Load unpacked**
- `chrome-extension.pem` — generated on the first pack; keep it private and
  reuse it to keep the extension ID stable.

Chrome may refuse to install a CRX that is not distributed through the Web
Store; the unpacked folder or the zip is the reliable development path.

---

## Source layout

| File | Responsibility |
| --- | --- |
| `FlexLOGO.svg` | Source logo for the application, tray and Chrome-extension icons |
| `src/common.h` | Master header: shared types, `Config`, `UrlParts`, `HttpClient`, `DownloadItem`, `DownloadManager` |
| `src/common.cpp` | Path/format/registry/`FileNameFromDisposition` helpers, `NowUnixMs` |
| `src/url.cpp` | URL parsing, `FileNameFromPath`, path joining |
| `src/config.cpp` | `Config::Load`/`Save` against `HKCU\Software\FLEXDownloadManager`, including remembered category folders and custom categories |
| `src/http.cpp` | WinHTTP client: request build, header parse, redirects, `Abort()`, final-URL capture |
| `src/download.cpp` | Segmented engine, part files, resume records, per-item worker, `DownloadManager` |
| `src/ui_qt.h` | Qt `MainWindow` declaration and dialog entry points |
| `src/ui_main.cpp` | Qt main window: menus, toolbar, categories tree, download table, status bar, tray, drag-drop, commands, dark theme, Chrome dialog queue |
| `src/ui_dialogs.cpp` | Qt Add / Download File Info / Options / About dialogs, `BrowseForFolderQt` |
| `src/bridge.h` / `src/bridge.cpp` | Loopback HTTP bridge used by the Chrome extension (`/ping`, `/download`) |
| `src/resources.qrc` | Embeds `app.ico` and the Fluent icon font for the window, tray and toolbar |
| `src/resource.h` | Icon identifier used by the resource script |
| `src/app.rc` | Executable icon and version info |
| `src/fonts/FluentSystemIcons-Subset.ttf` | Subsetted Fluent icon font used for toolbar icons |
| `tests/engine_test.cpp` | `flextest` console harness |
| `tools/test_server.py` | Local HTTP test server (ranges, 200, chunked, Content-Disposition, throttle, HEAD) |
| `tools/make_icon.py` | Renders `FlexLOGO.svg` into `src/app.ico` and the Chrome-extension icons |
| `chrome-extension/` | MV3 extension: automatic capture, media download panel, popup, context menu, English/Arabic locales |
| `tools/pack_chrome_extension.ps1` | Packs the extension into `dist/FLEXDownloadManager.crx` and a zip |
| `dist/` | Generated CRX and extension zip |

---

## Download engine behavior

- **Probe.** A ranged `GET` for bytes `0-1` classifies the resource:
  - `206` → resumable; total size from `Content-Range`.
  - `200` → not resumable unless `Accept-Ranges: bytes` and not chunked;
    transfer runs as a single stream.
  - `416` → treated as complete/empty.
  - `≥400` → item enters the `Error` state with the HTTP status text.
- **Segmentation.** Total size is divided into parts of at least
  `minPartBytes` (default 1 MiB), capped at `maxConnections` (default 8).
- **Part files.** Each part is written to a temp file; on completion the parts
  are stitched into the destination and the temp directory is cleaned up.
- **Resume.** A binary sidecar (`PersistedState` + per-segment records) stores
  the URL, total size, and each part's start/end/written/done state, so a later
  run — even a new process — continues instead of restarting.
- **Open-ended.** When the length is unknown, one sequential segment is used and
  the `openEnded` flag disables range splitting.
- **Retries.** Transient failures retry up to `maxRetries` (default 5) with
  backoff; `downloadedBytes` accounting is corrected on retry so the speed and
  progress readouts stay honest.
- **Cancellation.** `HttpClient::Abort()` closes the WinHTTP handles to unblock
  a thread sitting in a read; the worker then observes `stopRequested_`.
- **Redirects.** Followed with a policy cap, and the final URL is captured for
  display and resume metadata.
- **Notifications.** The engine publishes progress through atomic counters on
  each `DownloadItem`; the Qt main window refreshes them on a 300 ms timer and
  only rebuilds rows when the visible set changes.

---

## Headless test harness (`flextest`)

```
flextest <url> <output-path> [connections] [--pause-after MS] [--pause-only] [--cd-name]
```

- `connections` — parallel connections (default 8).
- `--pause-after MS` — stop after *MS* milliseconds, verify the `Paused` state,
  then resume.
- `--pause-only` — stop and exit, leaving part files for a separate resume run.
- `--cd-name` — treat the name as URL-derived so a `Content-Disposition` name
  replaces it (mimics the Add dialog with an empty name).

Examples:

```bat
:: plain segmented download (8 connections)
build\flextest.exe http://127.0.0.1:8765/payload.bin out\payload.bin 8

:: pause, then resume
build\flextest.exe http://127.0.0.1:8766/payload.bin out\payload.bin 8 --pause-after 400

:: cross-process resume
build\flextest.exe http://127.0.0.1:8766/payload.bin out\payload.bin 8 --pause-only
build\flextest.exe http://127.0.0.1:8766/payload.bin out\payload.bin 8

:: server ignores Range (HTTP 200) / chunked
build\flextest.exe http://127.0.0.1:8765/norange.bin out\norange.bin 8
build\flextest.exe http://127.0.0.1:8765/chunked.bin out\chunked.bin 8

:: Content-Disposition rename -> greeting.txt
build\flextest.exe http://127.0.0.1:8766/file.txt out\file.txt 4 --cd-name
```

A successful run prints `complete: size=<n> path=<file>` and the file's SHA-256
matches the expected payload hash.

---

## Local test server

```bat
python tools\test_server.py 8765
python tools\test_server.py 8766 --delay 0.05 --chunk 32768
```

Endpoints: `/payload.bin` (~3 MiB, range-capable), `/norange.bin` and
`/chunked.bin` (512 KiB, deliberately non-splittable), and `/file.txt`
(served with `Content-Disposition: attachment; filename="greeting.txt"`).
All endpoints answer `HEAD`, so the Chrome dialog can probe `Content-Length`.

---

## Credits

- Application logo: `FlexLOGO.svg`, supplied with the project.
- Toolbar icons: [Microsoft Fluent UI System Icons](https://github.com/microsoft/fluentui-system-icons),
  MIT licence. The build embeds a subset containing only the glyphs used by
  the application (`src/fonts/FluentSystemIcons-Subset.ttf`); the full licence
  text ships in `src/fonts/LICENSE-Fluent.txt`.

---

## Known limitations

- HTTP/HTTPS only; no FTP (the original IDM supports FTP — out of scope here).
- Browser integration is Chrome/Chromium-only (MV3 extension). There is no
  Firefox/Edge-specific package. Outside the social sites handled by yt-dlp, the
  media panel only downloads direct HTTP(S) renditions; it does not fetch or mux
  HLS/DASH adaptive streams.
- CRX installs outside the Chrome Web Store may be blocked by Chrome; use
  **Load unpacked** for development.
- The Chrome dialog only appears while the desktop app is running. If it is
  offline, Chrome keeps the download and the extension sends a notification for
  manual context-menu captures.
- Part stitching is done in a single pass and rewrites the whole file; no
  sparse/in-place assembly.
- Proxy modes are *direct*, *use system proxy* (WinHTTP automatic proxy, which
  covers PAC/WPAD), and *manual* host:port. There is no per-site proxy rule
  list.
- Speed limit is enforced per connection, not globally, so the aggregate rate
  can reach `connections × limit`.
- Resume records are validated by size/URL only, not by an ETag or
  `Last-Modified` guard, so a changed remote file can still be resumed against.
- Drag-and-drop and clipboard add accept a single URL at a time.
