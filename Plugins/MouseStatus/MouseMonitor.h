// MouseStatus —— 鼠标状态采集引擎
//
// 设计要点（也是相对参考项目 rapoo-tray 的两处修正）：
//
// 1) 连接状态不再等同于「设备节点存在」
//    参考实现只要接收器枚举成功就把状态置为「已连接」，鼠标关机或进入休眠后仍显示连接。
//    两条链路各有各的判据：
//      · 2.4G 接收器：接收器是常驻设备，鼠标关机后它的 HID 接口依然在，所以不能拿
//        「接口还在」当在线。改为对鼠标做「活性探测」：往命令通道下发一次读寄存器，
//        再在 1.2 秒窗口内按 60~160ms 的步长轮询特性报文，首字节变成 0x01 才算鼠标应了。
//        真机对照：鼠标开着约 270ms 应答（偶发 800ms），关着则恒回 0x02。
//        只要探测可用，它的负面结论就当准；个别机型探测不可用时退回「沉默时长」判定。
//        v1.0.18 起：探测只在「用户没在动鼠标」且「没有被动上报」时才做，间隔 4 秒，
//        单次探测通常 3~6 个请求就结束 —— 老版本会在这里连续打上百个请求，占满接收器的
//        厂商通道，反而把鼠标输入挤成「一卡一卡」。
//      · 蓝牙：节点在设备关机后也不消失，改查 BLUETOOTH_DEVICE_INFO::fConnected。
//    另外，「本轮没收到报文」绝不等于断开：雷柏鼠标静置时最长 3 分钟一帧不发，
//    离线判定必须基于「沉默时长」而不是「本轮无数据」，否则界面会一直跳。
//
// 2) 充电状态不再由「有线」或含义不明的报文位推断
//    参考实现用 (statusByte & 0x02) 与 buf[9] == 1/2 判定充电，一旦这些位被置起，
//    鼠标切回无线后仍会一直显示「充电中」。这里只根据「连接方式 + 电量」推导，
//    并区分「充电中」与「已充满」。
//
// 3) 三个数据源「汇总」而不是「短路」
//    早期版本里只要雷柏私有协议探测到设备在线，就直接返回并跳过通用 HID 与蓝牙两条
//    后备链路。结果是：设备认出来了、连接方式也对了，但私有报文里没有有效电量时，
//    电量就永远停在未知——而且后备链路明明能读到也被挡掉了。现在三个源每轮都跑，
//    按「谁有电量用谁」合成最终快照。
#pragma once

#include <windows.h>
#include <string>
#include <vector>

// 采集周期的默认值与允许范围（毫秒）。
// 放在头文件里是为了让设置界面复用同一套边界，避免两处各写一份数字、改一处忘一处。
// 默认 1000：TrafficMonitor 用 general.monitor_time_span（默认 1000ms）才调一次
// DataRequired，状态栏窗口也是 1000ms 重绘一次，采得更快也不会让界面更及时。
constexpr int kDefaultLoopPeriodMs = 1000;
constexpr int kMinLoopPeriodMs = 200;
constexpr int kMaxLoopPeriodMs = 30000;

#include "MouseTypes.h"
#include "RapooHid.h"
#include "DeviceQuery.h"

// 单个数据源一轮探测的结果
struct SourceResult
{
    bool valid{ false };                      // 该源是否认为设备在线
    LinkKind link{ LinkKind::None };
    int battery{ -1 };                        // -1 = 该源在线但没拿到电量
    bool charging{ false };                   // 设备自报「正在充电」（目前只有雷柏私有报文有）
    std::wstring name;
    DataSource source{ DataSource::None };
};

class CMouseMonitor
{
public:
    static CMouseMonitor& Instance();

    void Start();
    void Stop();
    void GetSnapshot(MouseSnapshot& out);

    // 立刻执行一次全量枚举并把结果写进诊断日志（初始化时调用一次）
    void DumpDiagnosticsOnce();

    // 请求工作线程尽快执行一次全量诊断。
    // 供设置对话框 / 右键菜单这类 UI 线程调用：DumpDiagnosticsOnce 会改动工作线程
    // 持有的报文记录状态，直接跨线程调用会产生数据竞争，所以统一改成「置标志 + 唤醒」。
    void RequestDiagnosticsDump();

