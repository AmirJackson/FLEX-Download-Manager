#include "common.h"
#include "ytdlp.h"

#include <algorithm>
#include <cstring>

namespace {

// Runs a cleanup action when the enclosing scope ends.
struct ScopeExit {
    std::function<void()> action;
    ~ScopeExit() {
        if (action) action();
    }
};

HANDLE OpenPartFile(const std::wstring& path) {
    return CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                       FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                       nullptr);
}

bool WriteAt(HANDLE file, uint64_t offset, const uint8_t* data, size_t len) {
    LARGE_INTEGER pos{};
    pos.QuadPart = (LONGLONG)offset;
    if (!SetFilePointerEx(file, pos, nullptr, FILE_BEGIN)) return false;
    while (len > 0) {
        DWORD written = 0;
        DWORD chunk = (DWORD)std::min<size_t>(len, 1u << 20);
        if (!WriteFile(file, data, chunk, &written, nullptr)) return false;
        if (written == 0) return false;
        data += written;
        len -= written;
    }
    return true;
}

bool AppendFromFile(HANDLE dest, HANDLE src) {
    LARGE_INTEGER zero{};
    if (!SetFilePointerEx(src, zero, nullptr, FILE_BEGIN)) return false;

    std::vector<uint8_t> buffer(1 << 20);
    for (;;) {
        DWORD read = 0;
        if (!ReadFile(src, buffer.data(), (DWORD)buffer.size(), &read, nullptr)) {
            return false;
        }
        if (read == 0) return true;
        DWORD written = 0;
        if (!WriteFile(dest, buffer.data(), read, &written, nullptr)) return false;
        if (written != read) return false;
    }
}

// The resume record lives beside the part files. A later run can only reuse it
// when the URL and the reported total size still match.
struct PersistedState {
    uint32_t magic = 0x314D4449;  // 'IDM1'
    uint32_t version = 2;
    uint64_t id = 0;
    uint64_t total = 0;
    uint32_t segmentCount = 0;
    uint32_t resumable = 0;
    wchar_t url[1024] = {};
};

struct SegmentRecord {
    uint64_t start = 0;
    uint64_t end = 0;
    uint64_t written = 0;
    uint32_t openEnded = 0;
};

std::wstring PartPath(const std::wstring& tempDir, uint64_t id, size_t index) {
    wchar_t name[64];
    swprintf_s(name, L"%llu_%04zu.part", (unsigned long long)id, index);
    return JoinPath(tempDir, name);
}

std::wstring StatePath(const std::wstring& tempDir, uint64_t id) {
    wchar_t name[64];
    swprintf_s(name, L"%llu.state", (unsigned long long)id);
    return JoinPath(tempDir, name);
}

bool SaveState(const std::wstring& path, const PersistedState& state,
               const std::vector<SegmentRecord>& segments) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = WriteFile(file, &state, sizeof(state), &written, nullptr) != FALSE &&
              written == sizeof(state);
    for (size_t i = 0; ok && i < segments.size(); ++i) {
        ok = WriteFile(file, &segments[i], sizeof(SegmentRecord), &written,
                       nullptr) != FALSE &&
             written == sizeof(SegmentRecord);
    }
    CloseHandle(file);
    return ok;
}

bool LoadState(const std::wstring& path, PersistedState& state,
               std::vector<SegmentRecord>& segments) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD read = 0;
    bool ok = ReadFile(file, &state, sizeof(state), &read, nullptr) != FALSE &&
              read == sizeof(state) && state.magic == 0x314D4449 &&
              state.version == 2 && state.segmentCount > 0 &&
              state.segmentCount <= 4096;
    if (ok) {
        segments.resize(state.segmentCount);
        const DWORD want = (DWORD)(segments.size() * sizeof(SegmentRecord));
        ok = ReadFile(file, segments.data(), want, &read, nullptr) != FALSE &&
             read == want;
    }
    CloseHandle(file);
    return ok;
}

// Picks a destination path that does not overwrite an existing file. The
// original tool applies the same "name (n).ext" convention.
std::wstring UniquePath(const std::wstring& dir, const std::wstring& name) {
    std::wstring candidate = JoinPath(dir, name);
    if (!FileExists(candidate)) return candidate;

    std::wstring stem = name;
    std::wstring ext;
    size_t dot = name.find_last_of(L'.');
    if (dot != std::wstring::npos && dot > 0) {
        stem = name.substr(0, dot);
        ext = name.substr(dot);
    }
    for (int i = 1; i < 1000; ++i) {
        wchar_t suffix[32];
        swprintf_s(suffix, L" (%d)", i);
        candidate = JoinPath(dir, stem + suffix + ext);
        if (!FileExists(candidate)) return candidate;
    }
    return JoinPath(dir, name);
}

}  // namespace

// ---------------------------------------------------------------------------
// DownloadItem
// ---------------------------------------------------------------------------

DownloadItem::DownloadItem() = default;

