// MouseStatus —— 轻量诊断日志实现
#include "Diag.h"

#include <windows.h>
#include <shellapi.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cwchar>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")

namespace {

constexpr ULONGLONG kMaxLogBytes = 1024 * 1024;   // 超过 1 MB 时滚转为 .old

const wchar_t* const kRegKey = L"Software\\MouseStatus";
const wchar_t* const kRegValueEnableLog = L"EnableLog";

struct DiagState
{
    CRITICAL_SECTION cs;
    std::wstring path;
    std::wstring dir;
    bool path_ready = false;
    bool enabled = true;
    bool hard_disabled = false;    // 环境变量强制关闭
    bool settings_loaded = false;
    ULONGLONG bytes_written = 0;

    DiagState()
    {
        ::InitializeCriticalSection(&cs);

        wchar_t buf[8] = { 0 };
        if (::GetEnvironmentVariableW(L"MOUSESTATUS_NOLOG", buf, 8) > 0 && buf[0] == L'1')
        {
            hard_disabled = true;
            enabled = false;
        }
    }

    ~DiagState()
    {
        ::DeleteCriticalSection(&cs);
    }
};

DiagState& State()
{
    // 故意「泄漏」这份状态：DLL 卸载时静态对象的析构顺序无法控制，若监视线程的最后
    // 几行日志落在析构之后就会踩已释放的 CRITICAL_SECTION。这里不回收，代价只有几十字节。
    static DiagState* s = new DiagState();
    return *s;
}

bool ReadRegEnabled(bool& value)
{
    HKEY key = nullptr;
    if (::RegOpenKeyExW(HKEY_CURRENT_USER, kRegKey, 0, KEY_READ, &key) != ERROR_SUCCESS)
        return false;

    DWORD data = 0;
    DWORD size = sizeof(data);
    DWORD type = 0;
    const bool ok = (::RegQueryValueExW(key, kRegValueEnableLog, nullptr, &type,
                         (LPBYTE)&data, &size)
                        == ERROR_SUCCESS)
        && type == REG_DWORD;
    ::RegCloseKey(key);
    if (!ok)
        return false;

    value = (data != 0);
    return true;
}

void WriteRegEnabled(bool value)
{
    HKEY key = nullptr;
    if (::RegCreateKeyExW(HKEY_CURRENT_USER, kRegKey, 0, nullptr, 0, KEY_WRITE, nullptr, &key,
            nullptr)
        != ERROR_SUCCESS)
    {
        return;
    }
    const DWORD data = value ? 1u : 0u;
    ::RegSetValueExW(key, kRegValueEnableLog, 0, REG_DWORD, (const BYTE*)&data, sizeof(data));
    ::RegCloseKey(key);
}

// 取 %LOCALAPPDATA%\MouseStatus，失败则退到 %TEMP%
void ResolvePath(DiagState& st)
{
    if (st.path_ready)
        return;
    st.path_ready = true;

    std::wstring base;
    wchar_t buf[MAX_PATH] = { 0 };
    if (::GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH) > 0)
        base = buf;
    if (base.empty() && ::GetEnvironmentVariableW(L"TEMP", buf, MAX_PATH) > 0)
        base = buf;
    if (base.empty())
        base = L".";

    st.dir = base + L"\\MouseStatus";
    ::CreateDirectoryW(st.dir.c_str(), nullptr);
    st.path = st.dir + L"\\MouseStatus.log";
}

