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
// 说明：参考项目的错误在于「有线即视为充电中」，而它判定有线的依据是厂商报文里的
// 模式位 0x10 —— 雷柏 VT3 在纯 2.4G 下同样报 0x10，于是永远显示充电中。
//
// 真机对照（插线 31 秒再拔掉）确认：报文 buf[7] 的 bit1 才是充电位
//   常态 0x01 -> 插线 0x02 -> 拔线 0x01（短暂 0x00 过渡）
// 所以现在的推导顺序是：
//   1) 设备自报在充电（或 USB 有线直连，必然有供电） -> 满则「已充满」，否则「充电中」
//   2) 其余（2.4G / 蓝牙）                          -> 「放电中」
// 这样「2.4G 连接 + 插着充电线」也能正确显示充电中。
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