DownloadItem::~DownloadItem() {
    Stop();
    WaitForStop();
    // Without a persisted download list the part files cannot be resumed in a
    // later session, so they are always reclaimed here.
    CleanupTemp();
}

void DownloadItem::SetError(const std::wstring& text) {
    std::lock_guard<std::mutex> lock(errorMutex);
    errorText = text;
}

std::wstring DownloadItem::GetError() {
    std::lock_guard<std::mutex> lock(errorMutex);
    return errorText;
}

std::vector<Segment> DownloadItem::SegmentSnapshot() {
    std::lock_guard<std::mutex> lock(segmentsMutex_);
    return segments_;
}

std::wstring DownloadItem::GetFileName() {
    std::lock_guard<std::mutex> lock(nameMutex_);
    return fileName;
}

std::wstring DownloadItem::GetSavePath() {
    std::lock_guard<std::mutex> lock(nameMutex_);
    return savePath;
}

void DownloadItem::SetFileName(const std::wstring& name) {
    std::lock_guard<std::mutex> lock(nameMutex_);
    fileName = name;
}

bool DownloadItem::IsRunning() const {
    DownloadState s = GetState();
    return s == DownloadState::Connecting || s == DownloadState::Downloading;
}

void DownloadItem::Start(const Config& cfg) {
    std::lock_guard<std::mutex> lock(workerMutex_);
    if (worker_.joinable()) {
        if (IsRunning()) return;
        worker_.join();
    }
    stopRequested_.store(false);
    SetError(L"");
    state.store((int)DownloadState::Connecting);
    startedAt.store(NowUnixMs());
    finishedAt.store(0);
    {
        std::lock_guard<std::mutex> progressLock(progressMutex_);
        lastProgressBytes_ = downloadedBytes.load();
        lastProgressTick_ = NowUnixMs();
        lastNotifyTick_ = 0;
    }
    speed.store(0.0);
    worker_ = std::thread(&DownloadItem::RunWorker, this, cfg);
}

void DownloadItem::Stop() {
    stopRequested_.store(true);
    // Abort under the lock so a worker cannot destroy a client mid-call.
    std::lock_guard<std::mutex> lock(clientsMutex_);
    for (HttpClient* client : clients_) client->Abort();
}

void DownloadItem::AttachClient(HttpClient& client) {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    clients_.push_back(&client);
}

void DownloadItem::DetachClient(HttpClient& client) {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    clients_.erase(std::remove(clients_.begin(), clients_.end(), &client),
                   clients_.end());
}

void DownloadItem::WaitForStop() {
    if (worker_.joinable()) worker_.join();
}

void DownloadItem::CleanupTemp() {
    size_t count = 0;
    {
        std::lock_guard<std::mutex> lock(segmentsMutex_);
        count = segments_.size();
    }
    if (count == 0) count = 1;
    for (size_t i = 0; i < count; ++i) {
        DeleteFileQuiet(PartPath(tempDir, id, i));
    }
    DeleteFileQuiet(StatePath(tempDir, id));
}

void DownloadItem::PersistState() {
    std::vector<SegmentRecord> records;
    {
        std::lock_guard<std::mutex> lock(segmentsMutex_);
        records.reserve(segments_.size());
        for (const auto& seg : segments_) {
            SegmentRecord rec;
            rec.start = seg.start;
            rec.end = seg.end;
            rec.written = seg.written;
            rec.openEnded = seg.openEnded ? 1u : 0u;
            records.push_back(rec);
        }
    }
    if (records.empty()) return;

    PersistedState st;
    st.id = id;
    st.total = totalBytes.load();
    st.segmentCount = (uint32_t)records.size();
    st.resumable = resumable ? 1 : 0;
    wcsncpy_s(st.url, url.c_str(), _TRUNCATE);
    SaveState(StatePath(tempDir, id), st, records);
}

