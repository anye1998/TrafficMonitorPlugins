// MouseStatus —— 插件设置对话框实现
#include "OptionsDlg.h"

#include <windows.h>
#include <string>

#include "Diag.h"
#include "MouseMonitor.h"

namespace {

constexpr int kCmdToggleLog = 0;
constexpr int kCmdClearLog = 1;
constexpr int kCmdOpenFolder = 2;
constexpr int kCmdDumpNow = 3;

const wchar_t* const kWindowClass = L"MouseStatusOptionsWindow";

// 取本 DLL 的模块句柄（不能依赖 MFC 的 AfxGetInstanceHandle）
char g_options_anchor = 0;

// 子控件
enum : int
{
    IDC_CHK_LOG = 1001,
    IDC_EDIT_PATH,
    IDC_STATIC_SIZE,
    IDC_BTN_OPEN,
    IDC_BTN_CLEAR,
    IDC_BTN_DUMP,
    IDC_BTN_CLOSE,
};

struct OptionsUi
{
    HWND hwnd{ nullptr };
    HWND chk_log{ nullptr };
    HWND edit_path{ nullptr };
    HWND static_size{ nullptr };
    bool initial{ true };
    bool changed{ false };
    bool done{ false };
    int scale{ 100 };   // DPI 缩放百分比
};

int Px(const OptionsUi& ui, int value)
{
    return ::MulDiv(value, ui.scale, 100);
}

std::wstring FormatSize(unsigned long long bytes)
{
    wchar_t buf[64] = { 0 };
    if (bytes < 1024)
        ::swprintf_s(buf, L"%llu 字节", bytes);
    else if (bytes < 1024ull * 1024ull)
        ::swprintf_s(buf, L"%.1f KB", (double)bytes / 1024.0);
    else
        ::swprintf_s(buf, L"%.1f MB", (double)bytes / (1024.0 * 1024.0));
    return std::wstring(buf);
}

void RefreshSizeLabel(OptionsUi* ui)
{
    if (ui == nullptr || ui->static_size == nullptr)
        return;
    const std::wstring text = L"当前日志大小：" + FormatSize(diag::LogFileSize());
    ::SetWindowTextW(ui->static_size, text.c_str());
}

HWND MakeChild(OptionsUi* ui, const wchar_t* cls, const wchar_t* text, DWORD style, DWORD ex_style,
    int x, int y, int w, int h, int id)
{
    HWND child = ::CreateWindowExW(ex_style, cls, text, WS_CHILD | WS_VISIBLE | style,
        Px(*ui, x), Px(*ui, y), Px(*ui, w), Px(*ui, h), ui->hwnd, (HMENU)(INT_PTR)id,
        ::GetModuleHandleW(nullptr), nullptr);
    if (child != nullptr)
    {
        ::SendMessageW(child, WM_SETFONT, (WPARAM)::GetStockObject(DEFAULT_GUI_FONT), TRUE);
    }
    return child;
}

void CreateChildren(OptionsUi* ui)
{
    // 顶部说明
    MakeChild(ui, L"STATIC", L"诊断日志用于排查「读不到电量」这类问题，默认开启。",
        SS_LEFT, 0, 16, 14, 388, 18, -1);

    ui->chk_log = MakeChild(ui, L"BUTTON", L"启用诊断日志",
        BS_AUTOCHECKBOX | WS_TABSTOP, 0, 16, 38, 300, 22, IDC_CHK_LOG);
    if (ui->chk_log != nullptr)
        ::SendMessageW(ui->chk_log, BM_SETCHECK,
            diag::Enabled() ? BST_CHECKED : BST_UNCHECKED, 0);

    MakeChild(ui, L"STATIC", L"日志文件位置：", SS_LEFT, 0, 16, 68, 388, 16, -1);

    ui->edit_path = MakeChild(ui, L"EDIT", diag::LogFilePath().c_str(),
        ES_LEFT | ES_READONLY | WS_TABSTOP, WS_EX_CLIENTEDGE, 16, 86, 388, 24, IDC_EDIT_PATH);

    ui->static_size = MakeChild(ui, L"STATIC", L"", SS_LEFT, 0, 16, 116, 388, 16, IDC_STATIC_SIZE);
    RefreshSizeLabel(ui);

    MakeChild(ui, L"BUTTON", L"打开文件夹", BS_PUSHBUTTON | WS_TABSTOP, 0, 16, 146, 100, 28,
        IDC_BTN_OPEN);
    MakeChild(ui, L"BUTTON", L"清空日志", BS_PUSHBUTTON | WS_TABSTOP, 0, 124, 146, 100, 28,
        IDC_BTN_CLEAR);
    MakeChild(ui, L"BUTTON", L"立即写入诊断", BS_PUSHBUTTON | WS_TABSTOP, 0, 232, 146, 110, 28,
        IDC_BTN_DUMP);
    MakeChild(ui, L"BUTTON", L"关闭", BS_DEFPUSHBUTTON | WS_TABSTOP, 0, 348, 146, 56, 28,
        IDC_BTN_CLOSE);
}

LRESULT CALLBACK OptionsProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    OptionsUi* ui = (OptionsUi*)::GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg)
    {
    case WM_CREATE:
    {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lparam;
        ui = (OptionsUi*)cs->lpCreateParams;
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)ui);
        ui->hwnd = hwnd;
        CreateChildren(ui);
        return 0;
    }
    case WM_COMMAND:
    {
        if (ui == nullptr)
            break;
        const int id = LOWORD(wparam);
        const int code = HIWORD(wparam);

        if (id == IDC_CHK_LOG && code == BN_CLICKED)
        {
            const bool checked = (::SendMessageW(ui->chk_log, BM_GETCHECK, 0, 0) == BST_CHECKED);
            diag::SetEnabled(checked);
            ui->changed = (checked != ui->initial);
            RefreshSizeLabel(ui);
            return 0;
        }
        if (id == IDC_BTN_OPEN)
        {
            diag::OpenLogFolder();
            return 0;
        }
        if (id == IDC_BTN_CLEAR)
        {
            if (::MessageBoxW(hwnd, L"确定要删除日志文件吗？", L"MouseStatus",
                    MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2)
                == IDYES)
            {
                if (!diag::DeleteLogFiles())
                {
                    ::MessageBoxW(hwnd, L"日志文件删除失败，可能正被其它程序打开。", L"MouseStatus",
                        MB_ICONWARNING | MB_OK);
                }
                RefreshSizeLabel(ui);
            }
            return 0;
        }
        if (id == IDC_BTN_DUMP)
        {
            // 日志关着的话先打开，否则写出去什么也看不到
            if (!diag::Enabled())
            {
                diag::SetEnabled(true);
                if (ui->chk_log != nullptr)
                    ::SendMessageW(ui->chk_log, BM_SETCHECK, BST_CHECKED, 0);
                ui->changed = (true != ui->initial);
            }
            CMouseMonitor::Instance().DumpDiagnosticsOnce();
            RefreshSizeLabel(ui);
            ::MessageBoxW(hwnd, L"已把当前设备信息写入日志文件。", L"MouseStatus",
                MB_ICONINFORMATION | MB_OK);
            return 0;
        }
        if (id == IDC_BTN_CLOSE)
        {
            ::DestroyWindow(hwnd);
            return 0;
        }
        break;
    }
    case WM_CLOSE:
        ::DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (ui != nullptr)
            ui->done = true;
        return 0;
    default:
        break;
    }
    return ::DefWindowProcW(hwnd, msg, wparam, lparam);
}

