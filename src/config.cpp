#include "common.h"

namespace {

std::wstring ReadString(HKEY key, const wchar_t* name, const std::wstring& fallback) {
    wchar_t buffer[2048] = {};
    DWORD size = sizeof(buffer);
    DWORD type = 0;
    LONG rc = RegQueryValueExW(key, name, nullptr, &type, (LPBYTE)buffer, &size);
    if (rc != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) {
        return fallback;
    }
    return std::wstring(buffer);
}

// Like ReadString, but sized from the registry so category-path lists are not
// silently truncated at 2 KB.
std::wstring ReadStringLong(HKEY key, const wchar_t* name) {
    DWORD type = 0;
    DWORD size = 0;
    if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &size) !=
            ERROR_SUCCESS ||
        (type != REG_SZ && type != REG_EXPAND_SZ) || size < sizeof(wchar_t)) {
        return std::wstring();
    }
    std::vector<wchar_t> buffer(size / sizeof(wchar_t) + 1, 0);
    if (RegQueryValueExW(key, name, nullptr, &type, (LPBYTE)buffer.data(),
                         &size) != ERROR_SUCCESS) {
        return std::wstring();
    }
    return std::wstring(buffer.data());
}

std::wstring EscapeCategoryPart(const std::wstring& value) {
    std::wstring out;
    out.reserve(value.size());
    for (wchar_t ch : value) {
        switch (ch) {
            case L'%': out += L"%25"; break;
            case L'|': out += L"%7C"; break;
            case L'=': out += L"%3D"; break;
            default: out.push_back(ch); break;
        }
    }
    return out;
}

int HexDigit(wchar_t ch) {
    if (ch >= L'0' && ch <= L'9') return (int)(ch - L'0');
    if (ch >= L'a' && ch <= L'f') return (int)(ch - L'a') + 10;
    if (ch >= L'A' && ch <= L'F') return (int)(ch - L'A') + 10;
    return -1;
}

