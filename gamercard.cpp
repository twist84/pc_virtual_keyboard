#include <windows.h>
#include <windowsx.h>
#include <xinput.h>
#include <wchar.h>
#include <string>
#include <vector>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "msimg32.lib")
#pragma comment(lib, "xinput.lib")

namespace
{
    constexpr wchar_t k_window_class_name[] = L"pc_xbox360_gamercard";

    constexpr int k_reference_width = 1280;
    constexpr int k_reference_height = 720;

    constexpr int k_panel_left = 300;
    constexpr int k_panel_top = 60;
    constexpr int k_panel_right = 980;
    constexpr int k_panel_bottom = 640;

    constexpr UINT k_controller_timer_id = 1;
    constexpr UINT k_controller_poll_ms = 16;

    struct s_achievement
    {
        std::wstring name;
        std::wstring game;
        int gamerscore = 0;
    };

    int scale_x(int value, int width)
    {
        return MulDiv(value, width, k_reference_width);
    }

    int scale_y(int value, int height)
    {
        return MulDiv(value, height, k_reference_height);
    }

    RECT scale_rect(RECT rect, int width, int height)
    {
        return RECT{
            scale_x(rect.left, width),
            scale_y(rect.top, height),
            scale_x(rect.right, width),
            scale_y(rect.bottom, height)};
    }

    int round_radius(int width, int height)
    {
        return __max(2, scale_y(4, height));
    }

    class c_gamercard_window
    {
    public:
        c_gamercard_window()
        {
            m_gamertag = L"Twister";
            m_motto = L"Finish the fight.";
            m_location = L"Seattle, WA";
            m_bio = L"Xbox Live since day one. Halo, Gears, and too much Arcade.";
            m_gamerscore = 34200;
            m_reputation = L"Good";
            m_zone = L"Family";
            m_member_since = L"November 2005";
            m_games_played = 87;
            m_achievements_unlocked = 412;

            m_recent = {
                { L"Completed Campaign",     L"Halo 3",           125 },
                { L"Around the World",       L"Gears of War 2",    20 },
                { L"Zombie Genocide Master", L"Left 4 Dead",       50 },
                { L"The Final Showdown",     L"Castle Crashers",   15 },
            };
        }

        bool create()
        {
            bool result = false;

            WNDCLASSEXW window_class{};
            window_class.cbSize = sizeof(window_class);
            window_class.hInstance = GetModuleHandleW(nullptr);
            window_class.lpfnWndProc = &c_gamercard_window::static_window_proc;
            window_class.lpszClassName = k_window_class_name;
            window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            window_class.hbrBackground = nullptr;

            if (RegisterClassExW(&window_class) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS)
            {
                int width = GetSystemMetrics(SM_CXSCREEN);
                int height = GetSystemMetrics(SM_CYSCREEN);

                m_handle = CreateWindowExW(
                    WS_EX_APPWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED,
                    k_window_class_name,
                    L"Gamercard",
                    WS_POPUP,
                    0, 0, width, height,
                    nullptr, nullptr,
                    GetModuleHandleW(nullptr),
                    this);

                if (m_handle != nullptr)
                {
                    SetLayeredWindowAttributes(m_handle, RGB(0, 0, 0), 0, LWA_COLORKEY);
                    create_fonts();
                    ShowWindow(m_handle, SW_SHOW);
                    UpdateWindow(m_handle);
                    SetFocus(m_handle);
                    SetTimer(m_handle, k_controller_timer_id, k_controller_poll_ms, nullptr);
                    result = true;
                }
            }

            return result;
        }

        void close()
        {
            if (m_handle)
            {
                DestroyWindow(m_handle);
                PostQuitMessage(0);
            }
        }

        static LRESULT CALLBACK static_window_proc(HWND window_handle, UINT message, WPARAM w_param, LPARAM l_param)
        {
            c_gamercard_window* instance =
                reinterpret_cast<c_gamercard_window*>(GetWindowLongPtrW(window_handle, GWLP_USERDATA));

            if (message == WM_NCCREATE)
            {
                CREATESTRUCTW* create = reinterpret_cast<CREATESTRUCTW*>(l_param);
                instance = static_cast<c_gamercard_window*>(create->lpCreateParams);
                SetWindowLongPtrW(window_handle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(instance));
                instance->m_handle = window_handle;
            }

            if (instance != nullptr)
                return instance->window_proc(message, w_param, l_param);

            return DefWindowProcW(window_handle, message, w_param, l_param);
        }

