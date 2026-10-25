# 关于「HardwareMonitor 插件必须管理员才能看温度」

这个目录是本 Fork 新增的，记录一次针对该问题的实测结论和可用的解决方案。
**不属于上游项目**，上游也没有对应的问题讨论。

---

## 一、结论先说

| 目标 | 能否做到 |
|---|---|
| 不弹 UAC、直接以管理员权限启动（温度正常） | **可以**，见方案 A |
| 进程保持普通用户权限，同时能读 CPU 温度 | **做不到**（不是没找到办法，是原理上不允许），原因见第二节 |

如果你只是嫌每次点「以管理员身份运行」麻烦，用 **方案 A** 就够了。
如果你要的是「进程本身不提权」，那必须先改变量：CPU 温度在 Windows 上没有用户态数据源。

---

## 二、为什么普通权限读不到温度（实测，不是推测）

### 1. 温度数据只能从 MSR / SMN 取，必须经过内核驱动

插件通过 `LibreHardwareMonitorLib` 读传感器。CPU 核心温度来自
`IA32_THERM_STATUS` / AMD 的 SMN 寄存器，这些只能在内核态访问，
因此 LHM 必须借助内核驱动，0.9.6 起用的是 **PawnIO**。

### 2. PawnIO 的设备对象只对 SYSTEM / Administrators 开放

非管理员进程尝试打开设备的实测结果：

```
当前进程已提权: False
\\?\GLOBALROOT\Device\PawnIO   RW  -> FAIL err=5   (ERROR_ACCESS_DENIED)
\\?\GLOBALROOT\Device\PawnIO   R   -> FAIL err=5
\\.\PawnIO                     RW  -> FAIL err=5
```

驱动源码里创建设备的代码是（`namazso/PawnIO` → `PawnIO/src/driver.cpp`）：

```c
IoCreateDevice(driver_object, sizeof(device_extension), nullptr,
               k_device_type, FILE_DEVICE_SECURE_OPEN, FALSE, &device_object);
```

没有传安全描述符，于是设备对象继承创建者（LocalSystem）的默认 DACL——
**只有 SYSTEM 和 Administrators 能拿到句柄**。驱动没有提供任何开关
（注册表 / INF / 参数）可以放开这个限制，所以只能靠改 ACL（见方案 B，不推荐）。

### 3. LHM 自身没有"非管理员降级模式"

- `Computer.Open()` 里没有任何管理员判断，它只是照常去打开驱动；
- `Mutexes.Open()` 在拿不到全局互斥体时有 `try/catch` 兜底，不会因此崩掉；
- 也就是说 **唯一的硬阻塞点就是上面的设备 ACL**。

驱动打不开时 LHM 不报错，只是那些需要 MSR 的传感器拿不到值，
插件显示成 `--`。这就是"以普通权限打开就没有温度"的直接原因。

### 4. 有没有不需要驱动就能拿 CPU 温度的路子？本机实测：没有

常见的免驱动方案是 ACPI 温区（`root/WMI` → `MSAcpi_ThermalZoneTemperature`）。
本机（AMD Ryzen 7 9700X）查询结果：

```
== ACPI 温区 ==
查询失败: 不支持
```

即使查询成功，ACPI 温区给的是"某个热区"的模糊温度，通常不随核心负载变化，
而且很多台式机主板压根不实现。它不能替代 MSR 读数。

> 顺带说明：显卡温度（NVAPI / ADL）、电池、网络这些不依赖 MSR 的项目，
> 普通权限本来就能读。缺的只是 CPU / 主板这类需要 ring0 的指标。

---

## 三、方案 A（推荐）：静默以管理员启动，不弹 UAC

### 原理

Windows 任务计划服务允许注册 `RunLevel = Highest` 的任务。
任务由管理员**注册一次**之后，任何一次 `schtasks /run` 都会由任务计划服务
用**完整的提权令牌**拉起进程，**不经过 UAC 确认**。
所以把快捷方式指向 `schtasks /run /tn <任务名>`，就得到"管理员权限 + 无 UAC 弹窗"。

### 使用

1. 右键 PowerShell / Windows Terminal → **以管理员身份运行**（只需这一次）
2. 执行：

   ```powershell
   cd <本目录>
   powershell -ExecutionPolicy Bypass -File .\Install-SilentElevatedLaunch.ps1 `
              -AppPath "D:\TrafficMonitor\TrafficMonitor.exe"
   ```

3. 桌面会生成 **TrafficMonitor（静默提权）.lnk**，以后从它启动即可。

脚本做的事（全部可逆）：

- 注册计划任务 `TrafficMonitor_Elevated`，动作 = `TrafficMonitor.exe`，工作目录 = 程序目录，
  运行级别 = 最高，登录类型 = 交互式，**无触发器**（不会开机自动拉起，不干扰你原有的自启设置）
- 在桌面创建一个窗口最小化的快捷方式，调用 `schtasks /run /tn TrafficMonitor_Elevated`

### 撤销

```powershell
powershell -ExecutionPolicy Bypass -File .\Uninstall-SilentElevatedLaunch.ps1
```

删除计划任务和快捷方式，不触碰 TrafficMonitor 的任何文件与配置。

### 这一方案的代价

- 进程**仍然是管理员权限**，只是省掉了 UAC 确认。权限面没有变小，
  但也没有变大——和原来"右键以管理员身份运行"完全等价。
- 不修改任何系统安全设置，不放宽驱动 ACL，出问题影响面最小。

---

## 四、方案 B（不推荐，未提供脚本）：放开 PawnIO 设备 ACL

从技术上说，让普通权限进程也能读 MSR 的唯一办法是给设备对象加 ACE：

1. 管理员用 `CreateFile(..., WRITE_DAC, ...)` 打开 `\Device\PawnIO`
2. `SetKernelObjectSecurity` 写入一条新的 DACL，给目标账户
   （建议只给当前用户 SID，不要用 `BUILTIN\Users`）`GENERIC_READ | GENERIC_WRITE`
3. 设备对象在每次驱动加载时重建，所以还要让一个 SYSTEM 计划任务在每次开机后
   `Start-Service PawnIO` 再重新应用一次 ACL

为什么没做：

- **安全代价**：PawnIO 的模块虽经签名校验，但放开句柄等于让本机任何非管理员进程
  都能执行这些模块，读写 MSR / PCI 配置空间 / I/O 端口。这是实打实的攻击面扩大，
  而且 LHM 的定位本来就是"需要管理员"。
- **效果不完整**：即使 ACL 放开，硬盘温度走的是 `\\.\PhysicalDriveN`
  （`IOCTL_SMART_RECEIVE_DRIVE_DATA`），那是另一个更严的 ACL，普通权限依然读不到。
  最后会变成"CPU 温度有了、硬盘温度没了"的半成品。
- **不可持续**：PawnIO 升级、驱动重装都会把 ACL 重置，需要长期维护一个开机任务。

如果确实有"进程不能提权"的硬性要求，再单独提，可以按上面的思路实现，
但请先确认接受以上三点。

---

## 五、附：判断当前进程有没有温度的办法

启动 TrafficMonitor 后在插件里看 CPU 温度项：

- 显示具体数值 → 驱动可用，权限正常
- 显示 `--` → 驱动打不开（权限不足或没装 PawnIO）

也可以直接查 PawnIO 状态：

```powershell
Get-Service PawnIO                      # 期望 Running
(Get-AuthenticodeSignature "C:\Windows\System32\drivers\PawnIO.sys").Status   # 期望 Valid
```
