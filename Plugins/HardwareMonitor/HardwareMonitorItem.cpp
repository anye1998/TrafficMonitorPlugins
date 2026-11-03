#include "stdafx.h"
#include "HardwareMonitorItem.h"
#include "HardwareMonitor.h"
#include "HardwareMonitorHelper.h"
#include "Common.h"

using namespace LibreHardwareMonitor::Hardware;

namespace HardwareMonitor
{
    CHardwareMonitorItem::CHardwareMonitorItem(const std::wstring& _identifier, const std::wstring& _item_name, const std::wstring& _label_text, int _sensor_type)
        : identifier(_identifier), item_name(_item_name), label_text(_label_text), sensor_type(_sensor_type)
    {
    }

    const wchar_t* CHardwareMonitorItem::GetItemName() const
    {
        return item_name.c_str();
    }

    const wchar_t* CHardwareMonitorItem::GetItemId() const
    {
        static std::wstring item_id;
        item_id = L"7KrUeFVl_" + identifier;
        return item_id.c_str();
    }

    const wchar_t* CHardwareMonitorItem::GetItemLableText() const
    {
        return label_text.c_str();
    }

    const wchar_t* CHardwareMonitorItem::GetItemValueText() const
    {
        if (CHardwareMonitor::GetInstance()->m_tm_settings.is_taskbar)
            return item_value_taskbar.c_str();
        else
            return item_value_main_wnd.c_str();
    }

    const wchar_t* CHardwareMonitorItem::GetItemValueSampleText() const
    {
        static std::wstring sample_text;
        const ItemInfo& item_info{ CHardwareMonitor::GetInstance()->m_settings.FindItemInfo(identifier) };
        if (item_info.specify_value_width)
        {
            sample_text = std::wstring(item_info.value_width, L'0');
            if (item_info.decimal_places > 0)
            {
                sample_text += L'.';
                sample_text += std::wstring(item_info.decimal_places, L'0');
            }
            if (item_info.show_unit)
            {
                if (CHardwareMonitor::GetInstance()->m_tm_settings.sperate_with_space())
                    sample_text += L' ';
                std::wstring unit;
                if (!item_info.unit.empty())
                    unit = item_info.unit;
                else if (sensor_type >= 0)
                    unit = Common::StringToStdWstring(HardwareMonitorHelper::GetSensorTypeDefaultUnit(static_cast<SensorType>(sensor_type)));
                sample_text += unit;
            }
        }
        else
        {
            std::wstring item_value(GetItemValueText());
            if (item_value.empty())
                sample_text = L"0.00";
            else
                sample_text = item_value;
        }
        return sample_text.c_str();
    }

    int CHardwareMonitorItem::IsDrawResourceUsageGraph() const
    {
        if (sensor_type < 0)
            return 0;
        SensorType type = static_cast<SensorType>(sensor_type);
        return type == SensorType::Temperature || type == SensorType::Load;
    }

    float CHardwareMonitorItem::GetResourceUsageGraphValue() const
    {
        if (sensor_type < 0)
            return 0.0f;
        SensorType type = static_cast<SensorType>(sensor_type);
        if (type == SensorType::Temperature || type == SensorType::Load)
        {
            return item_value_num / 100.0f;
        }
        return 0.0f;
    }

    bool CHardwareMonitorItem::IsNameResolved() const
    {
        return name_resolved;
    }

    void CHardwareMonitorItem::UpdateValue()
    {
        //每个监控项自己处理异常：以前只要有一项（例如电池）在取数据时抛出异常，
        //整个DataRequired的循环就会中断，排在它后面的监控项全部停止刷新，
        //表现出来就是“数值卡住不动”，直到重启主程序。
        try
        {
            CHardwareMonitor* plugin = CHardwareMonitor::GetInstance();
            ISensor^ sensor = HardwareMonitorHelper::FindSensorByIdentifyer(gcnew String(identifier.c_str()));
            if (sensor == nullptr)
            {
                //传感器当前不可用（开机时电池设备还没枚举出来、休眠/驱动重载后句柄失效等）。
                //这里必须显示“--”而不是保留上一次的数值：保留旧数值会让人以为电池还有读数，
                //实际上数据早就不更新了。
                item_value_main_wnd = L"--";
                item_value_taskbar = L"--";
                item_value_num = 0;
                item_value_with_unit = 0;
                return;
            }

            ItemInfo& item_info{ plugin->m_settings.FindItemInfo(identifier) };

            //传感器找回来了：把之前用identifier拼出来的临时名称换成真实名称，并写回配置文件
            if (!name_resolved)
            {
                item_name = Common::StringToStdWstring(HardwareMonitorHelper::GetSensorDisplayName(sensor));
                label_text = Common::StringToStdWstring(Common::GetTranslatedString(sensor->Name));
                name_resolved = true;
                item_info.item_name = item_name;
                plugin->NotifyConfigBackfilled();
            }
            if (item_info.sensor_type != static_cast<int>(sensor->SensorType))
            {
                item_info.sensor_type = static_cast<int>(sensor->SensorType);
                plugin->NotifyConfigBackfilled();
            }
            sensor_type = static_cast<int>(sensor->SensorType);

            String^ unit;
            if (!item_info.unit.empty())
                unit = gcnew String(item_info.unit.c_str());
            else
                unit = HardwareMonitorHelper::GetSensorTypeDefaultUnit(sensor->SensorType);
            const auto& cfg{ plugin->m_tm_settings };
            item_value_main_wnd = Common::StringToStdWstring(HardwareMonitorHelper::GetSensorValueText(sensor, unit, item_info.decimal_places, item_info.show_unit, cfg.main_wnd_sperate_with_space));
            item_value_taskbar = Common::StringToStdWstring(HardwareMonitorHelper::GetSensorValueText(sensor, unit, item_info.decimal_places, item_info.show_unit, cfg.taskbar_sperate_with_space));
            try
            {
                item_value_num = sensor->Value.Value;
            }
            catch (System::Exception^)
            {
                item_value_num = 0;
            }

            //判断是否要显示通知
            item_value_with_unit = HardwareMonitorHelper::GetSensorValue(sensor, unit);
            if (item_info.show_notify)
            {
                if (last_item_value_with_unit < item_info.notify_value && item_value_with_unit >= item_info.notify_value)
                {
                    String^ format_str = MonitorGlobal::Instance()->GetString(L"ValueExceedNotifyMsg");
                    String^ value_with_unit = HardwareMonitorHelper::GetSensorValueText(sensor, unit);
                    String^ str_msg = String::Format(format_str, gcnew String(item_name.c_str()), value_with_unit);
                    plugin->GetMainApp()->ShowNotifyMessage(Common::StringToStdWstring(str_msg).c_str());
                    last_item_value_with_unit = item_value_with_unit;
                }
            }
            last_item_value_with_unit = item_value_with_unit;
        }
        catch (System::Exception^)
        {
            //取数据失败时不能把旧数值留在界面上
            item_value_main_wnd = L"--";
            item_value_taskbar = L"--";
        }
    }

    const std::wstring& CHardwareMonitorItem::GetIdentifier() const
    {
        return identifier;
    }
}
