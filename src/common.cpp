#include "common.h"

#include <chrono>

std::wstring Utf8ToWide(const std::string& in) {
    if (in.empty()) return std::wstring();
    int need = MultiByteToWideChar(CP_UTF8, 0, in.data(), (int)in.size(), nullptr, 0);
    if (need <= 0) return std::wstring();
    std::wstring out((size_t)need, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, in.data(), (int)in.size(), out.data(), need);
    return out;
}

std::string WideToUtf8(const std::wstring& in) {
    if (in.empty()) return std::string();
    int need = WideCharToMultiByte(CP_UTF8, 0, in.data(), (int)in.size(),
                                   nullptr, 0, nullptr, nullptr);
    if (need <= 0) return std::string();
    std::string out((size_t)need, '\0');
    WideCharToMultiByte(CP_UTF8, 0, in.data(), (int)in.size(),
                        out.data(), need, nullptr, nullptr);
    return out;
}

std::wstring FormatBytes(uint64_t bytes) {
    wchar_t buf[64];
    const double kb = 1024.0;
    if (bytes < 1024ull) {
        swprintf_s(buf, L"%llu B", (unsigned long long)bytes);
    } else if (bytes < 1024ull * 1024ull) {
        swprintf_s(buf, L"%.1f KB", bytes / kb);
    } else if (bytes < 1024ull * 1024ull * 1024ull) {
        swprintf_s(buf, L"%.1f MB", bytes / (kb * kb));
    } else {
        swprintf_s(buf, L"%.2f GB", bytes / (kb * kb * kb));
    }
    return buf;
}

namespace {

std::wstring ToLowerAscii(std::wstring value) {
    for (wchar_t& ch : value) {
        if (ch >= L'A' && ch <= L'Z') ch = (wchar_t)(ch - L'A' + L'a');
    }
    return value;
}

}  // namespace

std::wstring InferCategory(const std::wstring& fileName,
                           const std::wstring& mime) {
    const std::wstring type = ToLowerAscii(mime);

    if (type.rfind(L"video/", 0) == 0) return L"video";
    if (type.rfind(L"audio/", 0) == 0) return L"music";
    if (type == L"application/pdf" || type == L"application/msword" ||
        type == L"application/rtf" || type == L"application/epub+zip" ||
        type.rfind(L"application/vnd.openxmlformats-officedocument", 0) == 0 ||
        type.rfind(L"application/vnd.ms-", 0) == 0) {
        return L"documents";
    }
    if (type == L"application/zip" || type == L"application/x-zip-compressed" ||
        type == L"application/x-rar-compressed" || type == L"application/vnd.rar" ||
        type == L"application/x-7z-compressed" || type == L"application/gzip" ||
        type == L"application/x-tar" || type == L"application/x-bzip2" ||
        type == L"application/x-xz") {
        return L"compressed";
    }
    if (type == L"application/vnd.android.package-archive" ||
        type == L"application/x-msdownload" || type == L"application/x-msi" ||
        type == L"application/java-archive") {
        return L"programs";
    }

    const size_t dot = fileName.find_last_of(L'.');
    const std::wstring ext =
        dot == std::wstring::npos ? std::wstring()
                                  : ToLowerAscii(fileName.substr(dot + 1));

    static const wchar_t* const video[] = {
        L"mp4", L"mkv", L"avi", L"mov", L"wmv", L"flv", L"webm",
        L"m4v", L"mpg", L"mpeg", L"ts", L"m2ts", L"3gp", L"ogv"
    };
    static const wchar_t* const music[] = {
        L"mp3", L"wav", L"flac", L"aac", L"ogg", L"m4a",
        L"wma", L"opus", L"aiff", L"alac"
    };
    static const wchar_t* const archives[] = {
        L"zip", L"rar", L"7z", L"tar", L"gz", L"bz2", L"xz", L"zst",
        L"iso", L"cab"
    };
    static const wchar_t* const documents[] = {
        L"pdf", L"doc", L"docx", L"xls", L"xlsx", L"ppt", L"pptx",
        L"txt", L"rtf", L"csv", L"epub", L"md", L"odt", L"ods"
    };
    static const wchar_t* const programs[] = {
        L"exe", L"msi", L"apk", L"jar", L"deb", L"rpm", L"dmg", L"pkg"
    };

    auto contains = [&ext](const auto& values) {
        for (const wchar_t* value : values) {
            if (ext == value) return true;
        }
        return false;
    };

    if (contains(video)) return L"video";
    if (contains(music)) return L"music";
    if (contains(archives)) return L"compressed";
    if (contains(documents)) return L"documents";
    if (contains(programs)) return L"programs";
    return L"other";
}