std::wstring UnescapeCategoryPart(const std::wstring& value) {
    std::wstring out;
    out.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == L'%' && i + 2 < value.size()) {
            const int hi = HexDigit(value[i + 1]);
            const int lo = HexDigit(value[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back((wchar_t)((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(value[i]);
    }
    return out;
}

std::wstring EncodeCategoryPaths(
    const std::map<std::wstring, std::wstring>& paths) {
    std::wstring out;
    for (const auto& entry : paths) {
        if (entry.first.empty() || entry.second.empty()) continue;
        if (!out.empty()) out.push_back(L'|');
        out += EscapeCategoryPart(entry.first);
        out.push_back(L'=');
        out += EscapeCategoryPart(entry.second);
    }
    return out;
}

std::wstring EncodeStringList(const std::vector<std::wstring>& values) {
    std::wstring out;
    for (const auto& value : values) {
        if (value.empty()) continue;
        if (!out.empty()) out.push_back(L'|');
        out += EscapeCategoryPart(value);
    }
    return out;
}

void DecodeStringList(const std::wstring& text,
                      std::vector<std::wstring>& values) {
    values.clear();
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find(L'|', start);
        if (end == std::wstring::npos) end = text.size();
        const std::wstring value =
            UnescapeCategoryPart(text.substr(start, end - start));
        if (!value.empty()) values.push_back(value);
        if (end == text.size()) break;
        start = end + 1;
    }
}

void DecodeCategoryPaths(const std::wstring& text,
                         std::map<std::wstring, std::wstring>& paths) {
    paths.clear();
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find(L'|', start);
        if (end == std::wstring::npos) end = text.size();
        const std::wstring part = text.substr(start, end - start);
        const size_t equals = part.find(L'=');
        if (equals != std::wstring::npos) {
            const std::wstring key = UnescapeCategoryPart(part.substr(0, equals));
            const std::wstring value =
                UnescapeCategoryPart(part.substr(equals + 1));
            if (!key.empty() && !value.empty()) paths[key] = value;
        }
        if (end == text.size()) break;
        start = end + 1;
    }
}

int ReadInt(HKEY key, const wchar_t* name, int fallback) {
    DWORD value = 0;
    DWORD size = sizeof(value);
    DWORD type = 0;
    LONG rc = RegQueryValueExW(key, name, nullptr, &type, (LPBYTE)&value, &size);
    if (rc != ERROR_SUCCESS || type != REG_DWORD) return fallback;
    return (int)value;
}

void WriteString(HKEY key, const wchar_t* name, const std::wstring& value) {
    RegSetValueExW(key, name, 0, REG_SZ, (const BYTE*)value.c_str(),
                   (DWORD)((value.size() + 1) * sizeof(wchar_t)));
}

void WriteInt(HKEY key, const wchar_t* name, int value) {
    DWORD data = (DWORD)value;
    RegSetValueExW(key, name, 0, REG_DWORD, (const BYTE*)&data, sizeof(data));
}

const wchar_t* kDefaultUserAgent =
    L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    L"(KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36";

// The application used to be called IDM Clone. Copy the old per-user settings
// once so the rebrand keeps the user's language, folders and other options.
const wchar_t* kLegacyRegRoot = L"Software\\IDMClone";

bool MigrateLegacySettings() {
    HKEY current = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, APP_REG_ROOT, 0, nullptr, 0,
                        KEY_READ | KEY_WRITE, nullptr, &current, nullptr) !=
        ERROR_SUCCESS) {
        return false;
    }

    bool copied = false;
    DWORD valueCount = 0;
    RegQueryInfoKeyW(current, nullptr, nullptr, nullptr, nullptr, nullptr,
                     nullptr, &valueCount, nullptr, nullptr, nullptr,
                     nullptr);
    if (valueCount == 0) {
        HKEY legacy = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kLegacyRegRoot, 0, KEY_READ,
                          &legacy) == ERROR_SUCCESS) {
            wchar_t name[512] = {};
            DWORD index = 0;
            for (;;) {
                DWORD nameLength = 512;
                DWORD type = 0;
                DWORD dataSize = 4096;
                std::vector<BYTE> data(dataSize);
                LONG rc = RegEnumValueW(legacy, index, name, &nameLength,
                                        nullptr, &type, data.data(),
                                        &dataSize);
                if (rc == ERROR_MORE_DATA) {
                    data.resize(dataSize);
                    nameLength = 512;
                    dataSize = (DWORD)data.size();
                    rc = RegEnumValueW(legacy, index, name, &nameLength,
                                       nullptr, &type, data.data(),
                                       &dataSize);
                }
                if (rc == ERROR_NO_MORE_ITEMS) break;
                if (rc == ERROR_SUCCESS) {
                    if (RegSetValueExW(current, name, 0, type, data.data(),
                                       dataSize) == ERROR_SUCCESS) {
                        copied = true;
                    }
                }
                ++index;
            }
            RegCloseKey(legacy);
        }
    }
    RegCloseKey(current);
    return copied;
}

}  // namespace

