// 这是主 DLL 文件。

#include "stdafx.h"

#include "HardwareMonitor.h"
#include <vector>
#include "HardwareMonitorHelper.h"
#include "../utilities/IniHelper.h"
#include "SettingsForm.h"
#include "Common.h"

namespace HardwareMonitor
{
    CHardwareMonitor* CHardwareMonitor::m_pIns = nullptr;

    //把identifier按'/'切分成若干段
    static std::vector<std::wstring> SplitIdentifyer(const std::wstring& identifier)
    {
        std::vector<std::wstring> parts;
        std::wstring::size_type start = 0;
        while (start <= identifier.size())
        {
            std::wstring::size_type pos = identifier.find(L'/', start);
            if (pos == std::wstring::npos)
            {
                parts.push_back(identifier.substr(start));
                break;
            }
            parts.push_back(identifier.substr(start, pos - start));
            start = pos + 1;
        }
        return parts;
    }

    //传感器找不到时，用identifier拼一个可读的监控项名称（形如“电量/电池电量”），
    //避免监控项因为名称是空的而在设置窗口中“消失”
    static std::wstring GetItemNameFromIdentifyer(const std::wstring& identifier)
    {
        std::vector<std::wstring> parts = SplitIdentifyer(identifier);
        std::wstring name;
        if (parts.size() >= 2)
        {
            name = parts[parts.size() - 2];
            name += L"/";
        }
        if (!parts.empty())
            name += parts.back();
        return Common::StringToStdWstring(Common::GetTranslatedString(gcnew String(name.c_str())));
    }

    //传感器找不到时，用identifier的最后一段作为监控项的标签文本（也就是传感器名称）
    static std::wstring GetLabelTextFromIdentifyer(const std::wstring& identifier)
    {
        std::vector<std::wstring> parts = SplitIdentifyer(identifier);
        if (parts.empty())
            return std::wstring();
        return Common::StringToStdWstring(Common::GetTranslatedString(gcnew String(parts.back().c_str())));
    }

    CHardwareMonitor::CHardwareMonitor()
    {
        //获取当前屏幕DPI值
        Graphics^ graphics = Graphics::FromHwnd(IntPtr::Zero);
        m_dpi = static_cast<int>(graphics->DpiX);
    }

    CHardwareMonitor::~CHardwareMonitor()
    {
    }

    CHardwareMonitor* CHardwareMonitor::GetInstance()
    {
        if (m_pIns == nullptr)
        {
            try
            {
                m_pIns = new CHardwareMonitor();
                MonitorGlobal::Instance()->Init();
            }
            catch (System::Exception^ e)
            {
                ShowErrorMessage(e);
            }
        }
        return m_pIns;
    }

    const std::wstring& CHardwareMonitor::StringRes(const wchar_t* name)
    {
        auto iter = string_table.find(name);
        if (iter != string_table.end())
        {
            return iter->second;
        }
        else
        {
            std::wstring& str = string_table[name];
            str = MonitorGlobal::Instance()->GetStdWString(gcnew String(name));
            return str;
        }
    }

    bool CHardwareMonitor::AddDisplayItem(ISensor^ sensor)
    {
        if (sensor != nullptr)
        {
            std::wstring identifyer = Common::StringToStdWstring(HardwareMonitorHelper::GetSensorIdentifyer(sensor));
            //检查监控项目是否存在
            if (!IsDisplayItemExist(identifyer))
            {
                ItemInfo item_info;
                item_info.identifyer = identifyer;
                item_info.item_name = Common::StringToStdWstring(HardwareMonitorHelper::GetSensorDisplayName(sensor));
                item_info.sensor_type = static_cast<int>(sensor->SensorType);
                item_info.unit = Common::StringToStdWstring(HardwareMonitorHelper::GetSensorTypeDefaultUnit(sensor->SensorType));
                m_settings.items_info.push_back(item_info);
                m_item_names[identifyer] = item_info.item_name;
                //注意：这里不往m_items里添加对象。TrafficMonitor只在启动时枚举一次插件项目，
                //运行过程中新增的监控项要重启主程序后才会出现在“显示设置”里；
                //而m_items里的对象地址一旦变化，主程序缓存的项目指针就会失效。
                return true;
            }
        }
        return false;
    }

    bool CHardwareMonitor::IsDisplayItemExist(const std::wstring& identifyer)
    {
        if (!identifyer.empty())
        {
            for (const auto& item : m_settings.items_info)
            {
                if (item.identifyer == identifyer)
                    return true;
            }
        }
        return false;
    }

