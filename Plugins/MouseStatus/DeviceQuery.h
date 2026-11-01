// MouseStatus —— 通用设备查询
//   1) 通用 HID Battery Strength（Usage Page 0x06 / Usage 0x20）——覆盖多数 2.4G 与蓝牙 HID 鼠标
//   2) Windows PnP 设备属性里缓存的蓝牙设备电量——覆盖走 GATT 电池服务的蓝牙鼠标
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
};

struct BtBatteryEntry
{
    std::wstring instance_id;
    std::wstring name;
    bool le{ false };
    bool is_mouse{ false };
    int percent{ -1 };
};

// 枚举所有可访问的 HID 设备接口
std::vector<HidIfaceInfo> EnumHidInterfaces();

// 探测单个接口：是否有电池能力、是否鼠标集合，并顺手读一次电量
bool ProbeHidInterface(const std::wstring& path, HidProbeResult& out);

// 从某个 HID 接口读取电池电量。成功返回 true，percent 为 0~100
bool ReadHidBatteryStrength(const std::wstring& path, int& percent);

// 读取所有「当前在线」的蓝牙设备节点上报的电量（Windows 缓存值）
std::vector<BtBatteryEntry> QueryBluetoothBatteries();

// 设备节点（含最多 3 层子节点）里是否出现了「鼠标」设备类
bool DevNodeContainsMouse(const std::wstring& instance_id);

// 名称是否像是鼠标
bool NameLooksLikeMouse(const std::wstring& name);
