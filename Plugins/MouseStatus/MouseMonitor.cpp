// MouseStatus —— 鼠标状态采集引擎实现
#include "MouseMonitor.h"

#include <cstdio>
#include <cwchar>
#include <cwctype>
#include <cstring>

extern "C"
{
#include <hidsdi.h>
}

#pragma comment(lib, "hid.lib")

namespace {

constexpr DWORD kReportBudgetMs = 600;           // 每轮最多等待被动上报的时长
constexpr ULONGLONG kProbeIntervalMs = 3000;     // 无上报时活性探测的间隔
constexpr ULONGLONG kEndpointRescanMs = 3000;    // 雷柏端点重新枚举的间隔
constexpr ULONGLONG kHidRescanMs = 8000;         // 通用 HID 候选重新枚举的间隔
constexpr ULONGLONG kBluetoothRescanMs = 15000;  // 蓝牙电量属性重新查询的间隔
constexpr DWORD kIdleSleepMs = 400;              // 主循环间隔
constexpr ULONGLONG kStaleThresholdMs = 8000;    // 无主动探测能力时的「过期」阈值

std::wstring NowTimeText()
{
    SYSTEMTIME st;
    ::GetLocalTime(&st);
    wchar_t buf[16] = { 0 };
    ::swprintf_s(buf, L"%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
    return std::wstring(buf);
}

std::wstring ToLower(const std::wstring& s)
{
    std::wstring r = s;
    for (auto& ch : r)
        ch = (wchar_t)::towlower(ch);
    return r;
}

// 充电状态只由「连接方式 + 电量」推导：
//   有线且未满 -> 充电中；有线且满 -> 已充满；无线 -> 放电中
ChargeKind DeriveCharge(LinkKind link, int battery)
{
    if (battery < 0)
        return ChargeKind::Unknown;

    switch (link)
    {
    case LinkKind::UsbWired:
        return (battery >= 100) ? ChargeKind::Full : ChargeKind::Charging;
    case LinkKind::Wireless24G:
    case LinkKind::Bluetooth:
    case LinkKind::BluetoothLE:
        return ChargeKind::Discharging;
    default:
        break;
    }
    return ChargeKind::Unknown;
}

// 由传输层与 VID/PID/产品名推断连接方式
LinkKind LinkFromTransport(HidTransport transport, unsigned short vid, unsigned short pid,
    const std::wstring& product)
{
    switch (transport)
    {
    case HidTransport::BtLe:
        return LinkKind::BluetoothLE;
    case HidTransport::BtClassic:
        return LinkKind::Bluetooth;
    case HidTransport::Usb:
    {
        // 雷柏：0x46xx 是插线状态下的节点，0x14xx 是接收器节点
        if (vid == rapoo::VENDOR_ID && (pid & 0xFF00) == 0x4600)
            return LinkKind::UsbWired;

        // 其余 USB 设备：能上报电量基本可以断定是接收器（无线）；
        // 产品名里明确写着有线的再修正为有线
        const std::wstring lower = ToLower(product);
        if (lower.find(L"wired") != std::wstring::npos
            || lower.find(L"有线") != std::wstring::npos)
        {
            return LinkKind::UsbWired;
        }
        return LinkKind::Wireless24G;
    }
    default:
        break;
    }
    return LinkKind::None;
}

} // namespace

/////////////////////////////////////////////////////////////////////////////////////////////

CMouseMonitor& CMouseMonitor::Instance()
{
    static CMouseMonitor instance;
    return instance;
}

CMouseMonitor::CMouseMonitor()
{
    ::InitializeCriticalSection(&m_cs);
    m_cs_ready = true;
    m_ov.hEvent = nullptr;
}

CMouseMonitor::~CMouseMonitor()
{
    Stop();
    if (m_cs_ready)
    {
        ::DeleteCriticalSection(&m_cs);
        m_cs_ready = false;
    }
}

void CMouseMonitor::Start()
{
    if (m_running)
        return;

    m_stop = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    m_read_event = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (m_stop == nullptr || m_read_event == nullptr)
        return;

    ::ZeroMemory(&m_ov, sizeof(m_ov));
    m_ov.hEvent = m_read_event;

    m_running = true;
    m_thread = ::CreateThread(nullptr, 0, &CMouseMonitor::ThreadProc, this, 0, nullptr);
    if (m_thread == nullptr)
        m_running = false;
}

void CMouseMonitor::Stop()
{
    m_running = false;
    if (m_stop != nullptr)
        ::SetEvent(m_stop);
    if (m_thread != nullptr)
    {
        ::WaitForSingleObject(m_thread, 4000);
        ::CloseHandle(m_thread);
        m_thread = nullptr;
    }
    CloseRapooHandles();
    if (m_stop != nullptr)
    {
        ::CloseHandle(m_stop);
        m_stop = nullptr;
    }
    if (m_read_event != nullptr)
    {
        ::CloseHandle(m_read_event);
        m_read_event = nullptr;
    }
    m_ov.hEvent = nullptr;
}

void CMouseMonitor::GetSnapshot(MouseSnapshot& out)
{
    if (!m_cs_ready)
    {
        out = MouseSnapshot();
        return;
    }
    ::EnterCriticalSection(&m_cs);
    out = m_snapshot;
    ::LeaveCriticalSection(&m_cs);
}

DWORD WINAPI CMouseMonitor::ThreadProc(LPVOID param)
{
    static_cast<CMouseMonitor*>(param)->Run();
    return 0;
}

void CMouseMonitor::Run()
{
    while (::WaitForSingleObject(m_stop, 0) == WAIT_TIMEOUT)
    {
        bool served = PollRapoo();
        if (!served)
            served = PollHidBattery();
        if (!served)
            served = PollBluetooth();
        if (!served)
            PublishOffline();

        if (::WaitForSingleObject(m_stop, kIdleSleepMs) == WAIT_OBJECT_0)
            break;
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////
// 雷柏私有协议

bool CMouseMonitor::EnsureRapoo()
{
    if (m_ep_valid)
        return true;

    const ULONGLONG now = ::GetTickCount64();
    if (m_ep_scan_tick != 0 && now - m_ep_scan_tick < kEndpointRescanMs)
        return false;
    m_ep_scan_tick = now;

    m_ep_valid = rapoo::FindEndpoints(m_ep);
    if (m_ep_valid)
    {
        m_rapoo_wired = m_ep.pid_wired;
    }
    return m_ep_valid;
}

bool CMouseMonitor::OpenRapooHandles()
{
    if (m_ep.status_path.empty())
        return false;

    CloseRapooHandles();

    m_h_status = ::CreateFileW(m_ep.status_path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED,
        nullptr);
    if (m_h_status == INVALID_HANDLE_VALUE)
        return false;

    if (!m_ep.control_path.empty())
    {
        m_h_control = ::CreateFileW(m_ep.control_path.c_str(), GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED,
            nullptr);
        if (m_h_control == INVALID_HANDLE_VALUE)
        {
            m_h_control = ::CreateFileW(m_ep.control_path.c_str(), GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                FILE_FLAG_OVERLAPPED, nullptr);
        }
    }

    if (!m_ep.feature_path.empty())
    {
        m_h_feature = ::CreateFileW(m_ep.feature_path.c_str(), GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (m_h_feature == INVALID_HANDLE_VALUE)
        {
            m_h_feature = ::CreateFileW(m_ep.feature_path.c_str(), GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        }
        if (m_h_feature == INVALID_HANDLE_VALUE)
        {
            m_h_feature = ::CreateFileW(m_ep.feature_path.c_str(), 0,
                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        }
    }

    // A5 A3 解锁握手：不握手时部分机型不会开始上报
    if (m_h_control != INVALID_HANDLE_VALUE)
    {
        BYTE packet[33] = { 0 };
        const DWORD len = rapoo::BuildUnlockPacket(packet, sizeof(packet));
        if (len > 0)
            WriteOverlapped(m_h_control, packet, len, 200);
    }

    return true;
}

void CMouseMonitor::CloseRapooHandles()
{
    if (m_h_status != INVALID_HANDLE_VALUE)
    {
        ::CloseHandle(m_h_status);
        m_h_status = INVALID_HANDLE_VALUE;
    }
    if (m_h_control != INVALID_HANDLE_VALUE)
    {
        ::CloseHandle(m_h_control);
        m_h_control = INVALID_HANDLE_VALUE;
    }
    if (m_h_feature != INVALID_HANDLE_VALUE)
    {
        ::CloseHandle(m_h_feature);
        m_h_feature = INVALID_HANDLE_VALUE;
    }
}

bool CMouseMonitor::WriteOverlapped(HANDLE h, const BYTE* data, DWORD len, DWORD timeout_ms)
{
    if (h == INVALID_HANDLE_VALUE || data == nullptr || len == 0)
        return false;

    OVERLAPPED ov;
    ::ZeroMemory(&ov, sizeof(ov));
    ov.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (ov.hEvent == nullptr)
        return false;

    DWORD written = 0;
    BOOL ok = ::WriteFile(h, data, len, &written, &ov);
    if (!ok && ::GetLastError() == ERROR_IO_PENDING)
    {
        if (::WaitForSingleObject(ov.hEvent, timeout_ms) == WAIT_OBJECT_0)
        {
            ok = ::GetOverlappedResult(h, &ov, &written, FALSE);
        }
        else
        {
            ::CancelIoEx(h, &ov);
            DWORD dummy = 0;
            ::GetOverlappedResult(h, &ov, &dummy, TRUE);
            ok = FALSE;
        }
    }
    ::CloseHandle(ov.hEvent);
    return ok != FALSE && written == len;
}

bool CMouseMonitor::ApplyRapooReport(DWORD bytes)
{
    rapoo::Status status;
    if (!rapoo::ParseStatus(m_read_buf, bytes, status))
        return false;

    m_rapoo_report_tick = ::GetTickCount64();
    m_rapoo_wired = status.wired;
    if (status.battery >= 0)
        m_rapoo_battery = status.battery;
    return true;
}

bool CMouseMonitor::ReadRapooReports(int budget_ms)
{
    if (m_h_status == INVALID_HANDLE_VALUE)
        return false;

    int report_len = m_ep.status_report_len;
    if (report_len < 9 || report_len > (int)sizeof(m_read_buf))
        report_len = 19;

    const ULONGLONG begin = ::GetTickCount64();
    bool got = false;

    while (::GetTickCount64() - begin < (ULONGLONG)budget_ms)
    {
        if (::WaitForSingleObject(m_stop, 0) == WAIT_OBJECT_0)
            break;

        ::ResetEvent(m_read_event);
        ::ZeroMemory(&m_ov, sizeof(m_ov));
        m_ov.hEvent = m_read_event;
        m_ov.Offset = 0;
        m_ov.OffsetHigh = 0;

        DWORD bytes = 0;
        const BOOL ok = ::ReadFile(m_h_status, m_read_buf, (DWORD)report_len, &bytes, &m_ov);
        if (ok)
        {
            if (bytes > 0 && ApplyRapooReport(bytes))
                got = true;
            continue;
        }

        const DWORD err = ::GetLastError();
        if (err != ERROR_IO_PENDING)
            return false;   // 句柄失效，交给上层重新打开

        const DWORD wait = ::WaitForSingleObject(m_read_event, 250);
        if (wait == WAIT_OBJECT_0)
        {
            if (!::GetOverlappedResult(m_h_status, &m_ov, &bytes, FALSE))
                return false;
            if (bytes > 0 && ApplyRapooReport(bytes))
                got = true;
        }
        else if (wait == WAIT_TIMEOUT)
        {
            ::CancelIoEx(m_h_status, &m_ov);
            DWORD dummy = 0;
            ::GetOverlappedResult(m_h_status, &m_ov, &dummy, TRUE);
            break;          // 本轮没有被动上报
        }
        else
        {
            ::CancelIoEx(m_h_status, &m_ov);
            DWORD dummy = 0;
            ::GetOverlappedResult(m_h_status, &m_ov, &dummy, TRUE);
            return false;
        }
    }
    return got;
}

bool CMouseMonitor::ProbeRapooAlive()
{
    if (m_h_control == INVALID_HANDLE_VALUE || m_h_feature == INVALID_HANDLE_VALUE)
        return false;

    BYTE cmd[33] = { 0 };
    const DWORD len = rapoo::BuildReadCommand(rapoo::BANK_SYSTEM, rapoo::ADDR_POLLING_HZ, 1,
        cmd, sizeof(cmd));
    if (len == 0 || !WriteOverlapped(m_h_control, cmd, len, 300))
        return false;

    ::Sleep(20);

    int report_len = m_ep.feature_report_len;
    if (report_len < 9 || report_len > (int)sizeof(m_read_buf))
        report_len = 33;

    // 特性报文能读回来就说明设备在应答；读不到即判为休眠/关机
    static BYTE feature_buf[128];
    ::ZeroMemory(feature_buf, sizeof(feature_buf));
    feature_buf[0] = rapoo::ID_FEATURE;
    return ::HidD_GetFeature(m_h_feature, feature_buf, (ULONG)report_len) != FALSE;
}

bool CMouseMonitor::PollRapoo()
{
    if (!EnsureRapoo())
        return false;

    if (m_h_status == INVALID_HANDLE_VALUE)
    {
        if (!OpenRapooHandles())
        {
            m_ep_valid = false;
            return false;
        }
        m_rapoo_report_tick = 0;
    }

    const bool got_report = ReadRapooReports(kReportBudgetMs);
    const ULONGLONG now = ::GetTickCount64();

    if (got_report)
    {
        m_rapoo_alive = true;
    }
    else if (m_h_control != INVALID_HANDLE_VALUE && m_h_feature != INVALID_HANDLE_VALUE)
    {
        if (m_rapoo_probe_tick == 0 || now - m_rapoo_probe_tick >= kProbeIntervalMs)
        {
            m_rapoo_probe_tick = now;
            m_rapoo_alive = ProbeRapooAlive();
        }
    }
    else
    {
        // 无法主动探测时退化为「最近是否收到过上报」
        m_rapoo_alive = (m_rapoo_report_tick != 0)
            && (now - m_rapoo_report_tick < kStaleThresholdMs);
    }

    if (!m_rapoo_alive)
    {
        CloseRapooHandles();
        m_ep_valid = false;
        return false;
    }

    const LinkKind link = m_rapoo_wired ? LinkKind::UsbWired : LinkKind::Wireless24G;

    std::wstring name;
    const unsigned short pid = (unsigned short)::wcstoul(
        m_ep.pid.empty() ? L"0" : m_ep.pid.c_str(), nullptr, 0);
    const wchar_t* model = rapoo::ModelNameFromPid(pid);
    if (model != nullptr && model[0] != L'\0')
        name = model;
    else
        name = L"Rapoo";

    Publish(link, m_rapoo_battery, DataSource::Rapoo, name);
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////
// 通用 HID 电量（Usage Page 0x06 / Usage 0x20）

void CMouseMonitor::ScanHidCandidates()
{
    m_candidates.clear();

    const std::vector<HidIfaceInfo> ifaces = EnumHidInterfaces();
    if (ifaces.empty())
        return;

    struct Probed
    {
        HidIfaceInfo info;
        HidProbeResult result;
    };
    std::vector<Probed> probed;
    std::vector<std::wstring> mouse_groups;

    for (const auto& info : ifaces)
    {
        if (info.vid == rapoo::VENDOR_ID)
            continue;   // 雷柏设备走私有协议，避免重复识别

        HidProbeResult result;
        if (!ProbeHidInterface(info.path, result))
            continue;

        Probed item;
        item.info = info;
        item.result = result;
        probed.push_back(item);

        if (result.is_mouse_collection && !info.group.empty())
            mouse_groups.push_back(info.group);
    }

    for (const auto& item : probed)
    {
        if (!item.result.has_battery_usage)
            continue;

        bool group_is_mouse = false;
        for (const auto& group : mouse_groups)
        {
            if (group == item.info.group)
            {
                group_is_mouse = true;
                break;
            }
        }
        // 同一物理设备里存在鼠标集合，或产品名本身写着鼠标，才认为是鼠标的电量
        if (!group_is_mouse && !NameLooksLikeMouse(item.info.product))
            continue;

        HidCandidate candidate;
        candidate.path = item.info.path;
        candidate.product = item.info.product;
        candidate.transport = item.info.transport;
        candidate.vid = item.info.vid;
        candidate.pid = item.info.pid;
        m_candidates.push_back(candidate);
    }
}

bool CMouseMonitor::PollHidBattery()
{
    const ULONGLONG now = ::GetTickCount64();
    if (m_hid_scan_tick == 0 || now - m_hid_scan_tick >= kHidRescanMs)
    {
        m_hid_scan_tick = now;
        ScanHidCandidates();
    }

    for (const auto& candidate : m_candidates)
    {
        int percent = -1;
        if (!ReadHidBatteryStrength(candidate.path, percent))
            continue;

        const LinkKind link = LinkFromTransport(candidate.transport, candidate.vid,
            candidate.pid, candidate.product);
        std::wstring name = candidate.product;
        if (name.empty())
            name = (link == LinkKind::Bluetooth || link == LinkKind::BluetoothLE)
                ? L"蓝牙鼠标" : L"无线鼠标";
        Publish(link, percent, DataSource::HidBattery, name);
        return true;
    }
    return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////
// 蓝牙设备节点属性

bool CMouseMonitor::PollBluetooth()
{
    const ULONGLONG now = ::GetTickCount64();
    if (m_bt_scan_tick == 0 || now - m_bt_scan_tick >= kBluetoothRescanMs)
    {
        m_bt_scan_tick = now;
        m_bt_entries = QueryBluetoothBatteries();
    }

    const BtBatteryEntry* chosen = nullptr;
    for (const auto& entry : m_bt_entries)
    {
        if (!entry.is_mouse)
            continue;
        chosen = &entry;
        break;
    }
    if (chosen == nullptr)
        return false;

    const LinkKind link = chosen->le ? LinkKind::BluetoothLE : LinkKind::Bluetooth;
    std::wstring name = chosen->name;
    if (name.empty())
        name = L"蓝牙鼠标";
    Publish(link, chosen->percent, DataSource::BluetoothProp, name);
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////

void CMouseMonitor::Publish(LinkKind link, int battery, DataSource source,
    const std::wstring& name)
{
    if (!m_cs_ready)
        return;

    ::EnterCriticalSection(&m_cs);
    if (battery >= 0)
    {
        m_snapshot.battery = battery;
        m_snapshot.battery_is_stale = false;
        m_snapshot.last_seen = NowTimeText();
    }
    else if (m_snapshot.battery >= 0)
    {
        m_snapshot.battery_is_stale = true;
    }

    m_snapshot.link = link;
    m_snapshot.source = source;
    m_snapshot.live = true;
    if (!name.empty())
        m_snapshot.device_name = name;
    m_snapshot.charge = DeriveCharge(link, m_snapshot.battery);
    ::LeaveCriticalSection(&m_cs);
}

void CMouseMonitor::PublishOffline()
{
    if (!m_cs_ready)
        return;

    ::EnterCriticalSection(&m_cs);
    m_snapshot.link = LinkKind::None;
    m_snapshot.charge = ChargeKind::Unknown;
    m_snapshot.live = false;
    m_snapshot.source = DataSource::None;
    if (m_snapshot.battery >= 0)
        m_snapshot.battery_is_stale = true;
    ::LeaveCriticalSection(&m_cs);
}