    bool CHardwareMonitor::RemoveDisplayItem(int index)
    {
        if (index >= 0 && index < static_cast<int>(m_settings.items_info.size()))
        {
            auto iter = m_settings.items_info.begin() + index;
            m_settings.items_info.erase(iter);
            return true;
        }
        return false;
    }

    std::wstring CHardwareMonitor::GetItemName(const std::wstring& identifier)
    {
        //优先使用配置文件里保存的名称
        const ItemInfo& item_info = m_settings.FindItemInfo(identifier);
        if (!item_info.item_name.empty())
            return item_info.item_name;

        auto iter = m_item_names.find(identifier);
        if (iter != m_item_names.end() && !iter->second.empty())
            return iter->second;

        //最后按identifier拼一个名称
        return GetItemNameFromIdentifyer(identifier);
    }

    const std::wstring& CHardwareMonitor::GetConfigPath() const
    {
        return m_config_path;
    }

    void CHardwareMonitor::RebuildItems()
    {
        m_items.clear();
        m_item_names.clear();
        for (auto& item_info : m_settings.items_info)
        {
            ISensor^ sensor = HardwareMonitorHelper::FindSensorByIdentifyer(gcnew String(item_info.identifyer.c_str()));
            if (sensor != nullptr)
            {
                //从传感器回填名称和类型（旧版本的配置文件里没有这两项）
                item_info.item_name = Common::StringToStdWstring(HardwareMonitorHelper::GetSensorDisplayName(sensor));
                item_info.sensor_type = static_cast<int>(sensor->SensorType);
            }
            std::wstring item_name{ GetItemName(item_info.identifyer) };
            std::wstring label_text;
            if (sensor != nullptr)
                label_text = Common::StringToStdWstring(Common::GetTranslatedString(sensor->Name));
            else
                label_text = GetLabelTextFromIdentifyer(item_info.identifyer);

            m_item_names[item_info.identifyer] = item_name;
            m_items.push_back(std::make_unique<CHardwareMonitorItem>(item_info.identifyer, item_name, label_text, item_info.sensor_type));
        }
    }

