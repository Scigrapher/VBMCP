#include "common.h"

#include <shlobj.h>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <cwctype>
#include <algorithm>

namespace mcpaddin {

static bool g_debugLog = false;

class LogLock {
public:
    LogLock() { InitializeCriticalSection(&criticalSection); }
    ~LogLock() { DeleteCriticalSection(&criticalSection); }
    CRITICAL_SECTION criticalSection;
};

static CRITICAL_SECTION& GetLogLock() {
    static LogLock* lock = new LogLock();
    return lock->criticalSection;
}

std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) return std::string();
    int needed = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), (int)value.size(), NULL, 0, NULL, NULL);
    if (needed <= 0) return std::string();
    std::string result((size_t)needed, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), (int)value.size(), &result[0], needed, NULL, NULL);
    return result;
}

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) return std::wstring();
    int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(), (int)value.size(), NULL, 0);
    if (needed <= 0) {
        needed = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), (int)value.size(), NULL, 0);
    }
    if (needed <= 0) return std::wstring();
    std::wstring result((size_t)needed, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), (int)value.size(), &result[0], needed);
    return result;
}

std::string WideToAnsi(const std::wstring& value) {
    if (value.empty()) return std::string();
    int needed = WideCharToMultiByte(CP_ACP, 0, value.c_str(), (int)value.size(), NULL, 0, NULL, NULL);
    if (needed <= 0) return std::string();
    std::string result((size_t)needed, '\0');
    WideCharToMultiByte(CP_ACP, 0, value.c_str(), (int)value.size(), &result[0], needed, NULL, NULL);
    return result;
}

std::wstring AnsiToWide(const std::string& value) {
    if (value.empty()) return std::wstring();
    int needed = MultiByteToWideChar(CP_ACP, 0, value.c_str(), (int)value.size(), NULL, 0);
    if (needed <= 0) return std::wstring();
    std::wstring result((size_t)needed, L'\0');
    MultiByteToWideChar(CP_ACP, 0, value.c_str(), (int)value.size(), &result[0], needed);
    return result;
}

std::string Trim(const std::string& value) {
    size_t start = 0;
    while (start < value.size() && (unsigned char)value[start] <= 0x20) ++start;
    size_t end = value.size();
    while (end > start && (unsigned char)value[end - 1] <= 0x20) --end;
    return value.substr(start, end - start);
}

std::wstring Trim(const std::wstring& value) {
    size_t start = 0;
    while (start < value.size() && value[start] <= 0x20) ++start;
    size_t end = value.size();
    while (end > start && value[end - 1] <= 0x20) --end;
    return value.substr(start, end - start);
}

bool IEquals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        unsigned char ca = (unsigned char)a[i];
        unsigned char cb = (unsigned char)b[i];
        if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb - 'A' + 'a');
        if (ca != cb) return false;
    }
    return true;
}

bool IEquals(const std::wstring& a, const std::wstring& b) {
    if (a.size() != b.size()) return false;
    return _wcsicmp(a.c_str(), b.c_str()) == 0;
}

std::string ToLowerAscii(const std::string& value) {
    std::string result = value;
    for (size_t i = 0; i < result.size(); ++i) {
        if (result[i] >= 'A' && result[i] <= 'Z') result[i] = (char)(result[i] - 'A' + 'a');
    }
    return result;
}

std::wstring ToLower(const std::wstring& value) {
    std::wstring result = value;
    for (size_t i = 0; i < result.size(); ++i) {
        if (result[i] >= L'A' && result[i] <= L'Z') result[i] = (wchar_t)(result[i] - L'A' + L'a');
    }
    return result;
}

std::string HexEncode(const std::vector<unsigned char>& bytes) {
    static const char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (size_t i = 0; i < bytes.size(); ++i) {
        out.push_back(kHex[bytes[i] >> 4]);
        out.push_back(kHex[bytes[i] & 0x0f]);
    }
    return out;
}

std::string NewGuidString() {
    GUID guid;
    if (FAILED(CoCreateGuid(&guid))) return std::string();
    wchar_t buffer[64] = {0};
    StringFromGUID2(guid, buffer, (int)(sizeof(buffer) / sizeof(buffer[0])));
    std::wstring value(buffer);
    if (!value.empty() && value[0] == L'{') value.erase(value.begin());
    if (!value.empty() && value[value.size() - 1] == L'}') value.erase(value.end() - 1);
    return WideToUtf8(value);
}

