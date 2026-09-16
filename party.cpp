#include <windows.h>
#include <windowsx.h>
#include <xinput.h>
#include <wchar.h>
#include <algorithm>
#include <string>
#include <vector>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "msimg32.lib")
#pragma comment(lib, "xinput.lib")

namespace
{
    constexpr wchar_t k_window_class_name[] = L"pc_xbox360_party";

    constexpr int k_reference_width = 1280;
    constexpr int k_reference_height = 720;

    constexpr int k_panel_left = 300;
    constexpr int k_panel_top = 60;
    constexpr int k_panel_right = 980;
    constexpr int k_panel_bottom = 640;

    constexpr int k_header_height = 52;
    constexpr int k_row_height = 56;
    constexpr int k_visible_rows = 7;
    constexpr int k_list_top = 200;

    constexpr UINT k_controller_timer_id = 1;
    constexpr UINT k_controller_poll_ms = 16;
    constexpr SHORT k_stick_deadzone = 7849;

    enum class e_member_status
    {
        leader,
        in_party,
        joining,
        invitable
    };

    struct s_member
    {
        std::wstring gamertag;
        e_member_status status = e_member_status::in_party;
        std::wstring activity;   // "In party chat", "Playing Halo 3", etc.
        bool talking = false;
        bool muted = false;
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

    wchar_t const* status_label(e_member_status s)
    {
        switch (s)
        {
        case e_member_status::leader:    return L"Party Leader";
        case e_member_status::in_party:  return L"In Party";
        case e_member_status::joining:   return L"Joining...";
        case e_member_status::invitable: return L"Invite";
        default:                         return L"";
        }
    }

    class c_party_window
    {
    public:
        c_party_window()
        {
            // Demo party: you + a few friends, plus some invitable online friends
            m_members = {
                { L"Twister",     e_member_status::leader,    L"In party chat",           true,  false },
                { L"MajorNelson", e_member_status::in_party,  L"Playing Gears of War 2",  false, false },
                { L"LarryHryb",   e_member_status::in_party,  L"In party chat",           true,  false },
                { L"Rareware",    e_member_status::joining,   L"Connecting...",           false, false },
                { L"Bungie",      e_member_status::invitable, L"Online",                  false, false },
                { L"EpicGames",   e_member_status::invitable, L"Away",                    false, false },
            };
            m_party_active = true;
            m_max_members = 8;
        }