    void CHardwareMonitor::LoadConfig(const std::wstring& config_dir)
    {
        m_config_path = config_dir + L"HardwareMonitor.ini";
        utilities::CIniHelper ini(m_config_path);
        //配置文件为空说明是首次使用本插件（必须在读取任何内容之前判断）
        bool ini_is_empty = ini.IsEmpty();

        //载入要监控的硬件
        Computer^ computer = MonitorGlobal::Instance()->computer;
        computer->IsCpuEnabled = ini.GetBool(L"hardware", L"IsCpuEnabled", true);                   //CPU
        computer->IsGpuEnabled = ini.GetBool(L"hardware", L"IsGpuEnabled", true);                   //显卡
        computer->IsMotherboardEnabled = ini.GetBool(L"hardware", L"IsMotherboardEnabled", true);   //主板
        computer->IsStorageEnabled = ini.GetBool(L"hardware", L"IsStorageEnabled", true);           //硬盘
        computer->IsBatteryEnabled = ini.GetBool(L"hardware", L"IsBatteryEnabled", false);          //电池
        computer->IsNetworkEnabled = ini.GetBool(L"hardware", L"IsNetworkEnabled", false);          //网络
        computer->IsMemoryEnabled = ini.GetBool(L"hardware", L"IsMemoryEnabled", false);            //内存
        computer->IsControllerEnabled = ini.GetBool(L"hardware", L"IsControllerEnabled", false);    //风扇控制器
        computer->IsPsuEnabled = ini.GetBool(L"hardware", L"IsPsuEnabled", false);                  //电源

        MonitorGlobal::Instance()->computer->Accept(MonitorGlobal::Instance()->updateVisitor);
        //硬件树已经建好，先建立一次传感器索引，供下面回填名称使用
        HardwareMonitorHelper::RebuildSensorIndex();

        //清空上一次载入的内容（LoadConfig可以被调用多次）
        m_settings.items_info.clear();
        m_item_names.clear();
        m_items.clear();

        //载入要监控的项目
        int item_count = ini.GetInt(L"config", L"item_count");
        for (int i = 0; i < item_count; i++)
        {
            std::wstring app_name = L"item" + std::to_wstring(i);
            ItemInfo item_info;
            item_info.identifyer = ini.GetString(app_name.c_str(), L"identifier");
            if (item_info.identifyer.empty())
                continue;       //忽略没有identifier的无效项目
            item_info.item_name = ini.GetString(app_name.c_str(), L"item_name");    //旧版本的配置文件里没有这两项
            item_info.sensor_type = ini.GetInt(app_name.c_str(), L"sensor_type", -1);
            item_info.decimal_places = ini.GetInt(app_name.c_str(), L"decimal_places", 1);
            item_info.specify_value_width = ini.GetBool(app_name.c_str(), L"specify_value_width", true);
            item_info.value_width = ini.GetInt(app_name.c_str(), L"value_width", 3);
            item_info.unit = ini.GetString(app_name.c_str(), L"unit");
            item_info.show_unit = ini.GetBool(app_name.c_str(), L"show_unit", true);
            item_info.show_notify = ini.GetBool(app_name.c_str(), L"show_notify", false);
            item_info.notify_value = ini.GetDouble(app_name.c_str(), L"notify_value");
            m_settings.items_info.push_back(item_info);
        }

        //如果配置文件为空，则认为是首次使用硬件监控插件，添加默认的监控项
        if (ini_is_empty)
        {
            List<ISensor^>^ default_sensors = gcnew List<ISensor^>();
            HardwareMonitorHelper::GetDefaultMonitorItem(default_sensors);
            for each (auto sensor in default_sensors)
            {
                ItemInfo item_info;
                item_info.identifyer = Common::StringToStdWstring(HardwareMonitorHelper::GetSensorIdentifyer(sensor));
                item_info.item_name = Common::StringToStdWstring(HardwareMonitorHelper::GetSensorDisplayName(sensor));
                item_info.sensor_type = static_cast<int>(sensor->SensorType);
                item_info.unit = Common::StringToStdWstring(HardwareMonitorHelper::GetSensorTypeDefaultUnit(sensor->SensorType));
                m_settings.items_info.push_back(item_info);
            }
        }

        //为每一个配置项建立监控项对象。
        //即使它对应的传感器现在找不到（例如开机时电池设备还没枚举出来），也要建立：
        //只有GetItem(index)返回的项目数量和顺序与items_info一致，TrafficMonitor按索引保存的
        //显示项配置才不会错位，用户也不用再去“重新添加”一次。
        RebuildItems();

        m_settings.hardware_info_auto_refresh = ini.GetBool(L"config", L"hardware_info_auto_refresh");
        m_settings.show_mouse_tooltip = ini.GetBool(L"config", L"show_mouse_tooltip", true);

        //载入树控件的展开折叠状态
        std::vector<std::wstring> collapse_nodes;
        ini.GetStringList(L"other", L"hardware_info_collapsed_nodes", collapse_nodes, std::vector<std::wstring>());
        for (const auto& node_path : collapse_nodes)
            MonitorGlobal::Instance()->treeCollapseNodes->Add(gcnew String(node_path.c_str()));
    }

