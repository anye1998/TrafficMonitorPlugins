#include "stdafx.h"
#include "HardwareMonitorHelper.h"
#include "HardwareMonitor.h"
#include <gcroot.h>
#include "Common.h"

using namespace LibreHardwareMonitor::Hardware;
using namespace System::Collections::Generic;
using namespace System;
using namespace System::Text;

namespace HardwareMonitor
{
    String^ HardwareMonitorHelper::GetSensorTypeName(SensorType type)
    {
        switch (type)
        {
        case SensorType::Voltage: return MonitorGlobal::Instance()->GetString(L"Voltage");
        case SensorType::Current: return MonitorGlobal::Instance()->GetString(L"Current");
        case SensorType::Power: return MonitorGlobal::Instance()->GetString(L"Power");
        case SensorType::Clock: return MonitorGlobal::Instance()->GetString(L"Clock");
        case SensorType::Temperature: return MonitorGlobal::Instance()->GetString(L"Temperature");
        case SensorType::Load: return MonitorGlobal::Instance()->GetString(L"Load");
        case SensorType::Frequency: return MonitorGlobal::Instance()->GetString(L"Frequency");
        case SensorType::Fan: return MonitorGlobal::Instance()->GetString(L"Fan");
        case SensorType::Flow: return MonitorGlobal::Instance()->GetString(L"Flow");
        case SensorType::Control: return MonitorGlobal::Instance()->GetString(L"Control");
        case SensorType::Level: return MonitorGlobal::Instance()->GetString(L"Level");
        case SensorType::Factor: return MonitorGlobal::Instance()->GetString(L"Factor");
        case SensorType::Data: return MonitorGlobal::Instance()->GetString(L"Data");
        case SensorType::SmallData: return MonitorGlobal::Instance()->GetString(L"Data");
        case SensorType::Throughput: return MonitorGlobal::Instance()->GetString(L"Throughput");
        case SensorType::TimeSpan: return MonitorGlobal::Instance()->GetString(L"TimeSpan");
        case SensorType::Timing: return MonitorGlobal::Instance()->GetString(L"Timing");   //0.9.6 新增
        case SensorType::Energy: return MonitorGlobal::Instance()->GetString(L"Energy");
        case SensorType::Noise: return MonitorGlobal::Instance()->GetString(L"Noise");
        case SensorType::Conductivity: return MonitorGlobal::Instance()->GetString(L"Conductivity");
        case SensorType::Humidity: return MonitorGlobal::Instance()->GetString(L"Humidity");
        }
        return L"";
    }

    String^ HardwareMonitorHelper::GetHardwareIconResName(LibreHardwareMonitor::Hardware::HardwareType type)
    {
        switch (type)
        {
        case HardwareType::Motherboard: return L"MotherBoard";
        case HardwareType::Battery: return L"batteries";
        case HardwareType::Cpu: return L"CPU";
        case HardwareType::EmbeddedController: return L"FanColtroller";
        case HardwareType::Network: return L"Network";
        case HardwareType::Memory: return L"RAM";
        case HardwareType::SuperIO: return L"MotherBoard";      //Super I/O 芯片，归到主板图标
        case HardwareType::Cooler: return L"FanColtroller";     //水冷水泵/风扇控制器
        case HardwareType::Storage: return L"Storage";
        case HardwareType::GpuAmd: return L"AMD";
        case HardwareType::GpuIntel: return L"intel";
        case HardwareType::GpuNvidia: return L"Nvidia";
        }
        return L"";
    }

