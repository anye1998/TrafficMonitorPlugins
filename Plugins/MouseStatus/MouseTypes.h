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
    // USB 接口，但判断不出是有线还是接收器。
    //
    // 2.4G 接收器和有线鼠标在系统里长得完全一样（都是 USB 上的 HID 鼠标，接收器也是
    // 常驻设备），Windows 也没有「这条链路是不是无线」的属性可查。设备不报电量、产品名
    // 里又没有线索时只能承认判不出来 —— 老版本这里一律当成 2.4G，结果所有有线鼠标都被
    // 写成「2.4G 无线」（用户反馈的正是这个）。要指定的话用右键菜单「连接方式」。
    UsbUnknown,
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

    // 两个时间必须分开，否则会误导：
    //   last_seen  —— 电量**数值最后一次变化**的时刻。它才是这个数字的可信时间，
    //                 蓝牙电量来自系统缓存，值可能几小时不变，这里就会一直停着。
    //   last_check —— 插件**最后一次检查**的时刻。主循环默认每 1000ms 走一轮
    //                 （可在设置里改 refresh_interval_ms），
    //                 所以它基本上一直在跳，只能说明"插件还在盯着"，不代表数据是新的。
    // 早先只有 last_seen 且每次检查都刷新它，于是鼠标提示里那个时间每秒都在变，
    // 看起来像"每秒都在重新读电量"，其实只是每秒重新读了一次缓存。
    std::wstring last_seen;
    std::wstring last_check;
};
