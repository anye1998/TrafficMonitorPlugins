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
#pragma once

#include <windows.h>
#include <string>
#include <vector>

#include "MouseTypes.h"
#include "RapooHid.h"
#include "DeviceQuery.h"

class CMouseMonitor
{
public:
    static CMouseMonitor& Instance();

    void Start();
    void Stop();
    void GetSnapshot(MouseSnapshot& out);

private:
    CMouseMonitor();
    ~CMouseMonitor();
    CMouseMonitor(const CMouseMonitor&) = delete;
    CMouseMonitor& operator=(const CMouseMonitor&) = delete;

    static DWORD WINAPI ThreadProc(LPVOID param);
    void Run();

    // 三个数据来源，按优先级依次尝试
    bool PollRapoo();
    bool PollHidBattery();
    bool PollBluetooth();

    bool EnsureRapoo();
    bool OpenRapooHandles();
    void CloseRapooHandles();
    bool ReadRapooReports(int budget_ms);
    bool ApplyRapooReport(DWORD bytes);
    bool ProbeRapooAlive();
    bool WriteOverlapped(HANDLE h, const BYTE* data, DWORD len, DWORD timeout_ms);

    void ScanHidCandidates();

    void Publish(LinkKind link, int battery, DataSource source, const std::wstring& name);
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
    int m_rapoo_battery{ -1 };
    bool m_rapoo_wired{ false };
    bool m_rapoo_alive{ false };
    ULONGLONG m_rapoo_report_tick{ 0 };
    ULONGLONG m_rapoo_probe_tick{ 0 };

    // 通用 HID
    std::vector<HidCandidate> m_candidates;
    ULONGLONG m_hid_scan_tick{ 0 };

    // 蓝牙
    std::vector<BtBatteryEntry> m_bt_entries;
    ULONGLONG m_bt_scan_tick{ 0 };

    CRITICAL_SECTION m_cs{};
    bool m_cs_ready{ false };
    MouseSnapshot m_snapshot;

    HANDLE m_thread{ nullptr };
    HANDLE m_stop{ nullptr };
    bool m_running{ false };
};
