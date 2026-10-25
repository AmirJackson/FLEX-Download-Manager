#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>
#include <mutex>
#include <atomic>
#include <thread>
#include <cstdint>
#include <cstdio>

// ---------------------------------------------------------------------------
// Application identity
// ---------------------------------------------------------------------------
#define APP_NAME        L"FLEX Download Manager"
#define APP_SHORT       L"FLEXDownloadManager"
#define APP_VERSION     L"1.0"
#define APP_CLASS       L"FLEXDownloadManagerMainWnd"
#define APP_REG_ROOT    L"Software\\FLEXDownloadManager"

// Control, dialog and command identifiers live in resource.h so that both the
// C++ sources and the resource compiler see the same values.
#include "resource.h"

// Window messages used internally.
#define WM_APP_UPDATE_ITEM  (WM_APP + 1)
#define WM_APP_REFRESH_ALL  (WM_APP + 2)
#define WM_APP_TRAY         (WM_APP + 3)

// A second launch posts this registered message to the running instance so it
// can bring its main window back from the notification area through its own Qt
// event loop. Forcing ShowWindow() on the hidden HWND from the new process
// leaves Qt unaware of the visibility change, which paints an empty white frame
// that cannot be closed.
inline UINT FlexShowMainWindowMessage() {
    static const UINT message =
        RegisterWindowMessageW(L"FLEXDownloadManagerShowMainWindow");
    return message;
}

// The real main window is tagged with this Win32 property. Looking it up by
// title alone is not reliable: Qt creates hidden top-level helper windows for
// menu shadows that inherit the application title, and they sit above the main
// window in the Z-order.
inline const wchar_t* FlexMainWindowProperty() {
    return L"FLEXDownloadManagerMainWindow";
}


// ---------------------------------------------------------------------------
// common.cpp
// ---------------------------------------------------------------------------
std::wstring Utf8ToWide(const std::string& in);
std::string  WideToUtf8(const std::wstring& in);
std::wstring FormatBytes(uint64_t bytes);
std::wstring FormatSpeed(double bytesPerSecond);
std::wstring FormatDuration(uint64_t seconds);
std::wstring GetErrorMessage(DWORD code);
std::wstring JoinPath(const std::wstring& dir, const std::wstring& name);
std::wstring DirName(const std::wstring& path);
std::wstring FileNameFromPath(const std::wstring& path);
std::wstring SanitizeFileName(const std::wstring& name);
std::wstring GetDefaultDownloadDir();
std::wstring GetTempDir();
std::wstring GetExeDir();
bool FileExists(const std::wstring& path);
bool DirectoryExists(const std::wstring& path);
bool EnsureDirectory(const std::wstring& path);
uint64_t FileSize(const std::wstring& path);
bool DeleteFileQuiet(const std::wstring& path);
uint64_t NowUnixMs();

// ---------------------------------------------------------------------------
// url.cpp
// ---------------------------------------------------------------------------
struct UrlParts {
    std::wstring scheme;   // http, https, ftp
    std::wstring host;
    std::wstring port;     // empty when default
    std::wstring path;     // includes leading '/'
    std::wstring query;
    bool valid = false;

    std::wstring HostWithPort() const;
    std::wstring RequestTarget() const;
    std::wstring FileName() const;
};

UrlParts ParseUrl(const std::wstring& url);
std::wstring UrlEncodePath(const std::wstring& path);

// Extracts a safe file name from a Content-Disposition header. Returns an
// empty string when the header carries no usable name.
std::wstring FileNameFromDisposition(const std::wstring& header);

// ---------------------------------------------------------------------------
// config.cpp
// ---------------------------------------------------------------------------
struct Config {
    int  maxConnections   = 8;
    int  maxRetries       = 5;
    int  connectTimeoutMs = 15000;
    int  readTimeoutMs    = 30000;
    int  minPartSize      = 1024 * 1024;   // minimum split unit in bytes
    int  speedLimitKB     = 0;             // 0 = unlimited
    bool startOnAdd       = false;
    std::wstring userAgent;
    std::wstring saveDir;
    std::wstring tempDir;
    int  proxyMode        = 0;             // 0 = direct, 1 = system, 2 = manual
    std::wstring proxyAddress;
    int  language         = 0;             // 0 = English, 1 = Arabic
    bool showCompleteDialog = true;        // IDM-style completion window
    bool showProgressDialog = true;        // IDM-style download progress window
    bool progressShowDetails = true;       // last "Show/Hide details" choice

    // Base save folder remembered per category key (video, music, ...).
    std::map<std::wstring, std::wstring> categoryPaths;
    // User-defined category names added with the "+" button.
    std::vector<std::wstring> customCategories;
    // Most recently used save folders, newest first.
    std::vector<std::wstring> recentSavePaths;