bool DownloadItem::ProbeRemote(Config& cfg, HttpClient& client) {
    (void)cfg;

    // A two-byte ranged GET is enough to learn whether ranges are honoured
    // without pulling the whole object down.
    RequestOptions probe;
    probe.useRange = true;
    probe.hasRangeEnd = true;
    probe.rangeStart = 0;
    probe.rangeEnd = 1;

    HttpResponse head = client.Request(url, probe,
        [](int, const uint8_t*, size_t) { return false; });

    if (head.statusCode == 0) {
        SetError(head.error.empty() ? L"Connection failed" : head.error);
        return false;
    }

    finalUrl = head.finalUrl.empty() ? url : head.finalUrl;
    if (!head.contentType.empty()) contentType = head.contentType;

    // Prefer a server-provided name unless the caller supplied one explicitly.
    if (nameFromUrl && !head.contentDisposition.empty()) {
        std::wstring suggested = FileNameFromDisposition(head.contentDisposition);
        if (!suggested.empty() && suggested != fileName) {
            const std::wstring dir = DirName(savePath);
            SetFileName(suggested);
            if (!dir.empty()) {
                std::lock_guard<std::mutex> lock(nameMutex_);
                savePath = JoinPath(dir, suggested);
            }
        }
    }

    if (head.statusCode == 206) {
        // Content-Range supplied the full object size.
        resumable = true;
        totalBytes.store(head.contentLength == UINT64_MAX ? 0 : head.contentLength);
        return true;
    }
    if (head.statusCode == 200) {
        resumable = head.acceptRanges && !head.chunked;
        totalBytes.store(head.contentLength == UINT64_MAX ? 0 : head.contentLength);
        return true;
    }
    if (head.statusCode >= 400 && head.statusCode != 416) {
        std::wstring detail = head.error;
        // A sink that stops early reports "Cancelled"; that is our own probe,
        // not a transport problem worth surfacing.
        if (detail == L"Cancelled") detail.clear();
        SetError(L"HTTP " + std::to_wstring(head.statusCode) +
                 (detail.empty() ? L"" : L": " + detail));
        return false;
    }

    // 416 or an unexpected redirect status: retry without a range header.
    RequestOptions plain;
    HttpResponse resp = client.Request(url, plain,
        [](int, const uint8_t*, size_t) { return false; });
    if (resp.statusCode >= 200 && resp.statusCode < 300) {
        resumable = resp.acceptRanges && !resp.chunked;
        totalBytes.store(resp.contentLength == UINT64_MAX ? 0
                                                         : resp.contentLength);
        if (!resp.contentType.empty()) contentType = resp.contentType;
        return true;
    }
    SetError(L"HTTP " + std::to_wstring(resp.statusCode));
    return false;
}

void DownloadItem::BuildSegments(uint64_t total, int connections,
                                 uint64_t minPart) {
    std::lock_guard<std::mutex> lock(segmentsMutex_);
    segments_.clear();

    if (total == 0) {
        // Unknown length: one sequential stream.
        Segment seg;
        seg.start = 0;
        seg.openEnded = true;
        segments_.push_back(seg);
        return;
    }

    if (minPart < 64 * 1024) minPart = 64 * 1024;
    uint64_t parts = (uint64_t)std::max(1, connections);
    if (total / parts < minPart) {
        parts = std::max<uint64_t>(1, total / minPart);
    }
    parts = std::min<uint64_t>(parts, 64);
    if (parts == 0) parts = 1;

    const uint64_t chunk = total / parts;
    uint64_t offset = 0;
    for (uint64_t i = 0; i < parts; ++i) {
        Segment seg;
        seg.start = offset;
        seg.end = (i + 1 == parts) ? (total - 1) : (offset + chunk - 1);
        offset = seg.end + 1;
        segments_.push_back(seg);
    }
}

