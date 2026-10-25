#include "ytdlp.h"

#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>

namespace {

const wchar_t* kFfmpegUrl =
    L"https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-essentials.zip";

// FLEX_FFMPEG_URL lets a mirror (or a local test archive) replace the default.
std::wstring FfmpegUrl() {
    wchar_t buffer[2048] = {};
    if (GetEnvironmentVariableW(L"FLEX_FFMPEG_URL", buffer, 2048) > 0) {
        return buffer;
    }
    return kFfmpegUrl;
}

bool IsFile(const std::wstring& path) {
    const DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES &&
           !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring ModuleFolder() {
    std::wstring dir = GetExeDir();
    if (!dir.empty() && dir.back() == L'\\') dir.pop_back();
    return dir;
}

// Finds name in the usual places. Keep the order deterministic: the copy the
// user dropped next to the app wins over a stale per-user download.
std::wstring FindTool(const wchar_t* name, const wchar_t* subFolder) {
    const std::wstring module = ModuleFolder();
    const std::vector<std::wstring> candidates = {
        module + L"\\" + name,
        module + L"\\tools\\" + name,
        module + L"\\..\\tools\\" + name,
        ToolDir() + L"\\" + name,
        ToolDir() + L"\\" + subFolder + L"\\" + name,
    };
    for (const auto& candidate : candidates) {
        if (IsFile(candidate)) return candidate;
    }

    wchar_t found[MAX_PATH * 2] = {};
    if (SearchPathW(nullptr, name, nullptr, MAX_PATH * 2, found, nullptr) > 0) {
        return found;
    }
    return std::wstring();
}

std::wstring Quote(const std::wstring& value) {
    std::wstring out = L"\"";
    for (wchar_t ch : value) {
        if (ch == L'"') out += L'\\';
        out += ch;
    }
    out += L'"';
    return out;
}

// CreateProcessW only searches PATH when lpApplicationName is null, so partial
// names ("tar.exe", "cmd.exe") must be resolved here first.
std::wstring ResolveTool(const std::wstring& exe) {
    if (exe.find(L'\\') != std::wstring::npos ||
        exe.find(L'/') != std::wstring::npos) {
        return exe;
    }
    wchar_t found[MAX_PATH * 2] = {};
    if (SearchPathW(nullptr, exe.c_str(), L".exe", MAX_PATH * 2, found,
                    nullptr) > 0) {
        return found;
    }
    return exe;
}

// Small diagnostic log next to the tools. It only records failures, so it is
// usually empty but gives users something to send when yt-dlp misbehaves.
void AppendLog(const std::wstring& text) {
    const std::wstring path = ToolDir() + L"\\tools.log";
    HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ,
                              nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE) return;

    SYSTEMTIME now = {};
    GetLocalTime(&now);
    wchar_t stamp[64] = {};
    swprintf_s(stamp, L"%04d-%02d-%02d %02d:%02d:%02d ",
               now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
               now.wSecond);
    const std::string line = WideToUtf8(std::wstring(stamp) + text + L"\r\n");
    DWORD written = 0;
    WriteFile(file, line.data(), (DWORD)line.size(), &written, nullptr);
    CloseHandle(file);
}

bool EndsWithNoCase(const std::wstring& value, const std::wstring& suffix) {
    if (suffix.size() > value.size()) return false;
    const size_t offset = value.size() - suffix.size();
    for (size_t i = 0; i < suffix.size(); ++i) {
        if (towlower(value[offset + i]) != towlower(suffix[i])) return false;
    }
    return true;
}

bool FindFileRecursive(const std::wstring& dir, const std::wstring& name,
                       int depth, std::wstring& out) {
    if (depth > 4) return false;

    WIN32_FIND_DATAW data = {};
    HANDLE find = FindFirstFileW((dir + L"\\*").c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) return false;

    bool found = false;
    do {
        if (wcscmp(data.cFileName, L".") == 0 ||
            wcscmp(data.cFileName, L"..") == 0) {
            continue;
        }
        const std::wstring path = dir + L"\\" + data.cFileName;
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (FindFileRecursive(path, name, depth + 1, out)) {
                found = true;
                break;
            }
        } else if (EndsWithNoCase(data.cFileName, name)) {
            out = path;
            found = true;
            break;
        }
    } while (FindNextFileW(find, &data));

    FindClose(find);
    return found;
}

}  // namespace

std::wstring ToolDir() {
    std::wstring base;
    wchar_t buffer[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH) > 0) {
        base = buffer;
    } else {
        base = GetTempDir();
    }
    const std::wstring dir = base + L"\\FLEXDownloadManager";
    EnsureDirectory(dir);
    return dir;
}

std::wstring FindYtDlp() {
    return FindTool(L"yt-dlp.exe", L"");
}