void Config::Load() {
    const bool migrated = MigrateLegacySettings();

    userAgent = kDefaultUserAgent;
    saveDir = GetDefaultDownloadDir();
    tempDir = JoinPath(GetTempDir(), APP_SHORT);

    HKEY key = nullptr;
    LONG rc = RegOpenKeyExW(HKEY_CURRENT_USER, APP_REG_ROOT, 0, KEY_READ, &key);
    if (rc != ERROR_SUCCESS) {
        // First run: create the key so later writes have a home.
        if (RegCreateKeyExW(HKEY_CURRENT_USER, APP_REG_ROOT, 0, nullptr, 0,
                            KEY_WRITE, nullptr, &key, nullptr) == ERROR_SUCCESS) {
            RegCloseKey(key);
        }
        EnsureDirectory(tempDir);
        return;
    }

    maxConnections   = ReadInt(key, L"MaxConnections", maxConnections);
    maxRetries       = ReadInt(key, L"MaxRetries", maxRetries);
    connectTimeoutMs = ReadInt(key, L"ConnectTimeoutMs", connectTimeoutMs);
    readTimeoutMs    = ReadInt(key, L"ReadTimeoutMs", readTimeoutMs);
    minPartSize      = ReadInt(key, L"MinPartSize", minPartSize);
    speedLimitKB     = ReadInt(key, L"SpeedLimitKB", speedLimitKB);
    startOnAdd       = ReadInt(key, L"StartOnAdd", startOnAdd ? 1 : 0) != 0;
    showCompleteDialog =
        ReadInt(key, L"ShowCompleteDialog", showCompleteDialog ? 1 : 0) != 0;
    showProgressDialog =
        ReadInt(key, L"ShowProgressDialog", showProgressDialog ? 1 : 0) != 0;
    progressShowDetails =
        ReadInt(key, L"ProgressShowDetails", progressShowDetails ? 1 : 0) != 0;
    proxyMode        = ReadInt(key, L"ProxyMode", proxyMode);
    language         = ReadInt(key, L"Language", language);
    userAgent        = ReadString(key, L"UserAgent", userAgent);
    saveDir          = ReadString(key, L"SaveDir", saveDir);
    tempDir          = ReadString(key, L"TempDir", tempDir);
    proxyAddress     = ReadString(key, L"ProxyAddress", proxyAddress);
    DecodeCategoryPaths(ReadStringLong(key, L"CategoryPaths"), categoryPaths);
    DecodeStringList(ReadStringLong(key, L"CustomCategories"),
                     customCategories);
    DecodeStringList(ReadStringLong(key, L"RecentSavePaths"),
                     recentSavePaths);

    RegCloseKey(key);

    if (maxConnections < 1) maxConnections = 1;
    if (maxConnections > 32) maxConnections = 32;
    if (maxRetries < 0) maxRetries = 0;
    if (minPartSize < 64 * 1024) minPartSize = 64 * 1024;
    if (userAgent.empty()) userAgent = kDefaultUserAgent;
    if (saveDir.empty()) saveDir = GetDefaultDownloadDir();
    // The old default temp folder carried the previous product name. Move it to
    // the new default while leaving a user-chosen custom folder untouched.
    if (tempDir == JoinPath(GetTempDir(), L"IDMClone")) {
        tempDir = JoinPath(GetTempDir(), APP_SHORT);
    }
    if (tempDir.empty()) tempDir = JoinPath(GetTempDir(), APP_SHORT);
    if (language < 0 || language > 1) language = 0;
    EnsureDirectory(tempDir);
    if (migrated) Save();
}

void Config::Save() const {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, APP_REG_ROOT, 0, nullptr, 0,
                        KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return;
    }
    WriteInt(key, L"MaxConnections", maxConnections);
    WriteInt(key, L"MaxRetries", maxRetries);
    WriteInt(key, L"ConnectTimeoutMs", connectTimeoutMs);
    WriteInt(key, L"ReadTimeoutMs", readTimeoutMs);
    WriteInt(key, L"MinPartSize", minPartSize);
    WriteInt(key, L"SpeedLimitKB", speedLimitKB);
    WriteInt(key, L"StartOnAdd", startOnAdd ? 1 : 0);
    WriteInt(key, L"ShowCompleteDialog", showCompleteDialog ? 1 : 0);
    WriteInt(key, L"ShowProgressDialog", showProgressDialog ? 1 : 0);
    WriteInt(key, L"ProgressShowDetails", progressShowDetails ? 1 : 0);
    WriteInt(key, L"ProxyMode", proxyMode);
    WriteInt(key, L"Language", language);
    WriteString(key, L"UserAgent", userAgent);
    WriteString(key, L"SaveDir", saveDir);
    WriteString(key, L"TempDir", tempDir);
    WriteString(key, L"ProxyAddress", proxyAddress);
    WriteString(key, L"CategoryPaths", EncodeCategoryPaths(categoryPaths));
    WriteString(key, L"CustomCategories", EncodeStringList(customCategories));
    WriteString(key, L"RecentSavePaths", EncodeStringList(recentSavePaths));
    RegCloseKey(key);
}
