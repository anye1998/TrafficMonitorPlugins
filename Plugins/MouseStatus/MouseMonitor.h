// MouseStatus —— 鼠标状态采集引擎
//
// 设计要点（也是相对参考项目 rapoo-tray 的两处修正）：
//
// 1) 连接状态不再等同于「设备节点存在」
//    参考实现只要接收器枚举成功就把状态置为「已连接」，鼠标关机或进入休眠后仍显示连接。
//    这里改为「活性探测」：雷柏设备在长时间没有主动上报时会主动读取一次寄存器，
//    读得到才算在线；通用 HID 设备以「本次电量读取是否成功」为活性依据；
//    蓝牙设备以「设备节点当前是否在线（DIGCF_PRESENT）」为依据。
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

    bool EnsureRapoo();
    bool OpenRapooHandles();
    void CloseRapooHandles();
    bool ReadRapooReports(int budget_ms);
    void CancelPendingStatusRead();
    bool ApplyRapooReport(DWORD bytes);
    bool ProbeRapooAlive();
    bool WriteOverlapped(HANDLE h, const BYTE* data, DWORD len, DWORD timeout_ms);

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
    ULONGLONG m_rapoo_report_tick{ 0 };
    ULONGLONG m_rapoo_probe_tick{ 0 };
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
    bool m_hid_scan_logged{ false };

    // 蓝牙
    std::vector<BtBatteryEntry> m_bt_entries;
    ULONGLONG m_bt_scan_tick{ 0 };
    std::wstring m_bt_scan_sig;

    // 日志节流：只在状态变化时写快照
    std::wstring m_last_published;

    CRITICAL_SECTION m_cs{};
    bool m_cs_ready{ false };
    MouseSnapshot m_snapshot;

    HANDLE m_thread{ nullptr };
    HANDLE m_stop{ nullptr };
    bool m_running{ false };
};