    List<String^>^ HardwareMonitorHelper::GetSensorTypeUnit(SensorType type)
    {
        List<String^>^ unitList = gcnew List<String^>();
        switch (type)
        {
        case SensorType::Voltage: unitList->Add("V"); unitList->Add("mV"); break;
        case SensorType::Current: unitList->Add("A"); unitList->Add("mA"); break;
        case SensorType::Power: unitList->Add("W"); unitList->Add("mW"); break;
        case SensorType::Clock: unitList->Add("MHz"); unitList->Add("GHz"); break;
        case SensorType::Temperature: unitList->Add("°C"); unitList->Add("°F"); break;
        case SensorType::Load: unitList->Add("%"); break;
        case SensorType::Frequency: unitList->Add("Hz"); break;
        case SensorType::Fan: unitList->Add("RPM"); break;
        case SensorType::Flow: unitList->Add("L/h"); break;
        case SensorType::Control: unitList->Add("%"); break;
        case SensorType::Level: unitList->Add("%"); break;
        case SensorType::Factor: unitList->Add(""); break;
        case SensorType::Data: unitList->Add("GB"); unitList->Add("MB"); break;
        case SensorType::SmallData: unitList->Add("MB"); unitList->Add("GB"); break;
        case SensorType::Throughput: unitList->Add("KB/s"); unitList->Add("MB/s"); break;
        case SensorType::TimeSpan: unitList->Add("s"); unitList->Add("min"); unitList->Add("h"); break;   //时间：秒/分钟/小时（电池的“剩余使用时间”就用这个）
        case SensorType::Timing: unitList->Add("ns"); break;   //0.9.6 新增
        case SensorType::Energy: unitList->Add("mWh"); unitList->Add("Wh"); break;
        case SensorType::Noise: unitList->Add("dBA"); break;
        case SensorType::Conductivity: unitList->Add(L"µS/cm"); break;
        case SensorType::Humidity: unitList->Add("%"); break;
        default: unitList->Add(""); break;
        }
        return unitList;
    }

    String^ HardwareMonitorHelper::GetSensorTypeDefaultUnit(LibreHardwareMonitor::Hardware::SensorType type)
    {
        List<String^>^ unitList = GetSensorTypeUnit(type);
        if (unitList->Count > 0)
            return unitList[0];
        return gcnew String("");
    }

    //把一个identifier归一化成“宽松匹配”用的键。
    //LibreHardwareMonitor里有两处会导致同一个传感器算出不同的identifier：
    //  1) 电池的功率/电流传感器会随状态改名：Charge Rate / Discharge Rate / Charge/Discharge Rate
    //     （电流同理），而监控项的identifier是保存在配置文件里的，充电/放电一切换就对不上了；
    //  2) 电池硬件的Identifier里带着BatteryTag（形如“battery/OEM_XXXX_1234”），
    //     电池设备重新枚举后这个Tag可能会变。
    //监控项的identifier不能跟着改：TrafficMonitor用“7KrUeFVl_”+identifier作为显示项ID，
    //改了ID就等于换了一个显示项，用户已经配置好的显示项会消失。所以只能在查找时做宽松匹配。
    static String^ GetLooseIdentifyer(String^ identifyer)
    {
        if (String::IsNullOrEmpty(identifyer))
            return nullptr;
        array<String^>^ parts = identifyer->Split(gcnew array<wchar_t>{L'/'});
        int n = parts->Length;
        if (n < 3)
            return identifyer->ToLowerInvariant();

        //最后一段是传感器名称：把随充电/放电状态变化的名称归一
        String^ sensor_name = parts[n - 1];
        if (sensor_name->Equals("Charge Rate", StringComparison::OrdinalIgnoreCase)
            || sensor_name->Equals("Discharge Rate", StringComparison::OrdinalIgnoreCase)
            || sensor_name->Equals("Charge/Discharge Rate", StringComparison::OrdinalIgnoreCase))
            sensor_name = "ChargeDischargeRate";
        else if (sensor_name->Equals("Charge Current", StringComparison::OrdinalIgnoreCase)
            || sensor_name->Equals("Discharge Current", StringComparison::OrdinalIgnoreCase)
            || sensor_name->Equals("Charge/Discharge Current", StringComparison::OrdinalIgnoreCase))
            sensor_name = "ChargeDischargeCurrent";

        StringBuilder^ sb = gcnew StringBuilder();
        for (int i = 0; i < n - 2; i++)
        {
            String^ seg = parts[i]->ToLowerInvariant();
            //电池硬件段的末尾是BatteryTag，去掉结尾的“_数字”
            if (i > 0 && parts[i - 1]->Equals("battery", StringComparison::OrdinalIgnoreCase))
            {
                int pos = seg->LastIndexOf('_');
                if (pos > 0)
                {
                    String^ tail = seg->Substring(pos + 1);
                    bool all_digit = tail->Length > 0;
                    for each (System::Char ch in tail)
                    {
                        if (!Char::IsDigit(ch))
                        {
                            all_digit = false;
                            break;
                        }
                    }
                    if (all_digit)
                        seg = seg->Substring(0, pos);
                }
            }
            sb->Append(seg)->Append('/');
        }
        sb->Append(parts[n - 2]->ToLowerInvariant())->Append('/')->Append(sensor_name->ToLowerInvariant());
        return sb->ToString();
    }