std::wstring FindFfmpeg() {
    return FindTool(L"ffmpeg.exe", L"ffmpeg");
}

// ---------------------------------------------------------------------------
// ConsoleProcess
// ---------------------------------------------------------------------------

ConsoleProcess::~ConsoleProcess() {
    Close();
}

bool ConsoleProcess::Start(const std::wstring& exe,
                           const std::wstring& arguments) {
    Close();

    const std::wstring program = ResolveTool(exe);
    if (!IsFile(program)) {
        AppendLog(L"Tool not found: " + exe);
        return false;
    }

    SECURITY_ATTRIBUTES attributes = {};
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = TRUE;

    HANDLE readEnd = nullptr;
    HANDLE writeEnd = nullptr;
    if (!CreatePipe(&readEnd, &writeEnd, &attributes, 0)) return false;
    SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdOutput = writeEnd;
    startup.hStdError = writeEnd;

    // A GUI process usually has no usable stdin; hand the child NUL instead of
    // an invalid handle, which makes CreateProcess fail with error 6/87.
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, &attributes,
                             OPEN_EXISTING, 0, nullptr);
    startup.hStdInput = (nul != INVALID_HANDLE_VALUE)
                            ? nul
                            : GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION info = {};
    std::wstring command = Quote(program) + L" " + arguments;
    std::vector<wchar_t> buffer(command.begin(), command.end());
    buffer.push_back(L'\0');

    const BOOL started = CreateProcessW(
        program.c_str(), buffer.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &info);
    const DWORD startError = started ? 0 : GetLastError();
    CloseHandle(writeEnd);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);

    if (!started) {
        CloseHandle(readEnd);
        AppendLog(L"CreateProcess failed for " + program + L" (" +
                  std::to_wstring(startError) + L"): " + arguments);
        return false;
    }

    process_ = info.hProcess;
    thread_ = info.hThread;
    pipe_ = readEnd;
    return true;
}

int ConsoleProcess::Pump(
    const std::function<void(const std::string&)>& onLine,
    const std::atomic<bool>& cancel, int timeoutMs) {
    if (!process_) return -1;

    const DWORD deadline =
        timeoutMs >= 0 ? GetTickCount() + (DWORD)timeoutMs : 0;
    std::string pending;
    char buffer[4096];
    DWORD read = 0;
    int exitCode = -1;

    auto emit = [&](const char* data, size_t len) {
        pending.append(data, len);
        size_t start = 0;
        for (;;) {
            const size_t newline = pending.find('\n', start);
            if (newline == std::string::npos) break;
            std::string line = pending.substr(start, newline - start);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (onLine) onLine(line);
            start = newline + 1;
        }
        pending.erase(0, start);
    };

    for (;;) {
        if (cancel.load()) {
            TerminateProcess((HANDLE)process_, 1);
        }

        DWORD available = 0;
        if (PeekNamedPipe((HANDLE)pipe_, nullptr, 0, nullptr, &available,
                          nullptr) &&
            available > 0) {
            const DWORD want = std::min<DWORD>(available, sizeof(buffer));
            if (!ReadFile((HANDLE)pipe_, buffer, want, &read, nullptr)) break;
            emit(buffer, read);
            continue;
        }

        if (WaitForSingleObject((HANDLE)process_, 50) == WAIT_OBJECT_0) {
            // Drain whatever the child wrote just before exiting.
            while (PeekNamedPipe((HANDLE)pipe_, nullptr, 0, nullptr, &available,
                                 nullptr) &&
                   available > 0) {
                const DWORD want = std::min<DWORD>(available, sizeof(buffer));
                if (!ReadFile((HANDLE)pipe_, buffer, want, &read, nullptr)) break;
                emit(buffer, read);
            }
            break;
        }

        if (timeoutMs >= 0 && GetTickCount() > deadline) {
            TerminateProcess((HANDLE)process_, 1);
            break;
        }
    }

    if (!pending.empty() && onLine) onLine(pending);

    DWORD code = 0;
    if (process_ && GetExitCodeProcess((HANDLE)process_, &code)) {
        exitCode = (int)code;
    }
    Close();
    return exitCode;
}