    void Load();
    void Save() const;
};

// ---------------------------------------------------------------------------
// http.cpp
// ---------------------------------------------------------------------------
struct HttpResponse {
    int      statusCode = 0;
    uint64_t contentLength = UINT64_MAX;   // UINT64_MAX when unknown
    bool     acceptRanges = false;
    bool     chunked = false;
    std::wstring finalUrl;
    std::wstring contentType;
    std::wstring contentDisposition;
    std::wstring etag;
    std::wstring lastModified;
    std::wstring error;
    bool     ok = false;
};

struct RequestOptions {
    std::wstring method = L"GET";
    std::wstring extraHeaders;   // already CRLF terminated
    uint64_t rangeStart = 0;
    uint64_t rangeEnd   = 0;     // inclusive, only used when hasRangeEnd
    bool     useRange   = false;
    bool     hasRangeEnd = false;
};

// Blocking HTTP client built on WinHTTP. One instance per worker thread.
class HttpClient {
public:
    HttpClient();
    ~HttpClient();

    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    bool Open(const Config& cfg);
    void Close();

    // Issues a request and returns headers. When a body sink is supplied the
    // body is streamed to it; the callback receives the HTTP status code and
    // returns false to abort.
    using BodySink = std::function<bool(int statusCode, const uint8_t* data,
                                        size_t len)>;

    HttpResponse Request(const std::wstring& url,
                         const RequestOptions& opts,
                         const BodySink& sink);

    void SetAbort(bool value) { abort_.store(value); }
    bool Aborted() const { return abort_.load(); }

    // Unblocks a pending WinHTTP call from another thread. Safe to call when
    // no request is in flight.
    void Abort();

private:
    // Opaque WinHTTP handles; http.cpp casts them back to HINTERNET.
    void* request_ = nullptr;
    std::mutex handleMutex_;
    void* session_ = nullptr;
    void* connect_ = nullptr;
    Config cfg_;
    std::atomic<bool> abort_{ false };
};

// ---------------------------------------------------------------------------
// download.cpp
// ---------------------------------------------------------------------------
enum class DownloadState {
    Queued,
    Connecting,
    Downloading,
    Paused,
    Complete,
    Error,
    Cancelled,
};

struct Segment {
    uint64_t start = 0;
    uint64_t end   = 0;      // inclusive; meaningful only when openEnded is false
    uint64_t written = 0;
    bool     openEnded = false;
    bool     done = false;
    bool     failed = false;
    int      retries = 0;
};

class DownloadItem {
public:
    DownloadItem();
    ~DownloadItem();

    DownloadItem(const DownloadItem&) = delete;
    DownloadItem& operator=(const DownloadItem&) = delete;

    // Identity and request description.
    uint64_t id = 0;
    std::wstring url;
    std::wstring finalUrl;
    std::wstring fileName;
    std::wstring savePath;
    std::wstring tempDir;      // part-file directory
    std::wstring contentType;
    std::wstring description; // shown in the Description column
    std::wstring category;    // video, music, documents, ... (may be empty)
    HWND notifyWindow = nullptr;

    // True when the file name was guessed from the URL, so a
    // Content-Disposition name may replace it after the probe.
    bool nameFromUrl = false;

    // yt-dlp backed items: the URL is the page that hosts the media and the
    // external tool resolves, downloads and (when needed) merges the streams.
    bool useYtDlp = false;
    std::wstring ytDlpFormat;
    std::wstring ytDlpPageUrl;
    bool ytDlpMerge = false;

    // State shared with the UI.
    std::atomic<int>      state{ (int)DownloadState::Queued };
    std::atomic<uint64_t> totalBytes{ 0 };
    std::atomic<uint64_t> downloadedBytes{ 0 };
    std::atomic<double>   speed{ 0.0 };
    std::atomic<uint64_t> startedAt{ 0 };
    std::atomic<uint64_t> finishedAt{ 0 };
    std::atomic<int>      activeConnections{ 0 };
    std::atomic<int>      connectionsWanted{ 8 };
    std::mutex            errorMutex;
    std::wstring          errorText;

    bool resumable = false;

    void Start(const Config& cfg);
    void Stop();
    void WaitForStop();
    void CleanupTemp();

    DownloadState GetState() const { return (DownloadState)state.load(); }
    void SetError(const std::wstring& text);
    std::wstring GetError();

    // Copy of the range state, used by the download progress window to list
    // each connection. The worker updates segments_ under segmentsMutex_.
    std::vector<Segment> SegmentSnapshot();

    // The worker may rename the file after reading Content-Disposition, so
    // these are read through a lock.
    std::wstring GetFileName();
    std::wstring GetSavePath();
    void SetFileName(const std::wstring& name);