std::string FormatHresult(HRESULT hr) {
    char buffer[64] = {0};
    _snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "0x%08lX", (unsigned long)hr);
    return std::string(buffer);
}

std::wstring GetLastErrorMessage(DWORD error) {
    if (error == 0) error = GetLastError();
    wchar_t* buffer = NULL;
    DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    DWORD len = FormatMessageW(flags, NULL, error, 0, (LPWSTR)&buffer, 0, NULL);
    std::wstring result;
    if (len && buffer) {
        result.assign(buffer, len);
        result = Trim(result);
    }
    if (buffer) LocalFree(buffer);
    if (result.empty()) {
        wchar_t fallback[64] = {0};
        _snwprintf_s(fallback, _countof(fallback), _TRUNCATE, L"Windows error %lu", (unsigned long)error);
        result = fallback;
    }
    return result;
}

std::wstring GetModulePath(HMODULE module) {
    std::vector<wchar_t> buffer(MAX_PATH);
    for (;;) {
        DWORD len = GetModuleFileNameW(module, &buffer[0], (DWORD)buffer.size());
        if (len == 0) return std::wstring();
        if (len < buffer.size() - 1) return std::wstring(&buffer[0], len);
        buffer.resize(buffer.size() * 2);
    }
}

std::wstring GetModuleDirectory(HMODULE module) {
    return PathDirectory(GetModulePath(module));
}

std::wstring GetAppDataDirectory() {
    wchar_t buffer[MAX_PATH] = {0};
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA | CSIDL_FLAG_CREATE, NULL, 0, buffer))) {
        return buffer;
    }
    std::wstring env = GetEnvVar(L"APPDATA");
    if (!env.empty()) return env;
    return GetTempDirectory();
}

std::wstring GetLocalAppDataDirectory() {
    wchar_t buffer[MAX_PATH] = {0};
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE, NULL, 0, buffer))) {
        return buffer;
    }
    std::wstring env = GetEnvVar(L"LOCALAPPDATA");
    if (!env.empty()) return env;
    return GetAppDataDirectory();
}

std::wstring GetTempDirectory() {
    DWORD needed = GetTempPathW(0, NULL);
    if (needed == 0) return L"C:\\Temp";
    std::vector<wchar_t> buffer(needed + 1);
    DWORD len = GetTempPathW((DWORD)buffer.size(), &buffer[0]);
    if (len == 0) return L"C:\\Temp";
    return std::wstring(&buffer[0], len);
}

std::wstring GetCurrentDirectoryString() {
    DWORD needed = GetCurrentDirectoryW(0, NULL);
    if (needed == 0) return std::wstring();
    std::vector<wchar_t> buffer(needed + 1);
    DWORD len = GetCurrentDirectoryW((DWORD)buffer.size(), &buffer[0]);
    if (len == 0) return std::wstring();
    return std::wstring(&buffer[0], len);
}

std::wstring GetEnvVar(const wchar_t* name) {
    DWORD needed = GetEnvironmentVariableW(name, NULL, 0);
    if (needed == 0) return std::wstring();
    std::vector<wchar_t> buffer(needed + 1);
    DWORD len = GetEnvironmentVariableW(name, &buffer[0], (DWORD)buffer.size());
    if (len == 0) return std::wstring();
    return std::wstring(&buffer[0], len);
}

