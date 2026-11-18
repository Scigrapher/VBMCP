#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <objbase.h>
#include <oleauto.h>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <stdint.h>

namespace mcpaddin {

std::string WideToUtf8(const std::wstring& value);
std::wstring Utf8ToWide(const std::string& value);
std::string WideToAnsi(const std::wstring& value);
std::wstring AnsiToWide(const std::string& value);

std::string Trim(const std::string& value);
std::wstring Trim(const std::wstring& value);
bool IEquals(const std::string& a, const std::string& b);
bool IEquals(const std::wstring& a, const std::wstring& b);
std::string ToLowerAscii(const std::string& value);
std::wstring ToLower(const std::wstring& value);
std::string HexEncode(const std::vector<unsigned char>& bytes);
std::string NewGuidString();
std::string FormatHresult(HRESULT hr);
std::wstring GetLastErrorMessage(DWORD error = 0);

std::wstring GetModulePath(HMODULE module = NULL);
std::wstring GetModuleDirectory(HMODULE module = NULL);
std::wstring GetAppDataDirectory();
std::wstring GetLocalAppDataDirectory();
std::wstring GetTempDirectory();
std::wstring GetCurrentDirectoryString();
std::wstring GetEnvVar(const wchar_t* name);
bool FileExists(const std::wstring& path);
bool DirectoryExists(const std::wstring& path);
bool EnsureDirectory(const std::wstring& path);
bool DeleteFileQuiet(const std::wstring& path);
bool IsAbsolutePath(const std::wstring& path);
std::wstring GetFullPath(const std::wstring& path, const std::wstring& base = L"");
std::wstring PathDirectory(const std::wstring& path);
std::wstring PathFileName(const std::wstring& path);
std::wstring PathStem(const std::wstring& path);
std::wstring PathExtension(const std::wstring& path);
std::wstring PathJoin(const std::wstring& a, const std::wstring& b);
std::wstring PathChangeExtension(const std::wstring& path, const std::wstring& ext);
std::wstring NormalizePathForCompare(const std::wstring& path);
bool IsPathWithin(const std::wstring& path, const std::wstring& root);
bool WildcardMatch(const std::wstring& pattern, const std::wstring& value);

bool ReadFileBytes(const std::wstring& path, std::vector<unsigned char>& out, DWORD* error = NULL);
bool WriteFileBytesAtomic(const std::wstring& path, const std::vector<unsigned char>& data, DWORD* error = NULL);
bool ReadTextFile(const std::wstring& path, std::wstring& out, std::wstring* encoding = NULL, DWORD* error = NULL);
bool ReadTextFileAs(const std::wstring& path, const std::wstring& encoding,
                    std::wstring& out, DWORD* error = NULL);
bool WriteTextFileAtomic(const std::wstring& path, const std::wstring& text,
                         const std::wstring& encoding = L"auto", DWORD* error = NULL);
bool AppendLogLine(const std::wstring& path, const std::string& line);
bool CopyFileQuiet(const std::wstring& source, const std::wstring& destination, bool overwrite);
bool CreateTempDirectory(const std::wstring& prefix, std::wstring& out);
bool RemoveDirectoryRecursiveQuiet(const std::wstring& path);

uint64_t FileSize(const std::wstring& path);
uint64_t UnixTimeFromFileTime(const FILETIME& ft);
FILETIME FileTimeNow();

template <typename T>
class ComPtr {
public:
    ComPtr() : p_(NULL) {}
    explicit ComPtr(T* p, bool addRef = false) : p_(p) {
        if (p_ && addRef) p_->AddRef();
    }
    ComPtr(const ComPtr& other) : p_(other.p_) {
        if (p_) p_->AddRef();
    }
    ~ComPtr() { Reset(); }
    ComPtr& operator=(const ComPtr& other) {
        if (this != &other) {
            T* old = p_;
            p_ = other.p_;
            if (p_) p_->AddRef();
            if (old) old->Release();
        }
        return *this;
    }
    void Reset(T* p = NULL, bool addRef = false) {
        if (p && addRef) p->AddRef();
        if (p_) p_->Release();
        p_ = p;
    }
    T* get() const { return p_; }
    T** put() {
        Reset();
        return &p_;
    }
    T* operator->() const { return p_; }
    explicit operator bool() const { return p_ != NULL; }
private:
    T* p_;
};

class Variant {
public:
    Variant() { VariantInit(&v_); }
    ~Variant() { VariantClear(&v_); }
    Variant(const Variant&) = delete;
    Variant& operator=(const Variant&) = delete;
    VARIANT* get() { return &v_; }
    const VARIANT* get() const { return &v_; }
    VARIANT* out() { VariantClear(&v_); VariantInit(&v_); return &v_; }
    bool IsString() const { return v_.vt == VT_BSTR; }
    bool IsDispatch() const { return v_.vt == VT_DISPATCH && v_.pdispVal != NULL; }
    bool IsLong() const { return v_.vt == VT_I4 || v_.vt == VT_I2 || v_.vt == VT_UI4 || v_.vt == VT_UI2; }
    long AsLong() const;
    bool AsBool() const;
    std::wstring AsWString() const;
    std::string AsString() const { return WideToUtf8(AsWString()); }
    IDispatch* GetDispatch() const {
        if (v_.vt == VT_DISPATCH) return v_.pdispVal;
        return NULL;
    }
private:
    VARIANT v_;
};

HRESULT GetProperty(IDispatch* object, const wchar_t* name, Variant& result);
HRESULT GetProperty(IDispatch* object, const wchar_t* name, const std::vector<Variant*>& args, Variant& result);
HRESULT PutProperty(IDispatch* object, const wchar_t* name, const Variant& value);
HRESULT InvokeMethod(IDispatch* object, const wchar_t* name, const std::vector<Variant*>& args, Variant& result);
HRESULT InvokeMethodByRef(IDispatch* object, const wchar_t* name, const std::vector<Variant*>& args,
                          Variant& result);
HRESULT InvokeMethodNoResult(IDispatch* object, const wchar_t* name, const std::vector<Variant*>& args);
bool GetBstrProperty(IDispatch* object, const wchar_t* name, std::wstring& value);
bool GetLongProperty(IDispatch* object, const wchar_t* name, long& value);
bool GetBoolProperty(IDispatch* object, const wchar_t* name, bool& value);
bool CallBoolMethod(IDispatch* object, const wchar_t* name, const std::vector<Variant*>& args, bool& value);
bool CallLongMethod(IDispatch* object, const wchar_t* name, const std::vector<Variant*>& args, long& value);
bool CallVoid(IDispatch* object, const wchar_t* name, const std::vector<Variant*>& args);
ComPtr<IDispatch> GetObjectProperty(IDispatch* object, const wchar_t* name);
ComPtr<IDispatch> GetObjectProperty(IDispatch* object, const wchar_t* name, const std::vector<Variant*>& args);
ComPtr<IDispatch> GetCollectionItem(IDispatch* collection, long index);
ComPtr<IDispatch> GetCollectionItem(IDispatch* collection, const std::wstring& name);
long GetCollectionCount(IDispatch* collection);

std::wstring HResultMessage(HRESULT hr);

void LogDebug(const char* format, ...);
void SetDebugLogEnabled(bool enabled);

} // namespace mcpaddin
