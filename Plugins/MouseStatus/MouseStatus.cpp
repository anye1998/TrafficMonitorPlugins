// MouseStatus —— 插件主体实现
#include "MouseStatus.h"

#include <cstdio>
#include <cwchar>

#include "MouseMonitor.h"
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
        text = L"未检测到已连接的鼠标";
        if (state.battery >= 0)
        {
            text += L"\n上次读数：";
            text += ToWString(state.battery);
            text += L"%";
            if (!state.last_seen.empty())
            {
                text += L"（";
                text += state.last_seen;
                text += L"）";
            }
        }
        m_tooltip = text;
        return;
    }

    if (!state.device_name.empty())
    {
        text += state.device_name;
        text += L"\n";
    }
    text += L"连接方式：";
    text += LinkName(state.link);
    text += L"\n电量：";
    text += (state.battery >= 0) ? (ToWString(state.battery) + L"%") : std::wstring(L"未知");
    if (state.battery_is_stale)
        text += L"（上次读数）";
    text += L"\n状态：";
    text += ChargeName(state.charge);
    text += L"\n数据来源：";
    text += SourceName(state.source);
    if (!state.last_seen.empty())
    {
        text += L"\n更新于 ";
        text += state.last_seen;
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
        return L"1.0.0";
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

extern "C" __declspec(dllexport) ITMPlugin* TMPluginGetInstance()
{
    return &CMouseStatus::Instance();
}