    //宽松匹配键在索引里的前缀，避免和真实的identifier冲突
    static String^ GetLooseIndexKey(String^ identifyer)
    {
        String^ loose = GetLooseIdentifyer(identifyer);
        if (loose == nullptr)
            return nullptr;
        return String::Concat("\u0001", loose);
    }

    //把一个硬件（连同它的子硬件）下的所有传感器加入索引
    static void AddHardwareSensorsToIndex(IHardware^ hardware, Dictionary<String^, ISensor^>^ map)
    {
        //注意：只加入到索引里的sensor是hardware->Sensors里的传感器，也就是当前处于“激活”状态的传感器。
        //被LibreHardwareMonitor停用（例如电池设备暂时读不到数据）的传感器不会出现在hardware->Sensors里，
        //这样索引重建后自然就查不到它，调用方会显示“--”，而不是继续显示过期的旧数值。
        for each (ISensor ^ sensor in hardware->Sensors)
        {
            String^ identifyer = HardwareMonitorHelper::GetSensorIdentifyer(sensor);
            if (String::IsNullOrEmpty(identifyer))
                continue;
            if (!map->ContainsKey(identifyer))
                map->Add(identifyer, sensor);
            String^ loose_key = GetLooseIndexKey(identifyer);
            if (loose_key != nullptr && !map->ContainsKey(loose_key))
                map->Add(loose_key, sensor);
        }
        for each (IHardware ^ sub_hardware in hardware->SubHardware)
        {
            AddHardwareSensorsToIndex(sub_hardware, map);
        }
    }

    void HardwareMonitorHelper::RebuildSensorIndex()
    {
        MonitorGlobal^ global = MonitorGlobal::Instance();
        if (global->computer == nullptr)
            return;
        //先建一个新的Dictionary，建好之后再整体替换，这样其它线程（例如设置窗口的UI线程）
        //在重建过程中读到的仍然是上一次的完整索引，不会读到一半的数据。
        Dictionary<String^, ISensor^>^ map = gcnew Dictionary<String^, ISensor^>();
        try
        {
            for each (IHardware ^ hardware in global->computer->Hardware)
            {
                AddHardwareSensorsToIndex(hardware, map);
            }
        }
        catch (System::Exception^)
        {
            //遍历过程中硬件树被修改（例如正在重新枚举电池设备），保留上一次的索引，下次再重建
            global->sensor_index_dirty = true;
            return;
        }
        global->sensorMap = map;
        global->sensor_index_dirty = false;
    }

    void HardwareMonitorHelper::InvalidateSensorIndex()
    {
        MonitorGlobal::Instance()->sensor_index_dirty = true;
    }

    ISensor^ HardwareMonitorHelper::FindSensorByIdentifyer(String^ identifyer)
    {
        if (String::IsNullOrEmpty(identifyer))
            return nullptr;
        MonitorGlobal^ global = MonitorGlobal::Instance();
        if (global->sensor_index_dirty)
            RebuildSensorIndex();
        ISensor^ sensor = nullptr;
        if (global->sensorMap->TryGetValue(identifyer, sensor))
            return sensor;
        //精确匹配失败时再按“宽松匹配”找一次：充电/放电切换导致传感器改名、
        //电池设备重新枚举导致BatteryTag变化，都会让原来的identifier对不上
        String^ loose_key = GetLooseIndexKey(identifyer);
        if (loose_key != nullptr && global->sensorMap->TryGetValue(loose_key, sensor))
            return sensor;
        //这里不再把“找不到”的结果写回索引。以前的做法会把nullptr缓存下来，
        //导致开机时电池设备还没枚举出来的情况下一整轮运行都找不到电池传感器。
        return nullptr;
    }

