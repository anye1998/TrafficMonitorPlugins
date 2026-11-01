// MouseStatus —— 鼠标状态采集引擎实现
#include "MouseMonitor.h"

#include <cstdio>
#include <cwchar>
#include <cwctype>
#include <cstring>

#include "Diag.h"

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

const wchar_t* LinkText(LinkKind link)
{
    switch (link)
    {
    case LinkKind::Bluetooth: return L"蓝牙";
    case LinkKind::BluetoothLE: return L"蓝牙LE";
    case LinkKind::Wireless24G: return L"2.4G";
    case LinkKind::UsbWired: return L"USB有线";
    default: break;
    }
    return L"无";
}

const wchar_t* SourceText(DataSource source)
{
    switch (source)
    {
    case DataSource::Rapoo: return L"雷柏";
    case DataSource::HidBattery: return L"HID电量";
    case DataSource::BluetoothProp: return L"蓝牙属性";
    default: break;
    }
    return L"无";
}

const wchar_t* TransportText(HidTransport t)
{
    switch (t)
    {
    case HidTransport::Usb: return L"USB";
    case HidTransport::BtClassic: return L"蓝牙(经典)";
    case HidTransport::BtLe: return L"蓝牙(LE)";
    default: break;
    }
    return L"未知";
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
#ifdef _WIN64
    const wchar_t* arch = L"x64";
#else
    const wchar_t* arch = L"Win32";
#endif
    diag::Log(L"=== MouseStatus %s 启动，日志：%s ===", arch, diag::LogFilePath().c_str());

    DumpDiagnosticsOnce();

    while (::WaitForSingleObject(m_stop, 0) == WAIT_TIMEOUT)
    {
        // 三个数据源每轮都跑，谁有电量用谁；不再「先到先得」短路后备链路
        const SourceResult rapoo = PollRapoo();
        const SourceResult hid = PollHidBattery();
        const SourceResult bt = PollBluetooth();
        Compose(rapoo, hid, bt);

        if (::WaitForSingleObject(m_stop, kIdleSleepMs) == WAIT_OBJECT_0)
            break;
    }

    diag::Log(L"=== MouseStatus 停止 ===");
}

// 把三个源合成为一个快照：优先采用「拿到电量」的源，其次采用「仅确认在线」的源
void CMouseMonitor::Compose(const SourceResult& rapoo, const SourceResult& hid,
    const SourceResult& bt)
{
    const SourceResult* const order[3] = { &rapoo, &hid, &bt };

    const SourceResult* with_battery = nullptr;
    const SourceResult* any_valid = nullptr;
    for (const SourceResult* src : order)
    {
        if (!src->valid)
            continue;
        if (any_valid == nullptr)
            any_valid = src;
        if (with_battery == nullptr && src->battery >= 0)
            with_battery = src;
    }

    if (with_battery != nullptr)
    {
        Publish(with_battery->link, with_battery->battery, with_battery->source,
            with_battery->name);
        return;
    }
    if (any_valid != nullptr)
    {
        // 设备在线，但没有任何来源能给出电量
        Publish(any_valid->link, -1, any_valid->source, any_valid->name);
        return;
    }
    PublishOffline();
}