bool DownloadItem::DownloadSegment(Config& cfg, size_t index) {
    uint64_t rangeStart = 0, rangeEnd = 0, written = 0;
    uint64_t lastPublishedBytes = 0;
    uint64_t lastPublishedTick = NowUnixMs();
    bool openEnded = false;
    {
        std::lock_guard<std::mutex> lock(segmentsMutex_);
        if (index >= segments_.size()) return false;
        rangeStart = segments_[index].start;
        rangeEnd = segments_[index].end;
        written = segments_[index].written;
        openEnded = segments_[index].openEnded;
    }

    const std::wstring partPath = PartPath(tempDir, id, index);
    HANDLE file = OpenPartFile(partPath);
    if (file == INVALID_HANDLE_VALUE) {
        SetError(L"Cannot create temporary part file: " +
                 GetErrorMessage(GetLastError()));
        return false;
    }

    const uint64_t capacity = openEnded ? 0 : (rangeEnd - rangeStart + 1);
    if (!openEnded && written >= capacity) {
        // Restored from the resume record: nothing left to fetch.
        CloseHandle(file);
        std::lock_guard<std::mutex> lock(segmentsMutex_);
        if (index < segments_.size()) {
            segments_[index].done = true;
            segments_[index].failed = false;
        }
        return true;
    }

    // Without range support the only safe restart point is the beginning, so
    // discard anything already written for this segment.
    if (!resumable && written > 0) {
        LARGE_INTEGER zero{};
        SetFilePointerEx(file, zero, nullptr, FILE_BEGIN);
        SetEndOfFile(file);
        downloadedBytes.fetch_sub(written);
        written = 0;
        std::lock_guard<std::mutex> lock(segmentsMutex_);
        if (index < segments_.size()) segments_[index].written = 0;
    }

    const int maxAttempts = std::max(1, cfg.maxRetries + 1);
    const uint64_t limitBytesPerSec =
        cfg.speedLimitKB > 0 ? (uint64_t)cfg.speedLimitKB * 1024ull : 0ull;
    int attempts = 0;
    bool success = false;

    while (!success && attempts < maxAttempts && !stopRequested_.load()) {
        ++attempts;

        HttpClient client;
        if (!client.Open(cfg)) {
            SetError(L"Cannot initialize HTTP session");
            break;
        }
        AttachClient(client);
        ScopeExit guard{ [this, &client] { DetachClient(client); } };

        RequestOptions opts;
        opts.useRange = resumable;
        opts.rangeStart = rangeStart + written;
        opts.rangeEnd = rangeEnd;
        opts.hasRangeEnd = !openEnded;

        const uint64_t writtenAtStart = written;
        int status = 0;
        bool rangeRejected = false;
        bool diskError = false;
        std::wstring transportError;
        const uint64_t sessionStart = NowUnixMs();

        HttpResponse resp = client.Request(url, opts,
            [&](int code, const uint8_t* data, size_t len) -> bool {
                if (stopRequested_.load()) return false;
                if (status == 0) status = code;

                // A 200 reply to a ranged request means the server ignored the
                // range. Only the first segment may consume such a stream.
                if (status == 200 && index > 0 && opts.useRange) {
                    rangeRejected = true;
                    return false;
                }

                if (!openEnded) {
                    if (written >= capacity) return false;
                    if (written + len > capacity) {
                        len = (size_t)(capacity - written);
                    }
                }
                if (len == 0) return true;
                if (!WriteAt(file, written, data, len)) {
                    SetError(L"Disk write failed: " +
                             GetErrorMessage(GetLastError()));
                    diskError = true;
                    return false;
                }
                written += len;
                downloadedBytes.fetch_add(len);

                // Publish live per-connection progress for the Download
                // progress window. Updating the shared vector on every chunk
                // would contend with the other workers, so throttle it.
                if (written < lastPublishedBytes ||
                    written - lastPublishedBytes >= 256 * 1024 ||
                    NowUnixMs() - lastPublishedTick >= 200) {
                    {
                        std::lock_guard<std::mutex> lock(segmentsMutex_);
                        if (index < segments_.size()) {
                            segments_[index].written = written;
                        }
                    }
                    lastPublishedBytes = written;
                    lastPublishedTick = NowUnixMs();
                }

                if (limitBytesPerSec > 0) {
                    // Simple pacing: keep the average throughput at the limit
                    // by sleeping whenever this session runs ahead of schedule.
                    const uint64_t elapsedMs = NowUnixMs() - sessionStart;
                    const uint64_t targetMs =
                        ((written - writtenAtStart) * 1000ull) /
                        limitBytesPerSec;
                    if (targetMs > elapsedMs) {
                        const uint64_t sleepMs =
                            std::min<uint64_t>(targetMs - elapsedMs, 200);
                        if (sleepMs > 0) Sleep((DWORD)sleepMs);
                    }
                }
                return true;
            });

        if (status == 0) status = resp.statusCode;
        if (resp.error != L"Cancelled") transportError = resp.error;

        {
            std::lock_guard<std::mutex> lock(segmentsMutex_);
            if (index < segments_.size()) {
                segments_[index].written = written;
                segments_[index].retries = attempts - 1;
            }
        }

        if (stopRequested_.load() || diskError) break;

        const bool acceptable = (status == 200 || status == 206);
        const bool complete = openEnded ? (written > 0) : (written >= capacity);

        // A truncated body leaves resp.error set even though the status line
        // looked fine, so require a clean transfer before accepting the part.
        if (acceptable && complete && transportError.empty()) {
            success = true;
            break;
        }

        if (rangeRejected) {
            // Remember this so the whole download can restart as a single
            // sequential stream instead of failing every segment.
            rangeRejected_.store(true);
            SetError(L"Server ignored the requested byte range");
        } else if (!transportError.empty()) {
            SetError(transportError);
        } else if (status >= 400) {
            SetError(L"HTTP " + std::to_wstring(status));
        } else if (status != 0) {
            SetError(L"Transfer interrupted (HTTP " + std::to_wstring(status) + L")");
        }

        // Back off before retrying while remaining responsive to Stop().
        for (int i = 0; i < 10 && !stopRequested_.load(); ++i) Sleep(200);
    }

    CloseHandle(file);

    std::lock_guard<std::mutex> lock(segmentsMutex_);
    if (index < segments_.size()) {
        segments_[index].written = written;
        segments_[index].done = success;
        segments_[index].failed = !success;
    }
    return success;
}

