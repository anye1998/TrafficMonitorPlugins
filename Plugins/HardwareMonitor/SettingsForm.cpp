#include "stdafx.h"
#include "SettingsForm.h"
#include "HardwareMonitor.h"
#include "HardwareMonitorHelper.h"
#include "Common.h"
using namespace System::Collections::Generic;

namespace HardwareMonitor
{
    SettingsForm::SettingsForm(void)
    {
        this->StartPosition = FormStartPosition::CenterParent;
        this->Icon = MonitorGlobal::Instance()->GetAppIcon();
        InitializeComponent();
        this->MinimumSize = this->Size;

        Common::LoadFormSize(this, L"settings");

        //添加图标
        Common::SetButtonIcon(addItemBtn, "Add");
        Common::SetButtonIcon(removeSelectBtn, "Delete");
        Common::SetButtonIcon(moveUpButton, "MoveUp");
        Common::SetButtonIcon(moveDownButton, "MoveDown");

        UpdateItemList();
        EnableControls();

        //初始化要监控的硬件选项
        Computer^ computer = MonitorGlobal::Instance()->computer;
        cpuCheck->Checked = computer->IsCpuEnabled;
        gpuCheck->Checked = computer->IsGpuEnabled;
        motherBoardCheck->Checked = computer->IsMotherboardEnabled;
        storageCheck->Checked = computer->IsStorageEnabled;
        batteryCheck->Checked = computer->IsBatteryEnabled;
        networkCheck->Checked = computer->IsNetworkEnabled;
        memoryCheck->Checked = computer->IsMemoryEnabled;
        controllerCheck->Checked = computer->IsControllerEnabled;
        psuCheck->Checked = computer->IsPsuEnabled;

        //打开了电池监控、但当前又有电池监控项读不到传感器时（开机时电池设备还没枚举出来，
        //或者休眠/驱动重载后电池句柄失效），请数据刷新线程重新枚举一次电池设备，
        //这样用户打开设置窗口就能看到数值恢复，而不是必须重启主程序
        if (batteryCheck->Checked)
        {
            for (const auto& item : CHardwareMonitor::GetInstance()->m_settings.items_info)
            {
                String^ identifyer = gcnew String(item.identifyer.c_str());
                if (HardwareMonitorHelper::IsBatteryIdentifyer(identifyer) && HardwareMonitorHelper::FindSensorByIdentifyer(identifyer) == nullptr)
                {
                    CHardwareMonitor::GetInstance()->RequestBatteryGroupRefresh();
                    break;
                }
            }
        }

        showTooltipCheck->Checked = CHardwareMonitor::GetInstance()->m_settings.show_mouse_tooltip;

        decimalPlaceCombo->DropDownStyle = ComboBoxStyle::DropDownList;
        decimalPlaceCombo->Items->Add("0");
        decimalPlaceCombo->Items->Add("1");
        decimalPlaceCombo->Items->Add("2");
        decimalPlaceCombo->Items->Add("3");

        monitorItemListBox->ItemHeight = CHardwareMonitor::GetInstance()->DPI(20);
        monitorItemListBox->DrawMode = DrawMode::OwnerDrawFixed; // 设置为自定义绘制模式
        monitorItemListBox->IntegralHeight = false; // 允许高度连续变化

        // 为ListBox的SelectedIndexChanged事件添加处理程序
        monitorItemListBox->SelectedIndexChanged += gcnew System::EventHandler(this, &SettingsForm::listBox_SelectedIndexChanged);
        decimalPlaceCombo->SelectedIndexChanged += gcnew EventHandler(this, &SettingsForm::OnDecimalPlaceComboBoxSelectedIndexChanged);
        monitorItemListBox->DrawItem += gcnew DrawItemEventHandler(this, &SettingsForm::ListBox_DrawItem);

        // 为FormClosing事件添加处理程序
        this->FormClosing += gcnew FormClosingEventHandler(this, &SettingsForm::OnFormClosing);
    
        SettingsForm_Resize(nullptr, nullptr);
    }