// 初始化时把系统里与鼠标相关的信息全量落盘，便于事后定位
void CMouseMonitor::DumpDiagnosticsOnce()
{
    diag::Log(L"--- HID 接口清单 ---");
    const std::vector<HidIfaceInfo> ifaces = EnumHidInterfaces();
    diag::Log(L"HID 接口总数：%u", (unsigned)ifaces.size());
    for (const auto& info : ifaces)
    {
        HidProbeResult probe;
        const bool opened = ProbeHidInterface(info.path, probe);
        diag::Log(L"  vid=%04X pid=%04X transport=%s open=%d usagePage=%04X usage=%04X "
                  L"in=%d out=%d feat=%d mouseCollection=%d hasBattery=%d battery=%d name=\"%s\"",
            info.vid, info.pid, TransportText(info.transport), opened ? 1 : 0, probe.usage_page,
            probe.usage, probe.input_report_len, probe.output_report_len, probe.feature_report_len,
            probe.is_mouse_collection ? 1 : 0, probe.has_battery_usage ? 1 : 0,
            probe.battery_percent, info.product.c_str());
        diag::Log(L"      path=%s", info.path.c_str());
    }

    diag::Log(L"--- 雷柏端点探测 ---");
    rapoo::Endpoints ep;
    const bool ok = rapoo::FindEndpoints(ep);
    diag::Log(L"FindEndpoints=%d pid=%s wired=%d statusLen=%d controlLen=%d featureLen=%d",
        ok ? 1 : 0, ep.pid.c_str(), ep.pid_wired ? 1 : 0, ep.status_report_len,
        ep.control_report_len, ep.feature_report_len);
    diag::Log(L"  status =%s", ep.status_path.c_str());
    diag::Log(L"  control=%s", ep.control_path.c_str());
    diag::Log(L"  feature=%s", ep.feature_path.c_str());

    diag::Log(L"--- 蓝牙设备节点电量 ---");
    const std::vector<BtBatteryEntry> bts = QueryBluetoothBatteries();
    diag::Log(L"带电量属性的蓝牙节点数：%u", (unsigned)bts.size());
    for (const auto& e : bts)
    {
        diag::Log(L"  le=%d battery=%d isMouse=%d name=\"%s\" id=%s", e.le ? 1 : 0, e.percent,
            e.is_mouse ? 1 : 0, e.name.c_str(), e.instance_id.c_str());
    }
    if (bts.empty())
    {
        diag::Log(L"  提示：若鼠标确实通过蓝牙连接却没有条目，说明 Windows 没有为该设备缓存电量");
    }
    diag::Log(L"--- 诊断结束 ---");

    // 用户按下「立即写入诊断信息」时，顺带重置报文记录预算：
    // 接下来 20 帧（约 1 分钟）的雷柏报文会被完整写进日志，方便复现插拔充电线这类操作。
    m_report_dump_budget = 20;
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
        diag::Log(L"雷柏端点已绑定 pid=%s wired=%d statusLen=%d control=%d feature=%d",
            m_ep.pid.c_str(), m_ep.pid_wired ? 1 : 0, m_ep.status_report_len,
            m_ep.control_path.empty() ? 0 : 1, m_ep.feature_path.empty() ? 0 : 1);
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
    {
        diag::Log(L"雷柏 status 端点打开失败，GetLastError=%lu", ::GetLastError());
        return false;
    }

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

    diag::Log(L"雷柏端点已打开 control=%d feature=%d",
        m_h_control == INVALID_HANDLE_VALUE ? 0 : 1,
        m_h_feature == INVALID_HANDLE_VALUE ? 0 : 1);
    return true;
}

void CMouseMonitor::CloseRapooHandles()
{
    CancelPendingStatusRead();
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
    {
        if (m_bad_report_log_count < 12)
        {
            ++m_bad_report_log_count;
            diag::LogHex(L"无法解析的雷柏报文（已忽略）:", m_read_buf, bytes);
        }
        return false;
    }

    m_rapoo_report_tick = ::GetTickCount64();
    // 连接方式保持由 PID 前缀决定（EnsureRapoo 里设置），不采纳报文里的模式位：
    // VT3 在 2.4G 下 buf[1] 高 4 位仍是 0x10，照搬会把无线误判成有线。
    if (status.battery >= 0)
        m_rapoo_battery = status.battery;

    // 原始报文记录策略：
    //   - 进程启动后先无条件记 m_report_dump_budget 帧，便于开机即看格式；
    //   - 之后只在「报文内容变化」时再记一帧。
    // 早先的版本只记「前 12 帧」就永久停止，结果排查「插拔充电线」这类事件时，
    // 等用户操作完预算早就用光了，关键的那几帧一条都没留下。
    // 末尾字节（buf[18]）在常量报文上也会跳动，疑似计数/校验，比较时排除它。
    const int cmp_len = (bytes > 1) ? (int)bytes - 1 : (int)bytes;
    const bool content_changed = (m_last_dumped_len != (int)bytes)
        || (std::memcmp(m_last_dumped, m_read_buf, (size_t)cmp_len) != 0);
    const ULONGLONG now = ::GetTickCount64();
    const bool too_soon = (m_last_dumped_tick != 0) && (now - m_last_dumped_tick < 300);

    if ((content_changed || m_report_dump_budget > 0) && !too_soon)
    {
        if (m_report_dump_budget > 0)
            --m_report_dump_budget;

        // buf7 / buf9 是参考实现里用来判充电的两个字节，单独打出来方便比对插拔前后的差异
        diag::Log(L"雷柏状态报文 battery=%d mode=%02X dpi=%d buf7=%02X buf9=%02X bytes=%lu"
                  L"（link 由 PID 判定=%s）%s",
            status.battery, (unsigned)status.mode_nibble, status.dpi,
            (unsigned)m_read_buf[7], (unsigned)m_read_buf[9], bytes,
            m_ep.pid_wired ? L"USB有线" : L"2.4G", content_changed ? L" [内容变化]" : L"");
        diag::LogHex(L"  原始字节:", m_read_buf, bytes);

        const int copy_len = (int)bytes;
        if (copy_len > 0 && copy_len <= (int)sizeof(m_last_dumped))
        {
            std::memcpy(m_last_dumped, m_read_buf, (size_t)copy_len);
            m_last_dumped_len = copy_len;
        }
        m_last_dumped_tick = now;
    }
    return true;
}

