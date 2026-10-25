// FLEX Download Manager media panel.
//
// A lightweight content script that adds an IDM-style "Download this video"
// toolbar over <video>/<audio> elements. It only ever forwards direct HTTP(S)
// media URLs; adaptive manifests (HLS/DASH) are detected but not offered as
// downloads because the desktop engine cannot mux their segments yet.

(() => {
  "use strict";

  if (globalThis.__flexMediaPanelInstalled) return;
  globalThis.__flexMediaPanelInstalled = true;

  const api = typeof chrome !== "undefined" ? chrome : null;
  const HOST_ATTR = "data-flex-media-panel-host";
  const MEDIA_SELECTOR = "video, audio";
  // The toolbar sits flush against the video frame (no gap) and hugs its right
  // edge. EDGE_KEEP only keeps it inside the viewport when the frame is offscreen.
  const EDGE_KEEP = 2;
  const MAX_REMEMBERED_OPTIONS = 24;
  // Sites whose player hides every download behind adaptive streaming. yt-dlp
  // (shipped with the desktop app) resolves their qualities instead.
  const YTDLP_SITES = [
    "youtube.com", "youtu.be", "youtube-nocookie.com", "vimeo.com",
    "dailymotion.com", "dai.ly", "tiktok.com", "instagram.com",
    "facebook.com", "fb.watch", "twitter.com", "x.com", "reddit.com",
    "redd.it", "twitch.tv", "soundcloud.com", "bilibili.com", "vk.com",
    "ok.ru", "rumble.com", "streamable.com"
  ];
  const MIN_VIDEO_WIDTH = 160;
  const MIN_VIDEO_HEIGHT = 90;
  const MIN_AUDIO_WIDTH = 120;
  const MIN_AUDIO_HEIGHT = 24;
  const DIRECTIONAL_ATTRIBUTES = [
    "src", "type", "label", "data-label", "data-quality", "data-res",
    "data-resolution", "res", "height", "data-height", "data-src",
    "data-video-src", "data-video-url", "data-url", "data-mp4", "data-file",
    "data-hls", "data-dash", "data-manifest"
  ];

  const VIDEO_EXTENSIONS = new Set([
    "mp4", "m4v", "mov", "webm", "mkv", "flv", "avi", "ogv", "ts",
    "m2ts", "3gp", "mpg", "mpeg"
  ]);
  const AUDIO_EXTENSIONS = new Set([
    "mp3", "m4a", "aac", "ogg", "oga", "opus", "wav", "flac", "wma",
    "aiff", "alac"
  ]);
  const DIRECT_EXTENSIONS = new Set([
    ...VIDEO_EXTENSIONS,
    ...AUDIO_EXTENSIONS
  ]);
  const ADAPTIVE_URL = /\.(m3u8|mpd)(?:$|[?#])/i;
  const ITAG_HEIGHT = {
    5: 240, 6: 240, 17: 144, 18: 360, 22: 720, 34: 360, 35: 480,
    36: 240, 37: 1080, 38: 3072, 43: 360, 44: 480, 45: 720, 46: 1080,
    82: 360, 83: 480, 84: 720, 85: 1080, 100: 360, 101: 480, 102: 720,
    133: 240, 134: 360, 135: 480, 136: 720, 137: 1080, 138: 4320,
    160: 144, 212: 480, 213: 480, 214: 720, 215: 720, 216: 1080,
    217: 1080, 264: 1440, 266: 2160, 278: 144, 298: 720, 299: 1080,
    302: 720, 303: 1080, 308: 1440, 313: 2160, 315: 2160, 330: 144,
    331: 240, 332: 360, 333: 480, 334: 720, 335: 1080, 336: 1440,
    337: 2160
  };
  const MIME_BY_EXTENSION = {
    mp4: "video/mp4",
    m4v: "video/mp4",
    mov: "video/quicktime",
    webm: "video/webm",
    mkv: "video/x-matroska",
    flv: "video/x-flv",
    avi: "video/x-msvideo",
    ogv: "video/ogg",
    ts: "video/mp2t",
    m2ts: "video/mp2t",
    "3gp": "video/3gpp",
    mpg: "video/mpeg",
    mpeg: "video/mpeg",
    mp3: "audio/mpeg",
    m4a: "audio/mp4",
    aac: "audio/aac",
    ogg: "audio/ogg",
    oga: "audio/ogg",
    opus: "audio/opus",
    wav: "audio/wav",
    flac: "audio/flac",
    wma: "audio/x-ms-wma",
    aiff: "audio/aiff",
    alac: "audio/mp4"
  };

  const language = (() => {
    try {
      return api && api.i18n && api.i18n.getUILanguage
        ? api.i18n.getUILanguage() || "en"
        : "en";
    } catch (error) {
      return "en";
    }
  })();
  const isRtl = language.toLowerCase().startsWith("ar");

  const records = new Map();
  const observedRoots = new Set();
  const pendingNodes = new Set();
  const pendingMedia = new Set();
  let observer = null;
  let scanTimer = 0;
  let refreshTimer = 0;
  let positionFrame = 0;
  let enabled = true;
  let started = false;

  const PANEL_CSS = `
    :host { all: initial; }
    *, *::before, *::after { box-sizing: border-box; }
    [hidden] { display: none !important; }
    .panel {
      position: absolute;
      top: 0;
      left: 0;
      display: flex;
      align-items: center;
      gap: 2px;
      min-height: 25px;
      padding: 2px 4px;
      background: linear-gradient(#fbfbfb, #e4e7e9 55%, #cbd0d4);
      border: 1px solid #7b8287;
      border-radius: 4px;
      box-shadow: 0 2px 7px rgba(0, 0, 0, .35);
      color: #1d2329;
      font: 11.5px/1.1 "Segoe UI", Tahoma, Arial, sans-serif;
      white-space: nowrap;
      pointer-events: auto;
      user-select: none;
      animation: flex-panel-in 120ms ease-out;
    }
    .panel:focus-within { border-color: #5b8fc4; }
    .download,
    .help,
    .close,
    .menu-item {
      margin: 0;
      font: inherit;
      color: inherit;
      cursor: pointer;
      -webkit-appearance: none;
      appearance: none;
    }
    .download {
      display: inline-flex;
      align-items: center;
      gap: 5px;
      min-height: 19px;
      padding: 1px 6px;
      background: transparent;
      border: 0;
      border-radius: 3px;
      font-weight: 600;
    }
    .download:hover { background: rgba(255, 255, 255, .62); }
    .download:active { background: rgba(0, 0, 0, .08); }
    .download[disabled] { opacity: .55; cursor: default; }
    .download[aria-expanded="true"] { background: rgba(255, 255, 255, .78); }
    .play {
      flex: 0 0 auto;
      width: 0;
      height: 0;
      margin-inline-start: 1px;
      border-top: 6px solid transparent;
      border-bottom: 6px solid transparent;
      border-left: 9px solid #2eae42;
      filter: drop-shadow(0 1px 0 rgba(255, 255, 255, .7));
    }
    .chevron {
      flex: 0 0 auto;
      width: 0;
      height: 0;
      margin-inline-start: 2px;
      border-left: 4px solid transparent;
      border-right: 4px solid transparent;
      border-top: 4px solid #245a90;
    }
    .spacer { flex: 1 1 auto; min-width: 2px; }
    .help,
    .close {
      display: inline-flex;
      align-items: center;
      justify-content: center;
      flex: 0 0 auto;
      width: 18px;
      height: 18px;
      padding: 0;
      background: transparent;
      border: 0;
      border-radius: 50%;
      line-height: 1;
    }
    .help {
      color: #245a90;
      font-size: 12px;
      font-weight: 700;
    }
    .close {
      color: #4b5257;
      font-size: 15px;
      font-weight: 400;
    }
    .help:hover,
    .close:hover { background: rgba(255, 255, 255, .72); }
    .help:active,
    .close:active { background: rgba(0, 0, 0, .09); }
    .menu {
      position: absolute;
      top: calc(100% + 2px);
      right: 0;
      left: auto;
      z-index: 2;
      min-width: 268px;
      max-width: min(430px, 86vw);
      max-height: 320px;
      overflow-y: auto;
      padding: 3px;
      background: #f7f8f9;
      border: 1px solid #858c91;
      border-radius: 4px;
      box-shadow: 0 4px 14px rgba(0, 0, 0, .34);
      color: #1d2329;
    }
    .menu.up {
      top: auto;
      bottom: calc(100% + 2px);
    }
    .menu.from-left {
      right: auto;
      left: 0;
    }
    .menu-head {
      padding: 4px 8px;
      margin-bottom: 2px;
      border-bottom: 1px solid #d6dbdf;
      color: #29323a;
      font-size: 11px;
      font-weight: 600;
      line-height: 1.35;
      max-height: 2.7em;
      overflow: hidden;
      white-space: normal;
    }
    .menu-section {
      padding: 5px 8px 2px;
      margin-top: 2px;
      border-top: 1px solid #d6dbdf;
      color: #5a6672;
      font-size: 10.5px;
      font-weight: 700;
      letter-spacing: .02em;
      pointer-events: none;
    }
    .menu-item {
      display: grid;
      grid-template-columns: auto minmax(0, 1fr) auto;
      align-items: center;
      gap: 8px;
      width: 100%;
      padding: 4px 8px;
      background: transparent;
      border: 0;
      border-radius: 3px;
      text-align: start;
      white-space: nowrap;
    }
    .menu-item:hover { background: #e6eef7; }
    .menu-item[aria-selected="true"] { background: #d8e8fa; }
    .menu-info {
      display: block;
      padding: 6px 8px;
      color: #4a5560;
      font-size: 11px;
      font-style: italic;
      cursor: default;
      pointer-events: none;
      opacity: .85;
    }
    .menu-quality {
      color: #1e4d7a;
      font-size: 12px;
      font-weight: 700;
      min-width: 38px;
    }
    .menu-name {
      min-width: 0;
      overflow: hidden;
      text-overflow: ellipsis;
      color: #3d474f;
      font-size: 11px;
      font-weight: 400;
    }
    .menu-size {
      color: #4a5560;
      font-size: 11px;
      font-variant-numeric: tabular-nums;
      white-space: nowrap;
    }
    .menu-size.pending { color: #8a939b; }
    .status {
      position: absolute;
      top: calc(100% + 2px);
      right: 0;
      left: auto;
      z-index: 3;
      max-width: min(340px, 82vw);
      padding: 4px 8px;
      background: #2b3238;
      border: 1px solid #151a1e;
      border-radius: 4px;
      box-shadow: 0 3px 10px rgba(0, 0, 0, .35);
      color: #fff;
      font-size: 11px;
      line-height: 1.4;
      white-space: normal;
    }
    .status.error { background: #7d2c2c; border-color: #4f1717; }
    .status.ok { background: #1f643d; border-color: #123f25; }
    .status.up {
      top: auto;
      bottom: calc(100% + 2px);
    }
    .download:focus-visible,
    .help:focus-visible,
    .close:focus-visible,
    .menu-item:focus-visible {
      outline: 2px solid #4a90d9;
      outline-offset: 1px;
    }
    @keyframes flex-panel-in {
      from { opacity: 0; transform: translateY(-3px); }
      to { opacity: 1; transform: translateY(0); }
    }
  `;

  function t(key, fallback, substitutions) {
    try {
      const message = substitutions
        ? api && api.i18n && api.i18n.getMessage(key, substitutions)
        : api && api.i18n && api.i18n.getMessage(key);
      return message || fallback || key;
    } catch (error) {
      return fallback || key;
    }
  }

  function normalizeMime(value) {
    return String(value || "").split(";")[0].trim().toLowerCase();
  }

  function resolveHttpUrl(value) {
    if (!value) return "";
    const text = String(value).trim();
    if (!text || /^(blob|data|javascript|about):/i.test(text)) return "";
    try {
      const url = new URL(text, document.baseURI);
      return /^https?:$/.test(url.protocol) ? url.href : "";
    } catch (error) {
      return "";
    }
  }

  function extensionFromUrl(url) {
    try {
      const path = new URL(url).pathname;
      const match = path.match(/\.([a-z0-9]{2,5})$/i);
      return match ? match[1].toLowerCase() : "";
    } catch (error) {
      return "";
    }
  }

  function mimeForExtension(extension) {
    return MIME_BY_EXTENSION[String(extension || "").toLowerCase()] || "";
  }

  function isAdaptive(url, mime) {
    const type = normalizeMime(mime);
    if (type.includes("mpegurl") || type.includes("dash+xml")) return true;
    return Boolean(url) && ADAPTIVE_URL.test(String(url));
  }

  function isDirectMedia(url, mime, extension, allowUnknown) {
    if (isAdaptive(url, mime)) return false;
    if (DIRECT_EXTENSIONS.has(extension)) return true;
    const type = normalizeMime(mime);
    if (type.startsWith("video/") || type.startsWith("audio/")) return true;
    return Boolean(allowUnknown);
  }

  function inferKind(url, mime, extension, allowUnknown, media) {
    const type = normalizeMime(mime);
    if (type.startsWith("video/")) return "video";
    if (type.startsWith("audio/")) return "audio";
    if (VIDEO_EXTENSIONS.has(extension)) return "video";
    if (AUDIO_EXTENSIONS.has(extension)) return "audio";
    if (allowUnknown) return media.tagName === "AUDIO" ? "audio" : "video";
    return "";
  }

  function parseResolution(value) {
    const text = String(value || "");
    if (!text) return null;

    let match = text.match(/(\d{3,4})\s*[x×]\s*(\d{3,4})/i);
    if (match) {
      let width = Number(match[1]);
      let height = Number(match[2]);
      if (height > width) [width, height] = [height, width];
      if (height >= 144 && height <= 4320) return { width, height };
    }

    match = text.match(/(?:^|[^0-9])(\d{3,4})p(?:[0-9]{1,3})?(?:[^0-9]|$)/i);
    if (match) {
      const height = Number(match[1]);
      if (height >= 144 && height <= 4320) return { width: 0, height };
    }

    match = text.match(/(?:quality|resolution|res|height)=(\d{2,4})/i);
    if (match) {
      const height = Number(match[1]);
      if (height >= 144 && height <= 4320) return { width: 0, height };
    }

    match = text.match(/(?:[?&/])itag[=/](\d{1,4})(?:[&/]|$)/i);
    if (match) {
      const height = ITAG_HEIGHT[Number(match[1])];
      if (height) return { width: 0, height };
    }

    match = text.match(/(?:^|[^a-z0-9])(8k|4k|2k|uhd|hd|sd)(?:[^a-z0-9]|$)/i);
    if (match) {
      const heights = { "8k": 4320, "4k": 2160, "uhd": 2160, "2k": 1440, "hd": 720, "sd": 480 };
      return { width: 0, height: heights[match[1].toLowerCase()] };
    }

    return null;
  }

  function parseBitrate(value) {
    const text = String(value || "");
    if (!text) return 0;

    let match = text.match(/(\d{3,6})\s*kbps/i);
    if (match) return Number(match[1]);

    match = text.match(/(\d+(?:\.\d+)?)\s*mbps/i);
    if (match) return Math.round(Number(match[1]) * 1000);

    match = text.match(/(?:bitrate|bandwidth|br)=(\d{4,})/i);
    if (match) {
      const bits = Number(match[1]);
      return bits > 10000 ? Math.round(bits / 1000) : bits;
    }

    return 0;
  }

  function formatResolution(height) {
    if (!height) return "";
    if (height >= 4320) return "8K";
    if (height >= 2160) return "4K";
    if (height >= 1440) return "2K";
    return `${height}p`;
  }

  function formatBitrate(kbps) {
    if (!kbps) return "";
    if (kbps >= 1000) {
      const value = kbps / 1000;
      return `${Number.isInteger(value) ? value : value.toFixed(1)} Mbps`;
    }
    return `${Math.round(kbps)} kbps`;
  }

  function formatBytes(bytes) {
    const value = Number(bytes) || 0;
    if (value <= 0) return "";
    const units = ["B", "KB", "MB", "GB", "TB"];
    let index = 0;
    let amount = value;
    while (amount >= 1024 && index < units.length - 1) {
      amount /= 1024;
      index += 1;
    }
    const digits = index === 0 ? 0 : amount >= 100 ? 0 : amount >= 10 ? 1 : 2;
    return `${amount.toFixed(digits)} ${units[index]}`;
  }

  function pageTitle() {
    let title = "";
    try {
      title = String(document.title || "").trim();
    } catch (error) {
      title = "";
    }
    return title.replace(/\s*[|\-–—]\s*(?:YouTube|Vimeo|Dailymotion)\s*$/i, "").trim();
  }

  function labelFor(option) {
    if (option.height) return formatResolution(option.height);

    const match = String(option.hints || "").match(
      /(?:^|[^a-z0-9])(8k|4k|2k|uhd|hd|sd)(?:[^a-z0-9]|$)/i
    );
    if (match) return match[1].toUpperCase();

    if (option.kind === "audio") {
      const rate = option.bitrate ? formatBitrate(option.bitrate) : "";
      const ext = option.ext ? option.ext.toUpperCase() : "";
      if (rate && ext) return `${ext} ${rate}`;
      if (rate) return rate;
      if (ext) return ext;
      return t("panelAudioQuality", "Audio");
    }
    return t("panelOriginal", "Original");
  }

  function metaFor(option) {
    const bits = [];
    const bitrate = option.bitrate ? formatBitrate(option.bitrate) : "";

    if (option.kind === "audio") {
      if (bitrate && bitrate !== option.label) bits.push(bitrate);
    } else if (bitrate) {
      bits.push(bitrate);
    }

    if (option.ext) bits.push(option.ext.toUpperCase());
    if (!bits.length && option.kind === "audio") {
      bits.push(t("panelAudioQuality", "Audio"));
    }
    return bits.join(" · ");
  }

  function mergeOption(existing, next) {
    if (!existing.height && next.height) existing.height = next.height;
    if (!existing.width && next.width) existing.width = next.width;
    if (!existing.bitrate && next.bitrate) existing.bitrate = next.bitrate;
    if (!existing.mime && next.mime) existing.mime = next.mime;
    if (!existing.ext && next.ext) existing.ext = next.ext;
    if (!existing.hints && next.hints) existing.hints = next.hints;
    if (!existing.size && next.size) existing.size = next.size;
    if (existing.sourceKind === "network" && next.sourceKind !== "network") {
      existing.sourceKind = next.sourceKind;
    }
  }

  function collectMediaOptions(media) {
    const options = new Map();
    let adaptive = false;

    function add(raw) {
      const url = resolveHttpUrl(raw.url);
      if (!url) return;

      const mime = normalizeMime(raw.mime);
      if (isAdaptive(url, mime)) {
        adaptive = true;
        return;
      }

      const extension = extensionFromUrl(url);
      if (!isDirectMedia(url, mime, extension, raw.allowUnknown === true)) return;

      const kind = inferKind(url, mime, extension, raw.allowUnknown === true, media);
      if (!kind) return;

      const hints = String(raw.hints || "");
      const resolution = parseResolution(hints) || parseResolution(url);
      const option = {
        url,
        mime: mime || mimeForExtension(extension),
        ext: extension,
        kind,
        width: Number(raw.width) || (resolution ? resolution.width : 0),
        height: Number(raw.height) || (resolution ? resolution.height : 0),
        bitrate: Number(raw.bitrate) || parseBitrate(`${hints} ${url}`),
        size: Number(raw.size) || 0,
        hints,
        sourceKind: raw.sourceKind || "dom"
      };

      const existing = options.get(url);
      if (existing) {
        mergeOption(existing, option);
        return;
      }
      options.set(url, option);
    }

    const currentUrl = resolveHttpUrl(media.currentSrc);
    if (currentUrl) {
      add({ url: currentUrl, allowUnknown: true, sourceKind: "current" });
    }

    const sourceAttribute = resolveHttpUrl(media.getAttribute("src"));
    if (sourceAttribute && sourceAttribute !== currentUrl) {
      add({
        url: sourceAttribute,
        mime: media.getAttribute("type"),
        allowUnknown: true,
        sourceKind: "current"
      });
    }

    for (const attribute of ["data-src", "data-video-src", "data-video-url", "data-mp4", "data-file"]) {
      const value = media.getAttribute(attribute);
      if (value) add({ url: value, allowUnknown: false, sourceKind: "attribute" });
    }

    for (const attribute of ["data-hls", "data-dash", "data-manifest"]) {
      if (media.getAttribute(attribute)) adaptive = true;
    }

    for (const source of media.querySelectorAll("source")) {
      const sourceMime = normalizeMime(source.getAttribute("type"));
      const value = source.getAttribute("src") ||
        source.getAttribute("data-src") ||
        source.src || "";
      const hints = [
        source.getAttribute("label"),
        source.getAttribute("data-label"),
        source.getAttribute("data-quality"),
        source.getAttribute("data-res"),
        source.getAttribute("data-resolution"),
        source.getAttribute("res"),
        source.getAttribute("height"),
        source.getAttribute("data-height"),
        source.getAttribute("title")
      ].filter(Boolean).join(" ");

      if (!value) {
        if (isAdaptive("", sourceMime)) adaptive = true;
        continue;
      }

      add({
        url: value,
        mime: sourceMime,
        hints,
        height: Number(source.getAttribute("height")) || 0,
        width: Number(source.getAttribute("width")) || 0,
        allowUnknown: true,
        sourceKind: "source"
      });
    }

    const list = Array.from(options.values());
    for (const option of list) {
      if (option.kind === "video" && option.sourceKind === "current") {
        if (!option.width && media.videoWidth > 0) option.width = media.videoWidth;
        if (!option.height && media.videoHeight > 0) option.height = media.videoHeight;
      }
      option.label = labelFor(option);
      option.meta = metaFor(option);
    }

    list.sort((a, b) => {
      if (a.kind !== b.kind) return a.kind === "video" ? -1 : 1;
      if (a.height !== b.height) return b.height - a.height;
      if (a.bitrate !== b.bitrate) return b.bitrate - a.bitrate;
      return a.label.localeCompare(b.label, undefined, { numeric: true, sensitivity: "base" });
    });

    return { options: list, adaptive };
  }

  function sortOptions(list) {
    list.sort((a, b) => {
      if (a.kind !== b.kind) return a.kind === "video" ? -1 : 1;
      if (a.height !== b.height) return b.height - a.height;
      if (a.bitrate !== b.bitrate) return b.bitrate - a.bitrate;
      if (a.ext !== b.ext && (a.ext === "m4a" || b.ext === "m4a")) {
        return a.ext === "m4a" ? -1 : 1;
      }
      if (a.size !== b.size) return b.size - a.size;
      return a.label.localeCompare(b.label, undefined, { numeric: true, sensitivity: "base" });
    });
    return list;
  }

  // Direct media requests captured by the service worker. They make the panel
  // useful on sites that play through MSE (`blob:`) where the <video> element
  // never exposes a downloadable URL.
  function buildNetworkOptions(media, candidates) {
    const options = [];
    if (!Array.isArray(candidates) || !candidates.length) return options;

    for (const raw of candidates) {
      const url = resolveHttpUrl(raw && raw.url);
      if (!url) continue;

      const mime = normalizeMime(raw.mime);
      if (isAdaptive(url, mime)) continue;

      const extension = extensionFromUrl(url);
      if (extension === "ts" || extension === "m2ts") continue;
      if (!isDirectMedia(url, mime, extension, raw.media === true)) continue;

      const kind = inferKind(url, mime, extension, raw.media === true, media);
      if (!kind) continue;
      if (media.tagName === "VIDEO" && kind !== "video") continue;
      if (media.tagName === "AUDIO" && kind !== "audio") continue;

      const hints = String(raw.hints || "");
      const resolution = parseResolution(hints) || parseResolution(url);
      const option = {
        url,
        mime: mime || mimeForExtension(extension),
        ext: extension,
        kind,
        width: resolution ? resolution.width : 0,
        height: resolution ? resolution.height : 0,
        bitrate: parseBitrate(`${hints} ${url}`),
        size: Number(raw.size) || 0,
        hints,
        sourceKind: "network"
      };
      option.label = labelFor(option);
      option.meta = metaFor(option);
      options.push(option);
    }

    return options;
  }

  function combineOptions(domOptions, networkOptions) {
    const map = new Map();
    for (const option of domOptions) map.set(option.url, option);

    for (const option of networkOptions) {
      const existing = map.get(option.url);
      if (existing) {
        mergeOption(existing, option);
      } else {
        map.set(option.url, option);
      }
    }

    const list = Array.from(map.values());
    for (const option of list) {
      option.label = labelFor(option);
      option.meta = metaFor(option);
    }
    return sortOptions(list);
  }

  function selectedOption(record) {
    if (!record.options.length) return null;
    return record.options.find((option) => option.url === record.selectedUrl) ||
      record.options[0];
  }

  function currentMediaUrl(media) {
    return resolveHttpUrl(media.currentSrc) ||
      resolveHttpUrl(media.getAttribute("src")) || "";
  }

  function optionSignature(record) {
    return record.options
      .map((option) => `${option.url}\u0000${option.label}\u0000${option.meta}\u0000${option.size}`)
      .join("\u0001") + `\u0002${record.selectedUrl}`;
  }

  function resetOptionHistory(record) {
    record.knownOptions.clear();
    record.sourceUrl = "";
    record.selectedUrl = "";
    record.optionSignature = "";
    record.ytState = "idle";
    record.ytKey = "";
    record.ytFormats = [];
    record.ytAt = 0;
  }

  function pageKey() {
    return `${location.origin}${location.pathname}${location.search}`;
  }

  function isYtDlpSite() {
    const host = String(location.hostname || "").toLowerCase();
    if (!host) return false;
    return YTDLP_SITES.some(
      (site) => host === site || host.endsWith(`.${site}`)
    );
  }

  // Cache key for the yt-dlp lookup. YouTube serves several clips from one
  // page (home previews, playlists), so only watch/live/shorts URLs qualify.
  function ytPageKey() {
    if (!isYtDlpSite()) return "";

    let url;
    try {
      url = new URL(location.href);
    } catch (error) {
      return "";
    }

    const host = url.hostname.toLowerCase();
    const isYouTube =
      host === "youtube.com" || host.endsWith(".youtube.com") ||
      host === "youtube-nocookie.com" ||
      host.endsWith(".youtube-nocookie.com") ||
      host === "youtu.be";

    if (!isYouTube) return `page:${url.origin}${url.pathname}`;

    let id = url.searchParams.get("v") || "";
    if (!id) {
      const match = url.pathname.match(
        /^\/(?:shorts|live|embed|v)\/([\w-]{6,})/
      );
      if (match) id = match[1];
    }
    if (!id && host === "youtu.be") id = url.pathname.replace(/^\//, "");
    return id ? `yt:${id}` : "";
  }

  // A media element is often reused for a different clip (SPA players swap the
  // source in place). Qualities belong to one clip only, so start over when the
  // page or the clip length changes. Quality switches keep the same duration.
  function maybeResetOptionHistory(record) {
    const media = record.media;
    const page = pageKey();

    if (record.historyPage && page !== record.historyPage) {
      record.historyPage = page;
      record.historyDuration = Number(media.duration) || 0;
      resetOptionHistory(record);
      return;
    }

    const duration = Number(media.duration) || 0;
    if (duration <= 0) return;
    if (record.historyDuration > 0 &&
        Math.abs(duration - record.historyDuration) > 2) {
      resetOptionHistory(record);
    }
    record.historyDuration = duration;
  }

  // Keep every quality discovered for this media element, exactly like IDM
  // keeps the qualities it has sniffed. Players usually expose only the
  // currently playing source, so without this history switching to 720p would
  // replace the 360p entry instead of adding to it.
  function rememberOptions(record, fresh, currentUrl, media) {
    const now = Date.now();
    const freshUrls = new Set();
    const added = [];

    for (const option of fresh) {
      freshUrls.add(option.url);
      const known = record.knownOptions.get(option.url);
      if (known) {
        const height = known.height;
        const width = known.width;
        mergeOption(known, option);
        // A later pass must never erase a resolution we already learned.
        if (height && !known.height) known.height = height;
        if (width && !known.width) known.width = width;
        known.lastSeen = now;
        known.stale = false;
      } else {
        option.lastSeen = now;
        option.stale = false;
        record.knownOptions.set(option.url, option);
        added.push(option);
      }
    }

    if (currentUrl && record.knownOptions.has(currentUrl)) {
      const current = record.knownOptions.get(currentUrl);
      if (media.videoHeight > 0) {
        current.height = media.videoHeight;
        if (media.videoWidth > 0) current.width = media.videoWidth;
      }
      current.lastSeen = now;
      current.stale = false;
    } else if (media.videoHeight > 0 && added.length === 1) {
      // MSE playback hides the real URL behind blob:. Attribute the size on
      // screen to the single quality that was just requested.
      const option = added[0];
      if (option.kind === "video" && !option.height) {
        option.height = media.videoHeight;
        if (media.videoWidth > 0) option.width = media.videoWidth;
      }
    }

    for (const [url, option] of record.knownOptions) {
      if (!freshUrls.has(url) && url !== currentUrl) option.stale = true;
      option.label = labelFor(option);
      option.meta = metaFor(option);
      if (!option.size && record.sizes[url]) option.size = record.sizes[url];
    }

    // Forget leftovers that carry no quality information; they would only add
    // identical "Original" rows to the menu.
    if (record.knownOptions.size > 1) {
      for (const [url, option] of record.knownOptions) {
        if (url === currentUrl) continue;
        if (option.stale && !option.height && !option.bitrate && !option.size) {
          record.knownOptions.delete(url);
        }
      }
    }

    if (record.knownOptions.size > MAX_REMEMBERED_OPTIONS) {
      const extras = Array.from(record.knownOptions.entries())
        .filter(([url]) => url !== currentUrl)
        .sort((a, b) => (a[1].lastSeen || 0) - (b[1].lastSeen || 0));
      for (const [url] of extras) {
        if (record.knownOptions.size <= MAX_REMEMBERED_OPTIONS) break;
        record.knownOptions.delete(url);
      }
    }
  }

  function createRecord(media) {
    const host = document.createElement("div");
    host.setAttribute(HOST_ATTR, "");
    host.style.setProperty("display", "none", "important");
    host.style.setProperty("position", "fixed", "important");
    host.style.setProperty("z-index", "2147483647", "important");
    host.style.setProperty("width", "0", "important");
    host.style.setProperty("height", "0", "important");
    host.style.setProperty("pointer-events", "none", "important");

    const shadow = host.attachShadow({ mode: "open" });
    const style = document.createElement("style");
    style.textContent = PANEL_CSS;
    shadow.appendChild(style);

    const panel = document.createElement("div");
    panel.className = "panel";
    panel.setAttribute("role", "toolbar");
    panel.setAttribute("aria-label", t("panelToolbar", "Media download panel"));
    panel.dir = isRtl ? "rtl" : "ltr";

    const main = document.createElement("button");
    main.className = "download";
    main.type = "button";
    const play = document.createElement("span");
    play.className = "play";
    play.setAttribute("aria-hidden", "true");
    const mainLabel = document.createElement("span");
    mainLabel.className = "label";
    const chevron = document.createElement("span");
    chevron.className = "chevron";
    chevron.setAttribute("aria-hidden", "true");
    chevron.hidden = true;
    main.append(play, mainLabel, chevron);

    const spacer = document.createElement("span");
    spacer.className = "spacer";

    const help = document.createElement("button");
    help.className = "help";
    help.type = "button";
    help.textContent = "?";
    help.title = t("panelHelp", "Help");
    help.setAttribute("aria-label", t("panelHelp", "Help"));

    const close = document.createElement("button");
    close.className = "close";
    close.type = "button";
    close.textContent = "×";
    close.title = t("panelClose", "Close");
    close.setAttribute("aria-label", t("panelClose", "Close"));

    const menu = document.createElement("div");
    menu.className = "menu";
    menu.setAttribute("role", "menu");
    menu.hidden = true;

    const status = document.createElement("div");
    status.className = "status";
    status.setAttribute("role", "status");
    status.setAttribute("aria-live", "polite");
    status.hidden = true;

    panel.append(main, spacer, help, close, menu, status);
    shadow.appendChild(panel);

    const record = {
      media,
      host,
      shadow,
      panel,
      main,
      mainLabel,
      chevron,
      help,
      close,
      menu,
      status,
      options: [],
      knownOptions: new Map(),
      sizes: Object.create(null),
      networkCandidates: [],
      networkSignature: "",
      networkPending: false,
      networkAt: 0,
      // yt-dlp quality cache for the current page/video.
      ytState: "idle",
      ytKey: "",
      ytFormats: [],
      ytTitle: "",
      ytPageUrl: "",
      ytAt: 0,
      ytPending: false,
      sizeSignature: "",
      selectedUrl: "",
      sourceUrl: "",
      historyPage: pageKey(),
      historyDuration: 0,
      optionSignature: "",
      adaptive: false,
      announced: false,
      dismissed: false,
      visible: false,
      hovered: false,
      panelHovered: false,
      mediaFocused: false,
      panelFocused: false,
      menuOpen: false,
      statusVisible: false,
      sending: false,
      hideTimer: 0,
      statusTimer: 0,
      outsideHandler: null,
      escapeHandler: null
    };

    record.mainLabel.textContent = t("panelVideo", "Download this video");

    record.outsideHandler = (event) => {
      if (!record.menuOpen) return;
      const path = typeof event.composedPath === "function" ? event.composedPath() : [];
      if (path.includes(record.host) || path.includes(record.panel) || path.includes(record.menu)) {
        return;
      }
      closeMenu(record);
    };
    record.escapeHandler = (event) => {
      if (event.key !== "Escape" || !record.menuOpen) return;
      event.preventDefault();
      closeMenu(record);
      try { record.main.focus({ preventScroll: true }); } catch (error) { /* ignore */ }
    };

    bindRecord(record);
    records.set(media, record);
    mountHost(record);
    return record;
  }

  function bindRecord(record) {
    const media = record.media;

    media.addEventListener("pointerenter", () => {
      record.hovered = true;
      cancelHide(record);
      showRecord(record);
    }, { passive: true });

    media.addEventListener("pointerleave", () => {
      record.hovered = false;
      scheduleHide(record, 280);
    }, { passive: true });

    media.addEventListener("focusin", () => {
      record.mediaFocused = true;
      cancelHide(record);
      showRecord(record);
    });

    media.addEventListener("focusout", () => {
      record.mediaFocused = false;
      scheduleHide(record, 280);
    });

    media.addEventListener("play", () => {
      showRecord(record);
      requestQualities(record);
      scheduleHide(record, 3600);
    });

    media.addEventListener("pause", () => scheduleHide(record, 500));
    media.addEventListener("click", () => {
      showRecord(record);
      scheduleHide(record, 1800);
    }, { passive: true });

    for (const eventName of [
      "loadedmetadata", "loadeddata", "canplay", "durationchange",
      "resize", "loadstart", "playing", "emptied"
    ]) {
      media.addEventListener(eventName, () => {
        updateRecord(record);
        if (eventName !== "emptied" &&
            (media.matches(":hover") || media.matches(":focus") || !media.paused)) {
          showRecord(record);
        }
      }, { passive: true });
    }

    record.panel.addEventListener("pointerenter", () => {
      record.panelHovered = true;
      cancelHide(record);
    });

    record.panel.addEventListener("pointerleave", () => {
      record.panelHovered = false;
      scheduleHide(record, 280);
    });

    record.panel.addEventListener("focusin", () => {
      record.panelFocused = true;
      cancelHide(record);
    });

    record.panel.addEventListener("focusout", (event) => {
      if (!event.relatedTarget || !record.panel.contains(event.relatedTarget)) {
        record.panelFocused = false;
        scheduleHide(record, 280);
      }
    });

    record.panel.addEventListener("pointerdown", (event) => event.stopPropagation());
    record.panel.addEventListener("click", (event) => event.stopPropagation());
    record.panel.addEventListener("keydown", (event) => {
      if (event.key !== "Escape") return;
      if (record.menuOpen) {
        event.preventDefault();
        closeMenu(record);
      } else {
        hideRecord(record, true);
      }
    });

    record.main.addEventListener("click", (event) => {
      event.preventDefault();
      event.stopPropagation();
      if (record.sending) return;
      if (record.options.length > 1) {
        toggleMenu(record);
        return;
      }
      if (!record.options.length) refreshNetworkCandidates(record, true);
      sendSelected(record);
    });

    record.help.addEventListener("click", (event) => {
      event.preventDefault();
      event.stopPropagation();
      showStatus(
        record,
        t(
          "panelHelpText",
          "Click the panel to choose a quality. Adaptive HLS/DASH streams are not supported yet."
        ),
        "info",
        7000
      );
    });

    record.close.addEventListener("click", (event) => {
      event.preventDefault();
      event.stopPropagation();
      dismissRecord(record);
    });
  }

  function mountHost(record) {
    const fullscreen = document.fullscreenElement;
    const root = fullscreen &&
      fullscreen !== document.documentElement &&
      fullscreen !== document.body &&
      fullscreen.tagName !== "VIDEO" &&
      fullscreen.tagName !== "AUDIO"
      ? fullscreen
      : document.documentElement;
    if (record.host.parentNode !== root) root.appendChild(record.host);
  }

  function isMediaVisible(media) {
    if (!media.isConnected) return false;
    const rect = media.getBoundingClientRect();
    const minWidth = media.tagName === "AUDIO" ? MIN_AUDIO_WIDTH : MIN_VIDEO_WIDTH;
    const minHeight = media.tagName === "AUDIO" ? MIN_AUDIO_HEIGHT : MIN_VIDEO_HEIGHT;
    if (rect.width < minWidth || rect.height < minHeight) return false;
    if (rect.right <= 0 || rect.bottom <= 0 ||
        rect.left >= window.innerWidth || rect.top >= window.innerHeight) {
      return false;
    }

    let style;
    try {
      style = getComputedStyle(media);
    } catch (error) {
      return false;
    }

    const opacity = parseFloat(style.opacity || "1");
    const hiddenStyles = style.display === "none" ||
      style.visibility === "hidden" || opacity === 0;
    const playing = media.tagName === "VIDEO" && !media.paused &&
      media.videoWidth > 0 && media.videoHeight > 0;

    // Some players keep the native <video> transparent and paint the frames
    // themselves. Still offer the panel while such a video is playing.
    if (hiddenStyles) {
      if (!playing) return false;
    } else if (typeof media.checkVisibility === "function") {
      try {
        if (!media.checkVisibility({ checkOpacity: true, checkVisibilityCSS: true })) {
          return false;
        }
      } catch (error) {
        // Older browsers may not understand the options object.
      }
    }
    return true;
  }

  function positionRecord(record) {
    const rect = record.media.getBoundingClientRect();
    const panelRect = record.panel.getBoundingClientRect();
    const menuRect = record.menuOpen ? record.menu.getBoundingClientRect() : null;
    const panelWidth = panelRect.width || 240;
    const panelHeight = panelRect.height || 25;

    // The strip is glued to the frame: flush against the video's top edge and
    // aligned with its right edge, the way IDM hangs its toolbar. There is no
    // gap, so it reads as part of the player chrome. It only drops below the
    // frame when the page top leaves no room above it.
    let top = rect.top - panelHeight;
    if (top < EDGE_KEEP) {
      const below = rect.bottom;
      top = below + panelHeight <= window.innerHeight - EDGE_KEEP
        ? below
        : EDGE_KEEP;
    }

    // The dropdown and the status bubble open from the strip's right edge and
    // grow leftwards over the picture. If that would leave the viewport they
    // re-anchor to the strip's left edge instead.
    const menuWidth = menuRect ? menuRect.width : 0;
    let left = rect.right - panelWidth;
    const anchorLeft = menuWidth > 0 &&
      left + panelWidth - menuWidth < EDGE_KEEP;
    record.menu.classList.toggle("from-left", anchorLeft);

    const menuHeight = menuRect ? menuRect.height + 2 : 0;
    const menuFlip = record.menuOpen &&
      top + panelHeight + menuHeight > window.innerHeight - EDGE_KEEP &&
      top - menuHeight >= EDGE_KEEP;
    const statusHeight = record.statusVisible
      ? record.status.getBoundingClientRect().height + 2
      : 0;
    const statusFlip = !record.menuOpen && record.statusVisible &&
      top + panelHeight + statusHeight > window.innerHeight - EDGE_KEEP &&
      top - statusHeight >= EDGE_KEEP;
    record.menu.classList.toggle("up", menuFlip);
    record.status.classList.toggle("up", menuFlip || statusFlip);

    const width = anchorLeft && menuWidth > panelWidth ? menuWidth : panelWidth;
    left = Math.max(EDGE_KEEP, Math.min(left,
      window.innerWidth - width - EDGE_KEEP));
    top = Math.max(EDGE_KEEP, Math.min(top,
      window.innerHeight - panelHeight - EDGE_KEEP));

    record.host.style.setProperty("left", `${Math.round(left)}px`, "important");
    record.host.style.setProperty("top", `${Math.round(top)}px`, "important");
  }

  function requestPosition() {
    if (positionFrame) return;
    positionFrame = requestAnimationFrame(positionTick);
  }

  function positionTick() {
    positionFrame = 0;
    if (document.fullscreenElement) {
      for (const record of records.values()) {
        if (record.visible) hideRecord(record, true);
      }
      return;
    }
    let active = false;

    for (const record of records.values()) {
      if (!record.visible) continue;
      if (!record.media.isConnected) {
        destroyRecord(record);
        continue;
      }
      if (!isMediaVisible(record.media)) {
        hideRecord(record, true);
        continue;
      }
      positionRecord(record);
      active = true;
    }

    if (active) requestPosition();
  }

  function showRecord(record) {
    if (!enabled || record.dismissed || !record.media.isConnected) return;
    // Fullscreen belongs to the player: never lay the strip over it.
    if (document.fullscreenElement) return;

    updateRecord(record);
    if (!isMediaVisible(record.media)) return;

    // The page may already be playing (autoplay, history restore, SPA switch),
    // in which case no fresh "play" event will fire for us.
    if (!record.media.paused || record.media.currentTime > 0) {
      requestQualities(record);
    }

    mountHost(record);
    record.host.style.setProperty("display", "block", "important");
    record.visible = true;
    positionRecord(record);
    requestPosition();
    cancelHide(record);
    refreshNetworkCandidates(record);
    probeOptionSizes(record);
  }

  function hideRecord(record, force = false) {
    if (!force && (record.hovered || record.panelHovered || record.mediaFocused ||
                   record.panelFocused || record.menuOpen || record.statusVisible)) {
      return;
    }
    record.visible = false;
    record.host.style.setProperty("display", "none", "important");
    if (force) {
      closeMenu(record, true);
      clearTimeout(record.statusTimer);
      record.statusTimer = 0;
      record.statusVisible = false;
      record.status.hidden = true;
    }
  }

  function cancelHide(record) {
    if (!record.hideTimer) return;
    clearTimeout(record.hideTimer);
    record.hideTimer = 0;
  }

  function scheduleHide(record, delay) {
    cancelHide(record);
    record.hideTimer = window.setTimeout(() => {
      record.hideTimer = 0;
      hideRecord(record);
    }, delay);
  }

  function dismissRecord(record) {
    record.dismissed = true;
    cancelHide(record);
    clearTimeout(record.statusTimer);
    record.statusTimer = 0;
    record.statusVisible = false;
    record.status.hidden = true;
    closeMenu(record, true);
    hideRecord(record, true);
  }

  // Asks the desktop app (yt-dlp) for every quality of the current page. The
  // result is cached per video so hovering and playback never re-run it.
  function requestQualities(record) {
    if (!isYtDlpSite() || record.dismissed || record.ytPending) return;

    const key = ytPageKey();
    if (!key) return;

    if (record.ytKey !== key) {
      record.ytKey = key;
      record.ytFormats = [];
      record.ytState = "idle";
      record.ytAt = 0;
    }

    if (record.ytState === "loading" || record.ytState === "ready") return;
    if ((record.ytState === "error" || record.ytState === "empty") &&
        Date.now() - record.ytAt < 60000) {
      return;
    }

    if (!api || !api.runtime || !api.runtime.sendMessage) return;

    record.ytState = "loading";
    record.ytPending = true;
    record.ytAt = Date.now();
    if (record.menuOpen) rebuildMenu(record);

    const pageUrl = location.href;
    try {
      api.runtime.sendMessage({ type: "flexMediaExtract", url: pageUrl }, (response) => {
        record.ytPending = false;

        let failure = null;
        try {
          failure = api.runtime.lastError || null;
        } catch (error) {
          failure = error;
        }

        if (failure || !response || response.ok !== true) {
          record.ytState = "error";
          if (record.visible) {
            const offline = Boolean(response && response.reason === "offline");
            showStatus(
              record,
              offline
                ? t("panelOffline", "FLEX Download Manager is not running.")
                : t(
                    "panelExtractFailed",
                    "Could not read this video's qualities. Make sure yt-dlp.exe is next to the desktop app."
                  ),
              "error",
              6000
            );
          }
          if (record.menuOpen) rebuildMenu(record);
          return;
        }

        applyQualities(record, response);
      });
    } catch (error) {
      record.ytPending = false;
      record.ytState = "error";
    }
  }

  function applyQualities(record, response) {
    const formats = Array.isArray(response.formats) ? response.formats : [];

    record.ytFormats = formats
      .map((format) => {
        const height = Number(format.height) || 0;
        const bitrate = Number(format.bitrate) || 0;
        const audio = format.kind === "audio" || !height;
        const extension = String(
          format.ext || (audio ? "m4a" : "mp4")
        ).toLowerCase();

        const option = {
          url: `yt:${String(format.id || "")}`,
          mime: audio
            ? (extension === "webm" ? "audio/webm" : (mimeForExtension(extension) || "audio/mp4"))
            : (mimeForExtension(extension) || "video/mp4"),
          ext: extension,
          kind: audio ? "audio" : "video",
          width: 0,
          height,
          bitrate,
          size: Number(format.size) || 0,
          hints: "",
          sourceKind: "ytdlp",
          ytdlp: true,
          formatId: String(format.id || ""),
          merge: format.merge === true
        };
        option.label = labelFor(option);
        option.meta = metaFor(option);
        return option;
      })
      .filter((option) => option.formatId);

    record.ytTitle = String(response.title || "");
    record.ytPageUrl = String(response.pageUrl || location.href);
    record.ytState = record.ytFormats.length ? "ready" : "empty";
    record.ytAt = Date.now();

    // The extracted rows replace the sniffed video streams of the same height.
    const heights = new Set(
      record.ytFormats
        .filter((option) => option.kind === "video" && option.height)
        .map((option) => option.height)
    );
    const hasAudio = record.ytFormats.some((option) => option.kind === "audio");
    for (const [url, option] of record.knownOptions) {
      if (option.sourceKind === "ytdlp") {
        record.knownOptions.delete(url);
        continue;
      }
      if (option.sourceKind !== "network") continue;
      if (option.kind === "video" && heights.has(option.height)) {
        record.knownOptions.delete(url);
      } else if (option.kind === "audio" && hasAudio) {
        record.knownOptions.delete(url);
      }
    }

    updateRecord(record);
    if (record.menuOpen) rebuildMenu(record);
  }

  function updateRecord(record) {
    if (!enabled || record.dismissed || !record.media.isConnected) return;

    const media = record.media;
    maybeResetOptionHistory(record);

    const result = collectMediaOptions(media);
    record.adaptive = result.adaptive;

    const networkOptions = buildNetworkOptions(media, record.networkCandidates);

    // On the yt-dlp sites the sniffed network streams are video-only DASH
    // fragments; the extracted list already carries the audio track. Direct
    // files exposed by the page (<source>, data attributes) are kept as-is.
    const ytSite = isYtDlpSite();
    const usable = (option) =>
      !ytSite || option.kind !== "video" || option.sourceKind !== "network";
    const fresh = combineOptions(
      result.options.filter(usable),
      networkOptions.filter(usable)
    );
    for (const option of record.ytFormats) {
      if (option.kind === "video" && !option.height) continue;
      fresh.push(option);
    }

    const currentUrl = currentMediaUrl(media);

    rememberOptions(record, fresh, currentUrl, media);

    record.options = sortOptions(Array.from(record.knownOptions.values()));

    if (!record.options.length) {
      record.selectedUrl = "";
      record.optionSignature = "";
      renderPanel(record);
      if (record.visible) probeOptionSizes(record);
      return;
    }

    // Follow the quality the player switched to, like IDM does.
    if (currentUrl && currentUrl !== record.sourceUrl) {
      record.sourceUrl = currentUrl;
      if (record.knownOptions.has(currentUrl)) {
        record.selectedUrl = currentUrl;
      }
    }

    const selectionValid = record.options.some(
      (option) => option.url === record.selectedUrl
    );
    if (!selectionValid) {
      record.selectedUrl = currentUrl && record.knownOptions.has(currentUrl)
        ? currentUrl
        : record.options[0].url;
    }

    const signature = optionSignature(record);
    if (signature === record.optionSignature) {
      if (record.visible) probeOptionSizes(record);
      return;
    }

    record.optionSignature = signature;
    renderPanel(record);
    if (record.visible) probeOptionSizes(record);
  }

  function renderPanel(record) {
    const option = selectedOption(record);
    const hasVideo = record.options.some((item) => item.kind === "video");
    const audio = record.media.tagName === "AUDIO" ||
      (!hasVideo && Boolean(option && option.kind === "audio"));
    const label = audio
      ? t("panelAudio", "Download this audio")
      : t("panelVideo", "Download this video");
    const multiple = record.options.length > 1;

    record.mainLabel.textContent = label;
    record.main.setAttribute("aria-label", label);
    record.main.title = option
      ? (option.meta ? `${option.label} · ${option.meta}` : option.label)
      : t("panelNoSource", "No direct media URL is available for this item.");
    record.main.disabled = record.sending;
    record.chevron.hidden = !multiple;
    record.main.setAttribute("aria-haspopup", multiple ? "menu" : "false");
    record.main.setAttribute("aria-expanded", record.menuOpen ? "true" : "false");

    if (!multiple && record.menuOpen) closeMenu(record);
    if (record.menuOpen) rebuildMenu(record);
  }

  function chosenName(record, option) {
    return suggestedFileName(record, option);
  }

  function rebuildMenu(record) {
    record.menu.textContent = "";

    const head = document.createElement("div");
    head.className = "menu-head";
    head.dir = "auto";
    const title = pageTitle();
    head.textContent = title ||
      (record.options.length ? chosenName(record, record.options[0]).replace(/\.[a-z0-9]{1,5}$/i, "") : "");
    if (head.textContent) record.menu.appendChild(head);

    if (record.ytState === "loading") {
      const row = document.createElement("div");
      row.className = "menu-item menu-info";
      row.setAttribute("role", "menuitem");
      row.setAttribute("aria-disabled", "true");
      row.textContent = t("panelExtracting", "Extracting video qualities…");
      record.menu.appendChild(row);
    } else if (record.ytState === "error" && !record.options.length) {
      const row = document.createElement("div");
      row.className = "menu-item menu-info";
      row.textContent = t(
        "panelExtractFailed",
        "Could not read this video's qualities."
      );
      record.menu.appendChild(row);
    }

    const hasVideo = record.options.some((option) => option.kind === "video");
    const hasAudio = record.options.some((option) => option.kind === "audio");
    let audioSectionAdded = false;

    for (const option of record.options) {
      if (hasVideo && hasAudio && option.kind === "audio" && !audioSectionAdded) {
        audioSectionAdded = true;
        const section = document.createElement("div");
        section.className = "menu-section";
        section.setAttribute("role", "separator");
        section.textContent = t("panelAudioQuality", "Audio");
        record.menu.appendChild(section);
      }

      const item = document.createElement("button");
      item.type = "button";
      item.className = "menu-item";
      item.setAttribute("role", "menuitem");
      item.setAttribute("aria-selected", option.url === record.selectedUrl ? "true" : "false");

      const name = chosenName(record, option);
      item.title = `${option.label} · ${name}${option.size ? ` · ${formatBytes(option.size)}` : ""}`;

      const quality = document.createElement("span");
      quality.className = "menu-quality";
      quality.dir = "ltr";
      quality.textContent = option.label || t("panelOriginal", "Original");

      const nameLabel = document.createElement("span");
      nameLabel.className = "menu-name";
      nameLabel.dir = "auto";
      nameLabel.textContent = name;

      const size = document.createElement("span");
      size.className = "menu-size";
      size.dir = "ltr";
      size.textContent = option.size ? formatBytes(option.size) : "—";
      if (!option.size) size.classList.add("pending");

      item.append(quality, nameLabel, size);

      item.addEventListener("click", (event) => {
        event.preventDefault();
        event.stopPropagation();
        record.selectedUrl = option.url;
        closeMenu(record);
        renderPanel(record);
        sendOption(record, option);
      });

      record.menu.appendChild(item);
    }
  }

  function openMenu(record) {
    if (record.options.length < 2 || record.sending || record.menuOpen) return;

    clearTimeout(record.statusTimer);
    record.statusTimer = 0;
    record.statusVisible = false;
    record.status.hidden = true;

    record.menuOpen = true;
    rebuildMenu(record);
    record.menu.hidden = false;
    record.main.setAttribute("aria-expanded", "true");
    record.panelHovered = true;
    cancelHide(record);
    probeOptionSizes(record);

    document.addEventListener("pointerdown", record.outsideHandler, true);
    document.addEventListener("keydown", record.escapeHandler, true);

    positionRecord(record);
    requestPosition();

    requestAnimationFrame(() => {
      const selected = record.menu.querySelector('[aria-selected="true"]') ||
        record.menu.querySelector(".menu-item");
      if (selected) {
        try { selected.focus({ preventScroll: true }); } catch (error) { /* ignore */ }
      }
    });
  }

  function closeMenu(record, immediate = false) {
    if (!record.menuOpen) return;

    record.menuOpen = false;
    record.menu.hidden = true;
    record.main.setAttribute("aria-expanded", "false");
    document.removeEventListener("pointerdown", record.outsideHandler, true);
    document.removeEventListener("keydown", record.escapeHandler, true);

    try {
      record.panelHovered = record.panel.matches(":hover");
    } catch (error) {
      record.panelHovered = false;
    }

    if (immediate) {
      hideRecord(record, true);
    } else {
      scheduleHide(record, 180);
    }
    requestPosition();
  }

  function toggleMenu(record) {
    if (record.menuOpen) closeMenu(record);
    else openMenu(record);
  }

  function refreshNetworkCandidates(record, force = false) {
    if (!api || !api.runtime || !api.runtime.sendMessage) return;

    const now = Date.now();
    if (record.networkPending) return;
    if (!force && record.networkAt && now - record.networkAt < 10000) return;
    record.networkPending = true;
    record.networkAt = now;

    try {
      api.runtime.sendMessage({ type: "flexMediaCandidates" }, (response) => {
        record.networkPending = false;

        let failed = false;
        try {
          failed = Boolean(api.runtime.lastError);
        } catch (error) {
          failed = true;
        }
        if (failed || !response || response.ok !== true) return;

        const list = Array.isArray(response.candidates) ? response.candidates : [];
        const signature = list
          .map((candidate) => `${candidate.url}\u0000${candidate.size || 0}`)
          .join("\u0001");
        if (signature === record.networkSignature) return;

        record.networkSignature = signature;
        record.networkCandidates = list;
        updateRecord(record);
      });
    } catch (error) {
      record.networkPending = false;
    }
  }

  function probeOptionSizes(record) {
    const urls = [];
    for (const option of record.options) {
      if (!option.size && /^https?:/i.test(option.url)) urls.push(option.url);
    }
    if (!urls.length) {
      record.sizeSignature = "";
      return;
    }

    const batch = urls.slice(0, 12);
    const signature = batch.join("\u0000");
    if (signature === record.sizeSignature) return;
    record.sizeSignature = signature;

    if (!api || !api.runtime || !api.runtime.sendMessage) return;

    try {
      api.runtime.sendMessage({ type: "flexMediaSizes", urls: batch }, (response) => {
        let failed = false;
        try {
          failed = Boolean(api.runtime.lastError);
        } catch (error) {
          failed = true;
        }
        if (failed || !response || response.ok !== true || !response.sizes) return;

        let changed = false;
        for (const option of record.options) {
          const size = Number(response.sizes[option.url]) || 0;
          if (size > 0) record.sizes[option.url] = size;
          if (size > 0 && size !== option.size) {
            option.size = size;
            changed = true;
          }
        }
        if (!changed) return;

        record.optionSignature = optionSignature(record);
        renderPanel(record);
      });
    } catch (error) {
      // The extension may have been reloaded while the page stayed open.
    }
  }

  function showStatus(record, text, tone = "info", duration = 4500) {
    if (!record.visible) showRecord(record);
    if (!record.visible) return;

    record.status.textContent = text;
    record.status.className = `status${tone === "error" ? " error" : tone === "ok" ? " ok" : ""}`;
    record.status.hidden = false;
    record.statusVisible = true;
    cancelHide(record);

    if (record.menuOpen) closeMenu(record);

    clearTimeout(record.statusTimer);
    record.statusTimer = window.setTimeout(() => {
      record.statusTimer = 0;
      record.status.hidden = true;
      record.statusVisible = false;
      scheduleHide(record, 300);
    }, duration);

    requestPosition();
  }

  function suggestedFileName(record, option) {
    const kind = option.kind || (record.media.tagName === "AUDIO" ? "audio" : "video");
    let base = "";

    if (option.ytdlp && record.ytTitle) base = record.ytTitle;

    if (!base) {
      try {
        const path = new URL(option.url).pathname;
        const pieces = path.split("/").filter(Boolean);
        base = pieces.length ? decodeURIComponent(pieces[pieces.length - 1]) : "";
      } catch (error) {
        base = "";
      }
    }

    if (!base) {
      base = (document.title || "").trim() || (kind === "audio" ? "audio" : "video");
    }

    base = base.replace(/\.[a-z0-9]{2,5}$/i, "");

    const genericName = /^(?:videoplayback|play|video|audio|media|stream|index|download|watch|master|manifest)$/i;
    if (!base || genericName.test(base) || /^[0-9a-f]{20,}$/i.test(base) || /^\d{3,}$/.test(base)) {
      base = (document.title || "").trim() || (kind === "audio" ? "audio" : "video");
      base = base.replace(/\.[a-z0-9]{2,5}$/i, "");
    }

    if (option.kind === "video" && option.height && record.options.length > 1) {
      const qualityToken = `${option.height}p`;
      // Rewrite an existing resolution token ("-360", "-360p", "_720p") instead
      // of stacking a second one on top of it.
      const exactToken = new RegExp(
        `(^|[-_.])${option.height}p?(?=$|[-_.])`,
        "i"
      );
      const token = /(^|[-_.])\d{3,4}p(?=$|[-_.])/i;
      if (exactToken.test(base)) base = base.replace(exactToken, `$1${qualityToken}`);
      else if (token.test(base)) base = base.replace(token, `$1${qualityToken}`);
      else base += `-${qualityToken}`;
    } else if (option.kind === "audio" && option.bitrate && record.options.length > 1) {
      const bitrateToken = `${Math.round(option.bitrate)}k`;
      const token = new RegExp(`(^|[-_.])${bitrateToken}(?:bps)?(?=$|[-_.])`, "i");
      if (!token.test(base)) base += `-${bitrateToken}`;
    }

    base = base
      .replace(/[\\/:*?"<>|]/g, "_")
      .replace(/\s+/g, " ")
      .trim()
      .slice(0, 150);

    if (!base) base = kind === "audio" ? "audio" : "video";
    if (/^(con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\.|$)/i.test(base)) {
      base = `_${base}`;
    }

    const extension = option.ext || (kind === "audio" ? "mp3" : "mp4");
    return `${base}.${extension}`;
  }

  function sendSelected(record) {
    if (record.sending) return;

    const option = selectedOption(record);
    if (!option) {
      if (record.ytState === "loading") {
        showStatus(
          record,
          t("panelExtracting", "Extracting video qualities…"),
          "info",
          7000
        );
        return;
      }
      if (record.ytState === "error") {
        showStatus(
          record,
          t(
            "panelExtractFailed",
            "Could not read this video's qualities. Make sure yt-dlp.exe is next to the desktop app."
          ),
          "error",
          7000
        );
        return;
      }
      // The lookup normally starts when the video plays; this covers a click
      // before playback ever started.
      if (isYtDlpSite() && ytPageKey() &&
          (record.ytState === "idle" || record.ytState === "empty")) {
        requestQualities(record);
        if (record.ytPending) {
          showStatus(
            record,
            t("panelExtracting", "Extracting video qualities…"),
            "info",
            7000
          );
          return;
        }
      }
      if (!record.options.length) {
        showStatus(
          record,
          t(
            "panelNoSource",
            "No direct media link was detected yet. Start the video, then click the panel again."
          ),
          "error"
        );
        return;
      }
      showStatus(record, t("panelNoSource", "No direct media URL is available for this item."), "error");
      return;
    }

    sendOption(record, option);
  }

  function sendOption(record, option) {
    if (record.sending || !option) return;

    const ytdlp = option.ytdlp === true;
    const pageUrl = record.ytPageUrl || location.href;

    const payload = {
      type: "flexMediaDownload",
      url: ytdlp ? pageUrl : option.url,
      filename: suggestedFileName(record, option),
      mime: option.mime || mimeForExtension(option.ext) ||
        (option.kind === "audio" ? "audio/mpeg" : "video/mp4"),
      referrer: location.href,
      description: record.ytTitle || pageTitle() || document.title || location.href,
      totalBytes: Number(option.size) || 0
    };

    if (ytdlp) {
      payload.pageUrl = pageUrl;
      payload.formatId = option.formatId;
      payload.merge = option.merge === true;
    }

    record.sending = true;
    record.main.disabled = true;
    showStatus(record, t("panelSending", "Sending to FLEX Download Manager…"), "info", 15000);

    try {
      if (!api || !api.runtime || !api.runtime.sendMessage) {
        throw new Error("Extension runtime is unavailable.");
      }

      api.runtime.sendMessage(payload, (response) => {
        record.sending = false;
        record.main.disabled = false;

        let failure = null;
        try {
          failure = api.runtime.lastError || null;
        } catch (error) {
          failure = error;
        }

        if (failure) {
          // No answer from the service worker: the extension was reloaded while
          // this page stayed open, or its background script is stale.
          showStatus(
            record,
            t(
              "panelBridgeUnavailable",
              "The extension could not reach the desktop app. Reload the extension in chrome://extensions, then reload this page and try again."
            ),
            "error",
            7000
          );
          return;
        }

        if (!response || response.ok !== true) {
          const reason = response && response.reason;
          if (reason === "rejected" || reason === "forbidden") {
            showStatus(
              record,
              t(
                "panelRejected",
                "The desktop app refused this download. Check its settings and try again."
              ),
              "error",
              6000
            );
            return;
          }
          showStatus(
            record,
            t("panelOffline", "FLEX Download Manager is not running."),
            "error",
            5000
          );
          return;
        }

        showStatus(
          record,
          t("panelSent", "Download added to the desktop app."),
          "ok",
          4000
        );
      });
    } catch (error) {
      record.sending = false;
      record.main.disabled = false;
      showStatus(
        record,
        t(
          "panelBridgeUnavailable",
          "The extension could not reach the desktop app. Reload the extension in chrome://extensions, then reload this page and try again."
        ),
        "error",
        7000
      );
    }
  }

  function announceRecord(record) {
    if (!enabled || record.dismissed || record.announced) return;
    updateRecord(record);
    if (!isMediaVisible(record.media)) return;

    record.announced = true;
    showRecord(record);
    if (!record.hovered && !record.panelHovered && !record.panelFocused &&
        !record.menuOpen && !record.statusVisible && record.media.paused) {
      scheduleHide(record, 6000);
    }
  }

  function registerMedia(media) {
    if (!enabled) return;
    if (!media || media.nodeType !== 1 || !media.isConnected) return;
    if (media.tagName !== "VIDEO" && media.tagName !== "AUDIO") return;

    let record = records.get(media);
    if (!record) record = createRecord(media);
    announceRecord(record);
    updateRecord(record);
  }

  function observeRoot(root) {
    if (!observer || !root || observedRoots.has(root)) return;
    observedRoots.add(root);
    try {
      observer.observe(root, {
        childList: true,
        subtree: true,
        attributes: true,
        attributeFilter: DIRECTIONAL_ATTRIBUTES
      });
    } catch (error) {
      // The root may have been removed before the observer attached.
    }
  }

  function scanSubtree(root) {
    if (!root || typeof root.querySelectorAll !== "function") return;

    if (root.nodeType === 1 && root.matches && root.matches(MEDIA_SELECTOR)) {
      registerMedia(root);
    }

    for (const media of root.querySelectorAll(MEDIA_SELECTOR)) {
      registerMedia(media);
    }

    for (const element of root.querySelectorAll("*")) {
      if (!element.shadowRoot) continue;
      observeRoot(element.shadowRoot);
      scanSubtree(element.shadowRoot);
    }
  }

  function scanKnownRoots() {
    if (!enabled) return;
    for (const root of observedRoots) {
      if (!root || typeof root.querySelectorAll !== "function") continue;
      for (const media of root.querySelectorAll(MEDIA_SELECTOR)) {
        registerMedia(media);
      }
    }
  }

  function scheduleScan() {
    if (scanTimer) return;
    scanTimer = window.setTimeout(flushScan, 120);
  }

  function flushScan() {
    scanTimer = 0;
    if (!enabled) {
      pendingNodes.clear();
      pendingMedia.clear();
      return;
    }

    for (const node of pendingNodes) {
      scanSubtree(node);
    }
    pendingNodes.clear();

    for (const media of pendingMedia) {
      const record = records.get(media);
      if (!record) continue;
      if (media.isConnected) updateRecord(record);
      else destroyRecord(record);
    }
    pendingMedia.clear();

    for (const [media, record] of records) {
      if (!media.isConnected) destroyRecord(record);
    }
  }

  function onMutations(mutations) {
    let removed = false;

    for (const mutation of mutations) {
      if (mutation.type === "attributes") {
        const target = mutation.target;
        if (!target || target.nodeType !== 1 || !target.closest) continue;
        const media = target.closest(MEDIA_SELECTOR);
        if (media) pendingMedia.add(media);
        continue;
      }

      for (const node of mutation.addedNodes) {
        if (node.nodeType !== 1) continue;
        if (node.matches && node.matches("source, track") && node.closest) {
          const media = node.closest(MEDIA_SELECTOR);
          if (media) pendingMedia.add(media);
        }
        pendingNodes.add(node);
      }

      if (mutation.target && mutation.target.nodeType === 1 && mutation.target.closest) {
        const parentMedia = mutation.target.closest(MEDIA_SELECTOR);
        if (parentMedia) pendingMedia.add(parentMedia);
      }

      for (const node of mutation.removedNodes) {
        if (node.nodeType !== 1) continue;
        removed = true;
        if (node.matches && node.matches("source, track") && node.closest) {
          const media = node.closest(MEDIA_SELECTOR);
          if (media) pendingMedia.add(media);
        }
        if (node.matches && node.matches(MEDIA_SELECTOR)) {
          const record = records.get(node);
          if (record) destroyRecord(record);
        }
      }
    }

    if (pendingNodes.size || pendingMedia.size || removed) scheduleScan();
  }

  function destroyRecord(record) {
    cancelHide(record);
    clearTimeout(record.statusTimer);
    record.statusTimer = 0;
    record.statusVisible = false;
    closeMenu(record, true);
    record.host.remove();
    records.delete(record.media);
  }

  function destroyAll() {
    for (const record of Array.from(records.values())) destroyRecord(record);
  }

  function setEnabled(next) {
    const value = next !== false;
    if (enabled === value) {
      if (value) scanKnownRoots();
      return;
    }

    enabled = value;
    if (!enabled) {
      destroyAll();
    } else {
      scanKnownRoots();
    }
  }

  function loadSettings() {
    if (!api || !api.storage || !api.storage.local) return;
    try {
      api.storage.local.get({ mediaPanel: true }, (result) => {
        try {
          if (api.runtime.lastError) return;
        } catch (error) {
          return;
        }
        setEnabled(result.mediaPanel !== false);
      });
    } catch (error) {
      // Keep the panel enabled if storage cannot be read.
    }
  }

  function refreshVisible() {
    if (!enabled) return;

    scanKnownRoots();

    for (const [media, record] of records) {
      if (!media.isConnected) {
        destroyRecord(record);
        continue;
      }
      if (!record.announced) announceRecord(record);
      if (!record.host.isConnected) mountHost(record);
      if (record.visible || media.matches(":hover") ||
          (!media.paused && !media.ended)) {
        updateRecord(record);
      }
    }
  }

  function onPointerMove(event) {
    if (!enabled || !event) return;

    for (const record of records.values()) {
      if (!record.media.isConnected) continue;
      const rect = record.media.getBoundingClientRect();
      const inside = event.clientX >= rect.left && event.clientX <= rect.right &&
        event.clientY >= rect.top && event.clientY <= rect.bottom;

      if (inside) {
        if (!record.hovered) {
          record.hovered = true;
          cancelHide(record);
        }
        if (!record.dismissed && !record.visible && isMediaVisible(record.media)) {
          showRecord(record);
        }
      } else if (record.hovered) {
        record.hovered = false;
        scheduleHide(record, 280);
      }
    }
  }

  function onFullscreenChange() {
    for (const record of records.values()) mountHost(record);

    // Fullscreen hides the strip completely; it comes back once the player
    // leaves fullscreen and the pointer returns to the video.
    if (document.fullscreenElement) {
      for (const record of records.values()) {
        record.hovered = false;
        record.panelHovered = false;
        record.panelFocused = false;
        record.mediaFocused = false;
        hideRecord(record, true);
      }
      return;
    }

    for (const record of records.values()) {
      if (!record.media.isConnected) continue;
      if (record.media.matches(":hover") || !record.media.paused) {
        showRecord(record);
      } else {
        hideRecord(record, true);
      }
    }

    requestPosition();
  }

  function onVisibilityChange() {
    if (document.visibilityState === "hidden") {
      for (const record of records.values()) hideRecord(record, true);
    } else {
      requestPosition();
    }
  }

  function start() {
    if (started || !document.documentElement) return;
    started = true;

    observer = new MutationObserver(onMutations);
    observeRoot(document.documentElement);
    scanSubtree(document.documentElement);
    loadSettings();

    try {
      api.storage.onChanged.addListener((changes, area) => {
        if (area !== "local" || !changes.mediaPanel) return;
        setEnabled(changes.mediaPanel.newValue !== false);
      });
    } catch (error) {
      // The extension may have been reloaded while this page stayed open.
    }

    window.addEventListener("scroll", requestPosition, { passive: true, capture: true });
    window.addEventListener("resize", requestPosition, { passive: true });
    document.addEventListener("pointermove", onPointerMove, { passive: true, capture: true });
    document.addEventListener("fullscreenchange", onFullscreenChange);
    document.addEventListener("visibilitychange", onVisibilityChange);

    refreshTimer = window.setInterval(refreshVisible, 2500);
    window.addEventListener("pagehide", () => {
      if (refreshTimer) clearInterval(refreshTimer);
    }, { once: true });
  }

  if (document.documentElement) start();
  else document.addEventListener("DOMContentLoaded", start, { once: true });
})();
