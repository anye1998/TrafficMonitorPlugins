// MouseStatus —— 鼠标状态采集引擎实现
#include "MouseMonitor.h"

#include <cstdio>
#include <cwchar>
#include <cwctype>
#include <cstring>
#include <algorithm>

#include "Diag.h"

extern "C"
{
#include <hidsdi.h>
}

#pragma comment(lib, "hid.lib")

namespace {

constexpr DWORD kReportBudgetMs = 600;           // 每轮最多等待被动上报的时长
constexpr ULONGLONG kProbeIntervalMs = 1800;     // 无上报时活性探测的间隔
constexpr ULONGLONG kProbeIntervalOfflineMs = 3000;  // 已经判定离线后放宽，避免对着不存在的设备猛问
constexpr ULONGLONG kEndpointRescanMs = 3000;    // 雷柏端点重新枚举的间隔（仅在句柄确实失效后）
// 重新枚举的间隔不宜太短：每扫一次 HID 就要打开系统里所有 HID 接口，
// 每扫一次蓝牙就要枚举全部 PnP 节点。设备插拔本来就会改变这两份清单，
// 15 秒 / 10 秒足够及时，又比原来的 8 秒 / 6 秒少一半以上的重复枚举。
constexpr ULONGLONG kHidRescanMs = 15000;        // 通用 HID 候选重新枚举的间隔
constexpr ULONGLONG kBluetoothRescanMs = 10000;  // 蓝牙电量属性重新查询的间隔

// 主动探测（读寄存器）等应答的窗口与轮询步长
//
// 这是整个连接判定的关键参数，真机对照实测（雷柏 VT3，2.4G 接收器）：
//   - 鼠标开着（哪怕已经静置到插件收不到任何上报）：读命令发下去约 270ms 后被填进特性报文，
//     偶发最慢约 800ms；连续 203 次全部成功。
//   - 鼠标关着：5 秒窗口内 45 次全部拿不到应答（首字节恒为 0x02）。
//     v1.0.14 之所以判不出来，就是因为只睡了 30/60ms 便读一次，远早于 270ms。
// 所以必须「轮询到窗口末尾」。窗口取 2 秒：既覆盖 800ms 的最坏延迟留足余量，
// 又让「关机 → 判定」这一步只花一次窗口的时间（见下面的阈值）。
// 另外实测探测间隔不能短于约 1.5 秒：1.3 秒间隔时会在鼠标明明开着的情况下连续失败，
// 2 秒间隔则 30/30 成功（延迟 267~516ms）。所以探测频率按 1.8 秒控制，不能再密。
constexpr DWORD kRapooProbeWindowMs = 2000;
constexpr DWORD kRapooProbePollMs = 20;
constexpr BYTE kProbeAck = 0x01;                 // 应答就绪
constexpr BYTE kProbeNack = 0x02;                // 设备未应答（关机 / 未唤醒）

// 雷柏鼠标「多久没有任何证据算断开」
//
// 探测可用时：3 秒。一次探测最长 2 秒，所以「关机」从最后一次上报算起约 4 秒内翻成离线
//   （旧版靠「一次探测失败就断开」做到 3 秒，但那会把静置误报成断开）。
// 探测不可用时（个别机型不支持特性通道）：退回纯「无上报」判定，见下面的
//   kRapooOfflineNoProbeMs。这类机型本来就没法准确判断，宁可慢也不误报。
constexpr ULONGLONG kRapooOfflineWithProbeMs = 3000;
// 探测不可用、只能看沉默时长时取 4 分钟：实测雷柏鼠标静置最长 3 分钟才发一帧，
// 阈值必须比它长，否则「静置」又会被误报成「断开」。代价是这类机型关机后要慢一些才变 --。
constexpr ULONGLONG kRapooOfflineNoProbeMs = 240000;
constexpr int kProbeFailStreakLimit = 3;         // 连续失败这么多次就写一条日志说明

// 主循环周期。默认与 TrafficMonitor 的采集间隔（general.monitor_time_span，默认 1000）对齐：
// 主程序要等 DataRequired 被调用才会来取一次数据、状态栏窗口也是 1000ms 重绘一次，
// 采样比这更快不会让界面更及时，只是白白多耗唤醒。用户可以在设置界面里改，也可以直接写
// MouseStatus.ini 的 refresh_interval_ms。
// （kDefaultLoopPeriodMs / kMinLoopPeriodMs / kMaxLoopPeriodMs 定义在 MouseMonitor.h）

int ResolveLoopPeriodMs()
{
    return diag::GetConfigInt(L"refresh_interval_ms", kDefaultLoopPeriodMs, kMinLoopPeriodMs,
        kMaxLoopPeriodMs);
}

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

// 充电状态由「设备自报的充电位 + 连接方式 + 电量」推导：
//   设备说在充电（或 USB 有线直连，必然有供电） -> 满则「已充满」，否则「充电中」
//   否则（2.4G / 蓝牙）                            -> 「放电中」
// 注意：优先采信设备自报的充电位。雷柏私有报文的 buf[7] bit1 就是充电位，
// 这样「2.4G 连接 + 插着充电线」也能正确显示充电中，而不是被连接方式一票否决。
ChargeKind DeriveCharge(LinkKind link, int battery, bool charging)
{
    if (battery < 0)
        return ChargeKind::Unknown;

    const bool on_power = charging || (link == LinkKind::UsbWired);
    if (on_power)
        return (battery >= 100) ? ChargeKind::Full : ChargeKind::Charging;

    switch (link)
    {
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
    m_wake = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);   // 自动复位
    if (m_stop == nullptr || m_read_event == nullptr || m_wake == nullptr)
    {
        // 事件没建齐就退干净，否则下次 Start 会覆盖并泄漏已有句柄
        Stop();
        return;
    }

    ::ZeroMemory(&m_ov, sizeof(m_ov));
    m_ov.hEvent = m_read_event;

    m_running = true;
    m_thread = ::CreateThread(nullptr, 0, &CMouseMonitor::ThreadProc, this, 0, nullptr);
    if (m_thread == nullptr)
        Stop();
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
    if (m_wake != nullptr)
    {
        ::CloseHandle(m_wake);
        m_wake = nullptr;
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

int CMouseMonitor::LoopPeriodMs()
{
    // 运行中读线程实际在用的值；线程还没起来时从 ini 取
    const LONG cached = ::InterlockedCompareExchange((volatile LONG*)&Instance().m_period_ms, 0, 0);
    if (cached > 0)
        return (int)cached;
    return ResolveLoopPeriodMs();
}

bool CMouseMonitor::SetLoopPeriodMs(int ms)
{
    if (ms < kMinLoopPeriodMs)
        ms = kMinLoopPeriodMs;
    if (ms > kMaxLoopPeriodMs)
        ms = kMaxLoopPeriodMs;

    CMouseMonitor& self = Instance();
    ::InterlockedExchange((volatile LONG*)&self.m_period_ms, (LONG)ms);

    // 若线程正睡在一个长周期里，叫醒它，让新周期立刻生效（否则最坏要等 30 秒）
    if (self.m_wake != nullptr)
        ::SetEvent(self.m_wake);

    diag::Log(L"采集周期已改为 %d ms", ms);
    return diag::SetConfigInt(L"refresh_interval_ms", ms);
}

void CMouseMonitor::Run()
{
#ifdef _WIN64
    const wchar_t* arch = L"x64";
#else
    const wchar_t* arch = L"Win32";
#endif
    diag::Log(L"=== MouseStatus %s 启动，日志：%s ===", arch, diag::LogFilePath().c_str());

    // 全量诊断要枚举所有 HID 接口、雷柏端点与全部 PnP 节点，只在日志开启时做；
    // 日志关闭时这些结果都会被 Log 丢掉，枚举的代价却照付，启动会白白变慢。
    if (diag::Enabled())
        DumpDiagnosticsOnce();

    int period_ms = ResolveLoopPeriodMs();
    ::InterlockedExchange((volatile LONG*)&m_period_ms, (LONG)period_ms);
    diag::Log(L"采集周期：%d ms（ini 键 refresh_interval_ms，默认 %d）", period_ms,
        kDefaultLoopPeriodMs);

    while (::WaitForSingleObject(m_stop, 0) == WAIT_TIMEOUT)
    {
        // UI 线程的「立即写入诊断」只置标志，真正的枚举放在工作线程里做，
        // 这样不会和本线程正在维护的报文记录状态并发。
        if (::InterlockedExchange((volatile LONG*)&m_dump_requested, 0) != 0)
            DumpDiagnosticsOnce();

        const ULONGLONG round_begin = ::GetTickCount64();

        // 三个数据源每轮都跑，谁有电量用谁；不再「先到先得」短路后备链路
        const SourceResult rapoo = PollRapoo();
        const SourceResult hid = PollHidBattery();
        const SourceResult bt = PollBluetooth();
        Compose(rapoo, hid, bt);

        // 睡到下一个整周期，而不是「干完活再固定睡一段」：
        // 雷柏在线时本轮要花一两百毫秒等报文，若再叠加固定睡眠，实际周期会漂到 1.2~1.6 秒，
        // 与主程序的 1 秒采集错拍，表现为电量偶尔「跳一格又停两秒」。
        const LONG latest = ::InterlockedCompareExchange((volatile LONG*)&m_period_ms, 0, 0);
        if (latest > 0)
            period_ms = (int)latest;

        const ULONGLONG spent = ::GetTickCount64() - round_begin;
        DWORD sleep_ms = 100;   // 兜底：本轮超时就不额外睡，留一点余量避免空转
        if (spent + 100 < (ULONGLONG)period_ms)
            sleep_ms = (DWORD)((ULONGLONG)period_ms - spent);

        // 同时等「停止」和「周期被改」两个事件：设置界面改完周期会 SetEvent(m_wake)，
        // 这里立刻结束本轮睡眠、按新周期重算，不必等旧的睡眠走完。
        HANDLE waits[2] = { m_stop, m_wake };
        const DWORD wait = ::WaitForMultipleObjects(m_wake != nullptr ? 2 : 1, waits, FALSE, sleep_ms);
        if (wait == WAIT_OBJECT_0)
            break;
        if (wait == WAIT_OBJECT_0 + 1)
            ::ResetEvent(m_wake);   // 重新进入下一轮，周期由上面重新读取
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
            with_battery->name, with_battery->charging);
        return;
    }
    if (any_valid != nullptr)
    {
        // 设备在线，但没有任何来源能给出电量
        Publish(any_valid->link, -1, any_valid->source, any_valid->name, any_valid->charging);
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

    diag::Log(L"--- 蓝牙设备节点 ---");
    unsigned scanned_nodes = 0;
    const std::vector<BtBatteryEntry> bts = QueryBluetoothBatteries(&scanned_nodes);
    diag::Log(L"蓝牙栈节点数：%u（带电量属性 %u 个）", scanned_nodes,
        (unsigned)std::count_if(bts.begin(), bts.end(),
            [](const BtBatteryEntry& e) { return e.has_battery; }));
    for (const auto& e : bts)
    {
        // 鼠标节点额外查一次真实连接状态：节点存在 ≠ 此刻连着
        const wchar_t* conn_text = L"";
        if (e.is_mouse)
        {
            bool connected = false;
            if (QueryBluetoothConnected(e.instance_id, connected))
                conn_text = connected ? L" conn=已连接" : L" conn=未连接";
            else
                conn_text = L" conn=查不到";
        }
        diag::Log(L"  le=%d hasBattery=%d battery=%d isMouse=%d name=\"%s\"%s", e.le ? 1 : 0,
            e.has_battery ? 1 : 0, e.percent, e.is_mouse ? 1 : 0, e.name.c_str(), conn_text);
        diag::Log(L"      id=%s", e.instance_id.c_str());
    }
    if (scanned_nodes == 0)
    {
        diag::Log(L"  提示：连一个蓝牙栈节点都没有枚举到。这说明系统里没有 BTHENUM/BTHLE/"
                  L"BTHLEDEVICE 设备节点，电量不可能从这条路径拿到");
    }
    else
    {
        const bool any_mouse = std::any_of(bts.begin(), bts.end(),
            [](const BtBatteryEntry& e) { return e.is_mouse; });
        if (!any_mouse)
            diag::Log(L"  提示：节点里没有认出鼠标，检查设备名与子设备类型");
        else if (std::none_of(bts.begin(), bts.end(),
                     [](const BtBatteryEntry& e) { return e.is_mouse && e.has_battery; }))
            diag::Log(L"  提示：认出了鼠标，但 Windows 没有为它缓存电量（设置页里应该也看不到百分比）");
    }

    // 鼠标是否存在，与能否读到电量是两件事，这里单独列出来
    diag::Log(L"--- 外接鼠标接口（在线判定的依据） ---");
    const std::vector<MouseIface> mice = EnumMouseInterfaces();
    diag::Log(L"外接鼠标接口数：%u", (unsigned)mice.size());
    for (const auto& m : mice)
    {
        diag::Log(L"  vid=%04X pid=%04X transport=%s usagePage=%04X usage=%04X "
                  L"hasBattery=%d battery=%d name=\"%s\"",
            (unsigned)m.vid, (unsigned)m.pid, TransportText(m.transport),
            (unsigned)m.usage_page, (unsigned)m.usage, m.has_battery_usage ? 1 : 0,
            m.battery_percent, m.product.c_str());
    }

    // 读一遍 feature 报文：不少蓝牙鼠标把电量放在厂商自定义集合里，
    // 拿到原始字节才能判断到底是哪一段（也方便后续按型号适配）。
    // 注意要按 device_key 把**同一只鼠标的所有集合**都读一遍 ——
    // 鼠标集合本身通常 feat=0，真正有自定义通道的是另一个 colNN 集合。
    for (const auto& m : mice)
    {
        if (m.transport != HidTransport::BtLe && m.transport != HidTransport::BtClassic)
            continue;

        diag::Log(L"蓝牙鼠标的全部集合（device_key=%s）：", m.device_key.c_str());
        for (const auto& info : ifaces)
        {
            if (DeviceKeyOf(info.path) != m.device_key)
                continue;

            HidProbeResult probe;
            ProbeHidInterface(info.path, probe);
            const std::vector<FeatureReportDump> dumps = DumpFeatureReports(info.path);

            diag::Log(L"  usagePage=%04X usage=%04X in=%d out=%d feat=%d → feature 报文 %u 个",
                (unsigned)probe.usage_page, (unsigned)probe.usage, probe.input_report_len,
                probe.output_report_len, probe.feature_report_len, (unsigned)dumps.size());
            for (const auto& d : dumps)
            {
                diag::LogHex(L"    feature 原始字节:", d.data.data(), (unsigned)d.data.size());
            }
        }
    }

    diag::Log(L"--- 诊断结束 ---");

    // 用户按下「立即写入诊断信息」时，顺带重置报文记录预算：
    // 接下来 20 帧（约 1 分钟）的雷柏报文会被完整写进日志，方便复现插拔充电线这类操作。
    m_report_dump_budget = 20;
}

void CMouseMonitor::RequestDiagnosticsDump()
{
    // 线程还没起来就先把线程拉起来，否则这个请求永远不会被消费
    if (!m_running)
        Start();

    ::InterlockedExchange((volatile LONG*)&m_dump_requested, 1);
    if (m_wake != nullptr)
        ::SetEvent(m_wake);   // 打断本轮睡眠，点完按钮就能很快看到结果
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
    // 充电位代表「此刻正在充电」，断连后不能沿用上一次的值，
    // 否则鼠标收进抽屉里还一直显示「充电中」。
    m_rapoo_charging = false;
    // 句柄没了就不再「负责」这台设备：通用 HID 链路可以重新作为判据
    m_rapoo_authoritative = false;
    // 重新绑定后要重新验证探测通道是否可用，别把上一次的结论带过来
    m_probe_ok_tick = 0;
    m_probe_fail_streak = 0;
    m_probe_tick = 0;
    m_saw_report = false;
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

    // 连接方式保持由 PID 前缀决定（EnsureRapoo 里设置），不采纳报文里的模式位：
    // VT3 在 2.4G 下 buf[1] 高 4 位仍是 0x10，照搬会把无线误判成有线。
    if (status.battery >= 0)
        m_rapoo_battery = status.battery;
    m_rapoo_charging = status.charging;

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
    // 兜底心跳：状态长时间不变也每 30 秒留一帧，方便事后对比不同时间点的完整报文
    const bool heartbeat = (m_last_dumped_tick == 0) || (now - m_last_dumped_tick >= 30000);

    if ((content_changed || heartbeat || m_report_dump_budget > 0) && !too_soon)
    {
        if (m_report_dump_budget > 0)
            --m_report_dump_budget;

        // buf7 / buf9 是参考实现里用来判充电的两个字节，单独打出来方便比对插拔前后的差异
        diag::Log(L"雷柏状态报文 battery=%d mode=%02X dpi=%d buf7=%02X(%s) buf9=%02X bytes=%lu"
                  L"（link 由 PID 判定=%s）%s",
            status.battery, (unsigned)status.mode_nibble, status.dpi,
            (unsigned)m_read_buf[7], status.charging ? L"充电中" : L"未充电",
            (unsigned)m_read_buf[9], bytes,
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
                // 同步完成：说明驱动里已经排着报文
                if (bytes > 0 && ApplyRapooReport(bytes))
                {
                    got = true;
                    break;      // 本轮已拿到一帧，剩下的时间留给另外两个数据源与睡眠
                }
                continue;       // 无法解析的帧，接着要下一帧
            }

            const DWORD err = ::GetLastError();
            if (err != ERROR_IO_PENDING)
            {
                // 句柄真的坏了（接收器被拔掉、设备节点重建）：这里必须重新枚举，
                // 否则会一直拿着一个死句柄打转。
                diag::Log(L"雷柏 ReadFile 失败，GetLastError=%lu，将重新打开端点", err);
                CloseRapooHandles();
                m_ep_valid = false;
                m_rapoo_alive = false;
                return false;
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
                const DWORD err = ::GetLastError();
                diag::Log(L"雷柏 GetOverlappedResult 失败，GetLastError=%lu，将重新打开端点", err);
                if (err == ERROR_DEVICE_NOT_CONNECTED || err == ERROR_INVALID_HANDLE
                    || err == ERROR_OPERATION_ABORTED)
                {
                    CloseRapooHandles();
                    m_ep_valid = false;
                    m_rapoo_alive = false;
                }
                return false;
            }
            m_status_read_pending = false;
            if (bytes > 0 && ApplyRapooReport(bytes))
            {
                got = true;
                break;      // 同上：拿到有效帧就收工，别把本轮的预算吃满
            }
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

// 雷柏「活性探测」：往命令通道下发一次「读寄存器」，再从特性通道取应答。
// 拿得到应答 = 鼠标此刻还在。接收器本身是常驻设备，它的 HID 接口永远不会因为
// 鼠标关机而消失，所以「接口还在」和「鼠标还在」是两件事，必须靠这条命令区分。
//
// 实测要点（VT3 + 2.4G 接收器，真机对照）：
//   1) 应答不是立刻就有：设备被命令唤醒后约 270ms 才把结果填进特性报文，偶发 800ms。
//      旧版睡 30/60ms 就读一次，读到的永远是上一次的缓冲（首字节 0x02 = 未应答），
//      于是「探测恒失败」，关机与静置再也分不开——这是 v1.0.14 判不出关机的根因。
//      现在改成在窗口内按 20ms 轮询，直到首字节变成 0x01。
//   2) status 端点上挂着的未完成读请求**不影响**特性通道取应答（实测 12/12 成功），
//      所以探测前不再 CancelIo：撤销读请求会吞掉正在到来的上报。
//   3) 鼠标关着时首字节恒为 0x02，5 秒窗口内 45 次全部如此，不会误报。
bool CMouseMonitor::ProbeRapooAlive()
{
    if (m_h_control == INVALID_HANDLE_VALUE || m_h_feature == INVALID_HANDLE_VALUE)
        return false;

    int report_len = m_ep.feature_report_len;
    if (report_len < 9 || report_len > (int)sizeof(m_read_buf))
        report_len = 33;

    // 补一次 A5 A3 解锁：设备睡沉时会丢掉上一条命令，握手能把它叫醒
    BYTE unlock[33] = { 0 };
    const DWORD unlock_len = rapoo::BuildUnlockPacket(unlock, sizeof(unlock));
    if (unlock_len > 0)
        WriteOverlapped(m_h_control, unlock, unlock_len, 200);

    BYTE cmd[33] = { 0 };
    const DWORD len = rapoo::BuildReadCommand(rapoo::BANK_SYSTEM, rapoo::ADDR_POLLING_HZ, 1,
        cmd, sizeof(cmd));
    if (len == 0 || !WriteOverlapped(m_h_control, cmd, len, 300))
    {
        LogProbeFailure(0, L"命令下发失败");
        return false;
    }

    BYTE last_code = 0xFF;
    const ULONGLONG start = ::GetTickCount64();
    while (::GetTickCount64() - start < kRapooProbeWindowMs)
    {
        if (::WaitForSingleObject(m_stop, 0) == WAIT_OBJECT_0)
            return false;

        BYTE reply[128] = { 0 };
        reply[0] = rapoo::ID_FEATURE;
        if (::HidD_GetFeature(m_h_feature, reply, (ULONG)report_len))
        {
            last_code = reply[0];
            if (last_code == kProbeAck)
            {
                m_probe_last_code = last_code;
                return true;
            }
        }
        else
        {
            last_code = 0xFE;   // HidD_GetFeature 本身失败
        }
        ::Sleep(kRapooProbePollMs);
    }

    m_probe_last_code = last_code;
    LogProbeFailure(0, last_code == kProbeNack ? L"设备未应答（0x02）"
                                               : L"窗口内未等到应答");
    return false;
}

void CMouseMonitor::LogProbeFailure(int attempt, const wchar_t* why)
{
    if (m_probe_fail_logged >= 6)
        return;
    ++m_probe_fail_logged;
    diag::Log(L"雷柏活性探测失败（第 %d 次尝试）：%s，GetLastError=%lu", attempt + 1, why,
        ::GetLastError());
}

SourceResult CMouseMonitor::PollRapoo()
{
    SourceResult result;
    result.source = DataSource::Rapoo;

    if (!EnsureRapoo())
        return result;

    bool just_opened = false;
    if (m_h_status == INVALID_HANDLE_VALUE)
    {
        if (!OpenRapooHandles())
        {
            m_ep_valid = false;
            return result;
        }
        m_rapoo_authoritative = true;
        m_rapoo_last_evidence = ::GetTickCount64();   // 宽限期从刚接上算起
        // 刚枚举到接收器就先当作在线：真要是鼠标没开，宽限期一到自然会翻成离线。
        // 反过来（一上来就报离线）会让状态栏闪一下「--」。
        m_rapoo_alive = true;
        just_opened = true;

        // 刚打开句柄、status 端点上还没有挂起的读请求，此时探一次活最干净：
        // 结果直接说明「这台机器的特性通道能不能当活性判据用」。
        const bool baseline = ProbeRapooAlive();
        diag::Log(L"雷柏端点首次探测：%s", baseline ? L"有应答" : L"无应答");
        if (baseline)
        {
            m_probe_ok_tick = m_rapoo_last_evidence;
            m_probe_fail_streak = 0;
        }
    }

    const bool got_report = ReadRapooReports(kReportBudgetMs);

    // 没有被动上报时再主动探一次活。
    // 探测本身会把鼠标从省电状态叫醒（这正是它能当判据的原因），所以按 3 秒一次控制频率。
    // 不再在探测前撤销挂起的读请求：实测挂起读不影响特性通道取应答，
    // 反而撤销会吞掉正赶来的上报。
    bool probe_ok = false;
    bool probed = false;
    const ULONGLONG probe_now = ::GetTickCount64();
    // 在线时按 1.8 秒探（关机要在 4 秒内发现）；已经判定离线后放宽到 3 秒，
    // 免得对着一台根本没应答的设备每两秒问一次。鼠标重新打开时它会自己开始上报，
    // 不依赖探测也能立刻恢复。
    const ULONGLONG probe_gap = m_rapoo_alive ? kProbeIntervalMs : kProbeIntervalOfflineMs;
    const bool probe_due = (m_probe_tick == 0) || (probe_now - m_probe_tick >= probe_gap);
    if (!got_report && !just_opened && probe_due && m_h_control != INVALID_HANDLE_VALUE
        && m_h_feature != INVALID_HANDLE_VALUE)
    {
        m_probe_tick = probe_now;
        probed = true;
        probe_ok = ProbeRapooAlive();
    }

    const ULONGLONG now = ::GetTickCount64();
    if (got_report || probe_ok)
    {
        if (got_report)
            m_saw_report = true;   // 收到过主动上报，说明设备本身是活的
        m_rapoo_last_evidence = now;
        if (probe_ok)
        {
            m_probe_fail_streak = 0;
            m_probe_fail_logged = 0;
            if (m_probe_ok_tick == 0)
                diag::Log(L"雷柏活性探测可用：静置无上报时也能确认鼠标还在");
            m_probe_ok_tick = now;
        }
        if (!m_rapoo_alive)
        {
            diag::Log(L"雷柏鼠标有应答，标记为已连接（来源：%s）",
                got_report ? L"状态上报" : L"活性探测");
            m_rapoo_alive = true;
        }
    }
    else
    {
        if (probed && ++m_probe_fail_streak == kProbeFailStreakLimit)
            diag::Log(L"雷柏活性探测连续失败 %d 次（最近一次应答首字节=0x%02X）",
                m_probe_fail_streak, m_probe_last_code);

        // 探测可用就用 3 秒；探测从未成功过则看情况：
        //   - 收到过上报：设备确实活着，只是这台机器的特性通道不认这条命令，
        //     只能靠沉默时长判断，退回 4 分钟（宁可慢，不能把静置误报成断开）。
        //   - 一帧上报都没收到过：多半是启动时鼠标就是关的，仍按 3 秒判离线，
        //     否则要干等几分钟才显示「未连接」。
        const ULONGLONG grace = ProbeTrusted()
            ? kRapooOfflineWithProbeMs
            : (m_saw_report ? kRapooOfflineNoProbeMs : kRapooOfflineWithProbeMs);
        if (now >= m_rapoo_last_evidence && now - m_rapoo_last_evidence >= grace)
        {
            if (m_rapoo_alive)
            {
                diag::Log(L"雷柏鼠标 %.1f 秒无应答（阈值 %.1f 秒），标记为未连接",
                    (double)(now - m_rapoo_last_evidence) / 1000.0, (double)grace / 1000.0);
                m_rapoo_alive = false;
            }
        }
        // 否则保持上一次的结论：沉默只是静置，不是断开
    }

    if (!m_rapoo_alive)
    {
        // 端点与句柄都留着：设备一有应答立刻恢复，不必再走「3 秒后重新枚举」那条路
        // （那条路正是旧版界面来回跳的根源）。只把对外报的状态置为离线。
        m_rapoo_charging = false;
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
    // 「2.4G 连接 + 插着充电线」也能被正确识别，靠的就是设备自报的充电位
    result.charging = m_rapoo_charging;
    return result;
}

bool CMouseMonitor::ProbeTrusted() const
{
    // 只在「这次绑定期间成功过」时相信探测的负面结论。
    // 一旦相信，就不再因为后来的连续失败而反悔：否则鼠标关机时探测必然失败，
    // 反悔会让阈值从 3 秒退回 4 分钟，反而更晚才能显示「未连接」。
    return m_probe_ok_tick != 0;
}

bool CMouseMonitor::IsRapooMouseIface(const MouseIface& m) const
{
    return m.vid == rapoo::VENDOR_ID
        && (((m.pid & 0xFF00) == 0x1400) || ((m.pid & 0xFF00) == 0x4600));
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
        candidate.battery = item.result.battery_percent;   // 扫描时已经读过一次
        m_candidates.push_back(candidate);
    }

    // 同时记下「哪些接口代表一只外接鼠标」—— 这是判断设备是否**在线**的依据，
    // 与能不能读到电量无关。蓝牙鼠标经常只暴露厂商自定义集合、没有标准电池用途，
    // 早先这里只收集「有电池用途」的接口，于是它连"在线"都算不上，状态栏一直是 --。
    m_mouse_ifaces.clear();
    for (const auto& item : probed)
    {
        if (!LooksLikeExternalMouse(item.info, item.result))
            continue;

        MouseIface iface;
        iface.path = item.info.path;
        iface.product = item.info.product;
        iface.transport = item.info.transport;
        iface.vid = item.info.vid;
        iface.pid = item.info.pid;
        iface.usage_page = item.result.usage_page;
        iface.usage = item.result.usage;
        iface.has_battery_usage = item.result.has_battery_usage;
        iface.battery_percent = item.result.battery_percent;
        m_mouse_ifaces.push_back(iface);
    }

    // 蓝牙优先：无线那只才是用户关心的
    std::stable_sort(m_mouse_ifaces.begin(), m_mouse_ifaces.end(),
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

    // 每次重扫（默认 15 秒）只在清单变化时写日志，避免长期开着日志把文件刷大。
    // 指纹里带数量前缀：清单为空时也能与"还没扫过"区分开（否则空清单永远不会写一行，
    // 排查时就看不到"确实扫过、但一个候选都没有"这个关键事实）。
    std::wstring sig = std::to_wstring(m_candidates.size());
    sig += L'#';
    sig += std::to_wstring(m_mouse_ifaces.size());
    sig += L'#';
    for (const auto& c : m_candidates)
    {
        sig += c.path;
        sig += L'|';
    }
    for (const auto& m : m_mouse_ifaces)
    {
        sig += m.path;
        sig += L'|';
    }
    if (sig != m_hid_scan_sig)
    {
        m_hid_scan_sig = sig;
        diag::Log(L"通用 HID 电量候选设备数：%u（清单有变化）", (unsigned)m_candidates.size());
        for (const auto& c : m_candidates)
            diag::Log(L"  候选 vid=%04X pid=%04X transport=%s name=\"%s\"", (unsigned)c.vid,
                (unsigned)c.pid, TransportText(c.transport), c.product.c_str());

        diag::Log(L"在线鼠标接口数：%u", (unsigned)m_mouse_ifaces.size());
        for (const auto& m : m_mouse_ifaces)
            diag::Log(L"  鼠标 vid=%04X pid=%04X transport=%s usagePage=%04X usage=%04X "
                      L"hasBattery=%d battery=%d name=\"%s\"",
                (unsigned)m.vid, (unsigned)m.pid, TransportText(m.transport),
                (unsigned)m.usage_page, (unsigned)m.usage, m.has_battery_usage ? 1 : 0,
                m.battery_percent, m.product.c_str());
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

    // 电量直接用 m_hid_scan_tick 那一轮扫描时读到的那一份，不再每轮重新读：
    // 之前的写法每秒都会对候选接口重开句柄并 GET_FEATURE，既费 CPU，也可能不停
    // 把蓝牙鼠标从省电状态叫醒。电池百分比本来就变化很慢，跟随 15 秒一次的扫描足够。
    for (const auto& candidate : m_candidates)
    {
        if (candidate.battery < 0)
            continue;

        result.valid = true;
        result.link = LinkFromTransport(candidate.transport, candidate.vid, candidate.pid,
            candidate.product);
        result.battery = candidate.battery;
        result.name = candidate.product;
        if (result.name.empty())
        {
            result.name = (result.link == LinkKind::Bluetooth
                              || result.link == LinkKind::BluetoothLE)
                ? L"蓝牙鼠标" : L"无线鼠标";
        }
        return result;
    }

    // 一个电量都读不到，但确实枚举到了外接鼠标：
    // 设备是**在线**的，只是它没有把电量暴露成标准 HID 电池用途
    // （蓝牙鼠标很常见，电量走 GATT 电池服务或厂商自定义报文）。
    // 这时必须报「在线 + 电量未知」，而不是让显示层回落到「未检测到鼠标」。
    if (!m_mouse_ifaces.empty())
    {
        // 雷柏接收器是常驻设备：鼠标关机后接收器（连同它的 HID 接口）不会从系统里消失，
        // 所以「接口还在」并不能推出「鼠标还连着」。只要雷柏私有协议确实在负责这台设备、
        // 且已经判定离线，就不许通用 HID 再顶上来报「已连接」。
        //
        // 这里不再要求 ProbeTrusted()：早先加上这个条件后，特性通道不支持读命令的机型
        // 即使雷柏侧靠 4 分钟沉默判定出了离线，也会被 HID 的「接口还在」永久盖住，
        // 等于离线回退根本没生效。改为「探测可用过 或 本次绑定收到过上报」，
        // 至少保证这条回退在真实设备上能走到。
        const bool rapoo_says_offline = m_rapoo_authoritative && !m_rapoo_alive
            && (ProbeTrusted() || m_saw_report);

        const MouseIface* first = nullptr;
        for (const auto& iface : m_mouse_ifaces)
        {
            if (rapoo_says_offline && IsRapooMouseIface(iface))
                continue;
            first = &iface;
            break;
        }

        if (first == nullptr)
        {
            diag::LogThrottled(L"hid_rapoo_offline",
                L"雷柏接收器接口仍在，但鼠标没有任何应答，按离线处理");
            return result;   // valid 保持 false
        }

        // 蓝牙鼠标关机后，系统里的 HID 接口可能还残留一两轮（能打开、能取 caps），
        // 这时单看接口会把它当成「已连接」。蓝牙栈已经明确说未连接的话，以它为准。
        const bool is_bt = (first->transport == HidTransport::BtClassic
            || first->transport == HidTransport::BtLe);
        if (is_bt && m_bt_conn_state == 0)
        {
            diag::LogThrottled(L"hid_bt_offline",
                L"HID 上仍有蓝牙鼠标接口，但蓝牙栈报告未连接，按离线处理");
            return result;   // valid 保持 false
        }

        result.valid = true;
        result.battery = first->battery_percent;   // 扫描时读到过就用，读不到是 -1
        result.link = LinkFromTransport(first->transport, first->vid, first->pid,
            first->product);
        result.name = first->product;
        if (result.name.empty())
        {
            result.name = (result.link == LinkKind::Bluetooth
                              || result.link == LinkKind::BluetoothLE)
                ? L"蓝牙鼠标" : L"无线鼠标";
        }
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

        // 顺手把每个「鼠标」节点的真实连接状态查出来。
        // 这一步是必须的：BTHLE\DEV_xxx 节点在设备关机后依然存在于系统里，
        // 只看「节点在不在」会把关机显示成「已连接」。
        m_bt_conn_state = -1;
        for (auto& e : m_bt_entries)
        {
            if (!e.is_mouse)
                continue;
            bool connected = false;
            e.conn_known = QueryBluetoothConnected(e.instance_id, connected);
            e.conn_connected = connected;
            if (e.conn_known && connected)
                m_bt_conn_state = 1;
            else if (e.conn_known && m_bt_conn_state != 1)
                m_bt_conn_state = 0;
        }

        std::wstring sig = std::to_wstring(m_bt_entries.size());
        sig += L'#';
        for (const auto& e : m_bt_entries)
        {
            sig += e.instance_id;
            sig += L'|';
            sig += std::to_wstring(e.percent);
            sig += L'|';
            sig += e.has_battery ? L'1' : L'0';
            sig += e.conn_known ? (e.conn_connected ? L'C' : L'D') : L'?';
        }
        if (sig != m_bt_scan_sig)
        {
            m_bt_scan_sig = sig;
            diag::Log(L"蓝牙设备节点数：%u（清单有变化）", (unsigned)m_bt_entries.size());
            for (const auto& e : m_bt_entries)
                diag::Log(L"  le=%d hasBattery=%d battery=%d isMouse=%d conn=%s name=\"%s\" id=%s",
                    e.le ? 1 : 0, e.has_battery ? 1 : 0, e.percent, e.is_mouse ? 1 : 0,
                    !e.conn_known ? L"查不到" : (e.conn_connected ? L"已连接" : L"未连接"),
                    e.name.c_str(), e.instance_id.c_str());
        }
    }

    // 优先用「确实带电量属性」的鼠标节点；都没有电量时，退而用在线的那一个，
    // 这样至少能显示「已连接 / 电量未知」，而不是「未检测到鼠标」。
    const BtBatteryEntry* chosen = nullptr;
    for (const auto& entry : m_bt_entries)
    {
        if (!entry.is_mouse)
            continue;

        // 在线判定的顺序：
        //   1) 蓝牙栈明确说「未连接」→ 断开了（多半是关机/离开范围），跳过；
        //   2) 蓝牙栈明确说「已连接」  → 在线；
        //   3) 查不到连接状态（无 radio / 服务未启动 / 地址没匹配上）→
        //      退回「HID 上还有没有蓝牙鼠标集合」这个判据，保守处理，不轻易判离线。
        const bool online = entry.conn_known ? entry.conn_connected : HasBluetoothMouseIface();
        if (!online)
        {
            if (entry.conn_known && m_bt_offline_sig != entry.instance_id)
            {
                m_bt_offline_sig = entry.instance_id;
                diag::Log(L"蓝牙鼠标节点仍在，但设备未连接（按离线处理）：%s",
                    entry.name.c_str());
            }
            continue;
        }

        if (chosen == nullptr)
            chosen = &entry;
        if (entry.has_battery)
        {
            chosen = &entry;
            break;
        }
    }

    if (chosen != nullptr)
    {
        result.valid = true;
        result.link = chosen->le ? LinkKind::BluetoothLE : LinkKind::Bluetooth;
        result.battery = chosen->has_battery ? chosen->percent : -1;
        result.name = chosen->name.empty() ? L"蓝牙鼠标" : chosen->name;
    }
    return result;
}

bool CMouseMonitor::HasBluetoothMouseIface() const
{
    for (const auto& m : m_mouse_ifaces)
    {
        if (m.transport == HidTransport::BtClassic || m.transport == HidTransport::BtLe)
            return true;
    }
    return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////

void CMouseMonitor::Publish(LinkKind link, int battery, DataSource source,
    const std::wstring& name, bool charging)
{
    if (!m_cs_ready)
        return;

    std::wstring signature;
    ::EnterCriticalSection(&m_cs);
    if (battery >= 0)
    {
        const bool value_changed = (m_snapshot.battery != battery);
        const std::wstring now_text = NowTimeText();

        m_snapshot.battery = battery;
        m_snapshot.battery_is_stale = false;

        // 每次检查都更新（说明插件还在盯着），但要和「数值变化」区分开：
        // 蓝牙电量来自系统缓存，值不变是常态，last_seen 必须只在数值真的变了时才动。
        m_snapshot.last_check = now_text;
        if (value_changed || m_snapshot.last_seen.empty())
            m_snapshot.last_seen = now_text;
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
    m_snapshot.charge = DeriveCharge(link, m_snapshot.battery, charging);

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