    bool IsRunning() const;

private:
    void RunWorker(Config cfg);
    // YouTube and other yt-dlp supported pages.
    void RunYtDlp(Config cfg);
    // Downloads every pending segment using a bounded worker pool.
    void RunSegments(Config& cfg);
    bool ProbeRemote(Config& cfg, HttpClient& client);
    void BuildSegments(uint64_t total, int connections, uint64_t minPart);
    bool DownloadSegment(Config& cfg, size_t index);
    bool AssembleFile();
    void UpdateProgress();
    void PersistState();

    std::thread worker_;
    std::atomic<bool> stopRequested_{ false };
    std::atomic<bool> rangeRejected_{ false };
    std::mutex workerMutex_;

    // Guards the URL-derived identity fields, which the worker can update
    // after the probe.
    mutable std::mutex nameMutex_;

    // In-flight transports, so Stop() can unblock them immediately instead of
    // waiting for the next socket read.
    std::mutex clientsMutex_;
    std::vector<HttpClient*> clients_;
    void AttachClient(HttpClient& client);
    void DetachClient(HttpClient& client);
    std::vector<Segment> segments_;
    std::mutex segmentsMutex_;
    std::mutex progressMutex_;
    uint64_t lastProgressBytes_ = 0;
    uint64_t lastProgressTick_ = 0;
    uint64_t lastNotifyTick_ = 0;};

// ---------------------------------------------------------------------------
// Download manager (process-wide)
// ---------------------------------------------------------------------------
class DownloadManager {
public:
    static DownloadManager& Instance();

    void LoadSettings();
    const Config& Settings() const { return cfg_; }
    Config& MutableSettings() { return cfg_; }
    void SaveSettings();

    std::shared_ptr<DownloadItem> Add(const std::wstring& url,
                                      const std::wstring& saveDir,
                                      const std::wstring& fileName,
                                      int connections,
                                      bool startNow,
                                      const std::wstring& description = L"",
                                      const std::wstring& category = L"");

    // Adds a download that is handed to yt-dlp instead of the HTTP engine.
    // `format` is a yt-dlp format selector; `merge` marks streams that only
    // become one file after ffmpeg muxes video and audio.
    std::shared_ptr<DownloadItem> AddYtDlp(const std::wstring& pageUrl,
                                           const std::wstring& saveDir,
                                           const std::wstring& fileName,
                                           const std::wstring& format,
                                           bool merge, bool startNow,
                                           const std::wstring& description = L"",
                                           const std::wstring& category = L"");

    void Remove(uint64_t id, bool deleteFile);
    std::shared_ptr<DownloadItem> Find(uint64_t id);
    std::vector<std::shared_ptr<DownloadItem>> Snapshot();
    void StartAll();
    void StopAll();
    size_t Count() const;

    // Stops every worker and releases the items so their threads are joined
    // before the process tears down.
    void Shutdown();

    HWND MainWindow() const { return mainWnd_; }
    void SetMainWindow(HWND wnd) { mainWnd_ = wnd; }
    void NotifyChanged();

private:
    DownloadManager() = default;
    mutable std::mutex mutex_;
    std::vector<std::shared_ptr<DownloadItem>> items_;
    uint64_t nextId_ = 1;
    Config cfg_;
    HWND mainWnd_ = nullptr;
};

// ---------------------------------------------------------------------------
// Qt Widgets UI (see ui_qt.h)
// ---------------------------------------------------------------------------
struct AddDownloadResult {
    std::wstring url;
    std::wstring saveDir;
    std::wstring fileName;
    int connections = 8;
    bool startNow = true;
    bool accepted = false;
};

// Result of the IDM-style "Download File Info" dialog.
struct DownloadInfoResult {
    std::wstring url;
    std::wstring fileName;
    std::wstring savePath;
    std::wstring categoryKey;
    std::wstring categoryName;
    std::wstring description;
    std::wstring rememberPath;
    // Updated history of save folders to persist when the dialog is accepted.
    std::vector<std::wstring> recentSavePaths;
    uint64_t totalBytes = 0;
    int connections = 8;
    bool startNow = true;
    bool remember = false;
    bool accepted = false;
};

// A download forwarded by the Chrome extension. The bridge parses the JSON
// payload into this plain structure so the UI does not depend on QtNetwork.
struct ChromeDownloadRequest {
    std::wstring url;
    std::wstring fileName;
    std::wstring referrer;
    std::wstring mime;
    std::wstring description;
    std::wstring category;
    uint64_t totalBytes = 0;
    // Set for yt-dlp supported pages (YouTube and friends): the requested
    // format selector plus the page that owns it.
    std::wstring formatId;
    std::wstring pageUrl;
    bool merge = false;
};

// Maps a file name / MIME type to one of the category keys used by the tree.
std::wstring InferCategory(const std::wstring& fileName,
                           const std::wstring& mime = std::wstring());