bool FileExists(const std::wstring& path) {
    DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool DirectoryExists(const std::wstring& path) {
    DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool EnsureDirectory(const std::wstring& path) {
    if (path.empty()) return false;
    if (DirectoryExists(path)) return true;
    std::wstring parent = PathDirectory(path);
    if (!parent.empty() && parent != path && !DirectoryExists(parent)) {
        if (!EnsureDirectory(parent)) return false;
    }
    if (CreateDirectoryW(path.c_str(), NULL)) return true;
    return GetLastError() == ERROR_ALREADY_EXISTS && DirectoryExists(path);
}

bool DeleteFileQuiet(const std::wstring& path) {
    if (path.empty()) return true;
    if (DeleteFileW(path.c_str())) return true;
    DWORD error = GetLastError();
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
}

bool IsAbsolutePath(const std::wstring& path) {
    if (path.size() >= 2 && path[1] == L':') return true;
    if (path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\') return true;
    return false;
}

std::wstring GetFullPath(const std::wstring& path, const std::wstring& base) {
    std::wstring combined = path;
    if (!base.empty() && !IsAbsolutePath(combined)) combined = PathJoin(base, combined);
    DWORD needed = GetFullPathNameW(combined.c_str(), 0, NULL, NULL);
    if (needed == 0) return combined;
    std::vector<wchar_t> buffer(needed + 1);
    DWORD len = GetFullPathNameW(combined.c_str(), (DWORD)buffer.size(), &buffer[0], NULL);
    if (len == 0) return combined;
    return std::wstring(&buffer[0], len);
}

std::wstring PathDirectory(const std::wstring& path) {
    size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return std::wstring();
    if (pos == 2 && path.size() >= 2 && path[1] == L':') return path.substr(0, 3);
    return path.substr(0, pos);
}

std::wstring PathFileName(const std::wstring& path) {
    size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return path;
    return path.substr(pos + 1);
}

std::wstring PathStem(const std::wstring& path) {
    std::wstring name = PathFileName(path);
    size_t pos = name.find_last_of(L'.');
    if (pos == std::wstring::npos || pos == 0) return name;
    return name.substr(0, pos);
}

std::wstring PathExtension(const std::wstring& path) {
    std::wstring name = PathFileName(path);
    size_t pos = name.find_last_of(L'.');
    if (pos == std::wstring::npos) return std::wstring();
    return name.substr(pos);
}

std::wstring PathJoin(const std::wstring& a, const std::wstring& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    wchar_t last = a[a.size() - 1];
    if (last == L'\\' || last == L'/') return a + b;
    return a + L"\\" + b;
}

std::wstring PathChangeExtension(const std::wstring& path, const std::wstring& ext) {
    std::wstring directory = PathDirectory(path);
    std::wstring stem = PathStem(path);
    std::wstring newName = stem + ext;
    return directory.empty() ? newName : PathJoin(directory, newName);
}

std::wstring NormalizePathForCompare(const std::wstring& path) {
    std::wstring full = GetFullPath(path);
    for (size_t i = 0; i < full.size(); ++i) {
        if (full[i] == L'/') full[i] = L'\\';
        else if (full[i] >= L'A' && full[i] <= L'Z') full[i] = (wchar_t)(full[i] - L'A' + L'a');
    }
    while (full.size() > 3 && full[full.size() - 1] == L'\\') full.erase(full.end() - 1);
    return full;
}

bool IsPathWithin(const std::wstring& path, const std::wstring& root) {
    std::wstring p = NormalizePathForCompare(path);
    std::wstring r = NormalizePathForCompare(root);
    if (r.empty()) return false;
    if (p == r) return true;
    if (p.size() <= r.size()) return false;
    if (p.compare(0, r.size(), r) != 0) return false;
    return p[r.size()] == L'\\';
}

bool WildcardMatch(const std::wstring& pattern, const std::wstring& value) {
    size_t p = 0, v = 0;
    size_t star = std::wstring::npos;
    size_t starValue = 0;
    while (v < value.size()) {
        if (p < pattern.size() && (pattern[p] == L'?' ||
            towlower(pattern[p]) == towlower(value[v]))) {
            ++p;
            ++v;
        } else if (p < pattern.size() && pattern[p] == L'*') {
            star = p++;
            starValue = v;
        } else if (star != std::wstring::npos) {
            p = star + 1;
            v = ++starValue;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == L'*') ++p;
    return p == pattern.size();
}

bool ReadFileBytes(const std::wstring& path, std::vector<unsigned char>& out, DWORD* error) {
    out.clear();
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        if (error) *error = GetLastError();
        return false;
    }
    LARGE_INTEGER size = {0};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 || size.QuadPart > 256 * 1024 * 1024) {
        if (error) *error = ERROR_FILE_TOO_LARGE;
        CloseHandle(file);
        return false;
    }
    out.resize((size_t)size.QuadPart);
    size_t offset = 0;
    while (offset < out.size()) {
        DWORD chunk = (DWORD)std::min<size_t>(out.size() - offset, 1024 * 1024);
        DWORD read = 0;
        if (!ReadFile(file, &out[offset], chunk, &read, NULL)) {
            if (error) *error = GetLastError();
            CloseHandle(file);
            out.clear();
            return false;
        }
        if (read == 0) break;
        offset += read;
    }
    out.resize(offset);
    CloseHandle(file);
    if (error) *error = ERROR_SUCCESS;
    return true;
}

bool WriteFileBytesAtomic(const std::wstring& path, const std::vector<unsigned char>& data, DWORD* error) {
    std::wstring directory = PathDirectory(path);
    if (!directory.empty() && !EnsureDirectory(directory)) {
        if (error) *error = GetLastError();
        return false;
    }
    std::wstring temp = path + L".tmp." + Utf8ToWide(NewGuidString());
    HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        if (error) *error = GetLastError();
        return false;
    }
    size_t offset = 0;
    bool ok = true;
    while (offset < data.size()) {
        DWORD chunk = (DWORD)std::min<size_t>(data.size() - offset, 1024 * 1024);
        DWORD written = 0;
        if (!WriteFile(file, &data[offset], chunk, &written, NULL) || written == 0) {
            ok = false;
            if (error) *error = GetLastError();
            break;
        }
        offset += written;
    }
    if (ok && !FlushFileBuffers(file)) {
        ok = false;
        if (error) *error = GetLastError();
    }
    CloseHandle(file);
    if (!ok) {
        DeleteFileQuiet(temp);
        return false;
    }
    if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        if (error) *error = GetLastError();
        DeleteFileQuiet(temp);
        return false;
    }
    if (error) *error = ERROR_SUCCESS;
    return true;
}