bool DownloadItem::AssembleFile() {
    std::vector<Segment> copy;
    {
        std::lock_guard<std::mutex> lock(segmentsMutex_);
        copy = segments_;
    }
    for (const auto& seg : copy) {
        if (!seg.done) return false;
    }

    std::wstring target;
    {
        std::lock_guard<std::mutex> lock(nameMutex_);
        target = savePath;
    }

    const std::wstring dir = DirName(target);
    if (!dir.empty() && !EnsureDirectory(dir)) {
        SetError(L"Cannot create destination directory: " + dir);
        return false;
    }

    HANDLE dest = CreateFileW(target.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (dest == INVALID_HANDLE_VALUE) {
        SetError(L"Cannot create output file: " +
                 GetErrorMessage(GetLastError()));
        return false;
    }

    bool ok = true;
    for (size_t i = 0; i < copy.size(); ++i) {
        const std::wstring part = PartPath(tempDir, id, i);
        HANDLE src = CreateFileW(part.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                 nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                                 nullptr);
        if (src == INVALID_HANDLE_VALUE) {
            ok = false;
            break;
        }
        if (!AppendFromFile(dest, src)) ok = false;
        CloseHandle(src);
        if (!ok) break;
    }
    CloseHandle(dest);

    if (!ok) {
        DeleteFileQuiet(target);
        SetError(L"Failed to assemble downloaded parts");
        return false;
    }

    CleanupTemp();
    return true;
}

void DownloadItem::UpdateProgress() {
    const uint64_t bytes = downloadedBytes.load();
    const uint64_t now = NowUnixMs();
    bool notify = false;

    {
        std::lock_guard<std::mutex> lock(progressMutex_);
        if (lastProgressTick_ != 0 && now > lastProgressTick_) {
            const double seconds = (now - lastProgressTick_) / 1000.0;
            if (seconds >= 0.25) {
                const double instant = (bytes >= lastProgressBytes_)
                                           ? (bytes - lastProgressBytes_) / seconds
                                           : 0.0;
                const double previous = speed.load();
                // Light smoothing keeps the display readable.
                speed.store(previous <= 0.0 ? instant
                                            : previous * 0.6 + instant * 0.4);
                lastProgressBytes_ = bytes;
                lastProgressTick_ = now;
            }
        }
        // Repainting on every worker callback would flood the message queue.
        if (now - lastNotifyTick_ >= 150) {
            lastNotifyTick_ = now;
            notify = true;
        }
    }

    if (notify && notifyWindow) {
        PostMessageW(notifyWindow, WM_APP_UPDATE_ITEM, (WPARAM)id, 0);
    }
}

void DownloadItem::RunSegments(Config& cfg) {
    size_t total = 0;
    {
        std::lock_guard<std::mutex> lock(segmentsMutex_);
        total = segments_.size();
    }
    if (total == 0) return;

    std::atomic<size_t> nextIndex{ 0 };
    const int wanted = resumable ? connectionsWanted.load() : 1;
    const size_t workers = std::min<size_t>(total, (size_t)std::max(1, wanted));
    std::atomic<bool> finished{ false };

    std::vector<std::thread> pool;
    pool.reserve(workers);
    for (size_t w = 0; w < workers; ++w) {
        pool.emplace_back([&]() {
            for (;;) {
                if (stopRequested_.load()) return;
                const size_t index = nextIndex.fetch_add(1);
                if (index >= total) return;
                activeConnections.fetch_add(1);
                DownloadSegment(cfg, index);
                activeConnections.fetch_sub(1);
                UpdateProgress();
            }
        });
    }

    std::thread ticker([&]() {
        while (!stopRequested_.load() && !finished.load()) {
            UpdateProgress();
            for (int i = 0; i < 4; ++i) {
                if (stopRequested_.load() || finished.load()) return;
                Sleep(50);
            }
        }
    });

    for (auto& t : pool) t.join();
    // The ticker waits on either flag; raising the local one keeps
    // stopRequested_ meaningful for the caller.
    finished.store(true);
    ticker.join();
}

void DownloadItem::RunWorker(Config cfg) {
    if (useYtDlp) {
        RunYtDlp(std::move(cfg));
        return;
    }

    EnsureDirectory(tempDir);

    HttpClient probe;
    if (!probe.Open(cfg)) {
        SetError(L"Cannot initialize HTTP session");
        state.store((int)DownloadState::Error);
        UpdateProgress();
        return;
    }
    {
        AttachClient(probe);
        ScopeExit guard{ [this, &probe] { DetachClient(probe); } };

        state.store((int)DownloadState::Connecting);
        UpdateProgress();

        if (!ProbeRemote(cfg, probe)) {
            state.store((int)(stopRequested_.load() ? DownloadState::Paused
                                                    : DownloadState::Error));
            UpdateProgress();
            return;
        }
    }
    if (stopRequested_.load()) {
        state.store((int)DownloadState::Paused);
        UpdateProgress();
        return;
    }

    // Reuse a persisted plan only when it still describes this download.
    bool resumed = false;
    {
        PersistedState st;
        std::vector<SegmentRecord> records;
        if (LoadState(StatePath(tempDir, id), st, records) &&
            st.total == totalBytes.load() && st.id == id && url == st.url) {
            std::lock_guard<std::mutex> lock(segmentsMutex_);
            segments_.clear();
            uint64_t sum = 0;
            for (const auto& rec : records) {
                Segment seg;
                seg.start = rec.start;
                seg.end = rec.end;
                seg.written = rec.written;
                seg.openEnded = rec.openEnded != 0;
                segments_.push_back(seg);
                sum += rec.written;
            }
            resumable = st.resumable != 0;
            downloadedBytes.store(sum);
            resumed = true;
        }
    }

    if (!resumed) {
        BuildSegments(totalBytes.load(),
                      resumable ? connectionsWanted.load() : 1,
                      (uint64_t)std::max(64 * 1024, cfg.minPartSize));
        downloadedBytes.store(0);
        CleanupTemp();
    }
    PersistState();

    state.store((int)DownloadState::Downloading);
    UpdateProgress();

    // If the server advertises byte ranges but ignores them, the parallel
    // attempt fails. Fall back once to a single sequential stream.
    for (int pass = 0; pass < 2; ++pass) {
        rangeRejected_.store(false);
        RunSegments(cfg);

        if (rangeRejected_.load() && pass == 0 && !stopRequested_.load()) {
            resumable = false;
            CleanupTemp();
            BuildSegments(totalBytes.load(), 1,
                          (uint64_t)std::max(64 * 1024, cfg.minPartSize));
            downloadedBytes.store(0);
            PersistState();
            continue;
        }
        break;
    }

    bool allDone = true;
    {
        std::lock_guard<std::mutex> lock(segmentsMutex_);
        for (const auto& seg : segments_) {
            if (!seg.done) allDone = false;
        }
    }
    const bool wasStopped = stopRequested_.load();

    if (!allDone) {
        PersistState();
        state.store((int)(wasStopped ? DownloadState::Paused
                                     : DownloadState::Error));
        speed.store(0.0);
        UpdateProgress();
        return;
    }

    if (AssembleFile()) {
        const uint64_t size = FileSize(GetSavePath());
        downloadedBytes.store(size);
        if (totalBytes.load() == 0) totalBytes.store(size);
        finishedAt.store(NowUnixMs());
        speed.store(0.0);
        state.store((int)DownloadState::Complete);
    } else {
        speed.store(0.0);
        state.store((int)DownloadState::Error);
    }
    UpdateProgress();
}

// ---------------------------------------------------------------------------
// yt-dlp downloads (YouTube and the other sites the tool understands)
// ---------------------------------------------------------------------------

namespace {

// One Windows command-line argument, quoted so paths with spaces survive.
std::wstring QuoteArgument(const std::wstring& value) {
    std::wstring out = L"\"";
    size_t backslashes = 0;
    for (wchar_t ch : value) {
        if (ch == L'\\') {
            ++backslashes;
            continue;
        }
        if (ch == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
            out += L'"';
            backslashes = 0;
            continue;
        }
        out.append(backslashes, L'\\');
        backslashes = 0;
        out += ch;
    }
    out.append(backslashes * 2, L'\\');
    out += L'"';
    return out;
}

// Reads the leading digits of a progress field; "NA" yields 0.
uint64_t ParseCount(const std::string& text) {
    size_t i = 0;
    while (i < text.size() && (text[i] < '0' || text[i] > '9')) ++i;
    uint64_t value = 0;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
        value = value * 10 + (uint64_t)(text[i] - '0');
        ++i;
    }
    return value;
}

}  // namespace

