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
> - **下载编译好的插件 —— [MouseStatus V1.0.15](https://github.com/anye1998/TrafficMonitorPlugins/releases/tag/MouseStatus_V1.0.15)**（新）
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

插件位置：[`Plugins/MouseStatus`](./Plugins/MouseStatus)，版本 **V1.0.15**，
下载：[MouseStatus V1.0.15](https://github.com/anye1998/TrafficMonitorPlugins/releases/tag/MouseStatus_V1.0.15)（x64 / x86 各一个 zip，解压出 `MouseStatus.dll`）。

### 它显示什么

| 显示项 | 类型 | 显示内容 |
|---|---|---|
| 鼠标电量 | 文本 | `85%`；设备在线但读不到电量时 `?`；完全没检测到设备时 `--` |
| 鼠标连接方式 | 文本 | `蓝牙` / `2.4G` / `USB`；未连接时 `--`。雷柏按 PID 前缀判定（`0x46xx` 为插线直连、`0x14xx` 为 2.4G 接收器），不采信厂商报文里的模式位 |
| 鼠标充电状态 | 文本 | `充电中` / `已充满` / `放电中`；无电量可判时同电量项。雷柏取报文 `buf[7]` 的 bit1（真机对照过），因此 2.4G 插着充电线也能正确显示 |
| 鼠标状态 | 自绘 | 电池图标（低电量转红、供电转绿）+ 电量 + 连接方式 |

安装方式和其它插件一样：把 `MouseStatus.dll` 放进 TrafficMonitor 的 `plugins` 目录，重启后
在「选项 → 常规设置 → 插件管理」里能看到；要让内容出现在**任务栏**上，需要在任务栏窗口
右键 →「显示设置」里勾选上面几个项目（四个显示项可以自由取舍、排序）。

鼠标提示（tooltip）里还会给出设备名、数据来源、两个时间和**诊断日志路径**：

- **读数更新于** —— 电量数值**真正变化**的时刻，这才是数据新鲜度；
- **最近检查** —— 插件最后一次轮询的时刻（默认每 1000ms 一轮，见上面的 `refresh_interval_ms`），只说明插件还在盯着。

两者分开显示是必要的：蓝牙电量取自系统缓存，数值可能几小时不变，
若只显示一个每秒跳动的时间，会让人误以为「每秒都在重新读电量」。

### 设置（V1.0.3 新增）

在显示项上**右键**，菜单里会出现四条插件命令：

| 命令 | 说明 |
|---|---|
| 启用诊断日志 | 勾选项，切换日志开关（状态会记住） |
| 清空诊断日志 | 一键删除 `MouseStatus.log`（含滚转出来的 `.old`） |
| 打开日志文件夹 | 用资源管理器打开 `%LOCALAPPDATA%\MouseStatus` |
| 立即写入诊断信息 | 把当前 HID 接口清单、雷柏端点、蓝牙电量一次性写进日志（日志关着会自动打开） |

菜单里的「插件设置」会打开一个设置对话框：日志开关、日志路径、当前文件大小、
**采集周期**、插件配置文件路径、打开文件夹、清空日志、立即写入诊断。

采集周期是一个可编辑的输入框（V1.0.12 起）：

```
采集周期（毫秒）：[ 1000 ] [应用]   200 ~ 30000
```

只接受数字；点「应用」或回车生效，失焦 / 关闭窗口 / 按 ESC 也会保存。
输入非法时点「应用」会提示并回退为当前生效值，其它路径静默回退。
改完**立即生效**，不用重启 TrafficMonitor —— 插件会叫醒工作线程，本轮睡眠直接结束。

日志开关存在 **`<TrafficMonitor>\plugins\MouseStatus.ini`**（和 Battery、DateTime、
KeyboardIndicator 等插件放在同一个目录，配置方式也一致）：

```ini
[config]
enable_log=1
refresh_interval_ms=1000
```

`refresh_interval_ms` 就是设置界面里的**采集周期**（毫秒，200~30000，默认 1000），
两处改的是同一个值。默认 1000 是刻意的：TrafficMonitor 用 `general.monitor_time_span`
（默认也是 1000ms）才调一次插件的 `DataRequired`，状态栏窗口也是 1000ms 重绘一次，
插件采得更快不会让界面更及时。如果你在主程序设置里把「监控时间间隔」调成了别的值，
把这里对齐过去即可；不写这一行就用默认的 1000。

**这个值只影响「多久检查一次」，不影响电量精度**：雷柏是设备主动上报、蓝牙是读系统缓存，
两者都与采集周期无关。它影响的是插拔充电线、鼠标开关机这些**状态变化被察觉的延迟**。

V1.0.6 之前存在注册表 `HKEY_CURRENT_USER\Software\MouseStatus` 的 `EnableLog`。
升级后第一次运行会自动把旧值迁移到 ini 并**删除该注册表键**，插件此后不再写注册表。

另外环境变量 `MOUSESTATUS_NOLOG=1` 可以强制关闭，优先级最高。

> 关闭日志后插件不再写任何文件；恢复默认只需重新勾选一次。

### 数据来源（按优先级自动选择）

1. **雷柏私有 HID 协议**（VID `0x24AE`，PID `0x14xx` 为无线节点、`0x46xx` 为有线节点）；
2. **通用 HID Battery Strength**（Usage Page `0x06` / Usage `0x20`）——多数 2.4G 接收器鼠标与蓝牙 HID 鼠标都会上报；
3. **Windows 蓝牙设备节点缓存的电量属性** `{104EA319-6EE2-4701-BD47-8DDBF425BBE5}, 2`——「设置」里显示的蓝牙外设电量就来自它。

因此蓝牙鼠标不需要任何厂商软件即可显示电量；连接方式是**从 HID 设备接口路径里的蓝牙服务
UUID 判定**的（`00001124` = 经典蓝牙、`00001812` = 低功耗蓝牙），不是靠名字猜的。

三个来源**每轮都会跑**，最后按「谁拿到电量用谁」合成一次快照。早先的 1.0.0 版是「先到先得」：
只要雷柏私有协议认出了设备就直接返回，另外两条后备链路被跳过，于是当私有报文里没有有效
电量时，明明能读到数字的通用 HID 也被挡住了 —— 表现就是**电量永远显示未知**。1.0.1 改成汇总。

**「鼠标在不在」和「电量读不读得到」是两个独立的问题**（V1.0.7 起）。只要枚举到一个
外接鼠标的 HID 集合，就算设备在线、连接方式照常显示，电量读不到就显示 `?`。
判定「外接」的规则是：

- 走蓝牙（BR/EDR 或 GATT）的鼠标集合 —— 一律算；
- 走 USB / 2.4G 的，路径里必须有 VID/PID —— 笔记本内置触摸板走 I2C-HID，
  路径里没有 VID/PID 段，据此排除，不会把触摸板当成"鼠标一直在线"。

早先只把「暴露了标准 Battery Strength 的接口」当成候选，于是很多只上报厂商自定义集合的
蓝牙鼠标连"在线"都算不上，状态栏一直是 `--`。

### 读不到电量时怎么排查（V1.0.1 新增）

先看状态栏显示的是哪一种：

- `?` —— 设备在线（连接方式也显示出来了），但**三个来源都没能给出数字**；
- `--` —— **完全没检测到在线设备**（鼠标关机、接收器没插、或插件没加载）。

然后看鼠标提示（tooltip），里面有一行「数据来源」，以及诊断日志的完整路径。日志在：

```
%LOCALAPPDATA%\MouseStatus\MouseStatus.log
```

日志记录：每个 HID 接口的 VID / PID / 用途页 / 报文长度 / 是否有 Battery Strength 用途、
雷柏端点探测结果、蓝牙设备节点的电量属性，以及收到的雷柏报文原始字节。按现象对照：

| 日志里的现象 | 含义 | 怎么办 |
|---|---|---|
| 所有条目都是 `hasBattery=0`，且 `FindEndpoints=0` | 设备没有向系统暴露任何标准电量 | 这种情况插件拿不到数字，只能靠厂商软件 |
| `FindEndpoints=1` 但一直是 `battery=-1` | 雷柏私有报文里电量字段为 0，或该型号格式不同 | 把日志里的「原始字节」发出来即可适配 |
| `在线蓝牙设备节点数：N` 但 `hasBattery=0` | Windows 没为该设备缓存电量（设置页里也看不到百分比） | 用一次鼠标让系统刷新；仍没有则该设备不通过 GATT 电池服务报电量 |
| `在线蓝牙设备节点数：0` | 没有任何在线蓝牙设备节点 | 确认鼠标确实处于连接状态（蓝牙设置里显示「已连接」） |
| `外接鼠标接口数：0`，但鼠标明明在用 | 该鼠标的集合没有被判定为外接鼠标 | 把「HID 接口清单」整段发出来，按实际路径补规则 |
| `HID 接口总数：0` | HID 枚举被拦或驱动异常 | 检查安全软件 |
| 日志文件根本不存在 | 插件没被加载 | 见下面「先确认插件真的加载了」 |

关闭日志：设一个环境变量 `MOUSESTATUS_NOLOG=1` 即可（插件下次启动生效）。

### 先确认插件真的加载了

「一切都是未知」有时根因跟电量无关，而是插件压根没运行：

1. **位数必须和主程序一致**。`TrafficMonitor.exe` 是 64 位就得放 `MouseStatus_x64.zip` 里的 DLL，
   放错位数会**静默加载失败**，状态栏里那几个项目根本不会出现。
2. **目录要对**：DLL 放主程序同级的 `plugins\` 子目录，不是主程序目录本身。
3. **要在「显示设置」里勾选**：插件加载成功 ≠ 自动上任务栏，得在任务栏窗口右键 →
   「显示设置」把 `鼠标电量` / `鼠标连接方式` 勾上。
4. **VC++ 运行库**：插件用动态 CRT，系统缺少 VC++ 2015-2022 运行库时同样会加载失败。

判断方法：打开「选项 → 常规设置 → 插件管理」，左侧列表里能看到「鼠标状态」就说明加载成功了。

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

- **蓝牙电量读的是 Windows 缓存值**，不是插件主动去问鼠标。`DEVPKEY_Bluetooth_Battery` 这个属性由
  系统在收到 GATT 通知时更新，所以电量数字可能在系统刷新前保持不变（鼠标提示里标的「系统缓存，
  刷新可能滞后」就是这个意思）。想尽快刷新，就点开一次「设置 → 蓝牙和设备」里的该设备。
- 雷柏机型的充电状态取厂商报文 `buf[7]` 的 bit1（真机对照过：常态 `01` → 插线 `02` → 拔线 `01`），
  因此「2.4G 连接 + 插着充电线」也能正确显示 `充电中`。该结论来自实测，换型号后若行为不符需重新核对。
- 有线 / 2.4G 的区分：雷柏靠 PID 前缀（`0x46xx` 为有线）判定；**其它品牌**只能靠「能否上报电量」
  推断，个别支持电量上报的有线鼠标可能被显示为 `2.4G`。
- 通用路径依赖设备自身暴露 HID 电池用途，或 Windows 已缓存其蓝牙电量；两者都没有的设备会显示 `?`
  （V1.0.0 及更早版本在这种情况下显示 `--`，容易和「没插设备」混淆，所以改了）。
- 鼠标休眠 / 关机时，电量保留最后一次真实读数，连接方式显示为 `--`（未连接），鼠标提示里会标注读数时间。
- **雷柏 2.4G 的「是否已连接」靠活性探测**：接收器是常驻设备，鼠标关机后它的 HID 接口依然在，
  所以「接口还在」推不出「鼠标还连着」。插件会周期性往设备的命令通道下发一次「读寄存器」，
  再在 **1.6 秒窗口内每 20ms 轮询**特性报文，首字节变成 `0x01` 才算应答。
  实测这台 VT3 即使已经静置到插件收不到任何上报，命令下发后约 **270ms** 也会应答
  （偶发最慢约 800ms，所以窗口要留足）；而关机后 5 秒窗口内 45 次全部不应答，不会误报。
  判离线用的是「9.5 秒内一次应答都没有」（每 3 秒探一次，连续 3 次失败）——
  雷柏鼠标静置时最长 3 分钟才发一帧报文，只看「这一轮有没有数据」必然误判。
  探测通道不支持该命令的机型会退化成 60 秒沉默判定，效果是「关机后慢一点才变 `--`」，不会误报离线。
- **蓝牙鼠标的「是否已连接」取自蓝牙栈的 `BLUETOOTH_DEVICE_INFO::fConnected`**，不是「PnP 节点是否存在」。
  这一点很关键：`BTHLE\DEV_xxx` 节点在设备关机后**不会从系统里消失**（Windows 会保留已配对设备的
  devnode 与电量缓存，就是「设置」里显示「已配对」的那种状态），单看节点会把关机误判成在线。
  查不到连接状态时（没有适配器、bthserv 未启动、地址没匹配上）才退回「HID 接口是否还在」这个判据。
- 如果连系统自己都认为设备还连着（「设置 → 蓝牙和设备」里显示「已连接」），插件也会跟着显示在线 ——
  那种情况属于系统层面尚未察觉断开，插件没有更可靠的信息源。

### 已实测适配的设备

| 设备 | 连接 | 电量来源 | 充电状态 |
|---|---|---|---|
| 雷柏 VT3（`24AE:1412`） | 2.4G | 私有报文 `buf[8]` | 私有报文 `buf[7]` bit1 |
| HUAWEI Mouse CD20（`12D1:1099`） | 蓝牙 LE | `BTHLE\DEV_*` 节点的蓝牙电量属性 | 无（蓝牙供电，按电量推导） |

后者的日志形态可以作为「蓝牙鼠标读不到电量」的对照样本：蓝牙栈节点里应该能看到

```
le=1 hasBattery=1 battery=100 isMouse=1 name="HUAWEI Mouse CD20"
    id=BTHLE\DEV_D1AA72778ACE\...
```

注意实例 ID 里的 `DEV_` 是**全大写** —— 早期版本用大小写敏感的 `Dev_` 匹配，一个节点都匹配不上，
这就是它显示「未检测到鼠标」的直接原因。

### 版本变更

| 版本 | 变更 |
|---|---|
| **V1.0.15** | 雷柏关机识别真正跑通：实测读命令下发后约 **270ms**（偶发 800ms）才把应答写进特性报文，V1.0.14 只睡 30/60ms 就读，探测 100% 失败。改为 1.6 秒窗口内每 20ms 轮询，首字节 `0x01` 才算应答；真机对照开机的 203 次探测全部成功、关机的 45 次全部不应答。离线阈值改为 9.5 秒（关机后约 10 秒翻成 `--`），不再撤销挂起的读请求（挂起读不影响取应答，撤销反而吞上报）；探测不可用的机型退回 60 秒沉默判定 |
| V1.0.14 | 修复雷柏 2.4G 鼠标「连没连」判断不准：活性探测带着 status 端点挂起的读请求去 ping，特性通道永远拿不到应答，一次失败就被当成断开并关句柄重枚举（界面在有读数与未知之间跳，关机也无法识别）。改为探测前先 `CancelPendingStatusRead`、探测本身加重试与失败明细；离线判定改看「最近一次有应答的时刻」+ 宽限期（探测可用 8s / 不可用 30s），句柄只在真正失效时重建；接收器常驻不再等于鼠标在线 |
| V1.0.13 | 修复蓝牙鼠标关机后仍显示「已连接」：蓝牙 PnP 节点在设备关机后不会消失，旧版把「节点存在」当成「已连接」；改为查 `BLUETOOTH_DEVICE_INFO::fConnected`，查不到时回退到「HID 接口是否还在」 |
| V1.0.12 | 采集周期做进设置界面（可编辑 + 应用按钮，改完立即生效，通过事件唤醒工作线程而不必等旧周期走完） |
| V1.0.11 | 采集周期 400ms → 1000ms（对齐主程序的 `monitor_time_span` 与状态栏重绘），并改为「睡到整周期」避免雷柏等报文把周期拖到 1.2~1.6s；周期可在 ini 的 `refresh_interval_ms` 调整 |
| V1.0.10 | 鼠标提示拆成「读数更新于」（数值变化时刻）与「最近检查」（插件轮询时刻），不再让每秒跳动的时间冒充数据新鲜度 |
| V1.0.9 | 鼠标提示标注蓝牙电量来自「系统缓存值」；README 修正雷柏充电状态说明、补充已实测适配设备（华为 CD20）与蓝牙电量刷新机制 |
| V1.0.8 | 蓝牙节点枚举补全（支持 `BTHLEDEVICE`、`Dev_` 大小写不敏感）、feature 报文改为按设备采集全部集合（不再只读鼠标集合）；诊断能区分「没扫到蓝牙节点」与「扫到了但没电量」 |
| V1.0.7 | 修复蓝牙鼠标「检测不到」：在线判定与电量读取解耦，只要枚举到外接鼠标的 HID 集合就算在线（不再要求它暴露标准电池用途）；笔记本内置触摸板按「I2C-HID 无 VID/PID」排除；日志新增鼠标接口小节与蓝牙 feature 报文 dump |
| V1.0.6 | 设置项改用 `plugins\MouseStatus.ini` 保存（与其它插件统一），不再写注册表；升级时自动迁移旧值并清理注册表残留；设置窗口改为按显示器工作区居中，不再跑到屏幕最下方/屏幕外 |
| V1.0.5 | 按真机报文解析出充电标志位（`buf[7]` 的 bit1），「2.4G 连接 + 插着充电线」现在能正确显示 `充电中`；断连时清空充电位 |
| V1.0.4 | 原始报文记录改为「启动记 20 帧 + 内容变化即记 + 30 秒心跳」，不再按进程生命周期截断；日志行补 `buf7` / `buf9`，便于比对插拔充电线前后的字节差异 |
| V1.0.3 | 连接方式改由 PID 前缀判定（VT3 在 2.4G 下报文里也有 `0x10`，旧版据此误判成「USB 有线 / 充电中」）；新增日志开关与「清空日志」设置项 |
| V1.0.2 | 修复雷柏端点选错集合：改为按 Usage `0x0002` 精确选取状态通道（原先会挑中 33 字节的配置/升级通道，一个状态报文都收不到），状态读请求改为跨轮次挂起避免漏帧 |
| V1.0.1 | 修复「雷柏私有协议认出设备后短路后备链路」导致电量恒为未知；通用 HID 扫描不再跳过雷柏 VID；活性探测不再把空特性报文当作在线；新增诊断日志与 `?` / `--` 区分 |
| V1.0.0 | 首个版本 |
