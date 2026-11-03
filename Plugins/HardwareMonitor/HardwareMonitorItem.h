#pragma once
#include "PluginInterface.h"
#include <string>
#include "CommonData.h"

namespace HardwareMonitor
{
    class CHardwareMonitorItem : public IPluginItem
    {
    public:
        CHardwareMonitorItem(const std::wstring& _identifier, const std::wstring& _item_name, const std::wstring& _label_text, int _sensor_type = -1);

        // 通过 IPluginItem 继承
        const wchar_t* GetItemName() const override;
        const wchar_t* GetItemId() const override;
        const wchar_t* GetItemLableText() const override;
        const wchar_t* GetItemValueText() const override;
        const wchar_t* GetItemValueSampleText() const override;
        virtual int IsDrawResourceUsageGraph() const override;
        virtual float GetResourceUsageGraphValue() const override;

        void UpdateValue();
        const std::wstring& GetIdentifier() const;

        //名称是否已经从真实传感器上取到过（没有的话显示的是用identifier拼出来的临时名称）
        bool IsNameResolved() const;

    private:
        std::wstring identifier;
        std::wstring item_name;
        std::wstring item_value_taskbar;
        std::wstring item_value_main_wnd;
        std::wstring label_text;
        int sensor_type{ -1 };
        bool name_resolved{ false };            //名称是否取自真实传感器
        float item_value_num{}; //项目的原始数值
        float item_value_with_unit{};           //根据单位转换过的数值
        float last_item_value_with_unit{};      //上次根据单位转换过的数值
    };
}