    void SettingsForm::UpdateItemsValue()
    {
        for each (const auto & obj in monitorItemListBox->Items)
        {
            ListItemInfo^ listItem = dynamic_cast<ListItemInfo^>(obj);
            if (listItem == nullptr)
                continue;
            //每一轮都按identifier重新解析一次传感器。传感器可能这一轮失效、下一轮又回来
            //（电池设备被重新枚举后就是这种情况），所以不能一直使用打开窗口时缓存的那个对象，
            //否则对象失效之后这里的数值就再也不会更新了。
            ISensor^ sensor = HardwareMonitorHelper::FindSensorByIdentifyer(listItem->identifier);
            listItem->sensor = sensor;
            if (sensor == nullptr)
            {
                listItem->displayValue = L"--";
                continue;
            }
            std::wstring identifier_str = Common::StringToStdWstring(listItem->identifier);
            //注意这里要用列表项自己保存的identifier去查配置：传感器当前的identifier可能和保存的
            //不完全一样（电池的充放电会改名），而单位、小数位数是按保存的identifier存的
            ItemInfo& itemInfo = CHardwareMonitor::GetInstance()->m_settings.FindItemInfo(identifier_str);
            //名称也可能刚刚才从传感器上取到，这里同步一下
            String^ item_name = gcnew String(CHardwareMonitor::GetInstance()->GetItemName(identifier_str).c_str());
            if (!String::IsNullOrEmpty(item_name))
                listItem->displayName = item_name;
            //获取displayValue
            listItem->displayValue = HardwareMonitorHelper::GetSensorValueText(sensor, gcnew String(itemInfo.unit.c_str()), itemInfo.decimal_places, itemInfo.show_unit);
        }
        monitorItemListBox->Invalidate();
    }

    void SettingsForm::UpdateItemList()
    {
        //清除列表
        monitorItemListBox->Items->Clear();
        EnableControls();

        //填充数据
        //注意：必须为items_info中的每一项都添加一行，即使它对应的传感器当前找不到。
        //列表行与items_info是按下标一一对应的，一旦跳过某些行，下标就错位了：
        //删除/上移/下移会作用到别的监控项上（把电池项目删掉就是这么来的），
        //而“电池监控的项目没有了”也正是因为传感器找不到时这一行被跳过。
        const auto& items_info{ CHardwareMonitor::GetInstance()->m_settings.items_info };
        for (size_t i = 0; i < items_info.size(); i++)
        {
            const ItemInfo& item = items_info[i];
            ListItemInfo^ listItem = gcnew ListItemInfo();
            listItem->identifier = gcnew String(item.identifyer.c_str());
            listItem->displayName = gcnew String(CHardwareMonitor::GetInstance()->GetItemName(item.identifyer).c_str());
            ISensor^ sensor = HardwareMonitorHelper::FindSensorByIdentifyer(listItem->identifier);
            listItem->sensor = sensor;
            if (sensor != nullptr)
            {
                //获取数值文本
                listItem->displayValue = HardwareMonitorHelper::GetSensorValueText(sensor, gcnew String(item.unit.c_str()), item.decimal_places, item.show_unit);
                //根据监控项类型获取图标
                IHardware^ hardware = sensor->Hardware;
                //如果是子硬件，则使用它的父硬件
                if (hardware->Parent != nullptr)
                    hardware = hardware->Parent;
                auto resName = HardwareMonitorHelper::GetHardwareIconResName(hardware->HardwareType);
                if (resName->Length > 0)
                    listItem->icon = MonitorGlobal::Instance()->GetIcon(resName);
            }
            else
            {
                //传感器当前不存在：显示“--”，图标按identifier推断，但监控项本身必须保留
                listItem->displayValue = L"--";
                String^ resName = HardwareMonitorHelper::GetHardwareIconResNameFromIdentifyer(listItem->identifier);
                if (resName->Length > 0)
                    listItem->icon = MonitorGlobal::Instance()->GetIcon(resName);
            }
            monitorItemListBox->Items->Add(listItem);
        }
        EnableControls();
    }

    bool SettingsForm::IsSelectionValid()
    {
        return (monitorItemListBox->SelectedIndex >= 0 && monitorItemListBox->SelectedIndex < monitorItemListBox->Items->Count);
    }

    ItemInfo& SettingsForm::GetSelectedItemInfo()
    {
        //列表行与items_info按下标一一对应，直接按下标取即可。
        //以前是按“列表里显示的传感器”反查配置项，传感器找不到的行被跳过之后就会取到错误的配置项。
        int index = monitorItemListBox->SelectedIndex;
        auto& items_info{ CHardwareMonitor::GetInstance()->m_settings.items_info };
        if (index >= 0 && index < static_cast<int>(items_info.size()))
            return items_info[index];
        static ItemInfo emptyInfo;
        return emptyInfo;
    }

