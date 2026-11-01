// MouseStatus —— 轻量诊断日志
//
// 插件在别人机器上「读数未知」时，光靠肉眼无法判断卡在哪一步：可能是 HID 枚举不到、
// 可能是设备压根没暴露电量用途、也可能是雷柏报文格式对不上。这里把每个数据源的探测
// 过程写进一个文本文件，出问题时直接把日志发回来即可定位。
//
// 日志位置：%LOCALAPPDATA%\MouseStatus\MouseStatus.log（UTF-8 带 BOM，可直接用记事本打开）
// 关闭日志：设置环境变量 MOUSESTATUS_NOLOG=1
#pragma once

#include <string>

namespace diag {

// 日志文件的完整路径；首次调用时初始化目录
std::wstring LogFilePath();

// 写一行日志（自动加时间戳与换行）。fmt 为 printf 风格宽字符格式串。
void Log(const wchar_t* fmt, ...);

// 把一段字节以十六进制写入日志，便于比对厂商报文格式
void LogHex(const wchar_t* label, const unsigned char* data, unsigned int len);

// 日志是否启用
bool Enabled();

} // namespace diag