// 首次写入时落 BOM，保证记事本能正确识别 UTF-8
void EnsureFile(DiagState& st)
{
    const DWORD attr = ::GetFileAttributesW(st.path.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES)
    {
        HANDLE h = ::CreateFileW(st.path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE)
        {
            LARGE_INTEGER size;
            size.QuadPart = 0;
            ::GetFileSizeEx(h, &size);
            st.bytes_written = (ULONGLONG)size.QuadPart;
            ::CloseHandle(h);
        }
        if (st.bytes_written > kMaxLogBytes)
        {
            const std::wstring old_path = st.path + L".old";
            ::DeleteFileW(old_path.c_str());
            ::MoveFileW(st.path.c_str(), old_path.c_str());
            st.bytes_written = 0;
            ::DeleteFileW(st.path.c_str());
        }
        if (::GetFileAttributesW(st.path.c_str()) != INVALID_FILE_ATTRIBUTES)
            return;
    }

    HANDLE h = ::CreateFileW(st.path.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
        nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return;
    const unsigned char bom[3] = { 0xEF, 0xBB, 0xBF };
    DWORD written = 0;
    ::WriteFile(h, bom, 3, &written, nullptr);
    st.bytes_written = written;
    ::CloseHandle(h);
}

void WriteRaw(DiagState& st, const std::string& utf8)
{
    HANDLE h = ::CreateFileW(st.path.c_str(), FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return;
    DWORD written = 0;
    ::WriteFile(h, utf8.data(), (DWORD)utf8.size(), &written, nullptr);
    ::CloseHandle(h);
    st.bytes_written += written;
}

std::string ToUtf8(const wchar_t* text, int len)
{
    if (len <= 0)
        return std::string();
    const int need = ::WideCharToMultiByte(CP_UTF8, 0, text, len, nullptr, 0, nullptr, nullptr);
    if (need <= 0)
        return std::string();
    std::string out((size_t)need, '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, text, len, &out[0], need, nullptr, nullptr);
    return out;
}

} // namespace

namespace diag {

bool Enabled()
{
    DiagState& st = State();
    ::EnterCriticalSection(&st.cs);
    if (!st.settings_loaded)
    {
        st.settings_loaded = true;
        bool reg_value = true;
        if (!st.hard_disabled && ReadRegEnabled(reg_value))
            st.enabled = reg_value;
        if (st.hard_disabled)
            st.enabled = false;
    }
    const bool value = st.enabled;
    ::LeaveCriticalSection(&st.cs);
    return value;
}

void SetEnabled(bool enabled)
{
    DiagState& st = State();
    ::EnterCriticalSection(&st.cs);
    st.settings_loaded = true;
    st.enabled = st.hard_disabled ? false : enabled;
    const bool write_value = st.enabled;
    ::LeaveCriticalSection(&st.cs);

    WriteRegEnabled(write_value);
}

std::wstring LogFilePath()
{
    DiagState& st = State();
    ::EnterCriticalSection(&st.cs);
    ResolvePath(st);
    std::wstring p = st.path;
    ::LeaveCriticalSection(&st.cs);
    return p;
}

std::wstring LogDirectory()
{
    DiagState& st = State();
    ::EnterCriticalSection(&st.cs);
    ResolvePath(st);
    std::wstring d = st.dir;
    ::LeaveCriticalSection(&st.cs);
    return d;
}

bool DeleteLogFiles()
{
    DiagState& st = State();
    ::EnterCriticalSection(&st.cs);
    ResolvePath(st);
    const std::wstring log_path = st.path;
    const std::wstring old_path = st.path + L".old";
    st.bytes_written = 0;
    ::LeaveCriticalSection(&st.cs);

    // 日志文件每次写都是「打开-追加-关闭」，不会长期占用句柄，直接删即可
    const bool ok_log = (::DeleteFileW(log_path.c_str()) != FALSE)
        || (::GetLastError() == ERROR_FILE_NOT_FOUND);
    ::DeleteFileW(old_path.c_str());

    if (ok_log)
    {
        // 删完立刻写一条，确认日志通道仍然正常（如果日志是关的就不会写）
        Log(L"=== 日志已被手动清空 ===");
    }
    return ok_log;
}

unsigned long long LogFileSize()
{
    DiagState& st = State();
    ::EnterCriticalSection(&st.cs);
    ResolvePath(st);
    std::wstring p = st.path;
    ::LeaveCriticalSection(&st.cs);

    HANDLE h = ::CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return 0;

    LARGE_INTEGER size;
    size.QuadPart = 0;
    ::GetFileSizeEx(h, &size);
    ::CloseHandle(h);
    return (unsigned long long)size.QuadPart;
}

bool OpenLogFolder()
{
    const std::wstring dir = LogDirectory();
    const HINSTANCE inst = ::ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr,
        SW_SHOWNORMAL);
    return (INT_PTR)inst > 32;
}

void Log(const wchar_t* fmt, ...)
{
    DiagState& st = State();
    if (fmt == nullptr || !Enabled())
        return;

    wchar_t body[2048] = { 0 };
    va_list ap;
    va_start(ap, fmt);
    ::_vsnwprintf_s(body, _countof(body), _TRUNCATE, fmt, ap);
    va_end(ap);

    SYSTEMTIME now;
    ::GetLocalTime(&now);

    wchar_t line[2200] = { 0 };
    ::_snwprintf_s(line, _countof(line), _TRUNCATE, L"[%02u:%02u:%02u.%03u] %s\r\n", now.wHour,
        now.wMinute, now.wSecond, now.wMilliseconds, body);

    const std::string utf8 = ToUtf8(line, (int)wcslen(line));

    ::EnterCriticalSection(&st.cs);
    ResolvePath(st);
    EnsureFile(st);
    WriteRaw(st, utf8);
    ::LeaveCriticalSection(&st.cs);
}

void LogHex(const wchar_t* label, const unsigned char* data, unsigned int len)
{
    if (data == nullptr || len == 0 || !Enabled())
        return;

    if (len > 64)
        len = 64;

    wchar_t body[512] = { 0 };
    int pos = ::_snwprintf_s(body, _countof(body), _TRUNCATE, L"%s [%u]",
        label == nullptr ? L"" : label, len);
    if (pos < 0)
        return;

    for (unsigned int i = 0; i < len && pos + 4 < (int)(sizeof(body) / sizeof(body[0])); ++i)
    {
        wchar_t hex[8] = { 0 };
        ::_snwprintf_s(hex, _countof(hex), _TRUNCATE, L" %02X", data[i]);
        for (int k = 0; hex[k] != L'\0' && pos + 1 < (int)(sizeof(body) / sizeof(body[0])); ++k)
            body[pos++] = hex[k];
        body[pos] = L'\0';
    }

    Log(L"%s", body);
}

} // namespace diag