    void SettingsForm::EnableControls()
    {
        bool selection_enabled = IsSelectionValid();
        removeSelectBtn->Enabled = selection_enabled;
        decimalPlaceCombo->Enabled = selection_enabled;
        specifyValueWidthCheck->Enabled = selection_enabled;
        valueWidthEdit->Enabled = selection_enabled && specifyValueWidthCheck->Checked;
        showUnitCheck->Enabled = selection_enabled;
        unitCombo->Enabled = selection_enabled;
        int index = monitorItemListBox->SelectedIndex;
        moveUpButton->Enabled = selection_enabled && index > 0;
        moveDownButton->Enabled = selection_enabled && index < monitorItemListBox->Items->Count - 1;
        showNotifyCheck->Enabled = selection_enabled;
        notifyValueEdit->Enabled = selection_enabled && showNotifyCheck->Checked;
    }

    void SettingsForm::listBox_SelectedIndexChanged(System::Object^ sender, System::EventArgs^ e)
    {
        // 根据是否有选中项启用或禁用按钮
        EnableControls();

        //设置选中项对应的设置控件
        if (IsSelectionValid())
        {
            ItemInfo& item_info = GetSelectedItemInfo();
            if (item_info.decimal_places >= 0 && item_info.decimal_places < decimalPlaceCombo->Items->Count)
            {
                decimalPlaceCombo->SelectedIndex = item_info.decimal_places;
            }
            else
            {
                decimalPlaceCombo->SelectedIndex = -1;
            }

            specifyValueWidthCheck->Checked = item_info.specify_value_width;
            valueWidthEdit->Value = item_info.value_width;

            showUnitCheck->Checked = item_info.show_unit;
            //填充“单位选择”下拉列表
            unitCombo->Items->Clear();
            ISensor^ sensor = HardwareMonitorHelper::FindSensorByIdentifyer(gcnew String(item_info.identifyer.c_str()));
            if (sensor != nullptr)
            {
                List<String^>^ unitList = HardwareMonitorHelper::GetSensorTypeUnit(sensor->SensorType);
                for each (String ^ unit in unitList)
                {
                    unitCombo->Items->Add(unit);
                }
            }
            if (unitCombo->Items->Count > 0)
            {
                int index = unitCombo->FindString(gcnew String(item_info.unit.c_str()));
                if (index >= 0)
                    unitCombo->SelectedIndex = index;
                else
                    unitCombo->SelectedIndex = 0;
            }
            //设置超过指定数值时显示通知
            showNotifyCheck->Checked = item_info.show_notify;
            notifyValueEdit->Value = System::Decimal(item_info.notify_value);
        }
    }

    void SettingsForm::OnDecimalPlaceComboBoxSelectedIndexChanged(Object^ sender, EventArgs^ e)
    {
        if (IsSelectionValid())
        {
            ItemInfo& item_info = GetSelectedItemInfo();
            item_info.decimal_places = decimalPlaceCombo->SelectedIndex;
        }
    }

    void SettingsForm::OnFormClosing(Object^ sender, FormClosingEventArgs^ e)
    {
        //更新要监控的硬件设置。
        //只给真正变化过的选项赋值：LibreHardwareMonitor的属性setter每改一次都会把
        //对应的硬件组整个移除再重新创建一次（电池组会重新枚举电池设备、重新打开句柄），
        //全部无脑赋值一遍会在用户打开又关闭设置窗口时白折腾一遍硬件。
        Computer^ computer = MonitorGlobal::Instance()->computer;
        bool hardware_changed = false;
        try
        {
            if (computer->IsCpuEnabled != cpuCheck->Checked) { computer->IsCpuEnabled = cpuCheck->Checked; hardware_changed = true; }
            if (computer->IsGpuEnabled != gpuCheck->Checked) { computer->IsGpuEnabled = gpuCheck->Checked; hardware_changed = true; }
            if (computer->IsMotherboardEnabled != motherBoardCheck->Checked) { computer->IsMotherboardEnabled = motherBoardCheck->Checked; hardware_changed = true; }
            if (computer->IsStorageEnabled != storageCheck->Checked) { computer->IsStorageEnabled = storageCheck->Checked; hardware_changed = true; }
            if (computer->IsBatteryEnabled != batteryCheck->Checked) { computer->IsBatteryEnabled = batteryCheck->Checked; hardware_changed = true; }
            if (computer->IsNetworkEnabled != networkCheck->Checked) { computer->IsNetworkEnabled = networkCheck->Checked; hardware_changed = true; }
            if (computer->IsMemoryEnabled != memoryCheck->Checked) { computer->IsMemoryEnabled = memoryCheck->Checked; hardware_changed = true; }
            if (computer->IsControllerEnabled != controllerCheck->Checked) { computer->IsControllerEnabled = controllerCheck->Checked; hardware_changed = true; }
            if (computer->IsPsuEnabled != psuCheck->Checked) { computer->IsPsuEnabled = psuCheck->Checked; hardware_changed = true; }
        }
        catch(System::Exception^)
        {}

        CHardwareMonitor::GetInstance()->m_settings.show_mouse_tooltip = showTooltipCheck->Checked;

        //保存设置
        CHardwareMonitor::GetInstance()->SaveConfig();
        Common::SaveFormSize(this, L"settings");

        //更改了监控的硬件设置后，传感器可能会发生变化，因此这里让传感器索引失效，下一轮刷新时重建
        if (hardware_changed)
            HardwareMonitorHelper::InvalidateSensorIndex();
    }

