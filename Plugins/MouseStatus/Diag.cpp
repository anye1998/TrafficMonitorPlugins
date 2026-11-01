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

// 配置项都放在 ini 的 [config] 段，与 Battery / DateTime 等插件一致
const wchar_t* const kIniSection = L"config";
const wchar_t* const kIniKeyEnableLog = L"enable_log";
const wchar_t* const kIniFileName = L"MouseStatus.ini";

// v1.0.5 及更早版本把日志开关存在这里，现已改用 ini，仅在迁移时读取一次并清理
const wchar_t* const kLegacyRegKey = L"Software\\MouseStatus";
const wchar_t* const kLegacyRegValue = L"EnableLog";

// 取本 DLL 的模块句柄（不能依赖 MFC 的 AfxGetInstanceHandle）
char g_diag_anchor = 0;

struct DiagState
{
    CRITICAL_SECTION cs;
    std::wstring path;
    std::wstring dir;
    std::wstring ini_path;
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

std::wstring ModuleDirectory()
{
    HMODULE module = nullptr;
    if (!::GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCWSTR)&g_diag_anchor, &module))
    {
        return std::wstring();
    }

    wchar_t path[MAX_PATH] = { 0 };
    if (::GetModuleFileNameW(module, path, MAX_PATH) == 0)
        return std::wstring();

    std::wstring full = path;
    const size_t pos = full.find_last_of(L"\\/");
    if (pos == std::wstring::npos)
        return std::wstring();
    return full.substr(0, pos + 1);
}

// ini 路径：优先用主程序给的配置目录，其次退到本 DLL 所在目录
std::wstring ResolveIniPath(DiagState& st)
{
    if (!st.ini_path.empty())
        return st.ini_path;
    return ModuleDirectory() + kIniFileName;
}

// 读 ini 里的日志开关；键不存在时返回 false（调用方决定用默认值）
bool ReadIniEnabled(DiagState& st, bool& value)
{
    const std::wstring ini = ResolveIniPath(st);
    if (ini.empty())
        return false;

    if (::GetFileAttributesW(ini.c_str()) == INVALID_FILE_ATTRIBUTES)
        return false;

    wchar_t buf[16] = { 0 };
    const DWORD got = ::GetPrivateProfileStringW(kIniSection, kIniKeyEnableLog, L"",
        buf, (DWORD)(sizeof(buf) / sizeof(buf[0])), ini.c_str());
    if (got == 0 || buf[0] == L'\0')
        return false;

    value = (buf[0] != L'0');
    return true;
}

void WriteIniEnabled(DiagState& st, bool value)
{
    const std::wstring ini = ResolveIniPath(st);
    if (ini.empty())
        return;
    ::WritePrivateProfileStringW(kIniSection, kIniKeyEnableLog, value ? L"1" : L"0", ini.c_str());
}

// v1.0.5 及更早版本把开关存在注册表里。这里做一次性迁移：
//   1) ini 还没有该键时，把注册表的旧值搬过来，避免升级后设置丢失；
//   2) 删掉整个 HKCU\Software\MouseStatus —— 插件之后只在 ini 里存配置。
void MigrateLegacyRegistry(DiagState& st, bool& value, bool& has_value)
{
    HKEY key = nullptr;
    if (::RegOpenKeyExW(HKEY_CURRENT_USER, kLegacyRegKey, 0, KEY_READ, &key) == ERROR_SUCCESS)
    {
        DWORD data = 0;
        DWORD size = sizeof(data);
        DWORD type = 0;
        const bool ok = (::RegQueryValueExW(key, kLegacyRegValue, nullptr, &type,
                            (LPBYTE)&data, &size)
                            == ERROR_SUCCESS)
            && type == REG_DWORD;
        ::RegCloseKey(key);

        if (ok && !has_value)
        {
            value = (data != 0);
            has_value = true;
            WriteIniEnabled(st, value);
        }
    }

    // 无论上面有没有读到值，都把旧键清掉（不存在时静默返回错误，忽略即可）
    ::RegDeleteTreeW(HKEY_CURRENT_USER, kLegacyRegKey);
}

