#include "common.h"

#include <algorithm>
#include <cwctype>

namespace {

bool IsSchemeChar(wchar_t c) {
    return std::iswalnum(c) || c == L'+' || c == L'-' || c == L'.';
}

std::wstring ToLower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](wchar_t c) { return (wchar_t)std::towlower(c); });
    return s;
}

}  // namespace

std::wstring UrlParts::HostWithPort() const {
    if (port.empty()) return host;
    return host + L":" + port;
}

std::wstring UrlParts::RequestTarget() const {
    std::wstring target = path.empty() ? L"/" : path;
    if (!query.empty()) target += L"?" + query;
    return target;
}

std::wstring UrlParts::FileName() const {
    std::wstring clean = path;
    // Drop everything after the final slash so query data cannot leak in.
    size_t slash = clean.find_last_of(L'/');
    std::wstring name = (slash == std::wstring::npos) ? clean
                                                      : clean.substr(slash + 1);
    if (name.empty()) name = L"index.html";
    return SanitizeFileName(name);
}

UrlParts ParseUrl(const std::wstring& url) {
    UrlParts out;
    std::wstring work = url;

    // Trim surrounding whitespace; clipboard payloads often carry it.
    while (!work.empty() && std::iswspace(work.front())) work.erase(work.begin());
    while (!work.empty() && std::iswspace(work.back())) work.pop_back();
    if (work.empty()) return out;

    size_t schemeEnd = work.find(L"://");
    if (schemeEnd == std::wstring::npos || schemeEnd == 0) return out;
    for (size_t i = 0; i < schemeEnd; ++i) {
        if (!IsSchemeChar(work[i])) return out;
    }
    out.scheme = ToLower(work.substr(0, schemeEnd));
    if (out.scheme != L"http" && out.scheme != L"https" && out.scheme != L"ftp") {
        return out;
    }

    size_t cursor = schemeEnd + 3;
    size_t authorityEnd = work.find_first_of(L"/?#", cursor);
    std::wstring authority = work.substr(
        cursor, authorityEnd == std::wstring::npos ? std::wstring::npos
                                                   : authorityEnd - cursor);

    // Strip any user information. Credentials are handled separately.
    size_t at = authority.find_last_of(L'@');
    if (at != std::wstring::npos) authority = authority.substr(at + 1);

    if (authority.empty()) return out;

    if (!authority.empty() && authority.front() == L'[') {
        // IPv6 literal.
        size_t close = authority.find(L']');
        if (close == std::wstring::npos) return out;
        out.host = authority.substr(0, close + 1);
        if (close + 1 < authority.size() && authority[close + 1] == L':') {
            out.port = authority.substr(close + 2);
        }
    } else {
        size_t colon = authority.find_last_of(L':');
        if (colon != std::wstring::npos) {
            out.host = authority.substr(0, colon);
            out.port = authority.substr(colon + 1);
        } else {
            out.host = authority;
        }
    }
    if (out.host.empty()) return out;

    std::wstring rest;
    if (authorityEnd != std::wstring::npos) rest = work.substr(authorityEnd);

    size_t frag = rest.find(L'#');
    if (frag != std::wstring::npos) rest.erase(frag);

    size_t queryPos = rest.find(L'?');
    if (queryPos != std::wstring::npos) {
        out.path = rest.substr(0, queryPos);
        out.query = rest.substr(queryPos + 1);
    } else {
        out.path = rest;
    }
    if (out.path.empty()) out.path = L"/";

    // Percent-decode nothing here; WinHTTP needs the escaped form preserved.
    out.valid = true;
    return out;
}

std::wstring UrlEncodePath(const std::wstring& path) {
    static const wchar_t* kHex = L"0123456789ABCDEF";
    std::wstring out;
    out.reserve(path.size());
    for (unsigned char c : WideToUtf8(path)) {
        const bool unreserved =
            (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
            c == '~' || c == '/' || c == ':' || c == '?' || c == '&' ||
            c == '=' || c == '%' || c == '+' || c == '@' || c == ';' ||
            c == ',' || c == '!' || c == '$' || c == '\'' || c == '(' ||
            c == ')' || c == '*';
        if (unreserved) {
            out.push_back((wchar_t)c);
        } else {
            out.push_back(L'%');
            out.push_back(kHex[(c >> 4) & 0xF]);
            out.push_back(kHex[c & 0xF]);
        }
    }
    return out;
}
