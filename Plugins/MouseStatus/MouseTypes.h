// MouseStatus —— 鼠标状态插件（TrafficMonitor）
// 公共数据类型定义
#pragma once

#include <string>

// 鼠标当前的连接方式
enum class LinkKind
{
    None = 0,       // 未检测到 / 已断开
    Bluetooth,      // 蓝牙（经典蓝牙 HID，HID over BR/EDR）
    BluetoothLE,    // 蓝牙（低功耗 HID，HID over GATT）
    Wireless24G,    // 2.4G 无线（USB 接收器）
    UsbWired,       // USB 有线直连
};

// 充电状态
//
// 说明：这里刻意不再沿用参考项目中的做法（有线即视为「充电中」、或凭厂商报文中
// 含义不明的位来判定）。充电状态一律由「连接方式 + 当前电量」推导：
//   有线 + 电量 < 100  -> 充电中
//   有线 + 电量 = 100  -> 已充满
//   非有线             -> 放电中
// 这样既修掉了「无线状态下一直显示充电中」的假阳性，也能区分「充电中」和「已充满」。
enum class ChargeKind
{
    Unknown = 0,
    Discharging,
    Charging,
    Full,
};

// 数据来源
enum class DataSource
{
    None = 0,
    Rapoo,          // 雷柏私有 HID 协议
    HidBattery,     // 通用 HID Battery Strength（Usage Page 0x06 / Usage 0x20）
    BluetoothProp,  // Windows 蓝牙设备节点上缓存电量属性
};

// 一次采样得到的鼠标状态
struct MouseSnapshot
{
    LinkKind link{ LinkKind::None };
    ChargeKind charge{ ChargeKind::Unknown };
    int battery{ -1 };              // -1 表示未知，单位 %
    bool battery_is_stale{ false }; // 电量是上一次已知读数（设备休眠/断连）
    bool live{ false };             // 最近一次探测是否有效（用于判断连接状态）
    DataSource source{ DataSource::None };
    std::wstring device_name;       // 设备名（型号/友好名）
    std::wstring last_seen;         // 最后一次拿到有效数据的本地时间 HH:MM:SS
};