// 加载配置（调用方必须已持有锁）
void LoadSettings(DiagState& st)
{
    if (st.settings_loaded)
        return;
    st.settings_loaded = true;

    bool value = true;
    bool has_value = ReadIniEnabled(st, value);

    MigrateLegacyRegistry(st, value, has_value);

    if (!has_value)
    {
        // 两个来源都没有：用默认值，并立刻把 ini 落盘，
        // 这样插件目录里能直接看到配置文件，和别的插件的习惯一致。
        value = true;
        WriteIniEnabled(st, true);
    }

    st.enabled = st.hard_disabled ? false : value;
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

void SetConfigDir(const std::wstring& dir)
{
    if (dir.empty())
        return;

    std::wstring normalized = dir;
    if (normalized.back() != L'\\' && normalized.back() != L'/')
        normalized += L'\\';

    DiagState& st = State();
    ::EnterCriticalSection(&st.cs);
    const bool changed = (st.ini_path != normalized + kIniFileName);
    st.ini_path = normalized + kIniFileName;
    if (changed)
        st.settings_loaded = false;   // 让下一次 Enabled() 重新按 ini 加载
    ::LeaveCriticalSection(&st.cs);
}

std::wstring ConfigFilePath()
{
    DiagState& st = State();
    ::EnterCriticalSection(&st.cs);
    std::wstring p = ResolveIniPath(st);
    ::LeaveCriticalSection(&st.cs);
    return p;
}

int GetConfigInt(const wchar_t* key, int default_value, int min_value, int max_value)
{
    if (key == nullptr)
        return default_value;

    const std::wstring ini = ConfigFilePath();
    if (ini.empty())
        return default_value;

    wchar_t buf[32] = { 0 };
    const DWORD got = ::GetPrivateProfileStringW(kIniSection, key, L"",
        buf, (DWORD)(sizeof(buf) / sizeof(buf[0])), ini.c_str());
    if (got == 0 || buf[0] == L'\0')
        return default_value;

    // 只接受纯数字：避免 ini 里写成 "1s" / "1000ms" 这类形式时被 wcstol 静默截成 1
    for (const wchar_t* p = buf; *p != L'\0'; ++p)
    {
        if (*p < L'0' || *p > L'9')
            return default_value;
    }

    const long value = ::wcstol(buf, nullptr, 10);
    if (value < min_value || value > max_value)
        return default_value;
    return (int)value;
}

bool SetConfigInt(const wchar_t* key, int value)
{
    if (key == nullptr)
        return false;

    const std::wstring ini = ConfigFilePath();
    if (ini.empty())
        return false;

    wchar_t buf[32] = { 0 };
    ::swprintf_s(buf, L"%d", value);
    return ::WritePrivateProfileStringW(kIniSection, key, buf, ini.c_str()) != FALSE;
}

bool Enabled()
{
    DiagState& st = State();
    ::EnterCriticalSection(&st.cs);
    LoadSettings(st);
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
    WriteIniEnabled(st, write_value);
    ::LeaveCriticalSection(&st.cs);
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

// 节流日志：同一个 key 在 60 秒内只写一次。
// 主循环每秒都会跑的判定逻辑用它，避免「状态没变却一直刷同一行」。
void LogThrottled(const wchar_t* key, const wchar_t* fmt, ...)
{
    if (key == nullptr || fmt == nullptr || !Enabled())
        return;

    // 函数内静态对象的构造在 C++11 起是线程安全的，省得自己写双重检查
    struct Throttle
    {
        CRITICAL_SECTION cs;
        ULONGLONG tick{ 0 };
        std::wstring key;
        Throttle() { ::InitializeCriticalSection(&cs); }
    };
    static Throttle state;

    const ULONGLONG now = ::GetTickCount64();
    ::EnterCriticalSection(&state.cs);
    const bool skip = (!state.key.empty() && state.key == key && now - state.tick < 60000);
    if (!skip)
    {
        state.key = key;
        state.tick = now;
    }
    ::LeaveCriticalSection(&state.cs);
    if (skip)
        return;

    wchar_t body[1024] = { 0 };
    va_list ap;
    va_start(ap, fmt);
    ::_vsnwprintf_s(body, _countof(body), _TRUNCATE, fmt, ap);
    va_end(ap);
    Log(L"%s", body);
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
