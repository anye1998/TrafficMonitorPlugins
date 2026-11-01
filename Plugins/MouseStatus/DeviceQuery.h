// MouseStatus —— 通用设备查询
//   1) 通用 HID Battery Strength（Usage Page 0x06 / Usage 0x20）——覆盖部分 2.4G 与蓝牙 HID 鼠标
//   2) Windows PnP 设备属性里缓存的蓝牙设备电量——覆盖走 GATT 电池服务的蓝牙鼠标
//   3) 「鼠标是否存在」单独判定：只要枚举到一个外接鼠标的 HID 集合就算在线，
//      与能不能读到电量无关（很多蓝牙鼠标只上报厂商自定义集合，不暴露标准电池用途）
#pragma once

#include <windows.h>
#include <string>
#include <vector>

// HID 接口的传输层
enum class HidTransport
{
    Unknown = 0,
    Usb,        // USB 总线（有线鼠标或 2.4G 接收器）
    BtClassic,  // 蓝牙经典 HID（HID over BR/EDR）
    BtLe,       // 蓝牙低功耗 HID（HID over GATT）
};

struct HidIfaceInfo
{
    std::wstring path;
    std::wstring product;   // HidD_GetProductString 读到的产品名，可能为空
    std::wstring group;     // 同一个物理设备的所有集合共用的实例标识
    unsigned short vid{ 0 };
    unsigned short pid{ 0 };
    HidTransport transport{ HidTransport::Unknown };
};

// 单个 HID 接口的探测结果
struct HidProbeResult
{
    bool opened{ false };
    unsigned short usage_page{ 0 };
    unsigned short usage{ 0 };
    bool has_battery_usage{ false };
    int battery_percent{ -1 };
    bool is_mouse_collection{ false };
    // 报文长度，仅用于诊断日志（判断端点能力时很有用）
    int input_report_len{ 0 };
    int output_report_len{ 0 };
    int feature_report_len{ 0 };
};

struct BtBatteryEntry
{
    std::wstring instance_id;
    std::wstring name;
    bool le{ false };
    bool is_mouse{ false };
    bool has_battery{ false };   // 节点上确实存在电量属性
    int percent{ -1 };
};

// 一个「看起来是外接鼠标」的 HID 集合。
// 注意：这里不要求它暴露电池用途 —— 蓝牙鼠标往往只有厂商自定义集合。
struct MouseIface
{
    std::wstring path;
    std::wstring product;
    HidTransport transport{ HidTransport::Unknown };
    unsigned short vid{ 0 };
    unsigned short pid{ 0 };
    unsigned short usage_page{ 0 };
    unsigned short usage{ 0 };
    bool has_battery_usage{ false };
    int battery_percent{ -1 };
};

// 一次 feature 报文读取的原始内容（诊断用）
struct FeatureReportDump
{
    unsigned int report_id{ 0 };
    std::vector<unsigned char> data;
};

// 枚举所有可访问的 HID 设备接口
std::vector<HidIfaceInfo> EnumHidInterfaces();

// 探测单个接口：是否有电池能力、是否鼠标集合，并顺手读一次电量
bool ProbeHidInterface(const std::wstring& path, HidProbeResult& out);

// 判定一个 HID 接口是否代表一只「外接鼠标」。
// 关键是排除笔记本内置触摸板：它们走 I2C-HID，路径里没有 vid_/pid_ 段（VID/PID 全 0）。
bool LooksLikeExternalMouse(const HidIfaceInfo& info, const HidProbeResult& probe);

// 枚举所有外接鼠标接口，蓝牙的排在前面（优先选无线那只）
std::vector<MouseIface> EnumMouseInterfaces();

// 从某个 HID 接口读取电池电量。成功返回 true，percent 为 0~100
bool ReadHidBatteryStrength(const std::wstring& path, int& percent);

// 读出该接口所有能应答的 feature 报文（诊断用，只读，不会改动设备状态）
std::vector<FeatureReportDump> DumpFeatureReports(const std::wstring& path);

// 列出所有当前在线的蓝牙设备节点（Dev_ 节点），无论有没有电量属性
std::vector<BtBatteryEntry> QueryBluetoothBatteries();

// 设备节点（含最多 3 层子节点）里是否出现了「鼠标」设备类
bool DevNodeContainsMouse(const std::wstring& instance_id);

// 名称是否像是鼠标
bool NameLooksLikeMouse(const std::wstring& name);
