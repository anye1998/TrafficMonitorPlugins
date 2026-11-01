// MouseStatus —— 插件设置对话框
//
// 界面全部用代码构建，不使用 .rc 里的对话框模板：rc.exe 默认按系统 ANSI 代码页
// 解码源文件，把中文写进模板在多语言机器上容易变乱码；代码里用宽字符就没有这个问题。
#pragma once

// 显示设置对话框。返回 true 表示用户改动了设置（调用方据此刷新显示宽度）。
bool ShowMouseStatusOptions(void* parent);