static bool DecodeUtf8Strict(const std::vector<unsigned char>& data, size_t offset, std::wstring& out) {
    if (data.size() <= offset) {
        out.clear();
        return true;
    }
    int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                     (const char*)&data[offset], (int)(data.size() - offset), NULL, 0);
    if (needed <= 0) return false;
    std::wstring result((size_t)needed, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, (const char*)&data[offset], (int)(data.size() - offset),
                        &result[0], needed);
    out.swap(result);
    return true;
}

bool ReadTextFile(const std::wstring& path, std::wstring& out, std::wstring* encoding, DWORD* error) {
    std::vector<unsigned char> data;
    if (!ReadFileBytes(path, data, error)) return false;
    if (encoding) *encoding = L"ansi";
    if (data.size() >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) {
        if (encoding) *encoding = L"utf8";
        if (!DecodeUtf8Strict(data, 3, out)) {
            if (error) *error = ERROR_NO_UNICODE_TRANSLATION;
            return false;
        }
        return true;
    }
    if (data.size() >= 2 && data[0] == 0xFF && data[1] == 0xFE) {
        if (encoding) *encoding = L"utf16le";
        out.assign((const wchar_t*)&data[2], (data.size() - 2) / sizeof(wchar_t));
        return true;
    }
    if (data.size() >= 2 && data[0] == 0xFE && data[1] == 0xFF) {
        if (encoding) *encoding = L"utf16be";
        size_t count = (data.size() - 2) / 2;
        out.resize(count);
        for (size_t i = 0; i < count; ++i) {
            out[i] = (wchar_t)(((wchar_t)data[2 + i * 2] << 8) | data[3 + i * 2]);
        }
        return true;
    }
    std::wstring ansi;
    if (!data.empty()) {
        int needed = MultiByteToWideChar(CP_ACP, 0, (const char*)&data[0], (int)data.size(), NULL, 0);
        if (needed <= 0) {
            if (error) *error = GetLastError();
            return false;
        }
        ansi.resize((size_t)needed);
        MultiByteToWideChar(CP_ACP, 0, (const char*)&data[0], (int)data.size(), &ansi[0], needed);
    }
    out.swap(ansi);
    return true;
}

