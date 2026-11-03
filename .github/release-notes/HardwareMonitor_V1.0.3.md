# HardwareMonitor 插件 V1.0.3（x64）

> **本版本由 AI 助手（WorkBuddy）协助修改与构建，非上游官方发布。**
> 上游仓库：[zhongyang219/TrafficMonitorPlugins](https://github.com/zhongyang219/TrafficMonitorPlugins)

## 下载

| 文件 | 说明 |
|---|---|
| `HardwareMonitor_V1.0.3_x64.zip` | 插件本体（`HardwareMonitor.dll` + `zh-CN` 资源 + 安装说明） |

## 这个版本修什么

笔记本上只启用电池信息时，三个老毛病：

1. 开机后电池信息**有时不显示**，进「插件设置」还发现电池那一行没了，得重新添加；
2. 重新添加后用一段时间，电池数值**卡住不动**；
3. 重启主程序后电池又不显示、项目又消失。

三个症状是同一串问题的不同阶段，根因都在插件侧：

| # | 位置 | 问题 |
|---|---|---|
| 1 | `LoadConfig` | 只给「当时能查到传感器」的配置项创建显示项。开机那一刻电池设备可能还没枚举出来（LibreHardwareMonitor 的 `BatteryGroup` 在构造时才用 `SetupDiGetClassDevs(GUID_DEVICE_BATTERY)` 找设备），这一项被跳过 |
| 2 | `GetItem` | 插件少交出一项 → 主程序「显示顺序」是按**下标**存的，`NormalizeItemOrder()` 又会删越界、补缺失，用户配好的显示项整体错位并固化 |
| 3 | `SettingsForm::UpdateItemList` | 传感器找不到的行直接跳过 → 「项目没了」 |
| 4 | `FindSensorByIdentifyer` | 「找不到」也被写进缓存 → 第一次没找到，这一整轮运行都找不到 |
| 5 | identifier | 由「硬件路径 + 类型 + **名称**」拼成，电池的功率/电流传感器会随充放电改名（`Charge Rate` / `Discharge Rate`），电池标识里还带着会变的 `BatteryTag` |
| 6 | `DataRequired` | 整个循环一个 try/catch，任一项取数抛异常，后面的项目全部停刷 → 「卡住」 |
| 7 | `RemoveDisplayItem` | 按下标删配置，列表行错位时会删掉**别的**项目 |

第 5 条是「卡住」的关键：旧代码缓存了 `ISensor` 对象，充放电一切换 identifier 就查不到新的，但缓存里的旧对象还在，继续返回上一次的读数。

## 本版本改动

**监控项列表稳定**

- `LoadConfig` 为每个配置项都创建显示项，传感器暂时找不到也保留（显示 `--`）；
- `GetItem(index)` 的数量与顺序永远等于配置项，主程序按索引存的显示顺序不再错位。

**传感器查找**

- 每轮 `Accept()` 之后整体重建 `identifier → sensor` 索引（先建新的再整体替换），只收录当前激活的传感器；
- 不再缓存「找不到」；
- identifier 宽松匹配：去掉电池 `BatteryTag`、把充放电三种改名归一后再匹配一次；
  配置里的 identifier 不改写（它是主程序的项目 ID）。

**电池设备自动恢复**

- 电池项连续 3 轮读不到 → 关掉再打开 `IsBatteryEnabled`（LibreHardwareMonitor 只有添加 `BatteryGroup` 时才枚举电池设备、重开设备句柄）；
- 限频 60 秒、最多 5 次，恢复成功后重新计数；
- 打开「插件设置」或「硬件信息」窗口时也会请求一次，不用重启主程序。

**其它**

- 每个监控项独立 try/catch，单项异常不再拖死其它项；
- 名称 `item_name`、类型 `sensor_type` 持久化到 ini，旧配置启动时自动回填并写回；
- 设置窗口列表与配置项按下标对齐，删除后重建列表，删除/上移/下移不会再作用到别的项目；
- 关闭设置窗口时只给真正改动过的硬件开关赋值，避免无谓地重建硬件组；
- **「剩余使用时间」等单位新增「分钟 / 小时」**（`SensorType::TimeSpan`：秒 / 分钟 / 小时，默认秒）。

## 安装

1. 解压得到 `HardwareMonitor.dll` 与 `zh-CN\HardwareMonitor.resources.dll`；
2. 放进 TrafficMonitor 程序目录下的 `plugins` 目录（没有就新建）；
3. 重启 TrafficMonitor，在「选项 → 常规设置 → 插件管理」里可以看到 **1.0.3**；
4. 要让数值显示在任务栏：任务栏窗口右键 →「显示设置」里勾选对应的项目。

## 版本号

| 位置 | 值 |
|---|---|
| 文件属性 / `app.rc` | `1.0.3.0` |
| 「插件管理」界面（`GetInfo(TMI_VERSION)`） | `1.0.3` |
| `plugins_version.xml`（主程序检查更新用） | `1.03` |