void DownloadItem::RunYtDlp(Config cfg) {
    const std::wstring tool = FindYtDlp();
    if (tool.empty()) {
        SetError(L"yt-dlp.exe was not found. Put it next to the app or in its "
                 L"tools folder, then retry.");
        state.store((int)DownloadState::Error);
        UpdateProgress();
        return;
    }

    std::wstring ffmpeg = FindFfmpeg();
    if (ytDlpMerge && ffmpeg.empty()) {
        // High-resolution streams arrive as separate video and audio tracks;
        // fetch the muxer once, then it stays in the per-user tool folder.
        HttpClient client;
        if (!client.Open(cfg)) {
            SetError(L"Cannot initialize the HTTP session");
            state.store((int)DownloadState::Error);
            UpdateProgress();
            return;
        }

        AttachClient(client);
        ScopeExit detach{ [this, &client] { DetachClient(client); } };

        std::wstring installError;
        const bool installed = InstallFfmpeg(
            client,
            [this](uint64_t done, uint64_t total) {
                totalBytes.store(total);
                downloadedBytes.store(done);
                UpdateProgress();
            },
            stopRequested_, installError);

        if (stopRequested_.load()) {
            state.store((int)DownloadState::Paused);
            UpdateProgress();
            return;
        }
        if (!installed) {
            SetError(installError.empty() ? L"Cannot install ffmpeg"
                                          : installError);
            state.store((int)DownloadState::Error);
            UpdateProgress();
            return;
        }

        ffmpeg = FindFfmpeg();
        if (ffmpeg.empty()) {
            SetError(L"ffmpeg was installed but cannot be located");
            state.store((int)DownloadState::Error);
            UpdateProgress();
            return;
        }
    }

    const std::wstring target = GetSavePath();
    const std::wstring dir = DirName(target);
    EnsureDirectory(dir);

    std::wstring base = FileNameFromPath(target);
    const size_t dot = base.find_last_of(L'.');
    if (dot != std::wstring::npos && dot > 0) base = base.substr(0, dot);
    if (base.empty()) base = L"video";

    std::wstring args =
        L"--ignore-config --no-playlist --no-warnings --no-colors --newline ";
    args += L"--progress-template "
            L"\"dl:%(progress.downloaded_bytes)s|"
            L"%(progress.total_bytes_estimate)s|%(progress.speed)s\" ";
    args += L"-f " + QuoteArgument(ytDlpFormat) + L" ";
    if (ytDlpMerge && !ffmpeg.empty()) {
        args += L"--merge-output-format mp4 ";
    }
    if (!ffmpeg.empty()) {
        args += L"--ffmpeg-location " + QuoteArgument(DirName(ffmpeg)) + L" ";
    }
    if (cfg.speedLimitKB > 0) {
        args += L"--limit-rate " + std::to_wstring(cfg.speedLimitKB) + L"K ";
    }
    if (cfg.proxyMode == 2 && !cfg.proxyAddress.empty()) {
        args += L"--proxy " + QuoteArgument(cfg.proxyAddress) + L" ";
    }
    args += L"--retries 5 --fragment-retries 5 --socket-timeout 20 "
            L"--concurrent-fragments 4 ";
    args += L"-o " + QuoteArgument(JoinPath(dir, base + L".%(ext)s")) + L" ";
    args += QuoteArgument(ytDlpPageUrl);

    downloadedBytes.store(0);
    totalBytes.store(0);
    state.store((int)DownloadState::Connecting);
    UpdateProgress();

    std::string tail;
    const auto onLine = [this, &tail](const std::string& line) {
        const size_t marker = line.find("dl:");
        if (marker != std::string::npos) {
            const std::string payload = line.substr(marker + 3);
            const size_t first = payload.find('|');
            const size_t second =
                first == std::string::npos ? std::string::npos
                                           : payload.find('|', first + 1);
            if (first != std::string::npos) {
                const uint64_t done =
                    ParseCount(payload.substr(0, first));
                const uint64_t total = second == std::string::npos
                                           ? 0
                                           : ParseCount(payload.substr(
                                                 first + 1, second - first - 1));
                if (done > 0) downloadedBytes.store(done);
                if (total > 0) totalBytes.store(total);
                UpdateProgress();
            }
            return;
        }

        // Progress/report lines are noisy; only keep the tail for errors.
        if (line.rfind("[download] ", 0) == 0 &&
            line.find("Destination") != std::string::npos) {
            return;
        }
        tail += line;
        tail += '\n';
        if (tail.size() > 1500) tail.erase(0, tail.size() - 1500);
    };

    ConsoleProcess process;
    if (!process.Start(tool, args)) {
        SetError(L"Cannot start yt-dlp.exe");
        state.store((int)DownloadState::Error);
        UpdateProgress();
        return;
    }

    state.store((int)DownloadState::Downloading);
    UpdateProgress();

    const int code = process.Pump(onLine, stopRequested_);

    if (stopRequested_.load()) {
        speed.store(0.0);
        state.store((int)DownloadState::Paused);
        UpdateProgress();
        return;
    }

    if (code != 0) {
        std::wstring text = L"yt-dlp failed";
        if (!tail.empty()) text += L": " + Utf8ToWide(tail);
        else text += L" (exit code " + std::to_wstring(code) + L")";
        SetError(text);
        speed.store(0.0);
        state.store((int)DownloadState::Error);
        UpdateProgress();
        return;
    }

    // yt-dlp picks the container and appends its extension, so the final name
    // can differ from what the dialog proposed.
    std::wstring produced;
    uint64_t newest = 0;
    WIN32_FIND_DATAW data = {};
    HANDLE find = FindFirstFileW(JoinPath(dir, base + L".*").c_str(), &data);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            std::wstring name = data.cFileName;
            if (name.size() > 4 &&
                _wcsicmp(name.c_str() + name.size() - 4, L"part") == 0) {
                continue;
            }
            const uint64_t stamp =
                ((uint64_t)data.ftLastWriteTime.dwHighDateTime << 32) |
                data.ftLastWriteTime.dwLowDateTime;
            if (stamp >= newest) {
                newest = stamp;
                produced = JoinPath(dir, name);
            }
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }

    if (!produced.empty()) {
        {
            std::lock_guard<std::mutex> lock(nameMutex_);
            savePath = produced;
            fileName = FileNameFromPath(produced);
        }
    }

    const uint64_t size = FileSize(GetSavePath());
    downloadedBytes.store(size);
    if (size > 0) totalBytes.store(size);
    finishedAt.store(NowUnixMs());
    speed.store(0.0);
    state.store((int)DownloadState::Complete);
    UpdateProgress();
}

