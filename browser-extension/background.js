// FLEX Download Manager Chrome integration.
//
// The desktop app exposes a small loopback-only JSON bridge on the first free
// port in PORTS. A download is forwarded there; only after the app acknowledges
// it do we cancel Chrome's copy. If the app is not running the Chrome download
// is left untouched, so browser downloads never get lost.

const PORTS = [48765, 48766, 48767, 48768, 48769];
const DOWNLOAD_SCHEME = /^https?:\/\//i;
const MENU_ID = "flex-download-link";
const DOWNLOAD_POST_TIMEOUT_MS = 6000;
const DECISION_TIMEOUT_MS = 10000;

// Media requests seen per tab. This is what makes the page panel work on sites
// that play through MSE (`blob:` URLs): the <video> element exposes nothing
// downloadable, but the direct streams it fetched still can be offered.
const MEDIA_URL = /\.(?:mp4|m4v|mov|webm|mkv|flv|avi|ogv|mp3|m4a|aac|ogg|oga|opus|wav|flac)(?:$|[?#])/i;
const ADAPTIVE_URL = /\.(?:m3u8|mpd)(?:$|[?#])/i;
const MEDIA_MIME = /^(?:video|audio)\//i;
const MAX_TAB_CANDIDATES = 32;
const MAX_CANDIDATE_AGE = 15 * 60 * 1000;

let cachedPort = null;
const tabMedia = new Map();
const sizeCache = new Map();

// Listener registration helpers. An extension can run with a permission set
// that no longer matches its manifest (for example right after the manifest
// gained webRequest but the browser has not re-approved it). A throw here would
// abort the whole service worker, including the panel's message handler, so
// every registration is isolated.
function listen(event, handler, ...args) {
  try {
    if (event && typeof event.addListener === "function") {
      event.addListener(handler, ...args);
      return true;
    }
  } catch (error) {
    // Permission missing: the related feature degrades, nothing else breaks.
  }
  return false;
}

function apiOf(property) {
  try {
    return chrome[property] || null;
  } catch (error) {
    return null;
  }
}

function message(key) {
  return chrome.i18n.getMessage(key) || key;
}

function baseName(path) {
  if (!path) return "";
  const pieces = String(path).split(/[\\/]/);
  return pieces[pieces.length - 1] || "";
}

function fileNameFromUrl(url) {
  try {
    const parsed = new URL(url);
    const pieces = parsed.pathname.split("/").filter(Boolean);
    const last = pieces.length ? pieces[pieces.length - 1] : "";
    return last ? decodeURIComponent(last) : "download";
  } catch (error) {
    return "download";
  }
}

function inferCategory(fileName, mime) {
  const type = String(mime || "").toLowerCase();
  if (type.startsWith("video/")) return "video";
  if (type.startsWith("audio/")) return "music";
  if (
    type === "application/pdf" ||
    type === "application/msword" ||
    type === "application/rtf" ||
    type === "application/epub+zip" ||
    type.startsWith("application/vnd.openxmlformats-officedocument") ||
    type.startsWith("application/vnd.ms-")
  ) {
    return "documents";
  }
  if (
    type === "application/zip" ||
    type === "application/x-zip-compressed" ||
    type === "application/x-rar-compressed" ||
    type === "application/vnd.rar" ||
    type === "application/x-7z-compressed" ||
    type === "application/gzip" ||
    type === "application/x-tar" ||
    type === "application/x-bzip2" ||
    type === "application/x-xz"
  ) {
    return "compressed";
  }
  if (
    type === "application/vnd.android.package-archive" ||
    type === "application/x-msdownload" ||
    type === "application/x-msi" ||
    type === "application/java-archive"
  ) {
    return "programs";
  }

  const dot = String(fileName || "").lastIndexOf(".");
  const extension =
    dot >= 0 ? String(fileName).slice(dot + 1).toLowerCase() : "";

  const video = new Set([
    "mp4", "mkv", "avi", "mov", "wmv", "flv", "webm", "m4v",
    "mpg", "mpeg", "ts", "m2ts", "3gp", "ogv"
  ]);
  const music = new Set([
    "mp3", "wav", "flac", "aac", "ogg", "m4a", "wma", "opus",
    "aiff", "alac"
  ]);
  const archives = new Set([
    "zip", "rar", "7z", "tar", "gz", "bz2", "xz", "zst", "iso", "cab"
  ]);
  const documents = new Set([
    "pdf", "doc", "docx", "xls", "xlsx", "ppt", "pptx", "txt",
    "rtf", "csv", "epub", "md", "odt", "ods"
  ]);
  const programs = new Set([
    "exe", "msi", "apk", "jar", "deb", "rpm", "dmg", "pkg"
  ]);

  if (video.has(extension)) return "video";
  if (music.has(extension)) return "music";
  if (archives.has(extension)) return "compressed";
  if (documents.has(extension)) return "documents";
  if (programs.has(extension)) return "programs";
  return "other";
}

function trackableMedia(url, mime, mediaRequest) {
  if (!url || !/^https?:/i.test(url)) return false;
  if (ADAPTIVE_URL.test(url)) return false;
  if (/\.(?:ts|m2ts)(?:$|[?#])/i.test(url)) return false;
  if (MEDIA_MIME.test(String(mime || "").toLowerCase())) return true;
  if (MEDIA_URL.test(url)) return true;
  return Boolean(mediaRequest) && !mime;
}

// YouTube (googlevideo) requests carry the byte range in the query string.
// Dropping it gives the downloader the whole stream instead of one chunk.
function normalizeMediaUrl(url) {
  try {
    const parsed = new URL(url);
    if (/(^|\.)googlevideo\.com$/i.test(parsed.hostname)) {
      parsed.searchParams.delete("range");
      parsed.searchParams.delete("rn");
      parsed.searchParams.delete("rbuf");
      return parsed.href;
    }
  } catch (error) {
    return url;
  }
  return url;
}

function rememberMedia(tabId, rawUrl, mime, size, mediaRequest) {
  const url = normalizeMediaUrl(rawUrl);
  if (tabId < 0 || !trackableMedia(url, mime, mediaRequest)) return;

  let entries = tabMedia.get(tabId);
  if (!entries) {
    entries = new Map();
    tabMedia.set(tabId, entries);
  }

  const existing = entries.get(url);
  const now = Date.now();
  if (existing) {
    existing.at = now;
    if (mime) existing.mime = String(mime);
    if (size > 0) existing.size = size;
    existing.media = existing.media || Boolean(mediaRequest);
    return;
  }

  if (entries.size >= MAX_TAB_CANDIDATES) {
    let oldestUrl = "";
    let oldestAt = Infinity;
    for (const [key, value] of entries) {
      if (value.at < oldestAt) {
        oldestAt = value.at;
        oldestUrl = key;
      }
    }
    if (oldestUrl) entries.delete(oldestUrl);
  }

  entries.set(url, {
    url,
    mime: String(mime || ""),
    size: Number(size) || 0,
    media: Boolean(mediaRequest),
    at: now
  });
}

function candidatesForTab(tabId) {
  const entries = tabMedia.get(tabId);
  if (!entries) return [];

  const now = Date.now();
  const list = [];
  for (const [url, value] of entries) {
    if (now - value.at > MAX_CANDIDATE_AGE) {
      entries.delete(url);
      continue;
    }
    list.push({
      url: value.url,
      mime: value.mime,
      size: value.size,
      media: value.media
    });
  }
  list.sort((a, b) => b.size - a.size);
  return list.slice(0, 16);
}

function headerValue(headers, name) {
  if (!Array.isArray(headers)) return "";
  const wanted = String(name).toLowerCase();
  for (const header of headers) {
    if (String(header.name || "").toLowerCase() === wanted) {
      return String(header.value || "");
    }
  }
  return "";
}

const webRequest = apiOf("webRequest");

listen(
  webRequest && webRequest.onBeforeRequest,
  (details) => {
    if (details.tabId < 0) return;
    if (details.type === "main_frame") {
      tabMedia.delete(details.tabId);
      return;
    }
    if (details.type === "media") {
      rememberMedia(details.tabId, details.url, "", 0, true);
    }
  },
  { urls: ["<all_urls>"], types: ["main_frame", "media"] }
);

listen(
  webRequest && webRequest.onHeadersReceived,
  (details) => {
    if (details.tabId < 0) return;
    const mime = String(headerValue(details.responseHeaders, "content-type"))
      .split(";")[0]
      .trim()
      .toLowerCase();
    const size = (() => {
      const contentRange = headerValue(details.responseHeaders, "content-range");
      const match = /\/(\d+)\s*$/.exec(contentRange);
      if (match) return Number(match[1]) || 0;
      return Number(headerValue(details.responseHeaders, "content-length")) || 0;
    })();
    rememberMedia(details.tabId, details.url, mime, size, details.type === "media");
  },
  { urls: ["<all_urls>"], types: ["media", "xmlhttprequest", "object", "other"] },
  ["responseHeaders"]
);

listen(apiOf("tabs") && apiOf("tabs").onRemoved, (tabId) => {
  tabMedia.delete(tabId);
});

async function probeSize(url) {
  if (sizeCache.has(url)) return sizeCache.get(url);

  let size = 0;
  let controller = new AbortController();
  let timer = setTimeout(() => controller.abort(), 8000);
  try {
    const response = await fetch(url, {
      method: "HEAD",
      cache: "no-store",
      credentials: "omit",
      signal: controller.signal
    });
    if (response.ok) {
      size = Number(response.headers.get("content-length")) || 0;
    }
  } catch (error) {
    // Some servers reject HEAD; try a one-byte range below.
  } finally {
    clearTimeout(timer);
  }

  if (!size) {
    controller = new AbortController();
    timer = setTimeout(() => controller.abort(), 8000);
    try {
      const response = await fetch(url, {
        cache: "no-store",
        credentials: "omit",
        headers: { Range: "bytes=0-0" },
        signal: controller.signal
      });
      const range = response.headers.get("content-range") || "";
      const match = /\/(\d+)\s*$/.exec(range);
      if (match) size = Number(match[1]) || 0;
      if (!size) size = Number(response.headers.get("content-length")) || 0;
      try {
        if (response.body) await response.body.cancel();
      } catch (error) {
        // The body may already be consumed.
      }
    } catch (error) {
      // Unknown size; the panel simply omits it.
    } finally {
      clearTimeout(timer);
    }
  }

  if (sizeCache.size > 200) sizeCache.clear();
  sizeCache.set(url, size);
  return size;
}

async function findPort(force = false) {
  if (cachedPort && !force) return cachedPort;

  // Probe every port at once. Sequential probing meant five 400 ms timeouts
  // whenever the desktop app was not running, which kept a paused browser
  // download waiting for up to two seconds.
  const results = await Promise.all(
    PORTS.map(async (port) => {
      const controller = new AbortController();
      const timer = setTimeout(() => controller.abort(), 400);
      try {
        const response = await fetch(`http://127.0.0.1:${port}/ping`, {
          cache: "no-store",
          signal: controller.signal
        });
        if (!response.ok) return null;
        const data = await response.json().catch(() => null);
        return data && data.ok === true ? port : null;
      } catch (error) {
        // The app is not listening on this port; try the next one.
        return null;
      } finally {
        clearTimeout(timer);
      }
    })
  );

  const match = results.find((port) => port !== null);
  cachedPort = match === undefined ? null : match;
  return cachedPort;
}

async function sendDownload(payload) {
  const port = await findPort();
  if (!port) return { ok: false, reason: "offline" };

  // The bridge answers as soon as it accepts the request, so this timeout only
  // covers a hung or half-dead app. Keeping it short matters: the browser
  // download stays paused until we decide.
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), DOWNLOAD_POST_TIMEOUT_MS);
  try {
    const response = await fetch(`http://127.0.0.1:${port}/download`, {
      method: "POST",
      cache: "no-store",
      headers: {
        "Content-Type": "application/json"
      },
      body: JSON.stringify(payload),
      signal: controller.signal
    });
    if (!response.ok) {
      const data = await response.json().catch(() => ({}));
      cachedPort = null;
      return {
        ok: false,
        reason: response.status === 403 ? "forbidden" : "rejected",
        error: data && data.error ? String(data.error) : "",
        status: response.status
      };
    }
    const data = await response.json().catch(() => ({}));
    if (data.ok === true) return { ok: true, reason: "sent" };
    return { ok: false, reason: "rejected", status: response.status };
  } catch (error) {
    cachedPort = null;
    return { ok: false, reason: "offline" };
  } finally {
    clearTimeout(timer);
  }
}

// Asks the desktop app to list the qualities yt-dlp reports for a page. The
// app runs yt-dlp in a child process, which can take a few seconds on YouTube.
async function extractQualities(url) {
  const port = await findPort();
  if (!port) return { ok: false, reason: "offline" };

  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), 55000);
  try {
    const response = await fetch(`http://127.0.0.1:${port}/extract`, {
      method: "POST",
      cache: "no-store",
      headers: {
        "Content-Type": "application/json"
      },
      body: JSON.stringify({ url }),
      signal: controller.signal
    });
    if (!response.ok) {
      cachedPort = null;
      return { ok: false, reason: "rejected" };
    }
    const data = await response.json().catch(() => ({}));
    return data && typeof data === "object"
      ? data
      : { ok: false, reason: "rejected" };
  } catch (error) {
    return { ok: false, reason: "offline" };
  } finally {
    clearTimeout(timer);
  }
}

// The capture setting must be known synchronously: onCreated has to pause the
// download before Chrome reaches target determination. Awaiting storage there
// was part of why Chrome's Save As dialog won the race. The cached value starts
// at the manifest default and is refreshed on load and on every change.
let autoCaptureCached = true;

function readAutoCaptureSetting() {
  try {
    chrome.storage.local
      .get({ autoCapture: true })
      .then((result) => {
        autoCaptureCached = result.autoCapture !== false;
      })
      .catch(() => {});
  } catch (error) {
    // Storage unavailable: keep capturing, same as before.
  }
}

readAutoCaptureSetting();

listen(apiOf("storage") && apiOf("storage").onChanged, (changes, area) => {
  if (area !== "local" || !changes.autoCapture) return;
  autoCaptureCached = changes.autoCapture.newValue !== false;
});

function showOfflineNotification() {
  try {
    chrome.notifications.create({
      type: "basic",
      iconUrl: "icons/icon128.png",
      title: message("appOffTitle"),
      message: message("appOffMessage")
    });
  } catch (error) {
    // Notifications are optional; never let them break the flow.
  }
}

function ensureContextMenu() {
  const contextMenus = apiOf("contextMenus");
  if (!contextMenus) return;
  try {
    contextMenus.removeAll(() => {
      try {
        contextMenus.create({
          id: MENU_ID,
          title: message("contextDownload"),
          contexts: ["link", "image", "video", "audio"]
        });
      } catch (error) {
        // The menu already exists or the API is unavailable.
      }
    });
  } catch (error) {
    // Context menus are optional.
  }
}

listen(apiOf("runtime") && apiOf("runtime").onInstalled, () => {
  ensureContextMenu();
});

listen(apiOf("runtime") && apiOf("runtime").onStartup, () => {
  cachedPort = null;
  ensureContextMenu();
});

// Takeover state, keyed by Chrome download id.
//
// Chrome runs target determination (and, when "Ask where to save each file
// before downloading" is enabled, shows its own Save As dialog) immediately
// after a download item is created. Cancelling from an async onCreated handler
// is therefore too late: the native picker is already on screen and
// chrome.downloads.cancel() does not close it.
//
// The original IDM extension avoids that by pausing the item synchronously in
// onCreated and by holding the onDeterminingFilename callback until the
// desktop app has answered. Chrome cannot finish target determination while
// the callback is pending, so its Save As dialog never opens.
const pendingDownloads = new Map();

function pauseDownload(id) {
  try {
    chrome.downloads.pause(id, () => void chrome.runtime.lastError);
  } catch (error) {
    // Not pausable (already finished); settle() handles the rest.
  }
}

function resumeDownload(id) {
  try {
    chrome.downloads.resume(id, () => void chrome.runtime.lastError);
  } catch (error) {
    // Nothing left to resume.
  }
}

function payloadFor(item) {
  const url = item.finalUrl || item.url || "";
  const fileName = baseName(item.filename) || fileNameFromUrl(url);
  return {
    url,
    filename: fileName,
    referrer: item.referrer || "",
    mime: item.mime || "",
    totalBytes: Number(item.totalBytes) > 0 ? Number(item.totalBytes) : 0,
    description: item.referrer || "",
    category: inferCategory(fileName, item.mime || "")
  };
}

// Finishes a paused download exactly once: either Chrome drops it because the
// desktop app took it over, or Chrome keeps it because the app is offline,
// refused it, or did not answer in time.
function settleDownload(id, takeOver) {
  const state = pendingDownloads.get(id);
  if (!state || state.settled) return;
  state.settled = true;
  pendingDownloads.delete(id);
  clearTimeout(state.timer);

  const suggest = state.suggest;
  try {
    if (takeOver) {
      // Chrome has no cancel flag on onDeterminingFilename: the custom
      // bindings accept only {filename, conflictAction} and treat anything
      // else as an empty suggestion, which completes target determination and
      // opens the Save As dialog. So do what the original IDM extension does:
      // cancel the item and leave the suggestion callback unanswered.
      // Cancelling and then erasing destroys the download item, which destroys
      // the target determiner holding our callback; the dialog never opens.
      chrome.downloads.cancel(id, () => {
        void chrome.runtime.lastError;
        chrome.downloads.erase({ id }, () => void chrome.runtime.lastError);
      });
    } else {
      // Accept the filename Chrome proposed, then let the browser continue.
      if (suggest) suggest();
      resumeDownload(id);
    }
  } catch (error) {
    resumeDownload(id);
  }
}

listen(apiOf("downloads") && apiOf("downloads").onCreated, (item) => {
  if (!autoCaptureCached) return;

  const url = item.finalUrl || item.url || "";
  if (!DOWNLOAD_SCHEME.test(url)) return;
  if (item.byExtensionId === chrome.runtime.id) return;
  if (pendingDownloads.has(item.id)) return;

  pendingDownloads.set(item.id, {
    item,
    suggest: null,
    settled: false,
    timer: setTimeout(
      () => settleDownload(item.id, false),
      DECISION_TIMEOUT_MS
    )
  });

  // Synchronous, before any await: this is what keeps Chrome from starting the
  // download and prompting for a path while we talk to the desktop app.
  pauseDownload(item.id);
});

listen(
  apiOf("downloads") && apiOf("downloads").onDeterminingFilename,
  (item, suggest) => {
    const state = pendingDownloads.get(item.id);
    if (!state) return; // Not a download we paused: let Chrome decide.

    state.item = item;
    state.suggest = suggest;

    sendDownload(payloadFor(item))
      .then((result) =>
        settleDownload(
          item.id,
          autoCaptureCached && Boolean(result) && result.ok === true
        )
      )
      .catch(() => settleDownload(item.id, false));

    // Tell Chrome the suggestion will arrive asynchronously. Without this,
    // downloads_custom_bindings.js immediately calls suggest() for us, which
    // completes target determination and opens the Save As dialog before the
    // desktop app has even answered.
    return true;
  }
);

listen(apiOf("contextMenus") && apiOf("contextMenus").onClicked, async (info) => {
  if (info.menuItemId !== MENU_ID) return;

  const url = info.linkUrl || info.srcUrl || "";
  if (!DOWNLOAD_SCHEME.test(url)) return;

  const fileName = fileNameFromUrl(url);
  const payload = {
    url,
    filename: fileName,
    referrer: info.pageUrl || "",
    mime: "",
    totalBytes: 0,
    description: info.pageUrl || "",
    category: inferCategory(fileName, "")
  };

  const result = await sendDownload(payload);
  if (!result.ok) showOfflineNotification();
});

// Explicit "Download this video/audio" clicks from the page panel. Unlike
// automatic capture, this does not depend on the Capture Chrome downloads
// setting: the user asked for this download directly.
listen(apiOf("runtime") && apiOf("runtime").onMessage, (request, sender, sendResponse) => {
  if (!request || typeof request.type !== "string") return undefined;

  if (request.type === "flexMediaCandidates") {
    if (!sender || sender.id !== chrome.runtime.id || !sender.tab) {
      sendResponse({ ok: false, error: "invalid-sender" });
      return false;
    }
    sendResponse({ ok: true, candidates: candidatesForTab(sender.tab.id) });
    return false;
  }

  if (request.type === "flexMediaSizes") {
    if (!sender || sender.id !== chrome.runtime.id) {
      sendResponse({ ok: false, error: "invalid-sender" });
      return false;
    }
    const urls = Array.isArray(request.urls)
      ? request.urls
          .map((value) => String(value || "").trim())
          .filter((value) => DOWNLOAD_SCHEME.test(value) && value.length <= 16384)
          .slice(0, 12)
      : [];

    Promise.all(urls.map(async (url) => [url, await probeSize(url)]))
      .then((entries) => {
        const sizes = {};
        for (const [url, size] of entries) {
          if (size > 0) sizes[url] = size;
        }
        sendResponse({ ok: true, sizes });
      })
      .catch(() => sendResponse({ ok: false }));

    return true;
  }

  if (request.type === "flexMediaExtract") {
    if (!sender || sender.id !== chrome.runtime.id || !sender.tab) {
      sendResponse({ ok: false, error: "invalid-sender" });
      return false;
    }

    const pageUrl = String(request.url || "").trim();
    if (!DOWNLOAD_SCHEME.test(pageUrl) || pageUrl.length > 4096) {
      sendResponse({ ok: false, reason: "url" });
      return false;
    }

    extractQualities(pageUrl)
      .then((result) => sendResponse(result))
      .catch(() => sendResponse({ ok: false, reason: "offline" }));

    return true;
  }

  if (request.type !== "flexMediaDownload") return undefined;

  if (!sender || sender.id !== chrome.runtime.id || !sender.tab) {
    sendResponse({ ok: false, error: "invalid-sender" });
    return false;
  }

  const url = String(request.url || "").trim();
  if (!DOWNLOAD_SCHEME.test(url) || url.length > 16384) {
    sendResponse({ ok: false, error: "invalid-url" });
    return false;
  }

  let fileName = baseName(String(request.filename || "").trim());
  if (!fileName) fileName = fileNameFromUrl(url);
  fileName = fileName.slice(0, 240);

  const mime = String(request.mime || "").slice(0, 160);
  const referrer = String(request.referrer || sender.tab.url || "").slice(0, 4096);
  const description = String(request.description || referrer)
    .slice(0, 4096);

  // yt-dlp entries carry a format selector instead of a direct stream URL; the
  // desktop app resolves it itself.
  const formatId = String(request.formatId || "").slice(0, 400);
  const pageUrl = formatId
    ? String(request.pageUrl || url).trim().slice(0, 4096)
    : "";

  const payload = {
    url,
    filename: fileName,
    referrer,
    mime,
    totalBytes: Number(request.totalBytes) > 0 ? Number(request.totalBytes) : 0,
    description,
    category: inferCategory(fileName, mime)
  };

  if (formatId && DOWNLOAD_SCHEME.test(pageUrl)) {
    payload.url = pageUrl;
    payload.pageUrl = pageUrl;
    payload.formatId = formatId;
    payload.merge = request.merge === true;
  }

  sendDownload(payload)
    .then((result) => sendResponse(result))
    .catch(() => sendResponse({ ok: false, reason: "offline" }));

  return true;
});
