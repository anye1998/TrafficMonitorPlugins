# TrafficMonitorPlugins
这是用于[TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor)的插件。

---

> **本仓库是 [zhongyang219/TrafficMonitorPlugins](https://github.com/zhongyang219/TrafficMonitorPlugins) 的个人 Fork，包含 AI 辅助的修改。**
> 目前只改动了 `HardwareMonitor` 插件：把内置的 `LibreHardwareMonitorLib` 从 0.9.4 升到 **0.9.6**，
> 从而彻底移除有漏洞的 **WinRing0** 驱动（CVE-2020-14979），改用 **PawnIO** 后端。
>
> 本次修改由 **AI 助手（WorkBuddy）** 分析、实现并提交。本仓库与上游无关，
> 相关问题请提到本仓库 Issues，不要打扰原作者。
>
> - 修改记录：[本 Fork 修改记录](#本-fork-修改记录)
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
| `Plugins/HardwareMonitor/app.rc` | 版本号 `1.0.1.1` → **`1.0.2.0`**，便于和上游 V1.0.1 的构建区分。 |
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