// ---------------------------------------------------------------------------
// DownloadManager
// ---------------------------------------------------------------------------

DownloadManager& DownloadManager::Instance() {
    static DownloadManager manager;
    return manager;
}

void DownloadManager::LoadSettings() {
    cfg_.Load();
}

void DownloadManager::SaveSettings() {
    std::lock_guard<std::mutex> lock(mutex_);
    cfg_.Save();
}

std::shared_ptr<DownloadItem> DownloadManager::Add(
    const std::wstring& url, const std::wstring& saveDir,
    const std::wstring& fileName, int connections, bool startNow,
    const std::wstring& description, const std::wstring& category) {
    UrlParts parts = ParseUrl(url);
    if (!parts.valid) return nullptr;

    std::wstring name = SanitizeFileName(fileName.empty() ? parts.FileName()
                                                          : fileName);
    std::wstring dir = saveDir.empty() ? cfg_.saveDir : saveDir;

    auto item = std::make_shared<DownloadItem>();
    item->url = url;
    item->fileName = name;
    item->description = description;
    item->category = category;
    item->nameFromUrl = fileName.empty();
    item->savePath = UniquePath(dir, name);
    item->tempDir = cfg_.tempDir;
    item->notifyWindow = mainWnd_;
    item->connectionsWanted = std::max(1, connections);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        item->id = nextId_++;
        items_.push_back(item);
    }

    NotifyChanged();
    if (startNow) item->Start(cfg_);
    return item;
}