    void CHardwareMonitor::SaveConfig()
    {
        if (m_config_path.empty())
            return;
        utilities::CIniHelper ini(m_config_path);

        //保存要监控的硬件
        Computer^ computer = MonitorGlobal::Instance()->computer;
        ini.WriteBool(L"hardware", L"IsCpuEnabled", computer->IsCpuEnabled);
        ini.WriteBool(L"hardware", L"IsGpuEnabled", computer->IsGpuEnabled);
        ini.WriteBool(L"hardware", L"IsMotherboardEnabled", computer->IsMotherboardEnabled);
        ini.WriteBool(L"hardware", L"IsStorageEnabled", computer->IsStorageEnabled);
        ini.WriteBool(L"hardware", L"IsBatteryEnabled", computer->IsBatteryEnabled);
        ini.WriteBool(L"hardware", L"IsNetworkEnabled", computer->IsNetworkEnabled);
        ini.WriteBool(L"hardware", L"IsMemoryEnabled", computer->IsMemoryEnabled);
        ini.WriteBool(L"hardware", L"IsControllerEnabled", computer->IsControllerEnabled);
        ini.WriteBool(L"hardware", L"IsPsuEnabled", computer->IsPsuEnabled);

        //保存监控的项目
        ini.WriteInt(L"config", L"item_count", static_cast<int>(m_settings.items_info.size()));
        int index = 0;
        for (const auto& item : m_settings.items_info)
        {
            std::wstring app_name = L"item" + std::to_wstring(index);
            ini.WriteString(app_name.c_str(), L"identifier", item.identifyer.c_str());
            ini.WriteString(app_name.c_str(), L"item_name", item.item_name);
            ini.WriteInt(app_name.c_str(), L"sensor_type", item.sensor_type);
            ini.WriteInt(app_name.c_str(), L"decimal_places", item.decimal_places);
            ini.WriteBool(app_name.c_str(), L"specify_value_width", item.specify_value_width);
            ini.WriteInt(app_name.c_str(), L"value_width", item.value_width);
            ini.WriteString(app_name.c_str(), L"unit", item.unit);
            ini.WriteBool(app_name.c_str(), L"show_unit", item.show_unit);
            ini.WriteBool(app_name.c_str(), L"show_notify", item.show_notify);
            ini.WriteDouble(app_name.c_str(), L"notify_value", item.notify_value);
            index++;
        }
        ini.WriteBool(L"config", L"hardware_info_auto_refresh", m_settings.hardware_info_auto_refresh);
        ini.WriteBool(L"config", L"show_mouse_tooltip", m_settings.show_mouse_tooltip);

        //保存树控件的展开折叠状态
        std::vector<std::wstring> collapse_nodes;
        for each (auto str in MonitorGlobal::Instance()->treeCollapseNodes)
            collapse_nodes.push_back(Common::StringToStdWstring(str));
        ini.WriteStringList(L"other", L"hardware_info_collapsed_nodes", collapse_nodes);

        ini.Save();
        m_config_backfilled = false;
    }

    void CHardwareMonitor::NotifyConfigBackfilled()
    {
        m_config_backfilled = true;
    }

    void CHardwareMonitor::RequestBatteryGroupRefresh()
    {
        m_battery_refresh_request = 1;
    }

    void CHardwareMonitor::RefreshBatteryGroup()
    {
        try
        {
            Computer^ computer = MonitorGlobal::Instance()->computer;
            if (!computer->IsBatteryEnabled)
                return;
            //LibreHardwareMonitor只在“添加BatteryGroup”的时候才去枚举电池设备并打开设备句柄。
            //把电池监控关掉再打开，等于让它重新枚举一次电池设备、重新打开句柄：
            //开机时电池设备还没枚举出来、休眠/唤醒或电池驱动重载后句柄失效，都靠这一步恢复。
            computer->IsBatteryEnabled = false;
            computer->IsBatteryEnabled = true;
            computer->Accept(MonitorGlobal::Instance()->updateVisitor);
        }
        catch (System::Exception^)
        {
        }
        HardwareMonitorHelper::RebuildSensorIndex();
    }

    void CHardwareMonitor::TryRecoverBatteryGroup()
    {
        try
        {
            Computer^ computer = MonitorGlobal::Instance()->computer;
            if (!computer->IsBatteryEnabled)
            {
                m_battery_missing_count = 0;
                return;
            }

            //统计电池类监控项中有多少项现在解析不到传感器
            int battery_item_count = 0;
            int missing_count = 0;
            for (const auto& item : m_items)
            {
                String^ identifyer = gcnew String(item->GetIdentifier().c_str());
                if (!HardwareMonitorHelper::IsBatteryIdentifyer(identifyer))
                    continue;
                battery_item_count++;
                if (HardwareMonitorHelper::FindSensorByIdentifyer(identifyer) == nullptr)
                    missing_count++;
            }
            if (battery_item_count == 0)
            {
                m_battery_missing_count = 0;
                return;
            }
            if (missing_count == 0)
            {
                m_battery_missing_count = 0;
                return;
            }

            m_battery_missing_count++;
            //连续3轮（约3秒）都读不到电池数据才处理，避免刚开机时还没来得及更新就反复重枚举
            if (m_battery_missing_count < 3)
                return;

            //最多每60秒重新枚举一次，避免电池真的坏了的时候一直重试
            unsigned now = static_cast<unsigned>(Environment::TickCount);
            if (m_last_battery_recover != 0 && static_cast<unsigned>(now - m_last_battery_recover) < 60000)
                return;

            //有窗口打开时不动硬件树：此时UI线程正在读取硬件信息，重新枚举可能让树在遍历中被修改
            if (MonitorGlobal::Instance()->monitor_form != nullptr || MonitorGlobal::Instance()->setttings_form != nullptr)
                return;

            m_last_battery_recover = now;
            RefreshBatteryGroup();
        }
        catch (System::Exception^)
        {
        }
    }