// 读一帧状态报文。
//
// 关键点：status 端点上的读请求「跨轮次挂起」——本轮等不到就原样留着，下轮接着等，
// 而不是每次超时都 CancelIoEx。雷柏的状态报文是周期性的（空闲时可能好几秒才来一帧），
// 早先每轮只等 250ms 就取消，等待窗口被切成碎片，很容易永远错过。
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
            return got;

        if (!m_status_read_pending)
        {
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
                continue;   // 同步完成，立刻再发一帧
            }

            const DWORD err = ::GetLastError();
            if (err != ERROR_IO_PENDING)
            {
                diag::Log(L"雷柏 ReadFile 失败，GetLastError=%lu，将重新打开端点", err);
                return false;   // 句柄失效，交给上层重新打开
            }
            m_status_read_pending = true;
        }

        const ULONGLONG elapsed = ::GetTickCount64() - begin;
        const DWORD remain = (DWORD)((ULONGLONG)budget_ms > elapsed
                                         ? ((ULONGLONG)budget_ms - elapsed)
                                         : 1);
        const DWORD wait = ::WaitForSingleObject(m_read_event, remain);
        if (wait == WAIT_OBJECT_0)
        {
            DWORD bytes = 0;
            if (!::GetOverlappedResult(m_h_status, &m_ov, &bytes, FALSE))
            {
                m_status_read_pending = false;
                diag::Log(L"雷柏 GetOverlappedResult 失败，GetLastError=%lu", ::GetLastError());
                return false;
            }
            m_status_read_pending = false;
            if (bytes > 0 && ApplyRapooReport(bytes))
                got = true;
        }
        else if (wait == WAIT_TIMEOUT)
        {
            // 本轮没等到，保留这一帧读请求继续挂着
            break;
        }
        else
        {
            CancelPendingStatusRead();
            return false;
        }
    }
    return got;
}

void CMouseMonitor::CancelPendingStatusRead()
{
    if (m_h_status == INVALID_HANDLE_VALUE || !m_status_read_pending)
        return;
    ::CancelIoEx(m_h_status, &m_ov);
    DWORD dummy = 0;
    ::GetOverlappedResult(m_h_status, &m_ov, &dummy, TRUE);
    m_status_read_pending = false;
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
    if (!::HidD_GetFeature(m_h_feature, feature_buf, (ULONG)report_len))
        return false;

    // 全零应答等同于没有应答：部分驱动在设备休眠时仍会「成功」返回一块空缓冲区
    bool all_zero = true;
    for (int i = 1; i < report_len; ++i)
    {
        if (feature_buf[i] != 0)
        {
            all_zero = false;
            break;
        }
    }
    return !all_zero;
}

SourceResult CMouseMonitor::PollRapoo()
{
    SourceResult result;
    result.source = DataSource::Rapoo;

    if (!EnsureRapoo())
        return result;

    if (m_h_status == INVALID_HANDLE_VALUE)
    {
        if (!OpenRapooHandles())
        {
            m_ep_valid = false;
            return result;
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
        return result;
    }

    const unsigned short pid = (unsigned short)::wcstoul(
        m_ep.pid.empty() ? L"0" : m_ep.pid.c_str(), nullptr, 0);
    const wchar_t* model = rapoo::ModelNameFromPid(pid);
    if (model != nullptr && model[0] != L'\0')
        result.name = model;
    else
        result.name = L"Rapoo";

    result.valid = true;
    // 连接方式只看系统枚举事实：PID 0x46xx = 鼠标插线直连，0x14xx = 2.4G 接收器
    result.link = m_ep.pid_wired ? LinkKind::UsbWired : LinkKind::Wireless24G;
    result.battery = m_rapoo_battery;
    return result;
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
        // 注意：这里不再跳过雷柏设备。雷柏私有协议读不到电量时（不同型号/固件差异），
        // 通用 HID 电池用途是唯一还能拿到数字的后备路径。
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

        // 同一物理设备里存在鼠标集合，或产品名本身写着鼠标，才认为是鼠标的电量；
        // 雷柏的话鼠标 0x14xx/0x46xx 产品线已按 PID 认定过，直接放行
        const bool is_rapoo_mouse = (item.info.vid == rapoo::VENDOR_ID
            && (((item.info.pid & 0xFF00) == 0x1400) || ((item.info.pid & 0xFF00) == 0x4600)));
        if (!group_is_mouse && !NameLooksLikeMouse(item.info.product) && !is_rapoo_mouse)
            continue;

        HidCandidate candidate;
        candidate.path = item.info.path;
        candidate.product = item.info.product;
        candidate.transport = item.info.transport;
        candidate.vid = item.info.vid;
        candidate.pid = item.info.pid;
        m_candidates.push_back(candidate);
    }

    // 每 8 秒扫一次，清单没变化就不重复写日志，避免长期开着日志把文件刷大
    std::wstring sig;
    for (const auto& c : m_candidates)
    {
        sig += c.path;
        sig += L'|';
    }
    if (sig != m_hid_scan_sig)
    {
        m_hid_scan_sig = sig;
        diag::Log(L"通用 HID 电量候选设备数：%u（清单有变化）", (unsigned)m_candidates.size());
        for (const auto& c : m_candidates)
            diag::Log(L"  候选 vid=%04X pid=%04X transport=%s name=\"%s\"", (unsigned)c.vid,
                (unsigned)c.pid, TransportText(c.transport), c.product.c_str());
    }
}