        bool create()
        {
            bool result = false;

            WNDCLASSEXW window_class{};
            window_class.cbSize = sizeof(window_class);
            window_class.hInstance = GetModuleHandleW(nullptr);
            window_class.lpfnWndProc = &c_party_window::static_window_proc;
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
                    L"Party",
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
            c_party_window* instance =
                reinterpret_cast<c_party_window*>(GetWindowLongPtrW(window_handle, GWLP_USERDATA));

            if (message == WM_NCCREATE)
            {
                CREATESTRUCTW* create = reinterpret_cast<CREATESTRUCTW*>(l_param);
                instance = static_cast<c_party_window*>(create->lpCreateParams);
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
            m_title_font  = make_font(scale_y(28, height), FW_SEMIBOLD);
            m_header_font = make_font(scale_y(15, height));
            m_row_font    = make_font(scale_y(17, height));
            m_meta_font   = make_font(scale_y(13, height));
            m_footer_font = make_font(scale_y(16, height));
            m_badge_font  = make_font(scale_y(12, height), FW_SEMIBOLD);
        }

        void release_fonts()
        {
            HFONT fonts[] = {
                m_title_font, m_header_font, m_row_font,
                m_meta_font, m_footer_font, m_badge_font
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

        int party_count() const
        {
            int n = 0;
            for (auto const& m : m_members)
                if (m.status == e_member_status::leader ||
                    m.status == e_member_status::in_party ||
                    m.status == e_member_status::joining)
                    ++n;
            return n;
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

            // Header
            RECT header = panel;
            header.bottom = panel.top + scale_y(k_header_height, height);
            fill_rect(dc, header, RGB(48, 54, 60));

            draw_text(dc, m_title_font,
                RECT{ header.left + scale_x(20, width), header.top,
                      header.right - scale_x(20, width), header.bottom },
                L"Party", DT_LEFT | DT_VCENTER | DT_SINGLELINE, RGB(240, 242, 244));

            wchar_t count_buf[32];
            swprintf_s(count_buf, L"%d / %d", party_count(), m_max_members);
            draw_text(dc, m_header_font,
                RECT{ header.left + scale_x(20, width), header.top,
                      header.right - scale_x(20, width), header.bottom },
                count_buf, DT_RIGHT | DT_VCENTER | DT_SINGLELINE, RGB(160, 168, 176));

            // Party status banner
            RECT banner{
                panel.left + scale_x(16, width),
                header.bottom + scale_y(12, height),
                panel.right - scale_x(16, width),
                header.bottom + scale_y(52, height)
            };
            fill_round_rect(dc, banner, RGB(50, 80, 40), RGB(90, 140, 60), round_radius(width, height));
            draw_text(dc, m_row_font, banner,
                m_party_active ? L"Party active  -  Chat open across games" : L"No party  -  Press A to start one",
                DT_CENTER | DT_VCENTER | DT_SINGLELINE, RGB(200, 230, 160));

            // Member list
            int list_top = scale_y(k_list_top, height);
            int row_h = scale_y(k_row_height, height);
            int list_left = panel.left + scale_x(12, width);
            int list_right = panel.right - scale_x(12, width);

            int first = m_scroll_offset;
            int last = __min(static_cast<int>(m_members.size()), first + k_visible_rows);

            for (int i = first; i < last; ++i)
            {
                int row_y = list_top + (i - first) * row_h;
                RECT row{ list_left, row_y, list_right, row_y + row_h - scale_y(4, height) };

                bool selected = (i == m_selected);
                COLORREF row_fill = selected ? RGB(70, 110, 40) : RGB(44, 48, 52);
                COLORREF row_border = selected ? RGB(110, 160, 60) : RGB(60, 66, 72);
                fill_round_rect(dc, row, row_fill, row_border, round_radius(width, height));

                s_member const& m = m_members[i];

                // Status / talking indicator
                int dot_size = scale_y(12, height);
                RECT dot{
                    row.left + scale_x(14, width),
                    row.top + (row.bottom - row.top - dot_size) / 2,
                    row.left + scale_x(14, width) + dot_size,
                    row.top + (row.bottom - row.top - dot_size) / 2 + dot_size
                };
                COLORREF dot_color = RGB(120, 124, 128);
                if (m.status == e_member_status::leader || m.status == e_member_status::in_party)
                    dot_color = m.talking ? RGB(90, 200, 90) : RGB(90, 160, 50);
                else if (m.status == e_member_status::joining)
                    dot_color = RGB(214, 161, 28);
                else
                    dot_color = RGB(100, 110, 120);
                draw_circle(dc, dot, dot_color);

                // Gamertag
                RECT tag_rect{
                    row.left + scale_x(36, width),
                    row.top + scale_y(4, height),
                    row.right - scale_x(130, width),
                    row.top + (row.bottom - row.top) / 2 + scale_y(2, height)
                };
                COLORREF tag_color = selected ? RGB(255, 255, 255) : RGB(230, 234, 238);
                draw_text(dc, m_row_font, tag_rect, m.gamertag.c_str(),
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, tag_color);

                // Activity / status
                RECT act_rect{
                    row.left + scale_x(36, width),
                    row.top + (row.bottom - row.top) / 2,
                    row.right - scale_x(130, width),
                    row.bottom - scale_y(4, height)
                };
                COLORREF act_color = selected ? RGB(190, 210, 160) : RGB(130, 138, 146);
                draw_text(dc, m_meta_font, act_rect, m.activity.c_str(),
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, act_color);

                // Right-side status label
                RECT status_rect{
                    row.right - scale_x(120, width),
                    row.top,
                    row.right - scale_x(12, width),
                    row.bottom
                };
                draw_text(dc, m_meta_font, status_rect, status_label(m.status),
                    DT_RIGHT | DT_VCENTER | DT_SINGLELINE, act_color);

                // Muted indicator
                if (m.muted)
                {
                    RECT mute_r{
                        row.right - scale_x(150, width),
                        row.top + (row.bottom - row.top - scale_y(18, height)) / 2,
                        row.right - scale_x(125, width),
                        row.top + (row.bottom - row.top + scale_y(18, height)) / 2
                    };
                    fill_round_rect(dc, mute_r, RGB(120, 50, 50), RGB(160, 70, 70), scale_y(2, height));
                    draw_text(dc, m_badge_font, mute_r, L"M",
                        DT_CENTER | DT_VCENTER | DT_SINGLELINE, RGB(255, 200, 200));
                }
            }

            // Scrollbar
            if (static_cast<int>(m_members.size()) > k_visible_rows)
            {
                int track_top = list_top;
                int track_bottom = list_top + k_visible_rows * row_h;
                int track_h = track_bottom - track_top;
                int thumb_h = __max(scale_y(24, height),
                    track_h * k_visible_rows / static_cast<int>(m_members.size()));
                int max_scroll = static_cast<int>(m_members.size()) - k_visible_rows;
                int thumb_y = track_top + (max_scroll > 0
                    ? (track_h - thumb_h) * m_scroll_offset / max_scroll
                    : 0);

                RECT track{
                    panel.right - scale_x(10, width),
                    track_top,
                    panel.right - scale_x(4, width),
                    track_bottom
                };
                fill_rect(dc, track, RGB(50, 54, 58));
                RECT thumb{ track.left, thumb_y, track.right, thumb_y + thumb_h };
                fill_rect(dc, thumb, RGB(100, 140, 60));
            }

            // Footer
            int badge_size = __max(18, scale_y(22, height));

            auto draw_footer = [&](int x, wchar_t letter, COLORREF color, wchar_t const* label) {
                RECT area{
                    scale_x(x, width), scale_y(660, height),
                    scale_x(x + 140, width), scale_y(700, height)
                };
                RECT badge{
                    area.left,
                    area.top + (area.bottom - area.top - badge_size) / 2,
                    area.left + badge_size,
                    area.top + (area.bottom - area.top - badge_size) / 2 + badge_size
                };
                draw_face_badge(dc, badge, letter, color);
                RECT lbl = area;
                lbl.left = badge.right + scale_x(8, width);
                draw_text(dc, m_footer_font, lbl, label, DT_LEFT | DT_VCENTER | DT_SINGLELINE, RGB(230, 232, 234));
            };

            draw_footer(340, L'A', RGB(90, 176, 54), L"Invite / Select");
            draw_footer(520, L'Y', RGB(214, 161, 28), L"Mute");
            draw_footer(680, L'B', RGB(196, 58, 48), L"Back");
        }

        void ensure_visible()
        {
            if (m_selected < m_scroll_offset)
                m_scroll_offset = m_selected;
            else if (m_selected >= m_scroll_offset + k_visible_rows)
                m_scroll_offset = m_selected - k_visible_rows + 1;
        }

        void move_selection(int delta)
        {
            int next = m_selected + delta;
            if (next < 0) next = 0;
            if (next >= static_cast<int>(m_members.size()))
                next = static_cast<int>(m_members.size()) - 1;
            if (next != m_selected)
            {
                m_selected = next;
                ensure_visible();
                InvalidateRect(m_handle, nullptr, FALSE);
            }
        }

        void activate_selected()
        {
            if (m_selected < 0 || m_selected >= static_cast<int>(m_members.size()))
                return;

            s_member& m = m_members[m_selected];
            if (m.status == e_member_status::invitable)
            {
                m.status = e_member_status::joining;
                m.activity = L"Connecting...";
                MessageBoxW(m_handle,
                    (L"Inviting " + m.gamertag + L" to the party...").c_str(),
                    L"Party Invite", MB_OK | MB_ICONINFORMATION);
            }
            else
            {
                MessageBoxW(m_handle,
                    (m.gamertag + L"\n" + status_label(m.status) + L"\n" + m.activity).c_str(),
                    L"Party Member", MB_OK | MB_ICONINFORMATION);
            }
            InvalidateRect(m_handle, nullptr, FALSE);
        }

        void toggle_mute()
        {
            if (m_selected < 0 || m_selected >= static_cast<int>(m_members.size()))
                return;
            s_member& m = m_members[m_selected];
            if (m.status == e_member_status::leader || m.status == e_member_status::in_party)
            {
                m.muted = !m.muted;
                InvalidateRect(m_handle, nullptr, FALSE);
            }
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

            if (pressed & XINPUT_GAMEPAD_B) { close(); return; }
            if (pressed & XINPUT_GAMEPAD_A) activate_selected();
            if (pressed & XINPUT_GAMEPAD_Y) toggle_mute();

            int dy = 0;
            if (pressed & XINPUT_GAMEPAD_DPAD_UP)   dy = -1;
            if (pressed & XINPUT_GAMEPAD_DPAD_DOWN) dy = 1;

            SHORT stick_y = state.Gamepad.sThumbLY;
            if (dy == 0)
            {
                if (stick_y > k_stick_deadzone)  dy = -1;
                if (stick_y < -k_stick_deadzone) dy = 1;
            }

            if (dy != 0)
            {
                DWORD now = GetTickCount();
                if (pressed != 0 || now - m_last_nav_time >= 120)
                {
                    move_selection(dy);
                    m_last_nav_time = now;
                }
            }
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
                switch (w_param)
                {
                case VK_ESCAPE:
                case VK_BACK:
                    close();
                    break;
                case VK_RETURN:
                case VK_SPACE:
                    activate_selected();
                    break;
                case 'Y':
                case 'y':
                    toggle_mute();
                    break;
                case VK_UP:    move_selection(-1); break;
                case VK_DOWN:  move_selection(1);  break;
                case VK_PRIOR: move_selection(-k_visible_rows); break;
                case VK_NEXT:  move_selection(k_visible_rows);  break;
                case VK_HOME:
                    m_selected = 0;
                    ensure_visible();
                    InvalidateRect(m_handle, nullptr, FALSE);
                    break;
                case VK_END:
                    m_selected = static_cast<int>(m_members.size()) - 1;
                    ensure_visible();
                    InvalidateRect(m_handle, nullptr, FALSE);
                    break;
                }
                return 0;

            case WM_LBUTTONDOWN:
            {
                POINT pt{ GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param) };
                int width = GetClientRectWidth();
                int height = GetClientRectHeight();
                int list_top = scale_y(k_list_top, height);
                int row_h = scale_y(k_row_height, height);
                RECT panel = scale_rect(
                    RECT{ k_panel_left, k_panel_top, k_panel_right, k_panel_bottom },
                    width, height);

                if (pt.x >= panel.left && pt.x <= panel.right &&
                    pt.y >= list_top && pt.y < list_top + k_visible_rows * row_h)
                {
                    int row = (pt.y - list_top) / row_h;
                    int index = m_scroll_offset + row;
                    if (index >= 0 && index < static_cast<int>(m_members.size()))
                    {
                        m_selected = index;
                        InvalidateRect(m_handle, nullptr, FALSE);
                        activate_selected();
                    }
                }
                return 0;
            }

            case WM_MOUSEWHEEL:
            {
                int delta = GET_WHEEL_DELTA_WPARAM(w_param);
                if (delta > 0) move_selection(-1);
                else if (delta < 0) move_selection(1);
                return 0;
            }

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
        HFONT m_row_font = nullptr;
        HFONT m_meta_font = nullptr;
        HFONT m_footer_font = nullptr;
        HFONT m_badge_font = nullptr;

        std::vector<s_member> m_members;
        bool m_party_active = false;
        int m_max_members = 8;
        int m_selected = 0;
        int m_scroll_offset = 0;
        WORD m_previous_buttons = 0;
        DWORD m_last_nav_time = 0;
    };
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    c_party_window window;
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