    // 当前生效的采集周期（毫秒），供设置窗口展示。
    // 来源：MouseStatus.ini 的 [config] refresh_interval_ms，缺省 1000ms。
    static int LoopPeriodMs();

    // 设置采集周期：写入 ini 并让正在跑的工作线程立刻按新周期走
    // （不重启主程序；若线程正在睡眠，会被唤醒后重新按新周期计算）
    static bool SetLoopPeriodMs(int ms);

private:
    CMouseMonitor();
    ~CMouseMonitor();
    CMouseMonitor(const CMouseMonitor&) = delete;
    CMouseMonitor& operator=(const CMouseMonitor&) = delete;

    static DWORD WINAPI ThreadProc(LPVOID param);
    void Run();

    // 三个数据来源，每轮全部尝试，最后统一合成
    SourceResult PollRapoo();
    SourceResult PollHidBattery();
    SourceResult PollBluetooth();

    // 把三个源的结果合成为一个快照并发布
    void Compose(const SourceResult& rapoo, const SourceResult& hid, const SourceResult& bt);

    // 本轮 HID 清单里是否有走蓝牙的鼠标集合。
    // 蓝牙 PnP 节点关机后不会消失，所以「HID 接口是否还在」才是更可靠的在线判据。
    bool HasBluetoothMouseIface() const;

    bool EnsureRapoo();
    bool OpenRapooHandles();
    void CloseRapooHandles();
    bool ReadRapooReports(int budget_ms);
    void CancelPendingStatusRead();
    // 光标近期是否在动（用来在用户真的在用鼠标时跳过探测）
    bool CursorActiveRecently(DWORD ms);
    bool ApplyRapooReport(DWORD bytes);
    bool ProbeRapooAlive();
    void LogProbeFailure(int attempt, const wchar_t* why);
    bool WriteOverlapped(HANDLE h, const BYTE* data, DWORD len, DWORD timeout_ms);

    // 活性探测是否可信：成功过、且最近没有连续失败。
    // 只用来决定「沉默多久算断开」，以及要不要相信它给出的离线结论。
    bool ProbeTrusted() const;
    // 是否是我们通过雷柏私有协议在盯的那台雷柏鼠标（接收器）
    bool IsRapooMouseIface(const MouseIface& m) const;

    void ScanHidCandidates();

    void Publish(LinkKind link, int battery, DataSource source, const std::wstring& name,
        bool charging);
    void PublishOffline();

    struct HidCandidate
    {
        std::wstring path;
        std::wstring product;
        HidTransport transport{ HidTransport::Unknown };
        unsigned short vid{ 0 };
        unsigned short pid{ 0 };
        int battery{ -1 };   // 扫描时 ProbeHidInterface 顺手读到的电量
    };