    bool HardwareMonitorHelper::IsBatteryIdentifyer(String^ identifyer)
    {
        if (String::IsNullOrEmpty(identifyer))
            return false;
        //电池硬件的Identifier形如“battery/OEM_XXXX_1234”，完整的监控项identifier为
        //“battery/OEM_XXXX_1234/Level/Charge Level”
        return identifyer->IndexOf("battery/", StringComparison::OrdinalIgnoreCase) >= 0;
    }

    String^ HardwareMonitorHelper::GetHardwareIconResNameFromIdentifyer(String^ identifyer)
    {
        if (String::IsNullOrEmpty(identifyer))
            return L"";
        String^ id = identifyer->ToLowerInvariant();
        if (IsBatteryIdentifyer(identifyer)) return L"batteries";
        if (id->StartsWith("cpu/")) return L"CPU";
        if (id->Contains("gpu-nvidia")) return L"Nvidia";
        if (id->Contains("gpu-amd")) return L"AMD";
        if (id->Contains("gpu-intel")) return L"intel";
        if (id->StartsWith("motherboard/") || id->StartsWith("smbios/") || id->Contains("/lpc/")) return L"MotherBoard";
        if (id->StartsWith("storage/") || id->StartsWith("nvme/")) return L"Storage";
        if (id->StartsWith("network/")) return L"Network";
        if (id->StartsWith("memory/")) return L"RAM";
        if (id->Contains("/ec/") || id->Contains("/cooler/")) return L"FanColtroller";
        return L"";
    }

    float HardwareMonitorHelper::GetSensorValue(LibreHardwareMonitor::Hardware::ISensor^ sensor, System::String^ unit)
    {
        float value = sensor->Value.Value;
        //电压
        if (sensor->SensorType == SensorType::Voltage)
        {
            if (unit->Equals("mV"))
                value *= 1000.0f;
        }
        //电流
        else if (sensor->SensorType == SensorType::Current)
        {
            if (unit->Equals("mA"))
                value *= 1000.0f;
        }
        //功率
        else if (sensor->SensorType == SensorType::Power)
        {
            if (unit->Equals("mW"))
                value *= 1000.0f;
        }
        //时钟频率
        else if (sensor->SensorType == SensorType::Clock)
        {
            if (unit->Equals("GHz"))
                value /= 1024.0f;
        }
        //温度
        else if (sensor->SensorType == SensorType::Temperature)
        {
            if (unit->Equals("°F"))
                value = (value * 9.0f / 5.0f) + 32.0f;
        }

        //数据（默认GB）
        else if (sensor->SensorType == SensorType::Data)
        {
            if (unit->Equals("MB"))
                value *= 1024.0f;
        }
        //数据（默认MB）
        else if (sensor->SensorType == SensorType::SmallData)
        {
            if (unit->Equals("GB"))
                value /= 1024.0f;
        }
        //速率
        else if (sensor->SensorType == SensorType::Throughput)
        {
            if (unit->Equals("MB/s")) //MB/s
                value /= (1024.0f * 1024.0f);
            else                    //KB/s
                value /= 1024.0f;
        }
        //时间（默认秒）
        else if (sensor->SensorType == SensorType::TimeSpan)
        {
            if (unit->Equals("min"))        //分钟
                value /= 60.0f;
            else if (unit->Equals("h"))     //小时
                value /= 3600.0f;
        }
        //电量
        else if (sensor->SensorType == SensorType::Energy)
        {
            if (unit->Equals("Wh"))
                value /= 1024.0f;
        }
        if (sensor->SensorType == SensorType::Power && sensor->Name == L"Discharge Rate")   //放电功率显示为负数
            value = -value;
        if (sensor->SensorType == SensorType::Current && sensor->Name == L"Discharge Current")   //放电电流显示为负数
            value = -value;
        return value;
    }