    void SettingsForm::ListBox_DrawItem(Object^ sender, DrawItemEventArgs^ e)
    {
        //确保index有效
        if (e->Index < 0 || e->Index >= monitorItemListBox->Items->Count)
            return;

        // 获取节点的矩形区域
        Rectangle bounds = e->Bounds;
        // 获取当前项的文本
        ListItemInfo^ listItem = dynamic_cast<ListItemInfo^>(monitorItemListBox->Items[e->Index]);
        if (listItem == nullptr)
            return;
        String^ text = listItem->displayName;

        // 检查是否为选中项
        bool isSelected = (e->State & DrawItemState::Selected) == DrawItemState::Selected;

        // 设置颜色
        System::Drawing::Color textColor;
        System::Drawing::Color valueColor;
        System::Drawing::Color backColor;
        if (isSelected)
        {
            textColor = SystemColors::HighlightText;
            valueColor = SystemColors::HighlightText;
            backColor = SystemColors::Highlight;
        }
        else
        {
            textColor = monitorItemListBox->ForeColor;
            valueColor = SystemColors::Highlight;
            backColor = SystemColors::Window;
        }
        // 绘制背景
        e->Graphics->FillRectangle(gcnew SolidBrush(backColor), e->Bounds);

        //绘制图标
        System::Drawing::Icon^ icon = listItem->icon;
        if (icon != nullptr)
        {
            Point start_pos = bounds.Location;
            int offset = (bounds.Height - icon->Size.Height) / 2;
            start_pos.Offset(offset, offset);
            e->Graphics->DrawIcon(icon, start_pos.X, start_pos.Y);
            bounds.Offset(bounds.Height, 0);
            bounds.Width -= bounds.Height;
        }

        //计算右侧数值部分的宽度
        SizeF rightTextSize = e->Graphics->MeasureString(listItem->displayValue, monitorItemListBox->Font);
        int rightWidth = std::min(bounds.Width, (int)rightTextSize.Width + CHardwareMonitor::GetInstance()->DPI(4));
        Rectangle rightRect = Rectangle(bounds.Right - rightWidth, bounds.Top, rightWidth, bounds.Height);
        Rectangle leftRect = Rectangle(bounds.Left, bounds.Top, bounds.Width - rightWidth, bounds.Height);

        // 绘制数值部分
        TextRenderer::DrawText(e->Graphics, listItem->displayValue, monitorItemListBox->Font, rightRect, valueColor, TextFormatFlags::Right | TextFormatFlags::VerticalCenter);

        // 绘制文本
        TextRenderer::DrawText(e->Graphics, text, monitorItemListBox->Font, leftRect, textColor, TextFormatFlags::VerticalCenter | TextFormatFlags::Left | TextFormatFlags::WordEllipsis);
    }

    System::Void SettingsForm::removeSelectBtn_Click(System::Object^ sender, System::EventArgs^ e)
    {
        int selectedIndex = monitorItemListBox->SelectedIndex;
        // 删除ListBox中的选中项
        if (selectedIndex >= 0)
        {
            // 弹出MessageBox询问用户是否要删除
            System::Windows::Forms::DialogResult result = System::Windows::Forms::MessageBox::Show(
                MonitorGlobal::Instance()->GetString(L"RemoveMonitorItemInquery"),
                MonitorGlobal::Instance()->GetString(L"PluginName"),
                System::Windows::Forms::MessageBoxButtons::OKCancel, 
                System::Windows::Forms::MessageBoxIcon::Question);
            // 如果用户点击“确定”，则删除选中项
            if (result == System::Windows::Forms::DialogResult::OK)
            {
                CHardwareMonitor::GetInstance()->RemoveDisplayItem(selectedIndex);
                //重新生成列表：列表行与配置项是按下标对应的，删除后必须重建，
                //否则后面所有行都会指向错误的配置项
                UpdateItemList();
            }
        }
    }

