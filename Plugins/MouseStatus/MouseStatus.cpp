// MouseStatus —— 插件主体实现
#include "MouseStatus.h"

#include <cstdio>
#include <cwchar>

#include "MouseMonitor.h"
#include "Diag.h"
#include "OptionsDlg.h"
#include "resource.h"

namespace {

// 用于取得本 DLL 的模块句柄（取自本模块内的静态对象地址）
char g_module_anchor = 0;

const wchar_t* LinkName(LinkKind link)
{
    switch (link)
    {
    case LinkKind::Bluetooth: return L"蓝牙";
    case LinkKind::BluetoothLE: return L"蓝牙（低功耗）";
    case LinkKind::Wireless24G: return L"2.4G 无线";
    case LinkKind::UsbWired: return L"USB 有线";
    default: break;
    }
    return L"未连接";
}

const wchar_t* ChargeName(ChargeKind charge)
{
    switch (charge)
    {
    case ChargeKind::Charging: return L"充电中";
    case ChargeKind::Full: return L"已充满";
    case ChargeKind::Discharging: return L"放电中";
    default: break;
    }
    return L"未知";
}

const wchar_t* SourceName(DataSource source)
{
    switch (source)
    {
    case DataSource::Rapoo: return L"雷柏私有协议";
    case DataSource::HidBattery: return L"HID 电池服务";
    case DataSource::BluetoothProp: return L"蓝牙设备属性";
    default: break;
    }
    return L"无";
}

std::wstring ToWString(int value)
{
    wchar_t buf[16] = { 0 };
    ::swprintf_s(buf, L"%d", value);
    return std::wstring(buf);
}

} // namespace

CMouseStatus CMouseStatus::m_instance;

CMouseStatus& CMouseStatus::Instance()
{
    return m_instance;
}

CMouseStatus::CMouseStatus()
{
    m_items[0].SetKind(CMouseStatusItem::Kind::Battery);
    m_items[1].SetKind(CMouseStatusItem::Kind::Link);
    m_items[2].SetKind(CMouseStatusItem::Kind::Charge);
    m_items[3].SetKind(CMouseStatusItem::Kind::Badge);
}

CMouseStatus::~CMouseStatus()
{
    CMouseMonitor::Instance().Stop();
    if (m_icon != nullptr)
    {
        ::DestroyIcon(m_icon);
        m_icon = nullptr;
    }
}

void CMouseStatus::EnsureStarted()
{
    if (m_started)
        return;
    m_started = true;
    CMouseMonitor::Instance().Start();
}

IPluginItem* CMouseStatus::GetItem(int index)
{
    if (index >= 0 && index < (int)(sizeof(m_items) / sizeof(m_items[0])))
        return &m_items[index];
    return nullptr;
}

const wchar_t* CMouseStatus::GetTooltipInfo()
{
    return m_tooltip.c_str();
}

void CMouseStatus::DataRequired()
{
    EnsureStarted();

    MouseSnapshot snapshot;
    CMouseMonitor::Instance().GetSnapshot(snapshot);

    for (auto& item : m_items)
        item.Update(snapshot);

    BuildTooltip(snapshot);
}

void CMouseStatus::BuildTooltip(const MouseSnapshot& state)
{
    std::wstring text;

    if (!state.live)
    {
        text = L"未检测到在线鼠标";
        if (state.battery >= 0)
        {
            text += L"\n上次读数：";
            text += ToWString(state.battery);
            text += L"%";
            // 离线后 last_check 就停在最后一次成功检查的时刻，用它更贴近
            // 「这个读数是什么时候看到的」；没有 last_check 时退回 last_seen。
            const std::wstring& stamp = state.last_check.empty() ? state.last_seen : state.last_check;
            if (!stamp.empty())
            {
                text += L"（";
                text += stamp;
                text += L"）";
            }
        }
        text += L"\n若鼠标确实已连接，请查看日志：\n";
        text += diag::LogFilePath();
        m_tooltip = text;
        return;
    }

    text += state.device_name.empty() ? L"鼠标" : state.device_name;
    text += L"\n连接方式：";
    text += LinkName(state.link);

    text += L"\n电量：";
    if (state.battery < 0)
    {
        text += L"设备未上报电量";
    }
    else
    {
        text += ToWString(state.battery);
        text += L"%";
        if (state.battery_is_stale)
            text += L"（上次读数）";
    }

    text += L"\n状态：";
    if (state.battery < 0)
        text += L"无法判定（缺少电量数据）";
    else
        text += ChargeName(state.charge);

    text += L"\n数据来源：";
    text += SourceName(state.source);
    if (state.source == DataSource::BluetoothProp)
    {
        // 这条路径读的是 Windows 缓存的设备属性，什么时候刷新由系统决定，
        // 不是插件能控制的，写清楚免得用户以为读数卡住了。
        text += L"（系统缓存值）";
    }

    // 两个时间分开显示：「读数更新于」是数值真正变化的时刻（数据新鲜度），
    // 「最近检查」是插件最后一次轮询的时刻（只说明插件还在盯着）。
    // 之前只显示一个每秒跳动的「更新于」，容易被误认为每秒都在重新读电量。
    if (!state.last_seen.empty())
    {
        text += L"\n读数更新于 ";
        text += state.last_seen;
    }
    if (!state.last_check.empty() && state.last_check != state.last_seen)
    {
        text += L"\n最近检查 ";
        text += state.last_check;
        text += L"（数值未变）";
    }

    if (state.battery < 0)
    {
        text += L"\n提示：没有任何数据来源能给出电量。"
                L"右键本项 →「插件设置」或右键菜单里的「启用诊断日志 / 清空诊断日志」"
                L"可管理诊断日志，日志里记录了完整的枚举结果：\n";
        text += diag::LogFilePath();
    }

    m_tooltip = text;
}