std::wstring FormatSpeed(double bytesPerSecond) {
    if (bytesPerSecond <= 0.0) return L"0 B/s";
    wchar_t buf[64];
    const double kb = 1024.0;
    if (bytesPerSecond < kb) {
        swprintf_s(buf, L"%.0f B/s", bytesPerSecond);
    } else if (bytesPerSecond < kb * kb) {
        swprintf_s(buf, L"%.1f KB/s", bytesPerSecond / kb);
    } else {
        swprintf_s(buf, L"%.2f MB/s", bytesPerSecond / (kb * kb));
    }
    return buf;
}

std::wstring FormatDuration(uint64_t seconds) {
    wchar_t buf[64];
    const uint64_t h = seconds / 3600;
    const uint64_t m = (seconds % 3600) / 60;
    const uint64_t s = seconds % 60;
    if (h > 0) {
        swprintf_s(buf, L"%llu:%02llu:%02llu",
                   (unsigned long long)h, (unsigned long long)m,
                   (unsigned long long)s);
    } else {
        swprintf_s(buf, L"%02llu:%02llu",
                   (unsigned long long)m, (unsigned long long)s);
    }
    return buf;
}

std::wstring GetErrorMessage(DWORD code) {
    if (code == 0) return L"Unknown error";
    wchar_t* buffer = nullptr;
    DWORD len = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPWSTR)&buffer, 0, nullptr);
    std::wstring result;
    if (len && buffer) {
        result.assign(buffer, len);
        while (!result.empty() &&
               (result.back() == L'\r' || result.back() == L'\n' ||
                result.back() == L' ')) {
            result.pop_back();
        }
    } else {
        wchar_t fallback[64];
        swprintf_s(fallback, L"Error %lu", (unsigned long)code);
        result = fallback;
    }
    if (buffer) LocalFree(buffer);
    return result;
}

std::wstring JoinPath(const std::wstring& dir, const std::wstring& name) {
    if (dir.empty()) return name;
    std::wstring out = dir;
    if (out.back() != L'\\' && out.back() != L'/') out.push_back(L'\\');
    out += name;
    return out;
}

std::wstring DirName(const std::wstring& path) {
    size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return std::wstring();
    if (pos == 0) return path.substr(0, 1);
    // Preserve a drive root such as "C:\" without the trailing separator.
    if (pos == 2 && path[1] == L':') return path.substr(0, 3);
    return path.substr(0, pos);
}

std::wstring FileNameFromPath(const std::wstring& path) {
    size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return path;
    return path.substr(pos + 1);
}

std::wstring SanitizeFileName(const std::wstring& name) {
    std::wstring out;
    out.reserve(name.size());
    for (wchar_t c : name) {
        switch (c) {
            case L'\\': case L'/': case L':': case L'*': case L'?':
            case L'"': case L'<': case L'>': case L'|':
                out.push_back(L'_');
                break;
            default:
                if (c < 32) {
                    out.push_back(L'_');
                } else {
                    out.push_back(c);
                }
                break;
        }
    }
    // Windows rejects names ending with a space or a dot.
    while (!out.empty() && (out.back() == L' ' || out.back() == L'.')) {
        out.pop_back();
    }
    if (out.empty()) out = L"download";
    return out;
}

std::wstring GetExeDir() {
    wchar_t buffer[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    std::wstring path(buffer);
    size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return L".";
    return path.substr(0, pos);
}

std::wstring GetTempDir() {
    wchar_t buffer[MAX_PATH] = {};
    DWORD len = GetTempPathW(MAX_PATH, buffer);
    if (len == 0) return GetExeDir();
    std::wstring path(buffer, len);
    while (!path.empty() && (path.back() == L'\\' || path.back() == L'/')) {
        path.pop_back();
    }
    return path;
}

std::wstring GetDefaultDownloadDir() {
    PWSTR raw = nullptr;
    std::wstring result;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &raw))) {
        result = raw;
        CoTaskMemFree(raw);
    }
    if (result.empty()) {
        result = JoinPath(GetExeDir(), L"Downloads");
    }
    return result;
}

bool FileExists(const std::wstring& path) {
    DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

bool DirectoryExists(const std::wstring& path) {
    DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

bool EnsureDirectory(const std::wstring& path) {
    if (path.empty()) return false;
    if (DirectoryExists(path)) return true;
    int rc = SHCreateDirectoryExW(nullptr, path.c_str(), nullptr);
    return rc == ERROR_SUCCESS || rc == ERROR_ALREADY_EXISTS ||
           DirectoryExists(path);
}

uint64_t FileSize(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        return 0;
    }
    ULARGE_INTEGER size{};
    size.LowPart = data.nFileSizeLow;
    size.HighPart = data.nFileSizeHigh;
    return size.QuadPart;
}

bool DeleteFileQuiet(const std::wstring& path) {
    if (!FileExists(path)) return true;
    SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
    return DeleteFileW(path.c_str()) != FALSE;
}

uint64_t NowUnixMs() {
    using namespace std::chrono;
    return (uint64_t)duration_cast<milliseconds>(
               system_clock::now().time_since_epoch())
        .count();
}