    private:
        HFONT make_font(int pixel_height, int weight = FW_NORMAL)
        {
            return CreateFontW(
                pixel_height, 0, 0, 0, weight,
                FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS,
                L"Segoe UI");
        }

        void create_fonts()
        {
            int height = GetClientRectHeight();
            m_title_font   = make_font(scale_y(32, height), FW_SEMIBOLD);
            m_header_font  = make_font(scale_y(15, height));
            m_body_font    = make_font(scale_y(16, height));
            m_small_font   = make_font(scale_y(13, height));
            m_score_font   = make_font(scale_y(36, height), FW_BOLD);
            m_footer_font  = make_font(scale_y(16, height));
            m_badge_font   = make_font(scale_y(12, height), FW_SEMIBOLD);
            m_section_font = make_font(scale_y(14, height), FW_SEMIBOLD);
        }

        void release_fonts()
        {
            HFONT fonts[] = {
                m_title_font, m_header_font, m_body_font, m_small_font,
                m_score_font, m_footer_font, m_badge_font, m_section_font
            };
            for (HFONT f : fonts)
                if (f) DeleteObject(f);
        }

        int GetClientRectWidth() const
        {
            RECT r{};
            GetClientRect(m_handle, &r);
            return r.right;
        }

        int GetClientRectHeight() const
        {
            RECT r{};
            GetClientRect(m_handle, &r);
            return r.bottom;
        }

        void fill_rect(HDC dc, RECT rect, COLORREF color) const
        {
            HBRUSH brush = CreateSolidBrush(color);
            FillRect(dc, &rect, brush);
            DeleteObject(brush);
        }

        void fill_round_rect(HDC dc, RECT rect, COLORREF fill, COLORREF border, int radius) const
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

        void draw_text(HDC dc, HFONT font, RECT rect, wchar_t const* text, UINT format, COLORREF color) const
        {
            HFONT old = static_cast<HFONT>(SelectObject(dc, font));
            COLORREF old_color = SetTextColor(dc, color);
            int old_mode = SetBkMode(dc, TRANSPARENT);
            DrawTextW(dc, text ? text : L"", -1, &rect, format);
            SetBkMode(dc, old_mode);
            SetTextColor(dc, old_color);
            SelectObject(dc, old);
        }

        void draw_circle(HDC dc, RECT rect, COLORREF fill) const
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

        void draw_face_badge(HDC dc, RECT rect, wchar_t letter, COLORREF fill)
        {
            draw_circle(dc, rect, fill);
            wchar_t buf[2]{ letter, L'\0' };
            draw_text(dc, m_badge_font, rect, buf, DT_CENTER | DT_VCENTER | DT_SINGLELINE, RGB(255, 255, 255));
        }