bool ReadTextFileAs(const std::wstring& path, const std::wstring& requestedEncoding,
                    std::wstring& out, DWORD* error) {
    std::wstring encoding = ToLower(requestedEncoding);
    if (encoding.empty() || encoding == L"auto") {
        return ReadTextFile(path, out, NULL, error);
    }
    std::vector<unsigned char> data;
    if (!ReadFileBytes(path, data, error)) return false;
    out.clear();
    size_t offset = 0;
    if (encoding == L"utf8" || encoding == L"utf-8") {
        if (data.size() >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) offset = 3;
        return DecodeUtf8Strict(data, offset, out);
    }
    if (encoding == L"utf16le" || encoding == L"utf-16le" || encoding == L"unicode") {
        if (data.size() >= 2 && data[0] == 0xFF && data[1] == 0xFE) offset = 2;
        if ((data.size() - offset) % 2 != 0) {
            if (error) *error = ERROR_INVALID_DATA;
            return false;
        }
        out.assign((const wchar_t*)&data[offset], (data.size() - offset) / sizeof(wchar_t));
        return true;
    }
    if (encoding == L"utf16be" || encoding == L"utf-16be") {
        if (data.size() >= 2 && data[0] == 0xFE && data[1] == 0xFF) offset = 2;
        if ((data.size() - offset) % 2 != 0) {
            if (error) *error = ERROR_INVALID_DATA;
            return false;
        }
        size_t count = (data.size() - offset) / 2;
        out.resize(count);
        for (size_t i = 0; i < count; ++i) {
            out[i] = (wchar_t)(((wchar_t)data[offset + i * 2] << 8) | data[offset + i * 2 + 1]);
        }
        return true;
    }
    if (!data.empty()) {
        int needed = MultiByteToWideChar(CP_ACP, 0, (const char*)&data[0], (int)data.size(), NULL, 0);
        if (needed <= 0) {
            if (error) *error = GetLastError();
            return false;
        }
        out.resize((size_t)needed);
        MultiByteToWideChar(CP_ACP, 0, (const char*)&data[0], (int)data.size(), &out[0], needed);
    }
    return true;
}

bool WriteTextFileAtomic(const std::wstring& path, const std::wstring& text,
                         const std::wstring& requestedEncoding, DWORD* error) {
    std::wstring encoding = requestedEncoding.empty() ? L"auto" : ToLower(requestedEncoding);
    if (encoding == L"auto") {
        std::vector<unsigned char> existing;
        if (ReadFileBytes(path, existing)) {
            if (existing.size() >= 3 && existing[0] == 0xEF && existing[1] == 0xBB && existing[2] == 0xBF) {
                encoding = L"utf8";
            } else if (existing.size() >= 2 && existing[0] == 0xFF && existing[1] == 0xFE) {
                encoding = L"utf16le";
            } else if (existing.size() >= 2 && existing[0] == 0xFE && existing[1] == 0xFF) {
                encoding = L"utf16be";
            } else {
                encoding = L"ansi";
            }
        } else {
            encoding = L"ansi";
        }
    }
    std::vector<unsigned char> data;
    if (encoding == L"utf8" || encoding == L"utf-8") {
        data.push_back(0xEF); data.push_back(0xBB); data.push_back(0xBF);
        std::string bytes = WideToUtf8(text);
        data.insert(data.end(), bytes.begin(), bytes.end());
    } else if (encoding == L"utf16le" || encoding == L"utf-16le" || encoding == L"unicode") {
        data.push_back(0xFF); data.push_back(0xFE);
        const unsigned char* p = (const unsigned char*)text.data();
        data.insert(data.end(), p, p + text.size() * sizeof(wchar_t));
    } else if (encoding == L"utf16be" || encoding == L"utf-16be") {
        data.push_back(0xFE); data.push_back(0xFF);
        for (size_t i = 0; i < text.size(); ++i) {
            wchar_t ch = text[i];
            data.push_back((unsigned char)((ch >> 8) & 0xff));
            data.push_back((unsigned char)(ch & 0xff));
        }
    } else {
        std::string bytes = WideToAnsi(text);
        data.insert(data.end(), bytes.begin(), bytes.end());
    }
    return WriteFileBytesAtomic(path, data, error);
}

bool AppendLogLine(const std::wstring& path, const std::string& line) {
    CRITICAL_SECTION& lock = GetLogLock();
    EnterCriticalSection(&lock);
    std::wstring directory = PathDirectory(path);
    if (!directory.empty()) EnsureDirectory(directory);
    HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    bool ok = false;
    if (file != INVALID_HANDLE_VALUE) {
        std::string record = line + "\r\n";
        DWORD written = 0;
        ok = WriteFile(file, record.data(), (DWORD)record.size(), &written, NULL) != 0;
        CloseHandle(file);
    }
    LeaveCriticalSection(&lock);
    return ok;
}

bool CopyFileQuiet(const std::wstring& source, const std::wstring& destination, bool overwrite) {
    std::wstring directory = PathDirectory(destination);
    if (!directory.empty()) EnsureDirectory(directory);
    return CopyFileW(source.c_str(), destination.c_str(), overwrite ? FALSE : TRUE) != 0;
}

