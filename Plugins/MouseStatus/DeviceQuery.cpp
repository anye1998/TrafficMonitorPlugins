// MouseStatus —— 通用设备查询实现
#include "DeviceQuery.h"

#include <windows.h>
#include <setupapi.h>
#include <devpropdef.h>
#include <cfgmgr32.h>
#include <bluetoothapis.h>
#include <algorithm>
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
#pragma comment(lib, "Bthprops.lib")

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

// 大小写不敏感的查找。设备实例 ID 里 "Dev_" 的大小写并不固定
// （蓝牙 HID 的接口路径里就是小写 _dev_），用 wcsstr 会漏掉。
size_t FindNoCase(const std::wstring& haystack, const wchar_t* needle)
{
    if (needle == nullptr || *needle == L'\0')
        return std::wstring::npos;
    const std::wstring lower_hay = ToLower(haystack);
    const std::wstring lower_needle = ToLower(std::wstring(needle));
    return lower_hay.find(lower_needle);
}

bool ContainsNoCase(const std::wstring& haystack, const wchar_t* needle)
{
    return FindNoCase(haystack, needle) != std::wstring::npos;
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

    // 常规 USB / 2.4G：...hid#vid_046d&pid_c52b&mi_00#...
    const wchar_t* p_vid = ::wcsstr(lower_path.c_str(), L"vid_");
    const wchar_t* p_pid = ::wcsstr(lower_path.c_str(), L"pid_");
    if (p_vid != nullptr && p_pid != nullptr)
    {
        vid = (unsigned short)::wcstoul(p_vid + 4, nullptr, 16);
        pid = (unsigned short)::wcstoul(p_pid + 4, nullptr, 16);
        return vid != 0 || pid != 0;
    }

    // 蓝牙 HID：..._dev_vid&0212d1_pid&1099_rev&9020...
    //   vid& 后面是 6 位：前 2 位是 Vendor ID Source（02 = 蓝牙 SIG 分配），后 4 位才是 VID。
    //   不解析的话蓝牙鼠标的 VID/PID 恒为 0，日志里完全看不出是哪家设备。
    p_vid = ::wcsstr(lower_path.c_str(), L"vid&");
    p_pid = ::wcsstr(lower_path.c_str(), L"pid&");
    if (p_vid == nullptr || p_pid == nullptr)
        return false;

    const wchar_t* const vid_start = p_vid + 4;
    if (::wcslen(vid_start) >= 6)
        vid = (unsigned short)::wcstoul(vid_start + 2, nullptr, 16);   // 跳过 2 位 source
    pid = (unsigned short)::wcstoul(p_pid + 4, nullptr, 16);
    return vid != 0 || pid != 0;
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

std::wstring DeviceKeyOf(const std::wstring& path)
{
    const size_t p1 = path.find(L'#');
    if (p1 == std::wstring::npos)
        return path;

    const size_t p2 = path.find(L'#', p1 + 1);
    std::wstring segment = (p2 == std::wstring::npos) ? path.substr(p1 + 1)
                                                      : path.substr(p1 + 1, p2 - p1 - 1);

    // 去掉 "...&colNN" 后缀，同一个物理设备的所有集合就落到同一个 key 上
    const size_t col = FindNoCase(segment, L"&col");
    if (col != std::wstring::npos)
        segment.erase(col);
    return segment;
}

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

bool LooksLikeExternalMouse(const HidIfaceInfo& info, const HidProbeResult& probe)
{
    if (!probe.is_mouse_collection)
        return false;

    // 走蓝牙连接的一律算外接鼠标（笔记本内置触摸板不会走蓝牙）
    if (info.transport == HidTransport::BtClassic || info.transport == HidTransport::BtLe)
        return true;

    // USB / 2.4G：路径里有 vid_/pid_ 段的才算。
    // 笔记本内置触摸板是 I2C-HID（hid#elan2205&col01、hid#hidclass 之类），
    // 路径里没有 VID/PID 段，据此排除，避免把触摸板长期当成"鼠标在线"。
    return info.vid != 0 && info.pid != 0;
}

std::vector<MouseIface> EnumMouseInterfaces()
{
    std::vector<MouseIface> result;

    const std::vector<HidIfaceInfo> ifaces = EnumHidInterfaces();
    for (const auto& info : ifaces)
    {
        HidProbeResult probe;
        if (!ProbeHidInterface(info.path, probe))
            continue;
        if (!LooksLikeExternalMouse(info, probe))
            continue;

        MouseIface m;
        m.path = info.path;
        m.product = info.product;
        m.group = info.group;
        m.device_key = DeviceKeyOf(info.path);
        m.transport = info.transport;
        m.vid = info.vid;
        m.pid = info.pid;
        m.usage_page = probe.usage_page;
        m.usage = probe.usage;
        m.has_battery_usage = probe.has_battery_usage;
        m.battery_percent = probe.battery_percent;
        result.push_back(m);
    }

    // 蓝牙优先 —— 无线鼠标的电量才是用户真正关心的，有线/接收器排在后面
    std::stable_sort(result.begin(), result.end(),
        [](const MouseIface& a, const MouseIface& b)
        {
            auto rank = [](const MouseIface& m)
            {
                if (m.transport == HidTransport::BtLe || m.transport == HidTransport::BtClassic)
                    return 0;
                return 1;
            };
            return rank(a) < rank(b);
        });

    return result;
}

std::vector<FeatureReportDump> DumpFeatureReports(const std::wstring& path)
{
    std::vector<FeatureReportDump> result;

    HANDLE h = OpenHidShared(path);
    if (h == INVALID_HANDLE_VALUE)
        return result;

    PHIDP_PREPARSED_DATA pp = nullptr;
    if (!::HidD_GetPreparsedData(h, &pp))
    {
        ::CloseHandle(h);
        return result;
    }

    HIDP_CAPS caps;
    ::ZeroMemory(&caps, sizeof(caps));
    if (::HidP_GetCaps(pp, &caps) == HIDP_STATUS_SUCCESS && caps.FeatureReportByteLength > 0
        && caps.FeatureReportByteLength <= 512)
    {
        std::vector<BYTE> buf((size_t)caps.FeatureReportByteLength, 0);
        for (unsigned int id = 0; id <= 32; ++id)
        {
            std::fill(buf.begin(), buf.end(), 0);
            buf[0] = (BYTE)id;
            if (!::HidD_GetFeature(h, buf.data(), (ULONG)buf.size()))
                continue;

            FeatureReportDump dump;
            dump.report_id = id;
            dump.data.assign(buf.begin(), buf.end());
            result.push_back(dump);
        }
    }

    ::HidD_FreePreparsedData(pp);
    ::CloseHandle(h);
    return result;
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

std::vector<BtBatteryEntry> QueryBluetoothBatteries(unsigned* total_nodes)
{
    std::vector<BtBatteryEntry> result;
    if (total_nodes != nullptr)
        *total_nodes = 0;

    HDEVINFO h_dev = ::SetupDiGetClassDevsW(nullptr, nullptr, nullptr,
        DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (h_dev == INVALID_HANDLE_VALUE)
        return result;

    unsigned scanned = 0;

    SP_DEVINFO_DATA did;
    ::ZeroMemory(&did, sizeof(did));
    did.cbSize = sizeof(SP_DEVINFO_DATA);

    for (DWORD i = 0; ::SetupDiEnumDeviceInfo(h_dev, i, &did); ++i)
    {
        wchar_t id[512] = { 0 };
        if (!::SetupDiGetDeviceInstanceIdW(h_dev, &did, id, ARRAYSIZE(id), nullptr))
            continue;

        // 蓝牙栈里的设备节点前缀：
        //   BTHENUM\       经典蓝牙设备
        //   BTHLE\         低功耗蓝牙设备（电量属性 DEVPKEY_Bluetooth_Battery 通常挂在这里）
        //   BTHLEDEVICE\   BLE 的 GATT 服务节点（HID over GATT 等）
        // 早先只认前两种，且 Dev_ 用大小写敏感匹配 —— 实测某台机器上一个都匹配不上。
        const bool is_bt_classic = (_wcsnicmp(id, L"BTHENUM\\", 8) == 0);
        const bool is_bt_le = (_wcsnicmp(id, L"BTHLE\\", 6) == 0);
        const bool is_bt_le_device = (_wcsnicmp(id, L"BTHLEDEVICE\\", 12) == 0);
        if (!is_bt_classic && !is_bt_le && !is_bt_le_device)
            continue;
        ++scanned;

        // "Dev_" 的写法在真实系统里并不统一（蓝牙 HID 的接口路径里就是小写 _dev_），
        // 这里改成大小写不敏感；非 Dev_ 的 Profile 子节点也保留，
        // 因为电量属性未必挂在设备节点本身上。
        const std::wstring instance(id);
        const bool has_dev_token = ContainsNoCase(instance, L"dev_");

        // 注意：这里不再「没有电量属性就跳过」。
        // 没有电量属性本身也是一条重要信息 —— 它说明设备在线、只是 Windows 没缓存电量，
        // 上层据此可以显示「已连接 / 电量未知」，而不是「未检测到鼠标」。
        BtBatteryEntry entry;
        entry.instance_id = id;
        entry.le = is_bt_le || is_bt_le_device;
        entry.has_battery = false;
        entry.percent = -1;

        DEVPROPTYPE type = 0;
        BYTE value[64] = { 0 };
        DWORD size = 0;
        if (::SetupDiGetDevicePropertyW(h_dev, &did, &kPkeyBluetoothBattery, &type,
                value, (DWORD)sizeof(value), &size, 0))
        {
            const long level = ReadNumericProp(value, size, type);
            if (level >= 0 && level <= 100)
            {
                entry.has_battery = true;
                entry.percent = (int)level;
            }
        }

        wchar_t name[256] = { 0 };
        if (!::SetupDiGetDeviceRegistryPropertyW(h_dev, &did, SPDRP_FRIENDLYNAME, nullptr,
                (PBYTE)name, (DWORD)sizeof(name), nullptr))
        {
            ::SetupDiGetDeviceRegistryPropertyW(h_dev, &did, SPDRP_DEVICEDESC, nullptr,
                (PBYTE)name, (DWORD)sizeof(name), nullptr);
        }
        entry.name = name;
        // 只有带 dev_ 的设备节点才值得向下找鼠标子设备；
        // GATT 服务节点 / Profile 子节点靠名字判断即可，省掉大量 CM_* 遍历。
        entry.is_mouse = NameLooksLikeMouse(entry.name)
            || (has_dev_token && DevNodeContainsMouse(entry.instance_id));
        result.push_back(entry);
    }

    ::SetupDiDestroyDeviceInfoList(h_dev);
    if (total_nodes != nullptr)
        *total_nodes = scanned;
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

/////////////////////////////////////////////////////////////////////////////////////////////
// 蓝牙连接状态

namespace {

int HexValue(wchar_t c)
{
    if (c >= L'0' && c <= L'9')
        return (int)(c - L'0');
    if (c >= L'a' && c <= L'f')
        return 10 + (int)(c - L'a');
    if (c >= L'A' && c <= L'F')
        return 10 + (int)(c - L'A');
    return -1;
}

bool IsHexDigit(wchar_t c)
{
    return HexValue(c) >= 0;
}

// 从实例 ID 里取出蓝牙设备的 48 位地址（6 字节，按 ID 里的书写顺序）。
//
// 实例 ID 的形态五花八门：
//   BTHLE\DEV_D1AA72778ACE\7&3D0EA8E&0&D1AA72778ACE
//   BTHLEDEVICE\{00001812-...}_DEV_VID&0212D1_PID&1099_REV&9020_D1AA72778ACE\8&2A4CAA22&2&0019
//   BTHENUM\{0000111E-...}_LOCALMFG&0002\7&25CA539&0&048C9AF7FE69_C00000000
//
// 里面还混着 GUID 的尾段（00805F9B34FB 也是 12 位十六进制）和 8 位的容器号，
// 但真正代表设备地址的那一段总是**最靠右**的 12 位片段，所以取最后一个匹配。
bool ExtractBluetoothAddress(const std::wstring& instance_id, BYTE mac[6])
{
    const size_t n = instance_id.size();
    size_t found = std::wstring::npos;

    for (size_t i = 0; i + 12 <= n; ++i)
    {
        if (i > 0)
        {
            const wchar_t prev = instance_id[i - 1];
            if (prev != L'_' && prev != L'\\' && prev != L'&' && prev != L'{')
                continue;
        }
        bool ok = true;
        for (size_t k = 0; k < 12; ++k)
        {
            if (!IsHexDigit(instance_id[i + k]))
            {
                ok = false;
                break;
            }
        }
        if (!ok)
            continue;
        if (i + 12 < n)
        {
            const wchar_t next = instance_id[i + 12];
            if (next != L'\\' && next != L'_' && next != L'&' && next != L'}')
                continue;
        }
        found = i;   // 继续往后找，最终留下最靠右的那一段
    }

    if (found == std::wstring::npos)
        return false;

    for (int k = 0; k < 6; ++k)
    {
        const int hi = HexValue(instance_id[found + k * 2]);
        const int lo = HexValue(instance_id[found + k * 2 + 1]);
        if (hi < 0 || lo < 0)
            return false;
        mac[k] = (BYTE)((hi << 4) | lo);
    }
    return true;
}

// BLUETOOTH_ADDRESS 的字节序在文档里没写清楚（rgBytes 是从最高位还是最低位开始），
// 与其猜，不如正反两种都试一遍 —— 反正只有 6 字节。
bool AddressEquals(const BLUETOOTH_ADDRESS& addr, const BYTE mac[6])
{
    if (::memcmp(addr.rgBytes, mac, 6) == 0)
        return true;
    BYTE reversed[6] = { 0 };
    for (int i = 0; i < 6; ++i)
        reversed[i] = mac[5 - i];
    return ::memcmp(addr.rgBytes, reversed, 6) == 0;
}

} // namespace

bool QueryBluetoothConnected(const std::wstring& instance_id, bool& connected)
{
    connected = false;

    BYTE mac[6] = { 0 };
    if (!ExtractBluetoothAddress(instance_id, mac))
        return false;

    BLUETOOTH_FIND_RADIO_PARAMS params;
    ::ZeroMemory(&params, sizeof(params));
    params.dwSize = sizeof(params);

    HANDLE radio = nullptr;
    HBLUETOOTH_RADIO_FIND h_radio = ::BluetoothFindFirstRadio(&params, &radio);
    if (h_radio == nullptr)
        return false;   // 没有蓝牙适配器或 bthserv 没跑

    BLUETOOTH_DEVICE_SEARCH_PARAMS search;
    ::ZeroMemory(&search, sizeof(search));
    search.dwSize = sizeof(search);
    search.fReturnAuthenticated = TRUE;
    search.fReturnRemembered = TRUE;   // 关键：关机/离线的设备是 "remembered" 状态
    search.fReturnConnected = TRUE;
    search.fReturnUnknown = FALSE;
    search.fIssueInquiry = FALSE;      // 不做搜索，只用系统已知状态，避免几秒的延迟
    search.cTimeoutMultiplier = 1;
    search.hRadio = radio;

    bool found = false;
    BLUETOOTH_DEVICE_INFO info;
    ::ZeroMemory(&info, sizeof(info));
    info.dwSize = sizeof(info);

    HBLUETOOTH_DEVICE_FIND h_dev = ::BluetoothFindFirstDevice(&search, &info);
    if (h_dev != nullptr)
    {
        do
        {
            if (AddressEquals(info.Address, mac))
            {
                found = true;
                connected = (info.fConnected != FALSE);
                break;
            }
            ::ZeroMemory(&info, sizeof(info));
            info.dwSize = sizeof(info);
        } while (::BluetoothFindNextDevice(h_dev, &info));

        ::BluetoothFindDeviceClose(h_dev);
    }

    ::BluetoothFindRadioClose(h_radio);
    if (radio != nullptr)
        ::CloseHandle(radio);

    return found;
}
