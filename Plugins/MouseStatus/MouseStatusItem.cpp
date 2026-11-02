// MouseStatus —— 显示项目实现
#include "MouseStatusItem.h"

#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <cwchar>

namespace {

const wchar_t* const kUnknown = L"--";   // 完全没检测到设备
const wchar_t* const kNoReading = L"?";  // 设备在线，但没有任何来源能给出电量

std::wstring FormatBattery(const MouseSnapshot& state)
{
    if (!state.live)
        return kUnknown;
    if (state.battery < 0)
        return kNoReading;
    wchar_t buf[16] = { 0 };
    ::swprintf_s(buf, L"%d%%", state.battery);
    return std::wstring(buf);
}

std::wstring FormatLink(const MouseSnapshot& state)
{
    if (!state.live)
        return kUnknown;
    switch (state.link)
    {
    case LinkKind::Bluetooth:
    case LinkKind::BluetoothLE:
        return L"蓝牙";
    case LinkKind::Wireless24G:
        return L"2.4G";
    case LinkKind::UsbWired:
        return L"USB";
    case LinkKind::UsbUnknown:
        // 判不出是有线还是接收器，但「走的是 USB 接口」这件事是确定的，
        // 不能落到 default 里去显示「--」（那会被读成「没检测到鼠标」）。
        return L"USB";
    default:
        break;
    }
    return kUnknown;
}

std::wstring FormatCharge(const MouseSnapshot& state)
{
    if (!state.live)
        return kUnknown;
    if (state.battery < 0)
        return kNoReading;
    switch (state.charge)
    {
    case ChargeKind::Charging: return L"充电中";
    case ChargeKind::Full: return L"已充满";
    case ChargeKind::Discharging: return L"放电中";
    default: break;
    }
    return kUnknown;
}

// 电池图标尺寸随当前字号等比缩放，保证与状态栏其它文字齐平
void GetIconMetrics(HDC hdc, int& icon_w, int& icon_h)
{
    TEXTMETRICW tm;
    ::ZeroMemory(&tm, sizeof(tm));
    if (!::GetTextMetricsW(hdc, &tm))
    {
        icon_w = 20;
        icon_h = 11;
        return;
    }
    icon_h = std::max(8, (int)((tm.tmHeight * 3) / 5));
    icon_w = (icon_h * 19 + 5) / 10;
}

void DrawBatteryIcon(HDC hdc, int left, int top, int width, int height, int percent,
    COLORREF color, bool charging, COLORREF outline)
{
    if (width < 6 || height < 4)
        return;

    const int cap_w = std::max(2, width / 7);
    const int body_w = width - cap_w - 1;
    const int radius = std::max(2, height / 4);
    const int border = std::max(1, height / 10);

    HPEN pen = ::CreatePen(PS_SOLID, border, color);
    if (pen == nullptr)
        return;
    HGDIOBJ old_pen = ::SelectObject(hdc, pen);
    HGDIOBJ old_brush = ::SelectObject(hdc, ::GetStockObject(NULL_BRUSH));

    // 外壳
    ::RoundRect(hdc, left, top, left + body_w, top + height, radius, radius);

    // 正极触片
    HBRUSH brush = ::CreateSolidBrush(color);
    if (brush != nullptr)
    {
        HGDIOBJ old_b = ::SelectObject(hdc, brush);
        const int cap_h = std::max(2, height / 2);
        ::Rectangle(hdc, left + body_w + 1, top + (height - cap_h) / 2,
            left + body_w + 1 + cap_w, top + (height - cap_h) / 2 + cap_h);

        // 电量填充（未知电量时不填充，只留空壳）
        if (percent >= 0)
        {
            const int inner_left = left + 1 + border;
            const int inner_right = left + body_w - 1 - border;
            const int inner_w = inner_right - inner_left;
            if (inner_w > 0)
            {
                int fill_w = inner_w * std::min(100, std::max(0, percent)) / 100;
                if (fill_w < 1)
                    fill_w = 1;
                ::Rectangle(hdc, inner_left, top + 1 + border, inner_left + fill_w,
                    top + height - 1 - border);
            }
        }
        ::SelectObject(hdc, old_b);
        ::DeleteObject(brush);
    }

    // 充电时在电池里叠一个闪电。只用「变绿」表达充电在深色任务栏和色弱用户眼里都不够明显，
    // 所以给闪电加一圈使用文字色（与背景对比）的描边：压在绿色填充上也不会糊成一团。
    if (charging)
    {
        const int bh = std::max(6, height - 2);
        const int bw = std::max(3, bh * 3 / 5);
        const int x0 = left + (body_w - bw) / 2;
        const int y0 = top + (height - bh) / 2;
        POINT bolt[6] = {
            { x0 + bw * 7 / 10, y0 },
            { x0 + bw / 10, y0 + bh * 55 / 100 },
            { x0 + bw / 2, y0 + bh * 55 / 100 },
            { x0 + bw * 45 / 100, y0 + bh },
            { x0 + bw * 9 / 10, y0 + bh * 45 / 100 },
            { x0 + bw / 2, y0 + bh * 45 / 100 },
        };

        HBRUSH bolt_brush = ::CreateSolidBrush(color);
        HPEN bolt_pen = ::CreatePen(PS_SOLID, 1, outline);
        if (bolt_brush != nullptr && bolt_pen != nullptr)
        {
            HGDIOBJ old_b = ::SelectObject(hdc, bolt_brush);
            HGDIOBJ old_p = ::SelectObject(hdc, bolt_pen);
            ::Polygon(hdc, bolt, 6);
            ::SelectObject(hdc, old_p);
            ::SelectObject(hdc, old_b);
        }
        if (bolt_brush != nullptr)
            ::DeleteObject(bolt_brush);
        if (bolt_pen != nullptr)
            ::DeleteObject(bolt_pen);
    }

    ::SelectObject(hdc, old_brush);
    ::SelectObject(hdc, old_pen);
    ::DeleteObject(pen);
}

void DrawTextPart(HDC hdc, const std::wstring& text, int left, int top, int height,
    COLORREF color, int& out_width)
{
    out_width = 0;
    if (text.empty())
        return;

    SIZE size;
    ::ZeroMemory(&size, sizeof(size));
    if (!::GetTextExtentPoint32W(hdc, text.c_str(), (int)text.size(), &size))
        return;

    RECT rc;
    rc.left = left;
    rc.top = top;
    rc.right = left + size.cx + 2;
    rc.bottom = top + height;
    ::SetTextColor(hdc, color);
    ::DrawTextW(hdc, text.c_str(), (int)text.size(), &rc,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    out_width = size.cx;
}

} // namespace

CMouseStatusItem::CMouseStatusItem()
    : m_kind(Kind::Battery)
{
}

CMouseStatusItem::CMouseStatusItem(Kind kind)
    : m_kind(kind)
{
}

void CMouseStatusItem::Update(const MouseSnapshot& state)
{
    m_state = state;
    m_badge_text = FormatBattery(state);
    m_link_text = FormatLink(state);

    switch (m_kind)
    {
    case Kind::Battery:
        m_text = m_badge_text;
        break;
    case Kind::Link:
        m_text = m_link_text;
        break;
    case Kind::Charge:
        m_text = FormatCharge(state);
        break;
    case Kind::Badge:
        m_text.clear();
        break;
    default:
        break;
    }
}

const wchar_t* CMouseStatusItem::GetItemName() const
{
    switch (m_kind)
    {
    case Kind::Battery: return L"鼠标电量";
    case Kind::Link: return L"鼠标连接方式";
    case Kind::Charge: return L"鼠标充电状态";
    case Kind::Badge: return L"鼠标状态";
    default: break;
    }
    return L"";
}

const wchar_t* CMouseStatusItem::GetItemId() const
{
    switch (m_kind)
    {
    case Kind::Battery: return L"MouseStatusBattery";
    case Kind::Link: return L"MouseStatusLink";
    case Kind::Charge: return L"MouseStatusCharge";
    case Kind::Badge: return L"MouseStatusBadge";
    default: break;
    }
    return L"";
}

const wchar_t* CMouseStatusItem::GetItemLableText() const
{
    return L"";
}

const wchar_t* CMouseStatusItem::GetItemValueText() const
{
    return m_text.c_str();
}

const wchar_t* CMouseStatusItem::GetItemValueSampleText() const
{
    switch (m_kind)
    {
    case Kind::Battery: return L"100%";
    case Kind::Link: return L"2.4G";
    case Kind::Charge: return L"充电中";
    case Kind::Badge: return L"100% 2.4G";
    default: break;
    }
    return L"";
}

bool CMouseStatusItem::IsCustomDraw() const
{
    return m_kind == Kind::Badge;
}

int CMouseStatusItem::GetItemWidthEx(void* hDC) const
{
    if (m_kind != Kind::Badge)
        return 0;

    HDC hdc = (HDC)hDC;
    if (hdc == nullptr)
        return 60;

    int icon_w = 20;
    int icon_h = 11;
    GetIconMetrics(hdc, icon_w, icon_h);

    TEXTMETRICW tm;
    ::ZeroMemory(&tm, sizeof(tm));
    if (!::GetTextMetricsW(hdc, &tm))
        return icon_w + 40;
    const int gap = std::max(2, (int)(tm.tmHeight / 6));

    SIZE size_percent;
    SIZE size_link;
    ::ZeroMemory(&size_percent, sizeof(size_percent));
    ::ZeroMemory(&size_link, sizeof(size_link));
    ::GetTextExtentPoint32W(hdc, m_badge_text.c_str(), (int)m_badge_text.size(), &size_percent);
    ::GetTextExtentPoint32W(hdc, m_link_text.c_str(), (int)m_link_text.size(), &size_link);

    return icon_w + gap + size_percent.cx + gap + size_link.cx;
}

void CMouseStatusItem::DrawItem(void* hDC, int x, int y, int w, int h, bool dark_mode)
{
    (void)dark_mode;

    HDC hdc = (HDC)hDC;
    if (hdc == nullptr || h < 2)
        return;

    TEXTMETRICW tm;
    ::ZeroMemory(&tm, sizeof(tm));
    if (!::GetTextMetricsW(hdc, &tm))
        return;

    int icon_w = 20;
    int icon_h = 11;
    GetIconMetrics(hdc, icon_w, icon_h);
    const int gap = std::max(2, (int)(tm.tmHeight / 6));
    const int center_y = y + h / 2;

    const COLORREF text_color = ::GetTextColor(hdc);
    COLORREF icon_color = text_color;
    if (m_state.live && m_state.battery >= 0)
    {
        if (m_state.charge == ChargeKind::Charging || m_state.charge == ChargeKind::Full)
            icon_color = RGB(46, 170, 92);      // 供电/充电中：绿色
        else if (m_state.battery <= 20)
            icon_color = RGB(220, 68, 68);      // 低电量：红色
    }

    const int old_mode = ::SetBkMode(hdc, TRANSPARENT);

    const int battery = m_state.live ? m_state.battery : -1;
    const bool charging = m_state.live && m_state.battery >= 0
        && m_state.charge == ChargeKind::Charging;
    DrawBatteryIcon(hdc, x, center_y - icon_h / 2, icon_w, icon_h, battery, icon_color,
        charging, text_color);

    int left = x + icon_w + gap;
    int drawn = 0;
    DrawTextPart(hdc, m_badge_text, left, center_y - tm.tmHeight / 2, tm.tmHeight + 1,
        text_color, drawn);
    left += drawn + gap;
    DrawTextPart(hdc, m_link_text, left, center_y - tm.tmHeight / 2, tm.tmHeight + 1,
        text_color, drawn);

    ::SetBkMode(hdc, old_mode);
}
