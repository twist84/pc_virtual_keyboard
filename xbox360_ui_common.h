#pragma once

#include <windows.h>

// Shared helpers for Xbox 360 Guide-style PC overlays.
// Reference layout space is 1280 x 720; all geometry scales with MulDiv.

namespace xbox360_ui
{
    constexpr int k_reference_width  = 1280;
    constexpr int k_reference_height = 720;

    constexpr COLORREF k_color_key       = RGB(255, 0, 255); // magenta transparency key
    constexpr COLORREF k_panel_fill      = RGB(32, 36, 40);
    constexpr COLORREF k_panel_border    = RGB(70, 76, 82);
    constexpr COLORREF k_header_fill     = RGB(48, 54, 60);
    constexpr COLORREF k_row_fill        = RGB(44, 48, 52);
    constexpr COLORREF k_row_border      = RGB(60, 66, 72);
    constexpr COLORREF k_row_selected    = RGB(70, 110, 40);
    constexpr COLORREF k_row_sel_border  = RGB(110, 160, 60);
    constexpr COLORREF k_text_primary    = RGB(230, 234, 238);
    constexpr COLORREF k_text_secondary  = RGB(140, 148, 156);
    constexpr COLORREF k_text_selected   = RGB(255, 255, 255);
    constexpr COLORREF k_text_sel_meta   = RGB(190, 210, 160);
    constexpr COLORREF k_accent_green    = RGB(90, 176, 54);
    constexpr COLORREF k_accent_yellow   = RGB(214, 161, 28);
    constexpr COLORREF k_accent_red      = RGB(196, 58, 48);
    constexpr COLORREF k_accent_blue     = RGB(46, 113, 176);
    constexpr COLORREF k_section_label   = RGB(120, 160, 80);

    constexpr SHORT k_stick_deadzone     = 7849;
    constexpr UINT  k_controller_timer_id = 1;
    constexpr UINT  k_controller_poll_ms  = 16;

    // --- Scaling (reference 1280x720) ---
    int scale_x(int value, int client_width);
    int scale_y(int value, int client_height);
    RECT scale_rect(RECT rect, int client_width, int client_height);
    int round_radius(int client_width, int client_height);

    // --- GDI drawing ---
    void fill_rect(HDC dc, RECT rect, COLORREF color);
    void fill_round_rect(HDC dc, RECT rect, COLORREF fill, COLORREF border, int radius);
    void draw_circle(HDC dc, RECT rect, COLORREF fill);
    void draw_text(HDC dc, HFONT font, RECT rect, wchar_t const* text, UINT format, COLORREF color);
    void draw_face_badge(HDC dc, HFONT badge_font, RECT rect, wchar_t letter, COLORREF fill);

    // Segoe UI font helper (caller must DeleteObject)
    HFONT make_font(int pixel_height, int weight = FW_NORMAL);

    // Presence status colors (online / away / busy / offline)
    enum class presence
    {
        online,
        away,
        busy,
        offline
    };
    COLORREF presence_color(presence p);

    // Create a full-screen topmost layered popup with color-key transparency.
    // Returns HWND or nullptr. Stores `user_data` via GWLP_USERDATA.
    HWND create_overlay_window(
        wchar_t const* class_name,
        wchar_t const* title,
        WNDPROC window_proc,
        void* user_data,
        HINSTANCE instance = nullptr);

    // Double-buffered paint: creates mem DC, calls paint_fn(mem_dc), blits to window.
    template <typename PaintFn>
    void paint_double_buffered(HWND hwnd, HDC window_dc, PaintFn&& paint_fn)
    {
        RECT client{};
        GetClientRect(hwnd, &client);
        int w = client.right;
        int h = client.bottom;
        HDC mem = CreateCompatibleDC(window_dc);
        HBITMAP bmp = CreateCompatibleBitmap(window_dc, w, h);
        HBITMAP old = static_cast<HBITMAP>(SelectObject(mem, bmp));
        paint_fn(mem);
        BitBlt(window_dc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
    }

    // Footer face-button strip helper
    struct footer_button
    {
        int x_ref;           // left in reference space
        wchar_t letter;
        COLORREF color;
        wchar_t const* label;
    };
    void draw_footer_buttons(
        HDC dc,
        HFONT footer_font,
        HFONT badge_font,
        int client_width,
        int client_height,
        footer_button const* buttons,
        int button_count,
        int y_ref = 660);
}