    String^ HardwareMonitorHelper::GetSensorValueText(ISensor^ sensor, String^ unit, int decimal_place, bool show_unit, bool seperate_with_space)
    {
        String^ sensor_str;
        if (sensor->Value.HasValue)
        {
            float value = GetSensorValue(sensor, unit);
            String^ formatString = String::Format("F{0}", decimal_place);
            sensor_str += value.ToString(formatString);
        }
        else
        {
            sensor_str += "--";
        }
        if (show_unit)
        {
            if (seperate_with_space)
                sensor_str += " ";
            sensor_str += unit;
        }
        return sensor_str;
    }

    String^ HardwareMonitorHelper::GetSensorDisplayName(ISensor^ sensor)
    {
        String^ name;
        IHardware^ hardware = sensor->Hardware;
        name = hardware->Name;
        name += "/";
        name += GetSensorTypeName(sensor->SensorType);
        name += "/";
        name += Common::GetTranslatedString(sensor->Name);
        return name;
    }

    String^ HardwareMonitorHelper::GetSensorIdentifyer(ISensor^ sensor)
    {
        //不使用sensor->Identifier，因为sensor->Identifier代表的传感器并不总是固定的
        //return sensor->Identifier->ToString();

        String^ path;
        IHardware^ hardware = sensor->Hardware;
        while (hardware != nullptr)
        {
            //Hardware部分仍然使用它自己的Identifier
            path = hardware->Identifier + "/" + path;
            hardware = hardware->Parent;
        }
        //Sensor部分使用“类型+名称”
        path += sensor->SensorType.ToString();
        path += "/";
        path += sensor->Name;
        return path;
    }

    void HardwareMonitorHelper::GetDefaultMonitorItem(List<ISensor^>^ default_sensors)
    {
        auto computer = MonitorGlobal::Instance()->computer;
        for (int i = 0; i < computer->Hardware->Count; i++)
        {
            IHardware^ hardware = computer->Hardware[i];
            //CPU
            if (hardware->HardwareType == HardwareType::Cpu)
            {
                ISensor^ cpu_temp = nullptr;
                ISensor^ cpu_temp_default = nullptr;
                //先查找"Core Average"
                for (int j = 0; j < hardware->Sensors->Length; j++)
                {
                    ISensor^ sensor = hardware->Sensors[j];
                    if (sensor->SensorType == SensorType::Temperature)
                    {
                        if (cpu_temp_default == nullptr)
                            cpu_temp_default = sensor;
                        if (sensor->Name->Equals("Core Average"))
                        {
                            cpu_temp = sensor;
                            break;
                        }
                    }
                }
                //没找到则使用第一个
                if (cpu_temp == nullptr && cpu_temp_default != nullptr)
                    cpu_temp = cpu_temp_default;
                if (cpu_temp != nullptr)
                    default_sensors->Add(cpu_temp);
            }

            //GPU
            if (hardware->HardwareType == HardwareType::GpuNvidia || hardware->HardwareType == HardwareType::GpuAmd
                || hardware->HardwareType == HardwareType::GpuIntel)
            {
                ISensor^ gpu_temp = nullptr;
                ISensor^ gpu_temp_default = nullptr;
                for (int j = 0; j < hardware->Sensors->Length; j++)
                {
                    ISensor^ sensor = hardware->Sensors[j];
                    //GPU温度
                    if (sensor->SensorType == SensorType::Temperature)
                    {
                        if (gpu_temp_default == nullptr)
                            gpu_temp_default = sensor;
                        if (sensor->Name->Equals("GPU Core"))
                        {
                            gpu_temp = sensor;
                            break;
                        }
                    }
                    //GPU负载
                    else if (sensor->SensorType == SensorType::Load)
                    {
                        if (sensor->Name->Equals("GPU Core") || sensor->Name->Equals("D3D 3D"))
                            default_sensors->Add(sensor);
                    }
                }
                //没找到则使用第一个
                if (gpu_temp == nullptr && gpu_temp_default != nullptr)
                    gpu_temp = gpu_temp_default;
                if (gpu_temp != nullptr)
                    default_sensors->Add(gpu_temp);
            }
        }
    }
}