        void draw_gamerpic(HDC dc, RECT rect)
        {
            // Placeholder gamerpic - green circle with initials
            fill_round_rect(dc, rect, RGB(55, 85, 45), RGB(90, 140, 60), scale_y(6, GetClientRectHeight()));

            wchar_t initials[3]{};
            size_t n = 0;
            for (wchar_t c : m_gamertag)
            {
                if ((c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z'))
                {
                    initials[n++] = (c >= L'a' && c <= L'z') ? static_cast<wchar_t>(c - 32) : c;
                    if (n >= 2) break;
                }
            }
            draw_text(dc, m_title_font, rect, initials,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE, RGB(200, 230, 160));
        }

        void paint(HDC dc)
        {
            RECT client{};
            GetClientRect(m_handle, &client);
            int width = client.right;
            int height = client.bottom;

            fill_rect(dc, client, RGB(0, 0, 0));

            RECT panel = scale_rect(
                RECT{ k_panel_left, k_panel_top, k_panel_right, k_panel_bottom },
                width, height);
            fill_round_rect(dc, panel, RGB(32, 36, 40), RGB(70, 76, 82), round_radius(width, height) * 2);

            // Header bar
            RECT header = panel;
            header.bottom = panel.top + scale_y(52, height);
            fill_rect(dc, header, RGB(48, 54, 60));
            draw_text(dc, m_title_font,
                RECT{ header.left + scale_x(20, width), header.top,
                      header.right - scale_x(20, width), header.bottom },
                L"Gamercard", DT_LEFT | DT_VCENTER | DT_SINGLELINE, RGB(240, 242, 244));

            // --- Top section: gamerpic + identity ---
            int pic_size = scale_y(110, height);
            RECT pic{
                panel.left + scale_x(24, width),
                header.bottom + scale_y(20, height),
                panel.left + scale_x(24, width) + pic_size,
                header.bottom + scale_y(20, height) + pic_size
            };
            draw_gamerpic(dc, pic);

            // Gamertag
            RECT tag_rect{
                pic.right + scale_x(20, width),
                pic.top,
                panel.right - scale_x(24, width),
                pic.top + scale_y(36, height)
            };
            draw_text(dc, m_title_font, tag_rect, m_gamertag.c_str(),
                DT_LEFT | DT_VCENTER | DT_SINGLELINE, RGB(255, 255, 255));

            // Motto
            RECT motto_rect{
                pic.right + scale_x(20, width),
                tag_rect.bottom,
                panel.right - scale_x(24, width),
                tag_rect.bottom + scale_y(24, height)
            };
            draw_text(dc, m_body_font, motto_rect, m_motto.c_str(),
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, RGB(180, 190, 160));

            // Location + member since
            RECT loc_rect{
                pic.right + scale_x(20, width),
                motto_rect.bottom + scale_y(4, height),
                panel.right - scale_x(24, width),
                motto_rect.bottom + scale_y(28, height)
            };
            std::wstring loc_line = m_location + L"  |  Member since " + m_member_since;
            draw_text(dc, m_small_font, loc_rect, loc_line.c_str(),
                DT_LEFT | DT_VCENTER | DT_SINGLELINE, RGB(140, 148, 156));

            // --- Stats row ---
            int stats_top = pic.bottom + scale_y(18, height);
            int stats_h = scale_y(72, height);
            int stats_gap = scale_x(12, width);
            int stats_w = (panel.right - panel.left - scale_x(48, width) - 3 * stats_gap) / 4;

            struct s_stat { wchar_t const* label; std::wstring value; };
            s_stat stats[] = {
                { L"Gamerscore", std::to_wstring(m_gamerscore) + L" G" },
                { L"Reputation", m_reputation },
                { L"Zone",       m_zone },
                { L"Games",      std::to_wstring(m_games_played) },
            };

            for (int i = 0; i < 4; ++i)
            {
                RECT box{
                    panel.left + scale_x(24, width) + i * (stats_w + stats_gap),
                    stats_top,
                    panel.left + scale_x(24, width) + i * (stats_w + stats_gap) + stats_w,
                    stats_top + stats_h
                };
                fill_round_rect(dc, box, RGB(44, 48, 52), RGB(60, 66, 72), round_radius(width, height));

                RECT label_r{ box.left, box.top + scale_y(8, height), box.right, box.top + scale_y(28, height) };
                draw_text(dc, m_small_font, label_r, stats[i].label,
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE, RGB(140, 148, 156));

                RECT value_r{ box.left, box.top + scale_y(28, height), box.right, box.bottom - scale_y(8, height) };
                draw_text(dc, m_body_font, value_r, stats[i].value.c_str(),
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE, RGB(230, 234, 238));
            }

            // --- Bio ---
            int bio_top = stats_top + stats_h + scale_y(16, height);
            RECT bio_label{
                panel.left + scale_x(24, width),
                bio_top,
                panel.right - scale_x(24, width),
                bio_top + scale_y(22, height)
            };
            draw_text(dc, m_section_font, bio_label, L"BIO",
                DT_LEFT | DT_VCENTER | DT_SINGLELINE, RGB(120, 160, 80));

            RECT bio_body{
                panel.left + scale_x(24, width),
                bio_label.bottom + scale_y(4, height),
                panel.right - scale_x(24, width),
                bio_label.bottom + scale_y(50, height)
            };
            draw_text(dc, m_body_font, bio_body, m_bio.c_str(),
                DT_LEFT | DT_WORDBREAK, RGB(200, 206, 212));

            // --- Recent achievements ---
            int ach_top = bio_body.bottom + scale_y(12, height);
            RECT ach_label{
                panel.left + scale_x(24, width),
                ach_top,
                panel.right - scale_x(24, width),
                ach_top + scale_y(22, height)
            };
            draw_text(dc, m_section_font, ach_label, L"RECENT ACHIEVEMENTS",
                DT_LEFT | DT_VCENTER | DT_SINGLELINE, RGB(120, 160, 80));

            int row_h = scale_y(36, height);
            for (size_t i = 0; i < m_recent.size(); ++i)
            {
                int y = ach_label.bottom + scale_y(6, height) + static_cast<int>(i) * row_h;
                RECT row{
                    panel.left + scale_x(24, width),
                    y,
                    panel.right - scale_x(24, width),
                    y + row_h - scale_y(4, height)
                };
                fill_round_rect(dc, row, RGB(44, 48, 52), RGB(60, 66, 72), round_radius(width, height));

                // Achievement name
                RECT name_r{
                    row.left + scale_x(12, width),
                    row.top,
                    row.right - scale_x(140, width),
                    row.bottom
                };
                draw_text(dc, m_body_font, name_r, m_recent[i].name.c_str(),
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, RGB(230, 234, 238));

                // Game + score
                wchar_t right[64];
                swprintf_s(right, L"%s  +%d G", m_recent[i].game.c_str(), m_recent[i].gamerscore);
                RECT right_r{
                    row.right - scale_x(200, width),
                    row.top,
                    row.right - scale_x(12, width),
                    row.bottom
                };
                draw_text(dc, m_small_font, right_r, right,
                    DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, RGB(140, 160, 120));
            }

            // Footer
            int badge_size = __max(18, scale_y(22, height));
            RECT footer_b{
                scale_x(520, width), scale_y(660, height),
                scale_x(680, width), scale_y(700, height)
            };
            RECT b_badge{
                footer_b.left,
                footer_b.top + (footer_b.bottom - footer_b.top - badge_size) / 2,
                footer_b.left + badge_size,
                footer_b.top + (footer_b.bottom - footer_b.top - badge_size) / 2 + badge_size
            };
            draw_face_badge(dc, b_badge, L'B', RGB(196, 58, 48));
            RECT b_label = footer_b;
            b_label.left = b_badge.right + scale_x(8, width);
            draw_text(dc, m_footer_font, b_label, L"Back", DT_LEFT | DT_VCENTER | DT_SINGLELINE, RGB(230, 232, 234));
        }

        void poll_controller()
        {
            XINPUT_STATE state{};
            if (XInputGetState(0, &state) != ERROR_SUCCESS)
            {
                m_previous_buttons = 0;
                return;
            }

            WORD buttons = state.Gamepad.wButtons;
            WORD pressed = static_cast<WORD>(buttons & ~m_previous_buttons);
            m_previous_buttons = buttons;

            if (pressed & XINPUT_GAMEPAD_B)
                close();
        }

        LRESULT window_proc(UINT message, WPARAM w_param, LPARAM l_param)
        {
            switch (message)
            {
            case WM_PAINT:
            {
                PAINTSTRUCT ps{};
                HDC dc = BeginPaint(m_handle, &ps);
                RECT client{};
                GetClientRect(m_handle, &client);
                int w = client.right, h = client.bottom;

                HDC mem = CreateCompatibleDC(dc);
                HBITMAP bmp = CreateCompatibleBitmap(dc, w, h);
                HBITMAP old = static_cast<HBITMAP>(SelectObject(mem, bmp));
                paint(mem);
                BitBlt(dc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
                SelectObject(mem, old);
                DeleteObject(bmp);
                DeleteDC(mem);
                EndPaint(m_handle, &ps);
                return 0;
            }

            case WM_ERASEBKGND:
                return TRUE;

            case WM_KEYDOWN:
                if (w_param == VK_ESCAPE || w_param == VK_BACK)
                    close();
                return 0;

            case WM_TIMER:
                if (w_param == k_controller_timer_id)
                    poll_controller();
                return 0;

            case WM_CLOSE:
                close();
                return 0;

            case WM_NCDESTROY:
                KillTimer(m_handle, k_controller_timer_id);
                release_fonts();
                return 0;
            }

            return DefWindowProcW(m_handle, message, w_param, l_param);
        }

        HWND m_handle = nullptr;
        HFONT m_title_font = nullptr;
        HFONT m_header_font = nullptr;
        HFONT m_body_font = nullptr;
        HFONT m_small_font = nullptr;
        HFONT m_score_font = nullptr;
        HFONT m_footer_font = nullptr;
        HFONT m_badge_font = nullptr;
        HFONT m_section_font = nullptr;

        std::wstring m_gamertag;
        std::wstring m_motto;
        std::wstring m_location;
        std::wstring m_bio;
        int m_gamerscore = 0;
        std::wstring m_reputation;
        std::wstring m_zone;
        std::wstring m_member_since;
        int m_games_played = 0;
        int m_achievements_unlocked = 0;
        std::vector<s_achievement> m_recent;

        WORD m_previous_buttons = 0;
    };
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    c_gamercard_window window;
    if (!window.create())
        return 1;

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
