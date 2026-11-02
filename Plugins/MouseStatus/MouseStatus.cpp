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
    case LinkKind::UsbUnknown: return L"USB 接口";
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

// 提示文本的长度预算。
//
// 主程序每秒都会把「主窗口 / 任务栏窗口 + 所有已加载插件」的提示拼成一个字符串，
// 然后交给 MFC 的 CToolTipCtrl::UpdateTipText；而 MFC 在该函数里有
//     ENSURE_ARG(AtlStrLen(lpszText) <= MAX_TIP_TEXT_LENGTH)   // MAX_TIP_TEXT_LENGTH = 1024
// 一旦总长超过 1024 就抛 CInvalidArgException，表现为弹出一个标题为 TrafficMonitor、
// 正文是「遇到不适当的参数」的错误框（上游 issue #2413 / #2411 / #1219）。
//
// 插件拿不到「别人已经用掉多少字符」，唯一能做的是保证自己足够短：
//   - 默认只输出两三行精简信息（约 30~50 字符）；
//   - 想看详细数据的用户可在 ini 里置 tooltip_detail=1；
//   - 无论哪种模式，最后都做一次硬截断兜底。
constexpr size_t kMaxTooltipChars = 200;

bool TooltipDetailEnabled()
{
    // 本函数每轮 DataRequired 都会走一次，而读 ini 是要开文件的；缓存一次即可。
    // 想在运行中切换就改 ini 后重启 TrafficMonitor（这个值很少变）。
    static const bool enabled = diag::GetConfigInt(L"tooltip_detail", 0, 0, 1) != 0;
    return enabled;
}

void AppendLine(std::wstring& text, const wchar_t* line)
{
    if (line == nullptr || line[0] == L'\0')
        return;
    if (!text.empty())
        text += L'\n';
    text += line;
}