bool CreateTempDirectory(const std::wstring& prefix, std::wstring& out) {
    std::wstring root = PathJoin(GetTempDirectory(), L"MCPAddin");
    if (!EnsureDirectory(root)) return false;
    for (int i = 0; i < 32; ++i) {
        std::wstring candidate = PathJoin(root, prefix + Utf8ToWide(NewGuidString()));
        if (EnsureDirectory(candidate)) {
            out = candidate;
            return true;
        }
    }
    return false;
}

bool RemoveDirectoryRecursiveQuiet(const std::wstring& path) {
    if (path.empty()) return true;
    DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) return true;
    if ((attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) return DeleteFileQuiet(path);
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW((path + L"\\*").c_str(), &data);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0) continue;
            std::wstring child = PathJoin(path, data.cFileName);
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                RemoveDirectoryRecursiveQuiet(child);
            } else {
                SetFileAttributesW(child.c_str(), FILE_ATTRIBUTE_NORMAL);
                DeleteFileQuiet(child);
            }
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }
    return RemoveDirectoryW(path.c_str()) != 0;
}

uint64_t FileSize(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return 0;
    return ((uint64_t)data.nFileSizeHigh << 32) | data.nFileSizeLow;
}

uint64_t UnixTimeFromFileTime(const FILETIME& ft) {
    ULARGE_INTEGER value;
    value.LowPart = ft.dwLowDateTime;
    value.HighPart = ft.dwHighDateTime;
    const uint64_t epoch = 116444736000000000ULL;
    if (value.QuadPart <= epoch) return 0;
    return (value.QuadPart - epoch) / 10000000ULL;
}

FILETIME FileTimeNow() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return ft;
}

long Variant::AsLong() const {
    switch (v_.vt) {
        case VT_I1: return v_.cVal;
        case VT_UI1: return v_.bVal;
        case VT_I2: return v_.iVal;
        case VT_UI2: return v_.uiVal;
        case VT_I4: return v_.lVal;
        case VT_UI4: return (long)v_.ulVal;
        case VT_INT: return v_.intVal;
        case VT_UINT: return (long)v_.uintVal;
        case VT_BOOL: return v_.boolVal ? 1 : 0;
        default: {
            VARIANT temp;
            VariantInit(&temp);
            if (SUCCEEDED(VariantChangeType(&temp, const_cast<VARIANT*>(&v_), 0, VT_I4))) {
                long result = temp.lVal;
                VariantClear(&temp);
                return result;
            }
            VariantClear(&temp);
            return 0;
        }
    }
}

bool Variant::AsBool() const {
    if (v_.vt == VT_BOOL) return v_.boolVal != VARIANT_FALSE;
    return AsLong() != 0;
}

std::wstring Variant::AsWString() const {
    if (v_.vt == VT_BSTR && v_.bstrVal) return std::wstring(v_.bstrVal, SysStringLen(v_.bstrVal));
    if (v_.vt == VT_NULL || v_.vt == VT_EMPTY) return std::wstring();
    VARIANT temp;
    VariantInit(&temp);
    if (SUCCEEDED(VariantChangeType(&temp, const_cast<VARIANT*>(&v_), 0, VT_BSTR))) {
        std::wstring result;
        if (temp.bstrVal) result.assign(temp.bstrVal, SysStringLen(temp.bstrVal));
        VariantClear(&temp);
        return result;
    }
    VariantClear(&temp);
    return std::wstring();
}

static HRESULT InvokeByName(IDispatch* object, const wchar_t* name, WORD flags,
                            const std::vector<Variant*>& args, Variant& result, DISPPARAMS* outParams) {
    if (!object) return E_POINTER;
    LPOLESTR names[1] = {const_cast<LPOLESTR>(name)};
    DISPID dispid = DISPID_UNKNOWN;
    HRESULT hr = object->GetIDsOfNames(IID_NULL, names, 1, LOCALE_USER_DEFAULT, &dispid);
    if (FAILED(hr)) return hr;

    std::vector<VARIANT> reversed(args.size());
    for (size_t i = 0; i < args.size(); ++i) {
        VariantInit(&reversed[args.size() - 1 - i]);
        if (args[i]) {
            VariantCopy(&reversed[args.size() - 1 - i], args[i]->get());
        }
    }
    DISPPARAMS params;
    params.rgvarg = reversed.empty() ? NULL : &reversed[0];
    params.rgdispidNamedArgs = NULL;
    params.cArgs = (UINT)reversed.size();
    params.cNamedArgs = 0;
    if (outParams) *outParams = params;

    EXCEPINFO except;
    memset(&except, 0, sizeof(except));
    UINT argError = 0;
    hr = object->Invoke(dispid, IID_NULL, LOCALE_USER_DEFAULT, flags, &params, result.out(), &except, &argError);
    for (size_t i = 0; i < reversed.size(); ++i) VariantClear(&reversed[i]);
    if (except.bstrSource) SysFreeString(except.bstrSource);
    if (except.bstrDescription) SysFreeString(except.bstrDescription);
    if (except.bstrHelpFile) SysFreeString(except.bstrHelpFile);
    return hr;
}

