// MouseStatus —— 雷柏（Rapoo）HID 报文处理实现
#include "RapooHid.h"

#include <windows.h>
#include <setupapi.h>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <cwctype>

#include "Diag.h"

extern "C"
{
#include <hidsdi.h>
#include <hidpi.h>
}

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "hid.lib")

namespace rapoo {

namespace {

void SetDetailSize(PSP_DEVICE_INTERFACE_DETAIL_DATA_W p)
{
#ifdef _WIN64
    p->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
#else
    // 32 位下 sizeof 会带上对齐填充（8），而驱动要求 4 + sizeof(WCHAR) = 6
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

struct IfaceInfo
{
    std::wstring path;
    USHORT usage_page{ 0 };
    USHORT usage{ 0 };
    int in_len{ 0 };
    int out_len{ 0 };
    int feat_len{ 0 };
    bool opened{ false };
};

bool ProbeInterface(const std::wstring& path, IfaceInfo& info)
{
    info.path = path;
    HANDLE h = ::CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        h = ::CreateFileW(path.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        h = ::CreateFileW(path.c_str(), 0,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;

    PHIDP_PREPARSED_DATA pp = nullptr;
    bool ok = false;
    if (::HidD_GetPreparsedData(h, &pp))
    {
        HIDP_CAPS caps;
        ::ZeroMemory(&caps, sizeof(caps));
        if (::HidP_GetCaps(pp, &caps) == HIDP_STATUS_SUCCESS)
        {
            info.usage_page = caps.UsagePage;
            info.usage = caps.Usage;
            info.in_len = caps.InputReportByteLength;
            info.out_len = caps.OutputReportByteLength;
            info.feat_len = caps.FeatureReportByteLength;
            info.opened = true;
            ok = true;
        }
        ::HidD_FreePreparsedData(pp);
    }
    ::CloseHandle(h);
    return ok;
}

// 收集全部 HID 设备接口路径
std::vector<std::wstring> EnumHidPaths()
{
    std::vector<std::wstring> paths;
    GUID hid_guid;
    ::HidD_GetHidGuid(&hid_guid);
    HDEVINFO h_dev = ::SetupDiGetClassDevsW(&hid_guid, nullptr, nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (h_dev == INVALID_HANDLE_VALUE)
        return paths;

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
            paths.emplace_back(detail->DevicePath);
        ::free(detail);
    }
    ::SetupDiDestroyDeviceInfoList(h_dev);
    return paths;
}

} // namespace

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

const wchar_t* ModelNameFromPid(unsigned short pid)
{
    // 仅收录社区在真机上验证过的型号（来源：Iris-0109/rapoo-tray 的支持列表），
    // 其余型号一律返回空，由上层回退到系统里的设备名，避免臆测型号。
    switch (pid)
    {
    case 0x1460:
    case 0x4660: return L"Rapoo VT7";
    case 0x1406:
    case 0x1407:
    case 0x1408:
    case 0x1410:
    case 0x1411:
    case 0x4606:
    case 0x4611: return L"Rapoo VT3S";
    case 0x1412:
    case 0x4612: return L"Rapoo VT3";
    case 0x1417:
    case 0x4617: return L"Rapoo VT3 MAX";
    default: break;
    }
    return L"";
}

DWORD BuildUnlockPacket(BYTE* out_buf, DWORD buf_size)
{
    if (out_buf == nullptr || buf_size < 33)
        return 0;
    std::memset(out_buf, 0, buf_size);
    out_buf[0] = ID_CONTROL;
    out_buf[1] = 0xA5;
    out_buf[2] = 0xA3;
    return 33;
}

DWORD BuildReadCommand(BYTE bank, BYTE addr, BYTE read_len, BYTE* out_buf, DWORD buf_size)
{
    if (out_buf == nullptr || buf_size < 33)
        return 0;
    std::memset(out_buf, 0, buf_size);
    out_buf[0] = ID_CONTROL;
    out_buf[1] = 0xA5;
    out_buf[2] = 0xA4;
    out_buf[3] = read_len;
    out_buf[4] = addr;
    out_buf[5] = bank;
    return 33;
}

bool ParseFeatureRead(const BYTE* buf, DWORD len, BYTE* out_data, BYTE expect_len)
{
    if (buf == nullptr || out_data == nullptr || len < 4)
        return false;
    // 情况一：HID 驱动剥掉了 Report ID，buf[0] 直接是应答码 0x01
    if (buf[0] == 0x01 && len >= (DWORD)(4 + expect_len))
    {
        std::memcpy(out_data, &buf[4], expect_len);
        return true;
    }
    // 情况二：buf[0] 是 Report ID，buf[1] 是应答码
    if (buf[0] == ID_FEATURE && buf[1] == 0x01 && len >= (DWORD)(5 + expect_len))
    {
        std::memcpy(out_data, &buf[5], expect_len);
        return true;
    }
    return false;
}

bool ParseStatus(const BYTE* buf, DWORD len, Status& out)
{
    if (buf == nullptr || len < 9)
        return false;
    if (buf[0] != ID_STATUS)
        return false;
    const BYTE marker = (BYTE)(buf[1] & 0xF0);
    if (marker != MARKER_DONGLE && marker != MARKER_WIRED)
        return false;

    out.wired = (marker == MARKER_WIRED);
    out.dpi = (int)buf[3] | ((int)buf[4] << 8);

    const BYTE raw = buf[8];
    if (raw >= 1 && raw <= 100)
    {
        out.battery = raw;
    }
    else
    {
        // 0 是设备刚上电/ADC 未稳定时的无效读数；>100 亦属异常。
        // 参考实现在这种情况下会「补」成 100%，等于凭空造出一个满电读数，这里改为未知，
        // 由上层沿用最近一次真实读数。
        out.battery = -1;
    }
    return true;
}

bool FindEndpoints(Endpoints& out)
{
    out = Endpoints();

    const std::vector<std::wstring> paths = EnumHidPaths();
    if (paths.empty())
        return false;

    // 第一步：找出所有雷柏接口，按 PID 归组
    struct Group
    {
        std::wstring pid_text;
        unsigned short pid{ 0 };
        bool wired{ false };
        std::vector<std::wstring> paths;
    };
    std::vector<Group> groups;

    for (const auto& raw_path : paths)
    {
        const std::wstring lower = ToLower(raw_path);
        unsigned short vid = 0, pid = 0;
        if (!ParseVidPid(lower, vid, pid) || vid != VENDOR_ID)
            continue;

        // 只关心鼠标类：PID 高位为 0x14（无线）/0x46（有线）的雷柏产品线
        const bool looks_mouse = ((pid & 0xFF00) == 0x1400) || ((pid & 0xFF00) == 0x4600);
        if (!looks_mouse)
            continue;

        Group* target = nullptr;
        for (auto& g : groups)
        {
            if (g.pid == pid)
            {
                target = &g;
                break;
            }
        }
        if (target == nullptr)
        {
            Group g;
            g.pid = pid;
            g.wired = ((pid & 0xFF00) == 0x4600);
            wchar_t buf[16] = { 0 };
            ::swprintf_s(buf, L"0x%04X", pid);
            g.pid_text = buf;
            groups.push_back(g);
            target = &groups.back();
        }
        target->paths.push_back(raw_path);
    }

    if (groups.empty())
        return false;

    // 有线节点优先：插着线时鼠标走 0x46xx 的节点，无线节点此时不会上报有效数据
    const Group* chosen = &groups.front();
    for (const auto& g : groups)
    {
        if (g.wired)
        {
            chosen = &g;
            break;
        }
    }

    // 第二步：逐个接口探测能力，选出 status / control / feature
    std::vector<IfaceInfo> infos;
    for (const auto& p : chosen->paths)
    {
        IfaceInfo info;
        if (ProbeInterface(p, info))
            infos.push_back(info);
    }
    if (infos.empty())
        return false;

    // 端点选择规则（对着真机的 HID 描述符核对过）：
    //
    // VT3 / VT3S / VT7 这类机型在同一条 VID/PID 下暴露了多个「厂商自定义 Usage Page」
    // (0xFF00) 集合，但只有 Usage == 0x0002 的那个会推送 0x07 状态报文：
    //
    //   col02  usagePage=0001 usage=0006  键盘        ← 不是鼠标通道
    //   col05  usagePage=FF00 usage=000E  in=33/out=33 ← 配置/升级通道，从不发状态报文
    //   col06  usagePage=FF00 usage=000F  feat=33      ← 寄存器读应答
    //   col07  usagePage=FF00 usage=0010  feat=33      ← 另一个寄存器通道
    //   col08  usagePage=FF00 usage=0011  in=32/out=32
    //   col09  usagePage=FF00 usage=0002  in=19/out=19 ← ★ 真正上报 0x07 状态报文
    //   col0a  usagePage=FF00 usage=0012  in=24/out=24
    //   mi_02  usagePage=FF00 usage=0001  in=513/out=513 ← 固件升级
    //
    // 早期版本按「第一个 in_len >= 9」去挑，结果正好挑中 col05 这个 33 字节的配置通道：
    // 设备在线、连接方式也对，但一个 0x07 报文都收不到，于是电量永远未知。
    // 现在改为按 Usage 精确匹配，长度只作兜底。
    constexpr USHORT kVendorPage = 0xFF00;
    constexpr USHORT kStatusUsage = 0x0002;
    constexpr USHORT kConfigUsage = 0x000E;

    const IfaceInfo* status = nullptr;
    const IfaceInfo* control = nullptr;
    const IfaceInfo* feature = nullptr;

    for (const auto& info : infos)
    {
        if (info.usage_page != kVendorPage || info.in_len < 9)
            continue;
        if (info.usage == kStatusUsage)
        {
            status = &info;
            break;
        }
        // 兜底一：路径里明确带 col09 且长度够放一帧状态报文
        if (status == nullptr && info.in_len >= 19 && info.usage != kConfigUsage
            && ToLower(info.path).find(L"col09") != std::wstring::npos)
        {
            status = &info;
        }
    }
    // 兜底二：任意厂商集合，排除 33 字节配置通道
    if (status == nullptr)
    {
        for (const auto& info : infos)
        {
            if (info.usage_page != kVendorPage || info.in_len < 9
                || info.usage == kConfigUsage)
            {
                continue;
            }
            status = &info;
            break;
        }
    }

    // 控制端点只用来下发 33 字节命令包（解锁 / 读寄存器），要能容纳 33 字节才可用。
    // 同一设备上还有个 513 字节的固件升级集合，长度也 >= 33，但写入必须严格匹配
    // 报告长度，所以这里取「不小于 33 的最小者」，正好落在 33 字节的命令通道上。
    for (const auto& info : infos)
    {
        if (info.usage_page != kVendorPage || info.out_len < 33)
            continue;
        if (control == nullptr || info.out_len < control->out_len)
            control = &info;
    }

    // 特性端点用于读寄存器应答（活性探测），同样取不小于 33 的最小者
    for (const auto& info : infos)
    {
        if (info.feat_len < 33)
            continue;
        if (feature == nullptr || info.feat_len < feature->feat_len)
            feature = &info;
    }
    if (feature == nullptr)
    {
        for (const auto& info : infos)
        {
            if (info.feat_len < 9)
                continue;
            if (feature == nullptr || info.feat_len < feature->feat_len)
                feature = &info;
        }
    }

    if (status != nullptr)
    {
        out.status_path = status->path;
        out.status_report_len = status->in_len;
    }
    if (control != nullptr)
    {
        out.control_path = control->path;
        out.control_report_len = control->out_len;
    }
    if (feature != nullptr)
    {
        out.feature_path = feature->path;
        out.feature_report_len = feature->feat_len;
    }

    out.pid = chosen->pid_text;
    out.pid_wired = chosen->wired;

    diag::Log(L"雷柏端点候选（pid=%s，共 %u 个接口，选中 status=%s）：",
        chosen->pid_text.c_str(), (unsigned)infos.size(),
        status == nullptr ? L"无" : status->path.c_str());
    for (const auto& info : infos)
    {
        diag::Log(L"   usagePage=%04X usage=%04X in=%d out=%d feat=%d role=%s",
            (unsigned)info.usage_page, (unsigned)info.usage, info.in_len, info.out_len,
            info.feat_len,
            (&info == status) ? L"status"
                              : ((&info == control) ? L"control"
                                                    : ((&info == feature) ? L"feature" : L"-")));
    }

    // 只要拿得到被动上报就算可用（部分机型不开放控制/特性端点）
    return out.HasStatus();
}

} // namespace rapoo
