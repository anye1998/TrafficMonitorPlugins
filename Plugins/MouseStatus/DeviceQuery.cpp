// MouseStatus —— 通用设备查询实现
#include "DeviceQuery.h"

#include <windows.h>
#include <setupapi.h>
#include <devpropdef.h>
#include <cfgmgr32.h>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <string.h>
#include <cwchar>
#include <cwctype>

extern "C"
{
#include <hidsdi.h>
#include <hidpi.h>
}

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "hid.lib")
#pragma comment(lib, "cfgmgr32.lib")

namespace {

// 蓝牙设备节点上缓存电量的设备属性 {104EA319-6EE2-4701-BD47-8DDBF425BBE5}, 2
// Windows「设置」里显示的蓝牙外设电量就是从它读出来的，类型通常为 BYTE。
const DEVPROPKEY kPkeyBluetoothBattery = {
    { 0x104EA319, 0x6EE2, 0x4701, { 0xBD, 0x47, 0x8D, 0xDB, 0xF4, 0x25, 0xBB, 0xE5 } },
    2 };

const USAGE kUsagePageGeneric = 0x0006;          // Generic Device Controls
const USAGE kUsageBatteryStrength = 0x0020;      // Battery Strength
const USAGE kUsagePageDesktop = 0x0001;          // Generic Desktop Controls
const USAGE kUsageMouse = 0x0002;                // Mouse

void SetDetailSize(PSP_DEVICE_INTERFACE_DETAIL_DATA_W p)
{
#ifdef _WIN64
    p->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
#else
    // 32 位下 sizeof 会带上对齐填充，而驱动要求 4 + sizeof(WCHAR) = 6
    p->cbSize = 6;
#endif
}

std::wstring ToLower(const std::wstring& s)
{
    std::wstring r = s;
    for (auto& ch : r)
        ch = (wchar_t)::towlower(ch);
    return r;
}

HANDLE OpenHidShared(const std::wstring& path)
{
    HANDLE h = ::CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        h = ::CreateFileW(path.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        h = ::CreateFileW(path.c_str(), 0,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    return h;
}

bool ParseVidPid(const std::wstring& lower_path, unsigned short& vid, unsigned short& pid)
{
    vid = 0;
    pid = 0;
    const wchar_t* p_vid = ::wcsstr(lower_path.c_str(), L"vid_");
    const wchar_t* p_pid = ::wcsstr(lower_path.c_str(), L"pid_");
    if (p_vid == nullptr || p_pid == nullptr)
        return false;
    vid = (unsigned short)::wcstoul(p_vid + 4, nullptr, 16);
    pid = (unsigned short)::wcstoul(p_pid + 4, nullptr, 16);
    return true;
}

// 设备接口路径形如 \\?\hid#vid_046d&pid_c52b&mi_00#8&2f1a3c4d&0&0000#{4d1e...}
// 同一物理设备的各个集合共享第 2、3 段，用它做分组标识
std::wstring ParseDeviceGroup(const std::wstring& path)
{
    const size_t p1 = path.find(L'#');
    if (p1 == std::wstring::npos)
        return std::wstring();
    const size_t p2 = path.find(L'#', p1 + 1);
    if (p2 == std::wstring::npos)
        return path.substr(p1 + 1);
    const size_t p3 = path.find(L'#', p2 + 1);
    if (p3 == std::wstring::npos)
        return path.substr(p1 + 1);
    return path.substr(p1 + 1, p3 - p1 - 1);
}

HidTransport ClassifyTransport(const std::wstring& lower_path)
{
    // 蓝牙 HID 的设备接口路径里带着蓝牙服务的 UUID：
    //   00001124-0000-1000-8000-00805F9B34FB  HID over BR/EDR（经典蓝牙）
    //   00001812-0000-1000-8000-00805F9B34FB  HID over GATT（低功耗蓝牙）
    if (lower_path.find(L"{00001812") != std::wstring::npos)
        return HidTransport::BtLe;
    if (lower_path.find(L"{00001124") != std::wstring::npos)
        return HidTransport::BtClassic;
    if (lower_path.find(L"hid#") != std::wstring::npos)
        return HidTransport::Usb;
    return HidTransport::Unknown;
}

int ClampPercent(long v)
{
    if (v < 0)
        return 0;
    if (v > 100)
        return 100;
    return (int)v;
}

// 依据 HID 值能力把原始读数换算成百分比
int ScaleToPercent(ULONG raw, const HIDP_VALUE_CAPS& cap)
{
    const long lmin = (long)cap.LogicalMin;
    const long lmax = (long)cap.LogicalMax;
    if (lmax <= 0)
        return ClampPercent((long)raw);
    if (lmin == 0 && lmax <= 100)
        return ClampPercent((long)raw);
    const long span = lmax - lmin;
    if (span <= 0)
        return ClampPercent((long)raw);
    return ClampPercent(((long)raw - lmin) * 100 / span);
}

// 在给定报文类型里找 Battery Strength 能力
//
// 用 HidP_GetSpecificValueCaps 直接把 Usage Page / Usage 作为筛选条件，避免自己去翻
// HIDP_VALUE_CAPS 里的联合体（不同 SDK 版本里的成员名不一样，踩过一次坑）。
bool FindBatteryCap(PHIDP_PREPARSED_DATA pp, HIDP_REPORT_TYPE type, HIDP_VALUE_CAPS& out)
{
    HIDP_VALUE_CAPS caps[4];
    ::ZeroMemory(caps, sizeof(caps));
    USHORT length = (USHORT)(sizeof(caps) / sizeof(caps[0]));

    const NTSTATUS status = ::HidP_GetSpecificValueCaps(type, kUsagePageGeneric, 0,
        kUsageBatteryStrength, caps, &length, pp);
    if (status != HIDP_STATUS_SUCCESS && status != HIDP_STATUS_BUFFER_TOO_SMALL)
        return false;
    if (length == 0)
        return false;

    out = caps[0];
    return true;
}

std::wstring ReadProductString(HANDLE h)
{
    wchar_t buf[256] = { 0 };
    if (::HidD_GetProductString(h, buf, sizeof(buf)))
        return std::wstring(buf);
    return std::wstring();
}

long ReadNumericProp(const BYTE* value, DWORD size, DEVPROPTYPE type)
{
    if (type == DEVPROP_TYPE_BYTE && size >= 1)
        return (long)value[0];
    if (type == DEVPROP_TYPE_UINT16 && size >= 2)
    {
        unsigned short v = 0;
        std::memcpy(&v, value, sizeof(v));
        return (long)v;
    }
    if (type == DEVPROP_TYPE_UINT32 && size >= 4)
    {
        unsigned int v = 0;
        std::memcpy(&v, value, sizeof(v));
        return (long)v;
    }
    if (type == DEVPROP_TYPE_INT32 && size >= 4)
    {
        int v = 0;
        std::memcpy(&v, value, sizeof(v));
        return (long)v;
    }
    if (type == DEVPROP_TYPE_STRING && size >= sizeof(wchar_t))
        return ::wcstol((const wchar_t*)value, nullptr, 10);
    return -1;
}

} // namespace

std::vector<HidIfaceInfo> EnumHidInterfaces()
{
    std::vector<HidIfaceInfo> result;

    GUID hid_guid;
    ::HidD_GetHidGuid(&hid_guid);
    HDEVINFO h_dev = ::SetupDiGetClassDevsW(&hid_guid, nullptr, nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (h_dev == INVALID_HANDLE_VALUE)
        return result;

    SP_DEVICE_INTERFACE_DATA did;
    ::ZeroMemory(&did, sizeof(did));
    did.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    for (DWORD i = 0; ::SetupDiEnumDeviceInterfaces(h_dev, nullptr, &hid_guid, i, &did); ++i)
    {
        DWORD need = 0;
        ::SetupDiGetDeviceInterfaceDetailW(h_dev, &did, nullptr, 0, &need, nullptr);
        if (need == 0)
            continue;
        PSP_DEVICE_INTERFACE_DETAIL_DATA_W detail =
            (PSP_DEVICE_INTERFACE_DETAIL_DATA_W)::malloc(need);
        if (detail == nullptr)
            continue;
        SetDetailSize(detail);
        if (::SetupDiGetDeviceInterfaceDetailW(h_dev, &did, detail, need, nullptr, nullptr))
        {
            HidIfaceInfo info;
            info.path = detail->DevicePath;
            info.group = ParseDeviceGroup(info.path);
            const std::wstring lower = ToLower(info.path);
            ParseVidPid(lower, info.vid, info.pid);
            info.transport = ClassifyTransport(lower);
            HANDLE h = OpenHidShared(info.path);
            if (h != INVALID_HANDLE_VALUE)
            {
                info.product = ReadProductString(h);
                ::CloseHandle(h);
            }
            result.push_back(info);
        }
        ::free(detail);
    }
    ::SetupDiDestroyDeviceInfoList(h_dev);
    return result;
}

bool ProbeHidInterface(const std::wstring& path, HidProbeResult& out)
{
    out = HidProbeResult();

    HANDLE h = OpenHidShared(path);
    if (h == INVALID_HANDLE_VALUE)
        return false;

    PHIDP_PREPARSED_DATA pp = nullptr;
    if (!::HidD_GetPreparsedData(h, &pp))
    {
        ::CloseHandle(h);
        return false;
    }

    HIDP_CAPS caps;
    ::ZeroMemory(&caps, sizeof(caps));
    if (::HidP_GetCaps(pp, &caps) == HIDP_STATUS_SUCCESS)
    {
        out.opened = true;
        out.usage_page = caps.UsagePage;
        out.usage = caps.Usage;
        out.input_report_len = caps.InputReportByteLength;
        out.output_report_len = caps.OutputReportByteLength;
        out.feature_report_len = caps.FeatureReportByteLength;
        out.is_mouse_collection = (caps.UsagePage == kUsagePageDesktop && caps.Usage == kUsageMouse);

        HIDP_VALUE_CAPS cap;
        ::ZeroMemory(&cap, sizeof(cap));

        // 优先特性报文：主机主动读取，不占用鼠标的输入通道
        if (caps.FeatureReportByteLength > 0
            && FindBatteryCap(pp, HidP_Feature, cap))
        {
            out.has_battery_usage = true;
            std::vector<BYTE> buf((size_t)caps.FeatureReportByteLength, 0);
            buf[0] = cap.ReportID;
            if (::HidD_GetFeature(h, buf.data(), (ULONG)buf.size()))
            {
                ULONG raw = 0;
                if (::HidP_GetUsageValue(HidP_Feature, kUsagePageGeneric, cap.LinkCollection,
                        kUsageBatteryStrength, &raw, pp, (PCHAR)buf.data(), (ULONG)buf.size())
                    == HIDP_STATUS_SUCCESS)
                {
                    out.battery_percent = ScaleToPercent(raw, cap);
                }
            }
        }

        // 退而求其次：输入报文（用 GET_REPORT 取，避免抢占鼠标的输入通道）
        if (out.battery_percent < 0 && caps.InputReportByteLength > 0
            && FindBatteryCap(pp, HidP_Input, cap))
        {
            out.has_battery_usage = true;
            std::vector<BYTE> buf((size_t)caps.InputReportByteLength, 0);
            buf[0] = cap.ReportID;
            if (::HidD_GetInputReport(h, buf.data(), (ULONG)buf.size()))
            {
                ULONG raw = 0;
                if (::HidP_GetUsageValue(HidP_Input, kUsagePageGeneric, cap.LinkCollection,
                        kUsageBatteryStrength, &raw, pp, (PCHAR)buf.data(), (ULONG)buf.size())
                    == HIDP_STATUS_SUCCESS)
                {
                    out.battery_percent = ScaleToPercent(raw, cap);
                }
            }
        }
    }

    ::HidD_FreePreparsedData(pp);
    ::CloseHandle(h);
    return out.opened;
}

bool ReadHidBatteryStrength(const std::wstring& path, int& percent)
{
    HidProbeResult probe;
    percent = -1;
    if (!ProbeHidInterface(path, probe))
        return false;
    if (probe.battery_percent < 0)
        return false;
    percent = probe.battery_percent;
    return true;
}

bool DevNodeContainsMouse(const std::wstring& instance_id)
{
    DEVINST dev = 0;
    if (::CM_Locate_DevNodeW(&dev, (DEVINSTID_W)instance_id.c_str(),
            CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS)
    {
        return false;
    }

    // 广度优先向下找 3 层：Dev_xxx -> HID\{...} -> HID-compliant mouse
    DEVINST queue[64];
    int count = 0;
    queue[count++] = dev;
    int level_start = 0;

    for (int depth = 0; depth < 3; ++depth)
    {
        const int level_end = count;
        for (int i = level_start; i < level_end; ++i)
        {
            DEVINST child = 0;
            if (::CM_Get_Child(&child, queue[i], 0) != CR_SUCCESS)
                continue;
            do
            {
                wchar_t cls[64] = { 0 };
                ULONG size = (ULONG)sizeof(cls);
                ULONG type = 0;
                if (::CM_Get_DevNode_Registry_PropertyW(child, CM_DRP_CLASS, &type, cls,
                        &size, 0) == CR_SUCCESS)
                {
                    if (_wcsicmp(cls, L"Mouse") == 0)
                        return true;
                }
                if (count < (int)(sizeof(queue) / sizeof(queue[0])))
                    queue[count++] = child;
            } while (::CM_Get_Sibling(&child, child, 0) == CR_SUCCESS);
        }
        level_start = level_end;
        if (level_start >= count)
            break;
    }
    return false;
}

std::vector<BtBatteryEntry> QueryBluetoothBatteries()
{
    std::vector<BtBatteryEntry> result;

    HDEVINFO h_dev = ::SetupDiGetClassDevsW(nullptr, nullptr, nullptr,
        DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (h_dev == INVALID_HANDLE_VALUE)
        return result;

    SP_DEVINFO_DATA did;
    ::ZeroMemory(&did, sizeof(did));
    did.cbSize = sizeof(SP_DEVINFO_DATA);

    for (DWORD i = 0; ::SetupDiEnumDeviceInfo(h_dev, i, &did); ++i)
    {
        wchar_t id[512] = { 0 };
        if (!::SetupDiGetDeviceInstanceIdW(h_dev, &did, id, ARRAYSIZE(id), nullptr))
            continue;

        const bool is_bt_classic = (_wcsnicmp(id, L"BTHENUM\\", 8) == 0);
        const bool is_bt_le = (_wcsnicmp(id, L"BTHLE\\", 6) == 0);
        if (!is_bt_classic && !is_bt_le)
            continue;

        // 只认 Dev_ 开头的节点，它们才代表设备本身；其余是各 Profile 的子节点
        if (::wcsstr(id, L"Dev_") == nullptr)
            continue;

        DEVPROPTYPE type = 0;
        BYTE value[64] = { 0 };
        DWORD size = 0;
        if (!::SetupDiGetDevicePropertyW(h_dev, &did, &kPkeyBluetoothBattery, &type,
                value, (DWORD)sizeof(value), &size, 0))
            continue;

        const long level = ReadNumericProp(value, size, type);
        if (level < 0 || level > 100)
            continue;

        BtBatteryEntry entry;
        entry.instance_id = id;
        entry.le = is_bt_le;
        entry.percent = (int)level;

        wchar_t name[256] = { 0 };
        if (!::SetupDiGetDeviceRegistryPropertyW(h_dev, &did, SPDRP_FRIENDLYNAME, nullptr,
                (PBYTE)name, (DWORD)sizeof(name), nullptr))
        {
            ::SetupDiGetDeviceRegistryPropertyW(h_dev, &did, SPDRP_DEVICEDESC, nullptr,
                (PBYTE)name, (DWORD)sizeof(name), nullptr);
        }
        entry.name = name;
        entry.is_mouse = NameLooksLikeMouse(entry.name) || DevNodeContainsMouse(entry.instance_id);
        result.push_back(entry);
    }

    ::SetupDiDestroyDeviceInfoList(h_dev);
    return result;
}

bool NameLooksLikeMouse(const std::wstring& name)
{
    const std::wstring lower = ToLower(name);
    static const wchar_t* const kKeywords[] = {
        L"mouse",   // 英文
        L"鼠标",     // 简体
        L"滑鼠",     // 繁体
        L"maus",    // 德语
        L"マウス",   // 日语
    };
    for (const wchar_t* kw : kKeywords)
    {
        if (lower.find(kw) != std::wstring::npos)
            return true;
    }
    return false;
}