const wchar_t* CMouseStatus::GetInfo(PluginInfoIndex index)
{
    switch (index)
    {
    case TMI_NAME:
        return L"鼠标状态";
    case TMI_DESCRIPTION:
        return L"在状态栏显示鼠标电量、连接方式（蓝牙 / 2.4G / USB）与充电状态";
    case TMI_AUTHOR:
        return L"anye1998";
    case TMI_COPYRIGHT:
        return L"MIT License";
    case TMI_VERSION:
        return L"1.0.10";
    case TMI_URL:
        return L"https://github.com/anye1998/TrafficMonitorPlugins";
    default:
        break;
    }
    return L"";
}

void CMouseStatus::OnInitialize(ITrafficMonitor* pApp)
{
    m_app = pApp;
    // 监视线程等 EI_CONFIG_DIR 到了再启动：那时才知道 ini 放在哪，
    // 免得刚启动时用默认值写几条日志才发现用户其实关掉了日志。
}

void CMouseStatus::OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data)
{
    // 主程序把 <配置目录>\plugins\ 传进来，配置就存在那里的 MouseStatus.ini，
    // 与 Battery / DateTime / KeyboardIndicator 等插件的做法一致。
    if (index == ITMPlugin::EI_CONFIG_DIR && data != nullptr && data[0] != L'\0')
        diag::SetConfigDir(data);

    EnsureStarted();
}

void* CMouseStatus::GetPluginIcon()
{
    if (m_icon == nullptr)
    {
        HMODULE module = nullptr;
        if (::GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                    | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                (LPCWSTR)&g_module_anchor, &module))
        {
            m_icon = (HICON)::LoadImageW(module, MAKEINTRESOURCEW(IDI_ICON1), IMAGE_ICON,
                ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON),
                LR_DEFAULTCOLOR);
        }
    }
    return m_icon;
}

/////////////////////////////////////////////////////////////////////////////////////////////
// 设置：右键菜单命令 + 选项对话框

namespace {

constexpr int kCmdToggleLog = 0;
constexpr int kCmdClearLog = 1;
constexpr int kCmdOpenFolder = 2;
constexpr int kCmdDumpNow = 3;

// TrafficMonitor 的插件命令 ID 段（主程序 resource.h 里的
// ID_PLUGIN_COMMAND_START / ID_PLUGIN_COMMAND_MAX）。主窗口菜单在查勾选状态时
// 传的是命令 ID 本身，任务栏菜单传的是序号，这里把两种都认下来。
constexpr int kCommandIdStart = 33408;
constexpr int kCommandIdMax = 33664;

} // namespace

int CMouseStatus::GetCommandCount()
{
    return 4;
}

const wchar_t* CMouseStatus::GetCommandName(int command_index)
{
    switch (command_index)
    {
    case kCmdToggleLog: return L"启用诊断日志";
    case kCmdClearLog: return L"清空诊断日志";
    case kCmdOpenFolder: return L"打开日志文件夹";
    case kCmdDumpNow: return L"立即写入诊断信息";
    default: break;
    }
    return nullptr;
}

int CMouseStatus::IsCommandChecked(int command_index)
{
    if (command_index >= kCommandIdStart && command_index <= kCommandIdMax)
        command_index -= kCommandIdStart;

    if (command_index == kCmdToggleLog)
        return diag::Enabled() ? 1 : 0;
    return 0;
}

void CMouseStatus::OnPluginCommand(int command_index, void* hWnd, void* para)
{
    (void)hWnd;
    (void)para;

    switch (command_index)
    {
    case kCmdToggleLog:
        diag::SetEnabled(!diag::Enabled());
        break;
    case kCmdClearLog:
        diag::DeleteLogFiles();
        break;
    case kCmdOpenFolder:
        diag::OpenLogFolder();
        break;
    case kCmdDumpNow:
        // 日志关着的话先打开，否则写出去什么也看不到
        if (!diag::Enabled())
            diag::SetEnabled(true);
        CMouseMonitor::Instance().DumpDiagnosticsOnce();
        diag::OpenLogFolder();
        break;
    default:
        break;
    }
}

ITMPlugin::OptionReturn CMouseStatus::ShowOptionsDialog(void* hParent)
{
    return ShowMouseStatusOptions(hParent) ? OR_OPTION_CHANGED : OR_OPTION_UNCHANGED;
}

extern "C" __declspec(dllexport) ITMPlugin* TMPluginGetInstance()
{
    return &CMouseStatus::Instance();
}