void RegisterClassOnce(HINSTANCE instance)
{
    WNDCLASSEXW wc;
    ::ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    if (::GetClassInfoExW(instance, kWindowClass, &wc))
        return;

    ::ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &OptionsProc;
    wc.hInstance = instance;
    wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = kWindowClass;
    ::RegisterClassExW(&wc);
}

} // namespace

bool ShowMouseStatusOptions(void* parent)
{
    HWND parent_wnd = (HWND)parent;
    if (parent_wnd != nullptr && !::IsWindow(parent_wnd))
        parent_wnd = nullptr;

    // 窗口类必须挂在本 DLL 的模块上
    HMODULE module = nullptr;
    if (!::GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCWSTR)&g_options_anchor, &module))
    {
        module = ::GetModuleHandleW(nullptr);
    }
    RegisterClassOnce(module);

    OptionsUi ui;
    ui.initial = diag::Enabled();
    ui.changed = false;

    HDC screen = ::GetDC(nullptr);
    if (screen != nullptr)
    {
        const int dpi = ::GetDeviceCaps(screen, LOGPIXELSY);
        if (dpi > 0)
            ui.scale = ::MulDiv(dpi, 100, 96);
        ::ReleaseDC(nullptr, screen);
    }
    if (ui.scale < 100)
        ui.scale = 100;

    // 布局按「客户区 420x192」设计，这里换算成含标题栏的整窗尺寸
    const DWORD win_style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    const DWORD win_ex_style = WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT;
    RECT frame = { 0, 0, MulDiv(420, ui.scale, 100), MulDiv(192, ui.scale, 100) };
    ::AdjustWindowRectEx(&frame, win_style, FALSE, win_ex_style);
    const int width = frame.right - frame.left;
    const int height = frame.bottom - frame.top;

    RECT parent_rect;
    ::ZeroMemory(&parent_rect, sizeof(parent_rect));
    if (parent_wnd != nullptr)
        ::GetWindowRect(parent_wnd, &parent_rect);
    else
    {
        parent_rect.left = 0;
        parent_rect.top = 0;
        parent_rect.right = ::GetSystemMetrics(SM_CXSCREEN);
        parent_rect.bottom = ::GetSystemMetrics(SM_CYSCREEN);
    }
    const int x = parent_rect.left + ((parent_rect.right - parent_rect.left) - width) / 2;
    const int y = parent_rect.top + ((parent_rect.bottom - parent_rect.top) - height) / 2;

    HWND hwnd = ::CreateWindowExW(win_ex_style, kWindowClass, L"MouseStatus 设置",
        win_style, x < 0 ? 0 : x, y < 0 ? 0 : y, width, height, parent_wnd,
        nullptr, module, &ui);
    if (hwnd == nullptr)
        return false;

    if (parent_wnd != nullptr)
        ::EnableWindow(parent_wnd, FALSE);

    ::ShowWindow(hwnd, SW_SHOW);
    ::UpdateWindow(hwnd);
    ::SetFocus(hwnd);

    MSG msg;
    bool quit = false;
    while (!ui.done)
    {
        const BOOL got = ::GetMessageW(&msg, nullptr, 0, 0);
        if (got == 0)
        {
            quit = true;   // 收到 WM_QUIT，跳出后重新投递
            break;
        }
        if (got == -1)
            break;

        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE)
        {
            ::DestroyWindow(hwnd);
            continue;
        }
        if (!::IsDialogMessageW(hwnd, &msg))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
    }

    if (parent_wnd != nullptr)
    {
        ::EnableWindow(parent_wnd, TRUE);
        ::SetForegroundWindow(parent_wnd);
    }

    if (quit)
        ::PostQuitMessage((int)msg.wParam);

    return ui.changed;
}