void ClampTooltip(std::wstring& text)
{
    if (text.size() <= kMaxTooltipChars)
        return;
    text.resize(kMaxTooltipChars - 1);
    text += L'…';
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
    const bool detail = TooltipDetailEnabled();
    std::wstring text;

    if (!state.live)
    {
        // 首次启动就落在这里（还没有任何数据）。老版本会顺手把日志文件的完整路径
        // 拼进提示里，仅这一项就有 60 多个字符，是「插件第一次启动就弹窗」的直接推手。
        text = L"未检测到在线鼠标";
        if (state.battery >= 0)
        {
            text += L"\n上次读数 ";
            text += ToWString(state.battery);
            text += L"%";
            // 用「数值最后一次变化的时刻」：它是稳定的，不会每秒抖动导致提示反复重排。
            if (!state.last_seen.empty())
            {
                text += L"（";
                text += state.last_seen;
                text += L"）";
            }
        }
        if (detail)
        {
            text += L"\n日志：";
            text += diag::LogFilePath();
        }
        ClampTooltip(text);
        m_tooltip = text;
        return;
    }

    text = state.device_name.empty() ? L"鼠标" : state.device_name;
    text += L" · ";
    text += LinkName(state.link);

    // 电量和充电状态合成一行：既能一眼看完，又比老版本省下 20 多个字符
    if (state.link == LinkKind::UsbWired && state.battery < 0)
    {
        // 有线鼠标本来就没有电池，写「电量 未知」会让人以为读数坏了
        text += L"\nUSB 直连供电，无需电量";
    }
    else
    {
        text += L"\n电量 ";
        if (state.battery < 0)
        {
            text += L"未知";
            if (detail)
                text += L"（没有数据源能给出电量）";
        }
        else
        {
            text += ToWString(state.battery);
            text += L"%（";
            text += ChargeName(state.charge);
            text += L"）";
            if (state.battery_is_stale)
                text += L" 上次读数";
        }
    }

    if (detail)
    {
        std::wstring source = L"数据来源：";
        source += SourceName(state.source);
        if (state.source == DataSource::BluetoothProp)
        {
            // 这条路径读的是 Windows 缓存的设备属性，什么时候刷新由系统决定，
            // 不是插件能控制的，写清楚免得用户以为读数卡住了。
            source += L"（系统缓存值）";
        }
        AppendLine(text, source.c_str());

        if (state.link == LinkKind::UsbUnknown)
        {
            // 判不出来就说明白为什么，并给出出路：右键菜单里可以人工指定
            AppendLine(text, L"连接方式无法自动判断（设备不报电量，名称里也没有无线字样），"
                             L"可在右键菜单「连接方式」里指定");
        }

        // 两个时间分开显示：「读数更新于」是数值真正变化的时刻（数据新鲜度），
        // 「最近检查」是插件最后一次轮询的时刻（只说明插件还在盯着）。
        if (!state.last_seen.empty())
        {
            std::wstring seen = L"读数更新于 ";
            seen += state.last_seen;
            AppendLine(text, seen.c_str());
        }
        if (!state.last_check.empty() && state.last_check != state.last_seen)
        {
            std::wstring check = L"最近检查 ";
            check += state.last_check;
            check += L"（数值未变）";
            AppendLine(text, check.c_str());
        }

        if (state.battery < 0)
        {
            std::wstring hint = L"日志：";
            hint += diag::LogFilePath();
            AppendLine(text, hint.c_str());
        }
    }

    ClampTooltip(text);
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
        return L"1.2.3";
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

constexpr int kCmdToggleProbe = 0;
constexpr int kCmdLinkMode = 1;
constexpr int kCmdToggleLog = 2;
constexpr int kCmdClearLog = 3;
constexpr int kCmdOpenFolder = 4;
constexpr int kCmdDumpNow = 5;

// TrafficMonitor 的插件命令 ID 段（主程序 resource.h 里的
// ID_PLUGIN_COMMAND_START / ID_PLUGIN_COMMAND_MAX）。主窗口菜单在查勾选状态时
// 传的是命令 ID 本身，任务栏菜单传的是序号，这里把两种都认下来。
constexpr int kCommandIdStart = 33408;
constexpr int kCommandIdMax = 33664;

} // namespace

int CMouseStatus::GetCommandCount()
{
    return 6;
}

const wchar_t* CMouseStatus::GetCommandName(int command_index)
{
    switch (command_index)
    {
    case kCmdToggleProbe: return L"主动探测鼠标在线状态";
    case kCmdLinkMode:
    {
        // 菜单文字要反映当前设置，否则用户点完不知道到自己切到哪一档了
        static wchar_t text[64] = { 0 };
        const int mode = CMouseMonitor::LinkOverride();
        const wchar_t* name = (mode == CMouseMonitor::kLinkOverrideWired) ? L"USB 有线"
            : (mode == CMouseMonitor::kLinkOverride24G) ? L"2.4G 无线" : L"自动";
        ::swprintf_s(text, L"连接方式：%s（点击切换）", name);
        return text;
    }
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

    if (command_index == kCmdToggleProbe)
        return CMouseMonitor::ProbeEnabled() ? 1 : 0;
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
    case kCmdToggleProbe:
        CMouseMonitor::SetProbeEnabled(!CMouseMonitor::ProbeEnabled());
        break;
    case kCmdLinkMode:
        // 三档循环：自动 → USB 有线 → 2.4G 无线 → 自动
        CMouseMonitor::SetLinkOverride((CMouseMonitor::LinkOverride() + 1)
            % (CMouseMonitor::kLinkOverride24G + 1));
        break;
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
        // 日志关着的话先打开，否则写出去什么也看不到。
        // 真正的枚举交给工作线程执行（RequestDiagnosticsDump），这里只置请求并唤醒它，
        // 避免在 UI 线程和采集线程并发访问同一份报文记录状态。
        if (!diag::Enabled())
            diag::SetEnabled(true);
        CMouseMonitor::Instance().RequestDiagnosticsDump();
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
