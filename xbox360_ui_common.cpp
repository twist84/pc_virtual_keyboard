#include "xbox360_ui_common.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "msimg32.lib")

namespace xbox360_ui
{
    int scale_x(int value, int client_width)
    {
        return MulDiv(value, client_width, k_reference_width);
    }

    int scale_y(int value, int client_height)
    {
        return MulDiv(value, client_height, k_reference_height);
    }

    RECT scale_rect(RECT rect, int client_width, int client_height)
    {
        return RECT{
            scale_x(rect.left, client_width),
            scale_y(rect.top, client_height),
            scale_x(rect.right, client_width),
            scale_y(rect.bottom, client_height)};
    }

    int round_radius(int client_width, int client_height)
    {
        (void)client_width;
        int r = scale_y(4, client_height);
        return r < 2 ? 2 : r;
    }

    void fill_rect(HDC dc, RECT rect, COLORREF color)
    {
        HBRUSH brush = CreateSolidBrush(color);
        FillRect(dc, &rect, brush);
        DeleteObject(brush);
    }

    void fill_round_rect(HDC dc, RECT rect, COLORREF fill, COLORREF border, int radius)
    {
        HBRUSH brush = CreateSolidBrush(fill);
        HPEN pen = CreatePen(PS_SOLID, 1, border);
        HBRUSH old_brush = static_cast<HBRUSH>(SelectObject(dc, brush));
        HPEN old_pen = static_cast<HPEN>(SelectObject(dc, pen));
        RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
        SelectObject(dc, old_brush);
        SelectObject(dc, old_pen);
        DeleteObject(pen);
        DeleteObject(brush);
    }

    void draw_circle(HDC dc, RECT rect, COLORREF fill)
    {
        HBRUSH brush = CreateSolidBrush(fill);
        HPEN pen = CreatePen(PS_SOLID, 1, fill);
        HBRUSH old_brush = static_cast<HBRUSH>(SelectObject(dc, brush));
        HPEN old_pen = static_cast<HPEN>(SelectObject(dc, pen));
        Ellipse(dc, rect.left, rect.top, rect.right, rect.bottom);
        SelectObject(dc, old_pen);
        SelectObject(dc, old_brush);
        DeleteObject(pen);
        DeleteObject(brush);
    }

    void draw_text(HDC dc, HFONT font, RECT rect, wchar_t const* text, UINT format, COLORREF color)
    {
        HFONT old = static_cast<HFONT>(SelectObject(dc, font));
        COLORREF old_color = SetTextColor(dc, color);
        int old_mode = SetBkMode(dc, TRANSPARENT);
        DrawTextW(dc, text ? text : L"", -1, &rect, format);
        SetBkMode(dc, old_mode);
        SetTextColor(dc, old_color);
        SelectObject(dc, old);
    }

    void draw_face_badge(HDC dc, HFONT badge_font, RECT rect, wchar_t letter, COLORREF fill)
    {
        draw_circle(dc, rect, fill);
        wchar_t buf[2]{ letter, L'\0' };
        draw_text(dc, badge_font, rect, buf, DT_CENTER | DT_VCENTER | DT_SINGLELINE, RGB(255, 255, 255));
    }

    HFONT make_font(int pixel_height, int weight)
    {
        return CreateFontW(
            pixel_height, 0, 0, 0, weight,
            FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS,
            L"Segoe UI");
    }

    COLORREF presence_color(presence p)
    {
        switch (p)
        {
        case presence::online:  return k_accent_green;
        case presence::away:    return k_accent_yellow;
        case presence::busy:    return k_accent_red;
        default:                return RGB(120, 124, 128);
        }
    }

    HWND create_overlay_window(
        wchar_t const* class_name,
        wchar_t const* title,
        WNDPROC window_proc,
        void* user_data,
        HINSTANCE instance)
    {
        if (instance == nullptr)
            instance = GetModuleHandleW(nullptr);

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.hInstance = instance;
        wc.lpfnWndProc = window_proc;
        wc.lpszClassName = class_name;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;

        if (RegisterClassExW(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return nullptr;

        int width = GetSystemMetrics(SM_CXSCREEN);
        int height = GetSystemMetrics(SM_CYSCREEN);

        HWND hwnd = CreateWindowExW(
            WS_EX_APPWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED,
            class_name,
            title,
            WS_POPUP,
            0, 0, width, height,
            nullptr, nullptr,
            instance,
            user_data);

        if (hwnd != nullptr)
            SetLayeredWindowAttributes(hwnd, k_color_key, 0, LWA_COLORKEY);

        return hwnd;
    }

    void draw_footer_buttons(
        HDC dc,
        HFONT footer_font,
        HFONT badge_font,
        int client_width,
        int client_height,
        footer_button const* buttons,
        int button_count,
        int y_ref)
    {
        int badge_size = scale_y(22, client_height);
        if (badge_size < 18) badge_size = 18;

        for (int i = 0; i < button_count; ++i)
        {
            footer_button const& b = buttons[i];
            RECT area{
                scale_x(b.x_ref, client_width),
                scale_y(y_ref, client_height),
                scale_x(b.x_ref + 150, client_width),
                scale_y(y_ref + 40, client_height)
            };
            RECT badge{
                area.left,
                area.top + (area.bottom - area.top - badge_size) / 2,
                area.left + badge_size,
                area.top + (area.bottom - area.top - badge_size) / 2 + badge_size
            };
            draw_face_badge(dc, badge_font, badge, b.letter, b.color);
            RECT label = area;
            label.left = badge.right + scale_x(8, client_width);
            draw_text(dc, footer_font, label, b.label,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE, k_text_primary);
        }
    }
}