HRESULT GetProperty(IDispatch* object, const wchar_t* name, Variant& result) {
    std::vector<Variant*> noArgs;
    return GetProperty(object, name, noArgs, result);
}

HRESULT GetProperty(IDispatch* object, const wchar_t* name, const std::vector<Variant*>& args, Variant& result) {
    DISPPARAMS params;
    HRESULT hr = InvokeByName(object, name, DISPATCH_PROPERTYGET, args, result, &params);
    if (FAILED(hr)) {
        hr = InvokeByName(object, name, DISPATCH_METHOD | DISPATCH_PROPERTYGET, args, result, &params);
    }
    return hr;
}

HRESULT PutProperty(IDispatch* object, const wchar_t* name, const Variant& value) {
    if (!object) return E_POINTER;
    LPOLESTR names[1] = {const_cast<LPOLESTR>(name)};
    DISPID dispid = DISPID_UNKNOWN;
    HRESULT hr = object->GetIDsOfNames(IID_NULL, names, 1, LOCALE_USER_DEFAULT, &dispid);
    if (FAILED(hr)) return hr;
    VARIANT val;
    VariantInit(&val);
    VariantCopy(&val, value.get());
    DISPID putId = DISPID_PROPERTYPUT;
    DISPPARAMS params;
    params.rgvarg = &val;
    params.rgdispidNamedArgs = &putId;
    params.cArgs = 1;
    params.cNamedArgs = 1;
    EXCEPINFO except;
    memset(&except, 0, sizeof(except));
    UINT argError = 0;
    hr = object->Invoke(dispid, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_PROPERTYPUT, &params, NULL, &except, &argError);
    VariantClear(&val);
    if (except.bstrSource) SysFreeString(except.bstrSource);
    if (except.bstrDescription) SysFreeString(except.bstrDescription);
    if (except.bstrHelpFile) SysFreeString(except.bstrHelpFile);
    return hr;
}

HRESULT InvokeMethod(IDispatch* object, const wchar_t* name, const std::vector<Variant*>& args, Variant& result) {
    DISPPARAMS params;
    HRESULT hr = InvokeByName(object, name, DISPATCH_METHOD | DISPATCH_PROPERTYGET, args, result, &params);
    return hr;
}

HRESULT InvokeMethodByRef(IDispatch* object, const wchar_t* name, const std::vector<Variant*>& args,
                          Variant& result) {
    if (!object) return E_POINTER;
    LPOLESTR names[1] = {const_cast<LPOLESTR>(name)};
    DISPID dispid = DISPID_UNKNOWN;
    HRESULT hr = object->GetIDsOfNames(IID_NULL, names, 1, LOCALE_USER_DEFAULT, &dispid);
    if (FAILED(hr)) return hr;

    std::vector<VARIANT> reversed(args.size());
    for (size_t i = 0; i < args.size(); ++i) {
        VariantInit(&reversed[args.size() - 1 - i]);
        if (args[i]) VariantCopy(&reversed[args.size() - 1 - i], args[i]->get());
    }
    DISPPARAMS params;
    params.rgvarg = reversed.empty() ? NULL : &reversed[0];
    params.rgdispidNamedArgs = NULL;
    params.cArgs = (UINT)reversed.size();
    params.cNamedArgs = 0;

    EXCEPINFO except;
    memset(&except, 0, sizeof(except));
    UINT argError = 0;
    hr = object->Invoke(dispid, IID_NULL, LOCALE_USER_DEFAULT,
                        DISPATCH_METHOD | DISPATCH_PROPERTYGET, &params,
                        result.out(), &except, &argError);
    if (SUCCEEDED(hr)) {
        for (size_t i = 0; i < args.size(); ++i) {
            if (!args[i]) continue;
            VariantClear(args[i]->get());
            VariantCopy(args[i]->get(), &reversed[args.size() - 1 - i]);
        }
    }
    for (size_t i = 0; i < reversed.size(); ++i) VariantClear(&reversed[i]);
    if (except.bstrSource) SysFreeString(except.bstrSource);
    if (except.bstrDescription) SysFreeString(except.bstrDescription);
    if (except.bstrHelpFile) SysFreeString(except.bstrHelpFile);
    return hr;
}