    // 雷柏
    rapoo::Endpoints m_ep;
    bool m_ep_valid{ false };
    ULONGLONG m_ep_scan_tick{ 0 };
    HANDLE m_h_status{ INVALID_HANDLE_VALUE };
    HANDLE m_h_control{ INVALID_HANDLE_VALUE };
    HANDLE m_h_feature{ INVALID_HANDLE_VALUE };
    HANDLE m_read_event{ nullptr };
    OVERLAPPED m_ov{};
    BYTE m_read_buf[128]{};
    bool m_status_read_pending{ false };  // status 端点上有一帧读请求正挂起
    int m_rapoo_battery{ -1 };
    bool m_rapoo_charging{ false };
    bool m_rapoo_alive{ false };
    // 「设备有应答」的最近时刻：被动上报与活性探测都算。
    // 沉默不等于断开——雷柏鼠标静置时十几秒不发一帧，所以离线判定必须基于这段时间差，
    // 而不是「本轮没收到报文就立刻判死」（旧版正是如此，界面每隔两三秒就在
    // 「66%」和「未知」之间来回跳）。
    ULONGLONG m_rapoo_last_evidence{ 0 };
    // 上一轮活性探测的时刻（探测会吵醒鼠标，按 kProbeIntervalMs 控制频率）
    ULONGLONG m_probe_tick{ 0 };
    // 光标位置与「最近一次移动」的时刻。用户在用鼠标时不探测 —— 探测会占用接收器的
    // 厂商通道，与鼠标输入报文抢同一段 2.4G 无线带宽（详见 MouseMonitor.cpp 里的说明）。
    POINT m_last_cursor{ 0, 0 };
    bool m_cursor_valid{ false };
    ULONGLONG m_cursor_active_tick{ 0 };
    // 雷柏私有协议是否正在负责这台接收器（句柄打开成功即算）。
    // 用来阻止通用 HID 链路在鼠标关机后顶上来报「已连接」：接收器是常驻设备，
    // 它的 HID 接口永远不会因为鼠标关机而消失。
    bool m_rapoo_authoritative{ false };
    // 活性探测的统计：最近一次成功的时刻（0 = 从未成功）与连续失败次数
    ULONGLONG m_probe_ok_tick{ 0 };
    int m_probe_fail_streak{ 0 };
    int m_probe_fail_logged{ 0 };   // 失败明细只记前几次，避免刷爆日志
    BYTE m_probe_last_code{ 0 };    // 最近一次探测读到的应答首字节（0x01 应答 / 0x02 未应答）
    bool m_saw_report{ false };     // 本次绑定期间是否收到过主动上报（用来识别「探测不可用」的机型）
    // 原始报文记录策略：进程启动后先记若干帧；之后只要报文内容变了就记一帧。
    // 不能只按「前 N 帧」截断——排查插拔充电这种事件时，等用户操作完，
    // 前 N 帧早用完了，正好错过关键的那几帧。
    int m_report_dump_budget{ 20 };      // 剩余「无条件记录」的帧数
    int m_bad_report_log_count{ 0 };     // 无法解析的报文只记前几帧
    BYTE m_last_dumped[64]{};
    int m_last_dumped_len{ 0 };
    ULONGLONG m_last_dumped_tick{ 0 };

    // 通用 HID
    std::vector<HidCandidate> m_candidates;
    ULONGLONG m_hid_scan_tick{ 0 };
    std::wstring m_hid_scan_sig;   // 上次候选清单的指纹，仅变化时写日志

    // 「鼠标是否存在」与电量分开记：只要枚举到外接鼠标集合就算在线，
    // 哪怕它一个标准电池用途都没暴露（很多蓝牙鼠标就是这样）。
    std::vector<MouseIface> m_mouse_ifaces;

    // 蓝牙
    std::vector<BtBatteryEntry> m_bt_entries;
    ULONGLONG m_bt_scan_tick{ 0 };
    std::wstring m_bt_scan_sig;
    // 上一次记录「节点在但设备未连接」的实例 ID，用于把这条日志限制在状态切换时写一次
    std::wstring m_bt_offline_sig;
    // 蓝牙鼠标的连接状态，由 PollBluetooth 维护，供 PollHidBattery 复用：
    //   -1 未知（没查到）、0 未连接、1 已连接。
    // 存在的意义是 HID 接口在设备关机后可能残留一两轮，光看接口会让「关机」显示成「已连接」。
    int m_bt_conn_state{ -1 };

    // 日志节流：只在状态变化时写快照
    std::wstring m_last_published;

    CRITICAL_SECTION m_cs{};
    bool m_cs_ready{ false };
    MouseSnapshot m_snapshot;

    HANDLE m_thread{ nullptr };
    HANDLE m_stop{ nullptr };
    HANDLE m_wake{ nullptr };              // 设置界面改了周期时用它打断本轮睡眠
    bool m_running{ false };

    // 采集周期（毫秒）。用 LONG + Interlocked 访问：设置界面在 UI 线程写，
    // 工作线程每轮读，避免为了一个 int 再加一把锁。
    // 0 表示「工作线程还没起来、尚未从 ini 读取」，此时查询要走 ini。
    volatile LONG m_period_ms{ 0 };

    // UI 线程请求「立即写入诊断」的标志，由工作线程取走并执行，避免跨线程访问采集状态。
    volatile LONG m_dump_requested{ 0 };
};
