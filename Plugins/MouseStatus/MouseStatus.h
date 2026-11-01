// MouseStatus —— 插件主体
#pragma once

#include <windows.h>
#include <string>

#include "PluginInterface.h"
#include "MouseStatusItem.h"
#include "MouseTypes.h"

class CMouseStatus : public ITMPlugin
{
public:
    static CMouseStatus& Instance();

    virtual IPluginItem* GetItem(int index) override;
    virtual const wchar_t* GetTooltipInfo() override;
    virtual void DataRequired() override;
    virtual const wchar_t* GetInfo(PluginInfoIndex index) override;
    virtual void OnInitialize(ITrafficMonitor* pApp) override;
    virtual void* GetPluginIcon() override;

private:
    CMouseStatus();
    ~CMouseStatus();
    CMouseStatus(const CMouseStatus&) = delete;
    CMouseStatus& operator=(const CMouseStatus&) = delete;

    void EnsureStarted();
    void BuildTooltip(const MouseSnapshot& state);

    static CMouseStatus m_instance;

    CMouseStatusItem m_items[4];
    std::wstring m_tooltip;
    bool m_started{ false };
    ITrafficMonitor* m_app{ nullptr };
    HICON m_icon{ nullptr };
};

#ifdef __cplusplus
extern "C" {
#endif
    __declspec(dllexport) ITMPlugin* TMPluginGetInstance();

#ifdef __cplusplus
}
#endif