HRESULT InvokeMethodNoResult(IDispatch* object, const wchar_t* name, const std::vector<Variant*>& args) {
    Variant result;
    return InvokeMethod(object, name, args, result);
}

bool GetBstrProperty(IDispatch* object, const wchar_t* name, std::wstring& value) {
    Variant result;
    if (FAILED(GetProperty(object, name, result))) return false;
    value = result.AsWString();
    return true;
}

bool GetLongProperty(IDispatch* object, const wchar_t* name, long& value) {
    Variant result;
    if (FAILED(GetProperty(object, name, result))) return false;
    value = result.AsLong();
    return true;
}

bool GetBoolProperty(IDispatch* object, const wchar_t* name, bool& value) {
    Variant result;
    if (FAILED(GetProperty(object, name, result))) return false;
    value = result.AsBool();
    return true;
}

bool CallBoolMethod(IDispatch* object, const wchar_t* name, const std::vector<Variant*>& args, bool& value) {
    Variant result;
    if (FAILED(InvokeMethod(object, name, args, result))) return false;
    value = result.AsBool();
    return true;
}

bool CallLongMethod(IDispatch* object, const wchar_t* name, const std::vector<Variant*>& args, long& value) {
    Variant result;
    if (FAILED(InvokeMethod(object, name, args, result))) return false;
    value = result.AsLong();
    return true;
}

bool CallVoid(IDispatch* object, const wchar_t* name, const std::vector<Variant*>& args) {
    return SUCCEEDED(InvokeMethodNoResult(object, name, args));
}

ComPtr<IDispatch> GetObjectProperty(IDispatch* object, const wchar_t* name) {
    std::vector<Variant*> noArgs;
    return GetObjectProperty(object, name, noArgs);
}

ComPtr<IDispatch> GetObjectProperty(IDispatch* object, const wchar_t* name, const std::vector<Variant*>& args) {
    Variant result;
    if (FAILED(GetProperty(object, name, args, result))) return ComPtr<IDispatch>();
    return ComPtr<IDispatch>(result.GetDispatch(), true);
}

ComPtr<IDispatch> GetCollectionItem(IDispatch* collection, long index) {
    Variant arg;
    arg.get()->vt = VT_I4;
    arg.get()->lVal = index;
    std::vector<Variant*> args;
    args.push_back(&arg);
    return GetObjectProperty(collection, L"Item", args);
}

ComPtr<IDispatch> GetCollectionItem(IDispatch* collection, const std::wstring& name) {
    Variant arg;
    arg.get()->vt = VT_BSTR;
    arg.get()->bstrVal = SysAllocString(name.c_str());
    std::vector<Variant*> args;
    args.push_back(&arg);
    return GetObjectProperty(collection, L"Item", args);
}

long GetCollectionCount(IDispatch* collection) {
    long count = 0;
    GetLongProperty(collection, L"Count", count);
    return count;
}

std::wstring HResultMessage(HRESULT hr) {
    wchar_t* buffer = NULL;
    DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    DWORD len = FormatMessageW(flags, NULL, (DWORD)hr, 0, (LPWSTR)&buffer, 0, NULL);
    std::wstring result;
    if (len && buffer) {
        result.assign(buffer, len);
        result = Trim(result);
    }
    if (buffer) LocalFree(buffer);
    if (result.empty()) result = GetLastErrorMessage((DWORD)hr);
    return result;
}

void SetDebugLogEnabled(bool enabled) {
    g_debugLog = enabled;
}

void LogDebug(const char* format, ...) {
    if (!g_debugLog) return;
    char buffer[2048];
    va_list args;
    va_start(args, format);
    _vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, format, args);
    va_end(args);
    OutputDebugStringA("[MCPAddin] ");
    OutputDebugStringA(buffer);
    OutputDebugStringA("\n");
}

} // namespace mcpaddin