void ConsoleProcess::Close() {
    if (pipe_) {
        CloseHandle((HANDLE)pipe_);
        pipe_ = nullptr;
    }
    if (thread_) {
        CloseHandle((HANDLE)thread_);
        thread_ = nullptr;
    }
    if (process_) {
        CloseHandle((HANDLE)process_);
        process_ = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Wrappers
// ---------------------------------------------------------------------------

int RunToolLines(const std::wstring& exe, const std::wstring& arguments,
                 const std::function<void(const std::string&)>& onLine,
                 const std::atomic<bool>& cancel) {
    ConsoleProcess process;
    if (!process.Start(exe, arguments)) return -1;
    return process.Pump(onLine, cancel);
}

bool RunToolCapture(const std::wstring& exe, const std::wstring& arguments,
                    int timeoutMs, std::string& output, int& exitCode) {
    ConsoleProcess process;
    if (!process.Start(exe, arguments)) {
        exitCode = -1;
        return false;
    }
    std::atomic<bool> never{ false };
    exitCode = process.Pump(
        [&output](const std::string& line) {
            output += line;
            output += '\n';
            if (output.size() > 16u * 1024u * 1024u) output.resize(16u * 1024u * 1024u);
        },
        never, timeoutMs);
    return exitCode >= 0;
}

// ---------------------------------------------------------------------------
// ffmpeg installer
// ---------------------------------------------------------------------------

bool InstallFfmpeg(HttpClient& client,
                   const std::function<void(uint64_t, uint64_t)>& progress,
                   const std::atomic<bool>& cancel, std::wstring& error) {
    const std::wstring toolDir = ToolDir();
    const std::wstring targetDir = toolDir + L"\\ffmpeg";
    const std::wstring zipPath = toolDir + L"\\ffmpeg-download.zip";
    const std::wstring unpackDir = toolDir + L"\\ffmpeg-unpack";
    EnsureDirectory(targetDir);

    RequestOptions head;
    head.method = L"HEAD";
    const std::wstring ffmpegUrl = FfmpegUrl();
    const HttpResponse probe =
        client.Request(ffmpegUrl, head, HttpClient::BodySink());
    const uint64_t total = probe.ok ? probe.contentLength : UINT64_MAX;

    HANDLE file = CreateFileW(zipPath.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = L"Cannot create the ffmpeg archive";
        return false;
    }

    uint64_t written = 0;
    bool writeOk = true;
    RequestOptions get;
    const HttpResponse response = client.Request(
        ffmpegUrl, get, [&](int, const uint8_t* data, size_t len) -> bool {
            if (cancel.load()) return false;
            DWORD wrote = 0;
            if (!WriteFile(file, data, (DWORD)len, &wrote, nullptr) ||
                wrote != len) {
                writeOk = false;
                return false;
            }
            written += wrote;
            if (progress) progress(written, total == UINT64_MAX ? 0 : total);
            return true;
        });
    CloseHandle(file);

    if (!writeOk || response.statusCode != 200 ||
        written < 20ull * 1024ull * 1024ull) {
        AppendLog(L"ffmpeg download failed: status " +
                  std::to_wstring(response.statusCode) + L", bytes " +
                  std::to_wstring(written));
        DeleteFileQuiet(zipPath);
        error = cancel.load() ? L"Stopped" : L"Cannot download ffmpeg";
        return false;
    }

    if (progress) progress(0, 0);

    // Windows ships bsdtar, which understands zip archives. It is the only
    // unpacker available without adding a compression library.
    EnsureDirectory(unpackDir);
    std::string output;
    int code = -1;
    const std::wstring tarArgs =
        L"-xf " + Quote(zipPath) + L" -C " + Quote(unpackDir);
    if (!RunToolCapture(L"tar.exe", tarArgs, 180000, output, code) || code != 0) {
        AppendLog(L"tar failed (code " + std::to_wstring(code) + L"): " +
                  Utf8ToWide(output));
        error = L"Cannot unpack the ffmpeg archive";
        DeleteFileQuiet(zipPath);
        return false;
    }

    std::wstring found;
    if (!FindFileRecursive(unpackDir, L"ffmpeg.exe", 0, found)) {
        AppendLog(L"ffmpeg.exe missing from " + unpackDir +
                  L"; tar output: " + Utf8ToWide(output));
        error = L"The ffmpeg archive did not contain ffmpeg.exe";
        DeleteFileQuiet(zipPath);
        return false;
    }

    const std::wstring destination = targetDir + L"\\ffmpeg.exe";
    DeleteFileQuiet(destination);
    if (!CopyFileW(found.c_str(), destination.c_str(), FALSE)) {
        error = L"Cannot install ffmpeg.exe: " + GetErrorMessage(GetLastError());
        DeleteFileQuiet(zipPath);
        return false;
    }

    // ffprobe is optional but yt-dlp uses it to detect the container.
    std::wstring probePath;
    if (FindFileRecursive(unpackDir, L"ffprobe.exe", 0, probePath)) {
        CopyFileW(probePath.c_str(), (targetDir + L"\\ffprobe.exe").c_str(),
                  FALSE);
    }

    DeleteFileQuiet(zipPath);
    std::string ignored;
    int ignoredCode = 0;
    RunToolCapture(L"cmd.exe", L"/c rmdir /s /q " + Quote(unpackDir), 60000,
                   ignored, ignoredCode);
    AppendLog(L"ffmpeg installed at " + destination);
    return true;
}
