// MouseStatus —— 轻量诊断日志
//
// 插件在别人机器上「读数未知」时，光靠肉眼无法判断卡在哪一步：可能是 HID 枚举不到、
// 可能是设备压根没暴露电量用途、也可能是厂商报文格式对不上。这里把每个数据源的
// 探测过程写进一个文本文件，出问题时直接把日志发回来即可定位。
//
// 日志位置：%LOCALAPPDATA%\MouseStatus\MouseStatus.log（UTF-8 带 BOM，可直接用记事本打开）
// 开关：插件右键菜单 / 设置对话框里的「启用诊断日志」，持久化在
//       HKEY_CURRENT_USER\Software\MouseStatus 的 EnableLog。
// 另外环境变量 MOUSESTATUS_NOLOG=1 可以作为强制关闭的兜底。
#pragma once

#include <string>

namespace diag {

// 日志文件的完整路径；首次调用时初始化目录
std::wstring LogFilePath();

// 日志所在目录（不含文件名）
std::wstring LogDirectory();

// 用户设置里的日志开关（每次调用都读一次运行时值，不做磁盘 IO）
bool Enabled();

// 修改日志开关，并写入注册表持久化
void SetEnabled(bool enabled);

// 删除 MouseStatus.log 与 MouseStatus.log.old，返回是否全部删干净
bool DeleteLogFiles();

// 当前日志文件大小（字节）；文件不存在时返回 0
unsigned long long LogFileSize();

// 用记事本打开日志目录
bool OpenLogFolder();

// 写一行日志（自动加时间戳与换行）。fmt 为 printf 风格宽字符格式串。
void Log(const wchar_t* fmt, ...);

// 把一段字节以十六进制写入日志，便于比对厂商报文格式
void LogHex(const wchar_t* label, const unsigned char* data, unsigned int len);

} // namespace diag