    int CHardwareMonitor::DPI(int pixel) const
    {
        return pixel * m_dpi / 96;
    }

    ITrafficMonitor* CHardwareMonitor::GetMainApp() const
    {
        return m_pMainApp;
    }

    void CHardwareMonitor::ShowErrorMessage(System::Exception^ e)
    {
        String^ str = e->Message;
        str += "\r\n";
        str += e->StackTrace;
        MessageBox::Show(str, "TrafficMonitor HardwareMonitor plugin", MessageBoxButtons::OK, MessageBoxIcon::Error);
    }

    IPluginItem* CHardwareMonitor::GetItem(int index)
    {
        if (index >= 0 && index < static_cast<int>(m_items.size()))
        {
            return m_items[index].get();
        }
        return nullptr;
    }

    void CHardwareMonitor::DataRequired()
    {
        try
        {
            Computer^ computer = MonitorGlobal::Instance()->computer;

            //设置窗口/硬件信息窗口请求重新枚举电池设备（放到这个线程里做，不在UI线程里改硬件树）
            if (m_battery_refresh_request != 0)
            {
                m_battery_refresh_request = 0;
                RefreshBatteryGroup();
            }

            //重新获取数据
            computer->Accept(MonitorGlobal::Instance()->updateVisitor);

            //硬件更新完成后重建一次传感器索引。
            //索引里只会包含当前处于激活状态的传感器，因此已经失效的传感器（休眠后句柄失效、
            //设备被重新枚举等）不会被继续当作有效对象使用——监控项会显示“--”，
            //而不是把上一次的数值一直“冻”在界面上。
            HardwareMonitorHelper::RebuildSensorIndex();

            //更新“硬件信息”树节点
            if (MonitorGlobal::Instance()->monitor_form != nullptr && m_settings.hardware_info_auto_refresh)
                MonitorGlobal::Instance()->monitor_form->UpdateData();
            //更新“选项设置”中的节点
            if (MonitorGlobal::Instance()->setttings_form != nullptr)
                MonitorGlobal::Instance()->setttings_form->UpdateItemsValue();
            //更新所有显示项目
            for (auto& item : m_items)
                item->UpdateValue();

            //电池类监控项读不到数据时，尝试重新枚举电池设备
            TryRecoverBatteryGroup();

            //监控项名称/类型是后来才补上的，写回配置文件（只写一次）
            if (m_config_backfilled)
                SaveConfig();
        }
        catch (System::Exception^)
        {
            //ShowErrorMessage(e);
        }
    }

    const wchar_t* CHardwareMonitor::GetInfo(PluginInfoIndex index)
    {
        static std::wstring str;
        switch (index)
        {
        case TMI_NAME:
            return StringRes(L"PluginName").c_str();
        case TMI_DESCRIPTION:
            return StringRes(L"PluginDescription").c_str();
        case TMI_AUTHOR:
            return L"zhongyang219";
        case TMI_COPYRIGHT:
            return L"Copyright (C) by Zhong Yang 2025";
        case ITMPlugin::TMI_URL:
            return L"https://github.com/zhongyang219/TrafficMonitorPlugins";
            break;
        case TMI_VERSION:
            return L"1.0.2";
        default:
            break;
        }
        return L"";
    }

    const wchar_t* CHardwareMonitor::GetTooltipInfo()
    {
        if (m_settings.show_mouse_tooltip)
        {
            static std::wstring tooltip_info;
            tooltip_info.clear();
            for (size_t i{}; i < m_items.size(); i++)
            {
                const auto& item = m_items[i];
                if (i > 0)
                    tooltip_info += L"\r\n";
                tooltip_info += item->GetItemName();
                tooltip_info += L": ";
                tooltip_info += item->GetItemValueText();
            }
            return tooltip_info.c_str();
        }
        else
        {
            return L"";
        }
    }

    ITMPlugin::OptionReturn CHardwareMonitor::ShowOptionsDialog(void* hParent)
    {
        try
        {
            SettingsForm^ form = gcnew SettingsForm();
            MonitorGlobal::Instance()->setttings_form = form;
            if (form->ShowDialog() == DialogResult::OK)
            {
                MonitorGlobal::Instance()->setttings_form = nullptr;
                SaveConfig();
                return ITMPlugin::OR_OPTION_CHANGED;
            }
        }
        catch (System::Exception^ e)
        {
            MonitorGlobal::Instance()->setttings_form = nullptr;
            ShowErrorMessage(e);
        }
        finally
        {
            MonitorGlobal::Instance()->setttings_form = nullptr;
        }
        return ITMPlugin::OR_OPTION_UNCHANGED;
    }

