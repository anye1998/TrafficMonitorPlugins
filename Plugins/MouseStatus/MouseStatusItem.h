// MouseStatus —— 显示项目
#pragma once

#include <string>
#include "PluginInterface.h"
#include "MouseTypes.h"

class CMouseStatusItem : public IPluginItem
{
public:
    enum class Kind
    {
        Battery = 0,    // 纯文本：电量
        Link,           // 纯文本：连接方式
        Charge,         // 纯文本：充电状态
        Badge,          // 自绘：电池图标 + 电量 + 连接方式
        Count
    };

    CMouseStatusItem();
    explicit CMouseStatusItem(Kind kind);

    // 由插件在构造后设置显示项类型
    void SetKind(Kind kind) { m_kind = kind; }

    // 由插件在 DataRequired 中调用，避免在绘制时做任何耗时操作
    void Update(const MouseSnapshot& state);

    virtual const wchar_t* GetItemName() const override;
    virtual const wchar_t* GetItemId() const override;
    virtual const wchar_t* GetItemLableText() const override;
    virtual const wchar_t* GetItemValueText() const override;
    virtual const wchar_t* GetItemValueSampleText() const override;
    virtual bool IsCustomDraw() const override;
    virtual int GetItemWidthEx(void* hDC) const override;
    virtual void DrawItem(void* hDC, int x, int y, int w, int h, bool dark_mode) override;

private:
    Kind m_kind;
    MouseSnapshot m_state;
    std::wstring m_text;        // 纯文本项目的显示内容
    std::wstring m_badge_text;  // 自绘项目里的电量文本
    std::wstring m_link_text;   // 自绘项目里的连接方式文本
};