SourceResult CMouseMonitor::PollHidBattery()
{
    SourceResult result;
    result.source = DataSource::HidBattery;

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

        result.valid = true;
        result.link = LinkFromTransport(candidate.transport, candidate.vid, candidate.pid,
            candidate.product);
        result.battery = percent;
        result.name = candidate.product;
        if (result.name.empty())
        {
            result.name = (result.link == LinkKind::Bluetooth
                              || result.link == LinkKind::BluetoothLE)
                ? L"蓝牙鼠标" : L"无线鼠标";
        }
        return result;
    }
    return result;
}

/////////////////////////////////////////////////////////////////////////////////////////////
// 蓝牙设备节点属性

SourceResult CMouseMonitor::PollBluetooth()
{
    SourceResult result;
    result.source = DataSource::BluetoothProp;

    const ULONGLONG now = ::GetTickCount64();
    if (m_bt_scan_tick == 0 || now - m_bt_scan_tick >= kBluetoothRescanMs)
    {
        m_bt_scan_tick = now;
        m_bt_entries = QueryBluetoothBatteries();

        std::wstring sig;
        for (const auto& e : m_bt_entries)
        {
            sig += e.instance_id;
            sig += L'|';
            sig += std::to_wstring(e.percent);
            sig += L'|';
        }
        if (sig != m_bt_scan_sig)
        {
            m_bt_scan_sig = sig;
            diag::Log(L"蓝牙电量条目数：%u（清单有变化）", (unsigned)m_bt_entries.size());
            for (const auto& e : m_bt_entries)
                diag::Log(L"  le=%d battery=%d isMouse=%d name=\"%s\"", e.le ? 1 : 0,
                    e.percent, e.is_mouse ? 1 : 0, e.name.c_str());
        }
    }

    for (const auto& entry : m_bt_entries)
    {
        if (!entry.is_mouse)
            continue;

        result.valid = true;
        result.link = entry.le ? LinkKind::BluetoothLE : LinkKind::Bluetooth;
        result.battery = entry.percent;
        result.name = entry.name.empty() ? L"蓝牙鼠标" : entry.name;
        return result;
    }
    return result;
}

/////////////////////////////////////////////////////////////////////////////////////////////

void CMouseMonitor::Publish(LinkKind link, int battery, DataSource source,
    const std::wstring& name)
{
    if (!m_cs_ready)
        return;

    std::wstring signature;
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

    signature = std::to_wstring((int)link);
    signature += L"|";
    signature += std::to_wstring(m_snapshot.battery);
    signature += L"|";
    signature += std::to_wstring((int)source);
    signature += L"|";
    signature += std::to_wstring(m_snapshot.charge == ChargeKind::Unknown ? 0 : 1);
    signature += L"|";
    signature += m_snapshot.device_name;
    ::LeaveCriticalSection(&m_cs);

    if (signature != m_last_published)
    {
        m_last_published = signature;
        diag::Log(L"快照更新：link=%s battery=%d source=%s name=\"%s\"", LinkText(link), battery,
            SourceText(source), name.c_str());
    }
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

    if (m_last_published != L"offline")
    {
        m_last_published = L"offline";
        diag::Log(L"快照更新：未检测到在线鼠标");
    }
}