    void CHardwareMonitor::OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data)
    {
        switch (index)
        {
        case ITMPlugin::EI_CONFIG_DIR:
            try
            {
                //从配置文件读取配置
                LoadConfig(std::wstring(data));
            }
            catch (System::Exception^ e)
            {
                ShowErrorMessage(e);
            }
            break;
        case ITMPlugin::EI_DRAW_TASKBAR_WND:
            m_tm_settings.is_taskbar = _wtoi(data);
            break;
        case ITMPlugin::EI_MAIN_WND_SPERATE_WITH_SPACE:
            m_tm_settings.main_wnd_sperate_with_space = _wtoi(data);
            break;
        case ITMPlugin::EI_TASKBAR_WND_SPERATE_WITH_SPACE:
            m_tm_settings.taskbar_sperate_with_space = _wtoi(data);
            break;
        default:
            break;
        }

    }

    void* CHardwareMonitor::GetPluginIcon()
    {
        return MonitorGlobal::Instance()->GetAppIcon()->Handle.ToPointer();
    }

    int CHardwareMonitor::GetCommandCount()
    {
        return 1;
    }

    const wchar_t* CHardwareMonitor::GetCommandName(int command_index)
    {
        return StringRes(L"HardwareInfo").c_str();
    }

    void CHardwareMonitor::OnPluginCommand(int command_index, void* hWnd, void* para)
    {
        if (command_index == 0)
        {
            MonitorGlobal::Instance()->ShowHardwareInfoDialog();
        }
    }

    void CHardwareMonitor::OnInitialize(ITrafficMonitor* pApp)
    {
        m_pMainApp = pApp;
    }


    ////////////////////////////////////////////////////////////////////////////////////
    MonitorGlobal::MonitorGlobal()
    {
        iconsMap = gcnew Dictionary<String^, Icon^>();
    }

    MonitorGlobal::~MonitorGlobal()
    {

    }

    void MonitorGlobal::Init()
    {
        updateVisitor = gcnew UpdateVisitor();
        computer = gcnew Computer();
        computer->Open();

        resourceManager = gcnew Resources::ResourceManager("HardwareMonitor.resource", Reflection::Assembly::GetExecutingAssembly());
    }

    void MonitorGlobal::UnInit()
    {
        computer->Close();
    }

    String^ MonitorGlobal::GetString(String^ name)
    {
        try
        {
            return resourceManager->GetString(name);
        }
        catch (System::Exception^)
        {
            return nullptr;
        }
    }

    std::wstring MonitorGlobal::GetStdWString(String^ name)
    {
        return Common::StringToStdWstring(GetString(name));
    }

    Icon^ MonitorGlobal::GetAppIcon()
    {
        return GetIcon("logo");
    }

    Icon^ MonitorGlobal::GetIcon(String^ name)
    {
        Icon^ icon;
        if (!iconsMap->TryGetValue(name, icon))
        {
            icon = LoadIcon(name, CHardwareMonitor::GetInstance()->DPI(16));
            iconsMap->Add(name, icon);
        }
        return icon;
    }

    Resources::ResourceManager^ MonitorGlobal::GetResourceManager()
    {
        return resourceManager;
    }

    void MonitorGlobal::ShowHardwareInfoDialog()
    {
        try
        {
            HardwareInfoForm^ form = gcnew HardwareInfoForm();
            monitor_form = form;
            form->ShowDialog();
            monitor_form = nullptr;
        }
        catch (System::Exception^ e)
        {
            monitor_form = nullptr;
            CHardwareMonitor::ShowErrorMessage(e);
        }
        finally
        {
            monitor_form = nullptr;
        }
    }

    Icon^ MonitorGlobal::LoadIcon(String^ name, int icon_size)
    {
        try
        {
            //载入图标
            Icon^ tmp = dynamic_cast<Icon^>(resourceManager->GetObject(name));
            //转换为新的尺寸
            return gcnew Icon(tmp, Size(icon_size, icon_size));
        }
        catch (System::Exception^)
        {
            return nullptr;
        }
    }
}



ITMPlugin* TMPluginGetInstance()
{
    return HardwareMonitor::CHardwareMonitor::GetInstance();
}