std::shared_ptr<DownloadItem> DownloadManager::AddYtDlp(
    const std::wstring& pageUrl, const std::wstring& saveDir,
    const std::wstring& fileName, const std::wstring& format, bool merge,
    bool startNow, const std::wstring& description,
    const std::wstring& category) {
    UrlParts parts = ParseUrl(pageUrl);
    if (!parts.valid || format.empty()) return nullptr;

    std::wstring name = SanitizeFileName(
        fileName.empty() ? parts.FileName() : fileName);
    std::wstring dir = saveDir.empty() ? cfg_.saveDir : saveDir;

    auto item = std::make_shared<DownloadItem>();
    item->url = pageUrl;
    item->fileName = name;
    item->description = description;
    item->category = category;
    item->nameFromUrl = fileName.empty();
    item->savePath = UniquePath(dir, name);
    item->tempDir = cfg_.tempDir;
    item->notifyWindow = mainWnd_;
    item->connectionsWanted = 1;
    item->resumable = true;
    item->useYtDlp = true;
    item->ytDlpFormat = format;
    item->ytDlpPageUrl = pageUrl;
    item->ytDlpMerge = merge;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        item->id = nextId_++;
        items_.push_back(item);
    }

    NotifyChanged();
    if (startNow) item->Start(cfg_);
    return item;
}

void DownloadManager::Remove(uint64_t id, bool deleteFile) {
    std::shared_ptr<DownloadItem> victim;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = std::find_if(items_.begin(), items_.end(),
                               [id](const std::shared_ptr<DownloadItem>& item) {
                                   return item->id == id;
                               });
        if (it == items_.end()) return;
        victim = *it;
        items_.erase(it);
    }

    victim->Stop();
    victim->WaitForStop();
    victim->CleanupTemp();
    if (deleteFile) {
        const std::wstring path = victim->GetSavePath();
        if (!path.empty()) DeleteFileQuiet(path);
    }
    NotifyChanged();
}

std::shared_ptr<DownloadItem> DownloadManager::Find(uint64_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& item : items_) {
        if (item->id == id) return item;
    }
    return nullptr;
}

std::vector<std::shared_ptr<DownloadItem>> DownloadManager::Snapshot() {
    std::lock_guard<std::mutex> lock(mutex_);
    return items_;
}

void DownloadManager::StartAll() {
    std::vector<std::shared_ptr<DownloadItem>> copy;
    Config cfg;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        copy = items_;
        cfg = cfg_;
    }
    for (const auto& item : copy) {
        DownloadState s = item->GetState();
        if (s == DownloadState::Queued || s == DownloadState::Paused ||
            s == DownloadState::Error || s == DownloadState::Cancelled) {
            item->Start(cfg);
        }
    }
    NotifyChanged();
}

void DownloadManager::StopAll() {
    std::vector<std::shared_ptr<DownloadItem>> copy;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        copy = items_;
    }
    for (const auto& item : copy) item->Stop();
    NotifyChanged();
}

size_t DownloadManager::Count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return items_.size();
}

void DownloadManager::Shutdown() {
    std::vector<std::shared_ptr<DownloadItem>> copy;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        copy = items_;
    }
    for (const auto& item : copy) {
        item->Stop();
    }
    for (const auto& item : copy) {
        item->WaitForStop();
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        items_.clear();
    }
    mainWnd_ = nullptr;
}

void DownloadManager::NotifyChanged() {
    HWND wnd = mainWnd_;
    if (wnd) PostMessageW(wnd, WM_APP_REFRESH_ALL, 0, 0);
}