    System::Void SettingsForm::addItemBtn_Click(System::Object^ sender, System::EventArgs^ e)
    {
        MonitorGlobal::Instance()->ShowHardwareInfoDialog();
        UpdateItemList();
    }

    System::Void SettingsForm::specifyValueWidthCheck_CheckedChanged(System::Object^ sender, System::EventArgs^ e)
    {
        if (IsSelectionValid())
        {
            ItemInfo& item_info = GetSelectedItemInfo();
            item_info.specify_value_width = specifyValueWidthCheck->Checked;
        }
        EnableControls();
    }

    System::Void SettingsForm::valueWidthEdit_ValueChanged(System::Object^ sender, System::EventArgs^ e)
    {
        if (IsSelectionValid())
        {
            ItemInfo& item_info = GetSelectedItemInfo();
            item_info.value_width = static_cast<int>(valueWidthEdit->Value);
        }
    }

    System::Void SettingsForm::unitCombo_SelectedIndexChanged(System::Object^ sender, System::EventArgs^ e)
    {
        if (IsSelectionValid())
        {
            ItemInfo& item_info = GetSelectedItemInfo();
            int index = unitCombo->SelectedIndex;
            if (index >= 0 && index < unitCombo->Items->Count)
            {
                String^ unit = unitCombo->Items[index]->ToString();
                item_info.unit = Common::StringToStdWstring(unit);
            }
        }
    }

    System::Void SettingsForm::moveUpButton_Click(System::Object^ sender, System::EventArgs^ e)
    {
        if (IsSelectionValid())
        {
            int index = monitorItemListBox->SelectedIndex;
            auto& items_info{ CHardwareMonitor::GetInstance()->m_settings.items_info };
            if (index > 0 && index < static_cast<int>(items_info.size()))
            {
                //交换列表中当前项和前一项的文本
                Common::SwapListBoxItems(monitorItemListBox, index, index - 1);
                //交换ItemInfo
                std::swap(items_info[index], items_info[index - 1]);
                //更改选中项
                monitorItemListBox->SelectedIndex--;
            }
        }
    }

    System::Void SettingsForm::moveDownButton_Click(System::Object^ sender, System::EventArgs^ e)
    {
        if (IsSelectionValid())
        {
            int index = monitorItemListBox->SelectedIndex;
            auto& items_info{ CHardwareMonitor::GetInstance()->m_settings.items_info };
            if (index >= 0 && index < static_cast<int>(items_info.size()) - 1)
            {
                //交换列表中当前项和后一项的文本
                Common::SwapListBoxItems(monitorItemListBox, index, index + 1);
                //交换ItemInfo
                std::swap(items_info[index], items_info[index + 1]);
                //更改选中项
                monitorItemListBox->SelectedIndex++;
            }
        }
    }

    System::Void SettingsForm::SettingsForm_Resize(System::Object^ sender, System::EventArgs^ e)
    {
        //这里让monitorItemListBox的下边缘总是和groupBox2的下边缘对齐，以修正在高DPI下ListBox下边缘超出窗口边界的问题
        int listBoxHeight = groupBox2->Bottom - monitorItemListBox->Top;
        monitorItemListBox->Height = listBoxHeight;
        //让monitorItemListBox的右边缘总是跟随groupBox2的左边缘，修正执行显示桌面时ListBox的右边缘超出窗口边界的问题
        int listBoxWidth = groupBox2->Left - monitorItemListBox->Left - CHardwareMonitor::GetInstance()->DPI(12);
        monitorItemListBox->Width = listBoxWidth;

        monitorItemListBox->Invalidate();
    }

    System::Void SettingsForm::showUnitCheck_CheckedChanged(System::Object^ sender, System::EventArgs^ e)
    {
        if (IsSelectionValid())
        {
            ItemInfo& item_info = GetSelectedItemInfo();
            item_info.show_unit = showUnitCheck->Checked;
        }
    }

    System::Void SettingsForm::showNotifyCheck_CheckedChanged(System::Object^ sender, System::EventArgs^ e)
    {
        if (IsSelectionValid())
        {
            ItemInfo& item_info = GetSelectedItemInfo();
            item_info.show_notify = showNotifyCheck->Checked;
        }
        EnableControls();
    }

    System::Void SettingsForm::notifyValueEdit_ValueChanged(System::Object^ sender, System::EventArgs^ e)
    {
        if (IsSelectionValid())
        {
            ItemInfo& item_info = GetSelectedItemInfo();
            item_info.notify_value = static_cast<double>(notifyValueEdit->Value);
        }
    }
}
