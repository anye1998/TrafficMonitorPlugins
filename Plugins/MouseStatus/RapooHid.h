// MouseStatus —— 雷柏（Rapoo）二代 Nordic 架构鼠标的 HID 报文处理
//
// 报文格式参考 rapoo-tray 项目（Iris-0109/rapoo-tray，MIT），并修正了其中
// 「充电状态」「连接状态」两处判定错误：
//   1) 参考项目把「有线连接」直接等同于「充电中」，且保留了含义不明的字节判定
//      (statusByte & 0x02 / buf[9] == 0x01 || 0x02)，一旦这些位被置起，鼠标切回
//      无线后仍会继续显示「充电中」。
//   2) 参考项目把「接收器枚举成功」等同于「鼠标已连接」，鼠标关机或休眠时仍显示已连接。
// 本实现只解析有明确语义的字段（连接模式标记、DPI、电量），充电与连接状态由上层
// 依据「连接方式 + 电量 + 数据新鲜度」推导，见 MouseMonitor.cpp。
#pragma once

#include <windows.h>
#include <string>

namespace rapoo {

// HID 报文 ID
constexpr BYTE ID_CONTROL = 0x06;   // 输出报文：下发命令
constexpr BYTE ID_STATUS  = 0x07;   // 输入报文：设备主动上报
constexpr BYTE ID_FEATURE = 0x08;   // 特性报文：读取寄存器应答

// 硬件寄存器
constexpr BYTE BANK_SYSTEM = 0x08;  // 系统设置区
constexpr BYTE ADDR_POLLING_HZ = 0x80;

// 设备标记（报文 buf[1] 的高 4 位）
constexpr BYTE MARKER_WIRED  = 0x10;    // 有线直连
constexpr BYTE MARKER_DONGLE = 0x20;    // 2.4G 接收器

// 雷柏的 USB VID
constexpr unsigned short VENDOR_ID = 0x24AE;

// 三个端点
struct Endpoints
{
    std::wstring status_path;   // 被动上报（输入）
    std::wstring control_path;  // 命令下发（输出）
    std::wstring feature_path;  // 寄存器读取（特性）
    std::wstring pid;           // 形如 "0x1460"
    bool pid_wired{ false };    // PID 属于 0x46xx（有线直连）
    int status_report_len{ 19 };
    int feature_report_len{ 33 };
    int control_report_len{ 33 };

    bool HasStatus() const { return !status_path.empty(); }
};

// 一次被动上报解析出的字段
struct Status
{
    int battery{ -1 };      // -1 表示本次报文里的电量读数无效
    BYTE mode_nibble{ 0 };  // 报文 buf[1] & 0xF0 的原始值，仅用于日志
    int dpi{ 0 };
};

DWORD BuildUnlockPacket(BYTE* out_buf, DWORD buf_size);
DWORD BuildReadCommand(BYTE bank, BYTE addr, BYTE read_len, BYTE* out_buf, DWORD buf_size);
bool ParseFeatureRead(const BYTE* buf, DWORD len, BYTE* out_data, BYTE expect_len);
bool ParseStatus(const BYTE* buf, DWORD len, Status& out);

// 在系统里找一台雷柏鼠标的三个端点；找不到返回 false
bool FindEndpoints(Endpoints& out);

// 从设备路径里解析 vid / pid（用于判断是否是雷柏设备）
bool ParseVidPid(const std::wstring& lower_path, unsigned short& vid, unsigned short& pid);

// 把 PID 转成可读型号（仅收录社区实测过的型号，其余返回空字符串由上层回退到设备名）
const wchar_t* ModelNameFromPid(unsigned short pid);

} // namespace rapoo
