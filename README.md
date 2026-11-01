# TrafficMonitorPlugins
这是用于[TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor)的插件。

---

> **本仓库是 [zhongyang219/TrafficMonitorPlugins](https://github.com/zhongyang219/TrafficMonitorPlugins) 的个人 Fork，包含 AI 辅助的修改。**
> 目前除改动了 `HardwareMonitor` 插件外（把内置的 `LibreHardwareMonitorLib` 从 0.9.4 升到 **0.9.6**，
> 从而彻底移除有漏洞的 **WinRing0** 驱动（CVE-2020-14979），改用 **PawnIO** 后端），
> 还新增了 **`MouseStatus` 插件** —— 在状态栏显示鼠标电量与连接方式（蓝牙 / 2.4G / USB）。
>
> 本次修改由 **AI 助手（WorkBuddy）** 分析、实现并提交。本仓库与上游无关，
> 相关问题请提到本仓库 Issues，不要打扰原作者。
>
> - 修改记录：[本 Fork 修改记录](#本-fork-修改记录)
> - **下载编译好的插件 —— [MouseStatus V1.0.0](https://github.com/anye1998/TrafficMonitorPlugins/releases/tag/MouseStatus_V1.0.0)**（新）
> - **下载编译好的插件 —— [HardwareMonitor V1.0.2](https://github.com/anye1998/TrafficMonitorPlugins/releases/tag/HardwareMonitor_V1.0.2)**
> - [`MouseStatus` 插件说明](#2026-11-01--新增-mousestatus-插件状态栏显示鼠标电量与连接方式)
> - 「必须管理员才能看温度」的原因与解决办法：[`tools/privilege/`](./tools/privilege/README.md)

## 插件下载

请点击以下链接转到插件下载页面：

[TrafficMonitor 插件下载](./download/plugin_download.md)

## 插件使用说明

根据TrafficMonitor的版本（x86为32位，x64为64位）选择对应版本的插件，下载后解压可得到dll文件，下载后将插件dll放到TrafficMonitor程序所在目录下的`plugins`目录下：

![image-20221013203124953](images/image-20221013203124953.png)

重新启动TrafficMonitor后可以在“选项”——“常规设置”——“插件管理”中看到所有的插件：

![image-20221013203353499](images/image-20221013203353499.png)

要使插件项目显示到任务栏中，请在任务栏窗口上点击鼠标右键，选择“显示设置”。

![image-20221013203527593](images/image-20221013203527593.png)

![image-20221013203621714](images/image-20221013203621714.png)

此时，“显示设置”中会显示已加载的插件项目，勾选你希望显示在任务栏上的项目，点击确定即可。

关于更多插件使用的详细说明，请参考以下链接：

[插件功能 · zhongyang219/TrafficMonitor Wiki (github.com)](https://github.com/zhongyang219/TrafficMonitor/wiki/插件功能)

## 如何开发插件

关于如何开发TrafficMonitor，请参考以下链接：

[插件开发指南 · zhongyang219/TrafficMonitor Wiki (github.com)](https://github.com/zhongyang219/TrafficMonitor/wiki/插件开发指南)

## 插件的提交

你可以向我发送电子邮件来提交你开发的插件，并附上插件简介和下载地址。也可以直接向此仓库提交pull request来更新插件下载页面：[plugin_download.md](download/plugin_download.md) 和 [plugin_download_en.md](download/plugin_download_en.md)，按照文档原来的格式添加插件介绍和下载链接。同时更新一下[plugins_version.xml](plugins_version.xml)文件，这个文件用于在TrafficMonitor的“插件管理”界面显示插件是否有更新，在`plugins_version.xml`中添加你插件的文件名和最新的版本号，当插件有更新时也要更新一下这个文件里的版本号。

---

# 本 Fork 修改记录

## 2026-10-25 · HardwareMonitor 插件的内核驱动从 WinRing0 迁移到 PawnIO

**性质**：本 Fork 的本地修改，**由 AI 助手（WorkBuddy）完成分析与实现**，未提交到上游。

| 文件 | 改动 |
|---|---|
| `Plugins/HardwareMonitor/LibreHardwareMonitorLib.dll` | 712,192 → 1,203,200 字节，版本 **0.9.4 → 0.9.6**（`runtimes/win-x64/lib/net472`）。旧版内嵌 **WinRing0 1.2.0.5**（`WinRing0` 字符串命中 2 处），该驱动存在 CVE-2020-14979，安全软件会拦截程序释放的 `*.sys`；0.9.6 已彻底移除 WinRing0，改用 **PawnIO**（签名驱动、兼容 HVCI / 内存完整性），二进制内 `WinRing0` 命中 **0**、`PawnIO` 命中 4。 |
| `Plugins/HardwareMonitor/HardwareMonitorHelper.cpp` | 补上 0.9.6 新增的枚举：`SensorType::Timing`（单位 `ns`）、`HardwareType::SuperIO`（归到主板图标）、`HardwareType::Cooler`（归到风扇控制器图标）。缺这三个 case 时对应传感器的名称会显示为空白。 |
| `Plugins/HardwareMonitor/resource.resx` | 新增 `Timing` 显示名。 |
| `Plugins/HardwareMonitor/resource.zh-CN.resx` | 新增 `Timing` 中文名（`时序`）。 |
| `Plugins/HardwareMonitor/app.rc` | 版本号 `1.0.1.1` → **`1.0.2.0`**（文件属性里的版本），便于和上游 V1.0.1 的构建区分。 |
| `Plugins/HardwareMonitor/HardwareMonitor.cpp` | `GetInfo(TMI_VERSION)` 返回值 `1.0.1` → **`1.0.2`**。这个字符串才是 TrafficMonitor「插件管理」界面显示的版本，和 `app.rc` 里的文件版本是两套，必须同步改。 |
| `plugins_version.xml` | `HardwareMonitor.dll` 的版本 `1.00` → **`1.02`**，用于主程序检查插件更新。 |
| `.github/workflows/build-hardware-monitor.yml`（新增） | 上游没有 CI。该工作流用 `windows-2022` + VS2022（v143），先编 `utilities` 静态库再编 `HardwareMonitor`；编译前按需补装 MFC（`app.rc` 需要 `afxres.h`）、C++/CLI 与 .NET Framework 4.7.2 目标包；上传前校验随源码带的 LHM 必须无 WinRing0、有 PawnIO、程序集版本为 `0.9.6.0`。 |
| `tools/privilege/`（新增） | 「插件必须管理员才能看温度」的实测分析，以及「静默以管理员启动、不弹 UAC」的安装/卸载脚本。详见 [`tools/privilege/README.md`](./tools/privilege/README.md)。 |

### 为什么选 0.9.6 而不是 0.9.7

截至 2026-10-25，NuGet 上 `LibreHardwareMonitorLib` 的最新**稳定版**是 `0.9.6`；
`0.9.7` 只有 `0.9.7-pre771` 这类预发布版本（共 128 个 pre 版本，尚未发布正式版），
且 0.9.7 起把 P/Invoke 换成了 `Windows.Win32`（CsWin32）生成的代码，依赖面变化较大。
插件跟稳定版更合适。

### 关于「需要管理员才能显示温度」

**这不是本 Fork 引入的问题**——0.9.4 + WinRing0 时代同样需要管理员。读 CPU 核心温度
必须在内核态访问 MSR，进而必须打开内核驱动设备，而驱动设备对象默认只对
SYSTEM / Administrators 放行；PawnIO 驱动里没有提供任何放开该限制的开关。

实测数据、驱动源码依据，以及可行的「免 UAC 提示」方案，见
[`tools/privilege/README.md`](./tools/privilege/README.md)。

### 验证方式

```powershell
# 随源码带的 DLL 已不含 WinRing0
$b = [System.Text.Encoding]::ASCII.GetString(
        [System.IO.File]::ReadAllBytes("Plugins/HardwareMonitor/LibreHardwareMonitorLib.dll"))
$b.Contains('WinRing0')      # 期望 False
$b.Contains('PawnIO')        # 期望 True
[System.Reflection.AssemblyName]::GetAssemblyName(
    (Resolve-Path "Plugins/HardwareMonitor/LibreHardwareMonitorLib.dll")).Version   # 期望 0.9.6.0
```

### 回滚

```powershell
git checkout HEAD~1 -- Plugins/HardwareMonitor
```

注意回滚等于把带 CVE-2020-14979 的 WinRing0 恢复回来，安全软件会重新拦截。

---

## 2026-11-01 · 新增 MouseStatus 插件（状态栏显示鼠标电量与连接方式）

**性质**：本 Fork 新增的插件，**由 AI 助手（WorkBuddy）完成设计与实现**，未提交到上游。

插件位置：[`Plugins/MouseStatus`](./Plugins/MouseStatus)，版本 **V1.0.0**，
下载：[MouseStatus V1.0.0](https://github.com/anye1998/TrafficMonitorPlugins/releases/tag/MouseStatus_V1.0.0)（x64 / x86 各一个 zip，解压出 `MouseStatus.dll`）。

### 它显示什么

| 显示项 | 类型 | 显示内容 |
|---|---|---|
| 鼠标电量 | 文本 | `85%`；无数据时 `--` |
| 鼠标连接方式 | 文本 | `蓝牙` / `2.4G` / `USB` |
| 鼠标充电状态 | 文本 | `充电中` / `已充满` / `放电中` |
| 鼠标状态 | 自绘 | 电池图标（低电量转红、供电转绿）+ 电量 + 连接方式 |

安装方式和其它插件一样：把 `MouseStatus.dll` 放进 TrafficMonitor 的 `plugins` 目录，重启后
在「选项 → 常规设置 → 插件管理」里能看到；要让内容出现在**任务栏**上，需要在任务栏窗口
右键 →「显示设置」里勾选上面几个项目（四个显示项可以自由取舍、排序）。

鼠标提示（tooltip）里还会给出设备名、数据来源和最后一次读数时间。

### 数据来源（按优先级自动选择）

1. **雷柏私有 HID 协议**（VID `0x24AE`，PID `0x14xx` 为无线节点、`0x46xx` 为有线节点）；
2. **通用 HID Battery Strength**（Usage Page `0x06` / Usage `0x20`）——多数 2.4G 接收器鼠标与蓝牙 HID 鼠标都会上报；
3. **Windows 蓝牙设备节点缓存的电量属性** `{104EA319-6EE2-4701-BD47-8DDBF425BBE5}, 2`——「设置」里显示的蓝牙外设电量就来自它。

因此蓝牙鼠标不需要任何厂商软件即可显示电量；连接方式是**从 HID 设备接口路径里的蓝牙服务
UUID 判定**的（`00001124` = 经典蓝牙、`00001812` = 低功耗蓝牙），不是靠名字猜的。

### 相对参考项目 rapoo-tray 修正的两处判定错误

参考项目：[Iris-0109/rapoo-tray](https://github.com/Iris-0109/rapoo-tray)（MIT）。它的思路很有参考价值，
但**充电状态**与**连接状态**的判定有误，本插件重写了这两部分：

| 问题 | 参考项目的做法 | 本插件的做法 |
|---|---|---|
| **连接状态** | 只要接收器 / HID 端点枚举成功就置为「已连接」，鼠标关机或进入休眠后仍显示已连接 | 改为**活性探测**：雷柏设备在长时间没有主动上报时主动读一次寄存器，读得到才算在线；通用 HID 以「本次电量读取是否成功」为准；蓝牙以「设备节点当前是否在线」为准 |
| **充电状态** | 把「有线」直接等同于「充电中」，再用 `statusByte & 0x02`、`buf[9] == 1/2` 这类未公开的位判定；一旦被置起，鼠标切回无线后仍一直显示「充电中」，满电时也仍显示「充电中」 | 只用**「连接方式 + 电量」**推导：有线且 < 100% → 充电中；有线且 = 100% → 已充满；无线 → 放电中。厂商报文中含义不明的位一律不采信 |
| **电量** | 报文里电量为 0 且没有缓存时，直接「补」成 100% | 0 视为无效读数（上电瞬间 ADC 未稳定），沿用上一次真实读数；从未读到就显示 `--` |

另外两点实现差异：

- **蓝牙支持**：参考项目只识别雷柏 VID，本插件同时覆盖通用蓝牙鼠标（走上面第 2、3 条数据来源）。
- **依赖**：插件是纯 Win32 实现，不用 MFC，CI 里不必额外安装 ATL/MFC 组件，构建更快。

### 构建与发布

- 工程：`Plugins/MouseStatus/MouseStatus.vcxproj`，已加入 `TrafficMonitorPlugins.sln`；
- CI：[`.github/workflows/build-mouse-status.yml`](./.github/workflows/build-mouse-status.yml)，
  在 `windows-2022` 上分别编 x64 与 Win32（x86），产出 `MouseStatus.dll`，
  并校验 `TMPluginGetInstance` 导出、文件版本号，以及中文界面字符串确实按 UTF-16 落地；
- 打 `MouseStatus_V*` 标签会自动创建 Release 并附上 `MouseStatus_x64.zip` / `MouseStatus_x86.zip`。

本地单独编译（不需要打开解决方案）：

```bat
msbuild Plugins\MouseStatus\MouseStatus.vcxproj /p:Configuration=Release /p:Platform=x64
rem 产物：bin\x64\Release\MouseStatus.dll
```

### 已知限制

- 雷柏机型的充电状态是**推导值**，不是硬件标志位：厂商报文中没有公开、可靠的充电位，与其采信
  含义不明的位（参考项目就是这样产生假阳性的），不如按「有线 + 未满电」推导；满电时显示「已充满」。
- 有线 / 2.4G 的区分：雷柏靠 PID 前缀（`0x46xx` 为有线）判定；**其它品牌**只能靠「能否上报电量」
  推断，个别支持电量上报的有线鼠标可能被显示为 `2.4G`。
- 通用路径依赖设备自身暴露 HID 电池用途，或 Windows 已缓存其蓝牙电量；两者都没有的设备会显示 `--`。
- 鼠标休眠 / 关机时，电量保留最后一次真实读数，连接方式显示为 `--`（未连接），鼠标提示里会标注读数时间。
