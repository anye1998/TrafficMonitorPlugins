// HardwareMonitor.h

#pragma once
#include <string>
#include <memory>
#include "UpdateVisitor.h"
#include <map>
#include "PluginInterface.h"
#include "HardwareInfoForm.h"
#include "SettingsForm.h"
#include "HardwareMonitorItem.h"
#include <vector>
#include "CommonData.h"

using namespace System;
using namespace System::Collections::Generic;
using namespace LibreHardwareMonitor::Hardware;

namespace HardwareMonitor
{
    public class CHardwareMonitor : public ITMPlugin
	{
    private:
        CHardwareMonitor();
        virtual ~CHardwareMonitor();

    public:
        static CHardwareMonitor* GetInstance();

        const std::wstring& StringRes(const wchar_t* name);

        //添加一个监控项目。
        //如果监控项目已存在，返回false，否则返回true
        bool AddDisplayItem(ISensor^ sensor);

        //判断一个监控项目是否已存在
        bool IsDisplayItemExist(const std::wstring& identifyer);

        //移除一个监控项目
        //成功返回true，否则返回false
        bool RemoveDisplayItem(int index);

        std::wstring GetItemName(const std::wstring& identifier);

        const std::wstring& GetConfigPath() const;

        void LoadConfig(const std::wstring& config_dir);
        void SaveConfig();

        //标记配置文件需要更新（例如监控项的名称是后来才补上的），在下一轮数据刷新时写回ini
        void NotifyConfigBackfilled();

        //请求重新枚举一次电池设备（由数据刷新线程执行，避免在UI线程里修改硬件树）
        void RequestBatteryGroupRefresh();

        int DPI(int pixel) const;

        ITrafficMonitor* GetMainApp() const;

        OptionSettings m_settings;
        TMSettings m_tm_settings;

        static void ShowErrorMessage(System::Exception^ e);

    private:
        static CHardwareMonitor* m_pIns;
        //注意：这里保存的是指针，不能用CHardwareMonitorItem对象数组。
        //TrafficMonitor在启动时会把GetItem()返回的指针缓存下来，如果这些对象的地址在运行过程中
        //发生变化（例如往vector里push_back导致重新分配），主程序里的指针就会失效。
        std::vector<std::unique_ptr<CHardwareMonitorItem>> m_items;
        std::wstring m_config_path;
        std::map<std::wstring, std::wstring> m_item_names;  //保存每个Sensor的identifier和名称的map

        bool m_config_backfilled{};                 //配置文件里有内容需要补写（名称、类型等）
        int m_battery_missing_count{};              //电池类监控项连续解析不到传感器的轮数
        int m_battery_recover_attempts{};           //已经重新枚举过几次电池设备（读不到电池的次数太多时放弃）
        unsigned m_last_battery_recover{};          //上次重新枚举电池设备的时间（TickCount）
        volatile int m_battery_refresh_request{};   //是否请求在下一轮刷新时重新枚举电池设备（UI线程置位，数据刷新线程清零）

        //根据m_settings.items_info建立（或重建）监控项列表。
        //每个配置项都会对应一个CHardwareMonitorItem，即使它对应的传感器当前找不到——
        //这样GetItem(index)的数量和顺序就与items_info完全一致，TrafficMonitor里
        //按索引保存的显示项配置才不会错位。
        void RebuildItems();

        //重新枚举电池设备并重建索引
        void RefreshBatteryGroup();

        //当电池类监控项长时间解析不到传感器时，尝试重新枚举电池设备
        void TryRecoverBatteryGroup();

        // 通过 ITMPlugin 继承
        IPluginItem* GetItem(int index) override;
        void DataRequired() override;
        const wchar_t* GetInfo(PluginInfoIndex index) override;
        virtual const wchar_t* GetTooltipInfo() override;
        virtual OptionReturn ShowOptionsDialog(void* hParent) override;
        virtual void OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data) override;
        virtual void* GetPluginIcon() override;

        virtual int GetCommandCount() override;
        virtual const wchar_t* GetCommandName(int command_index) override;
        virtual void OnPluginCommand(int command_index, void* hWnd, void* para) override;
        virtual void OnInitialize(ITrafficMonitor* pApp) override;

    private:
        std::map<const wchar_t*, std::wstring> string_table;
        int m_dpi{ 96 };
        ITrafficMonitor* m_pMainApp{};
    };

    public ref class MonitorGlobal
    {
    public:
        MonitorGlobal();
        ~MonitorGlobal();
        static MonitorGlobal^ Instance()
        {
            if (m_instance == nullptr)
            {
                m_instance = gcnew MonitorGlobal();
            }
            return m_instance;
        }

        void Init();
        void UnInit();

        String^ GetString(String^ name);
        std::wstring GetStdWString(String^ name);
        Icon^ GetAppIcon();
        Icon^ GetIcon(String^ name);
        Resources::ResourceManager^ GetResourceManager();

        void ShowHardwareInfoDialog();

    private:
        //从resx资源文件载入一个图标，并指定图标大小
        Icon^ LoadIcon(String^ name, int icon_size);

    public:
        Computer^ computer;
        UpdateVisitor^ updateVisitor{};

        HardwareInfoForm^ monitor_form{};
        SettingsForm^ setttings_form{};
        SortedSet<String^>^ treeCollapseNodes{ gcnew SortedSet<String^>() };                    //保存“硬件信息”对话框树控件中折叠的节点
        //保存传感器的identifyer和传感器对象的映射。
        //这里保存的是“某一时刻整个硬件树里所有可用传感器”的快照，由HardwareMonitorHelper::RebuildSensorIndex()
        //在每次硬件数据更新后整体重建（整体替换引用，不做原地修改），因此里面不会残留已经失效的传感器对象。
        Dictionary<String^, ISensor^>^ sensorMap{ gcnew Dictionary<String^, ISensor^>() };
        bool sensor_index_dirty{ true };                                                        //为true时表示索引需要重建

    private:
        Resources::ResourceManager^ resourceManager{};
        //保存所有图标
        Dictionary<String^, Icon^>^ iconsMap{};

    private:
        static MonitorGlobal^ m_instance{};
    };
}


#ifdef __cplusplus
extern "C" {
#endif
    __declspec(dllexport) ITMPlugin* TMPluginGetInstance();

#ifdef __cplusplus
}
#endif
