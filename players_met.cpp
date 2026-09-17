#include <windows.h>
#include <windowsx.h>
#include <xinput.h>
#include <wchar.h>
#include <string>
#include <vector>

#include "xbox360_ui_common.h"
#include "controller_message_box.h"
#include "config.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "msimg32.lib")
#pragma comment(lib, "xinput.lib")

namespace
{
    using namespace xbox360_ui;

    constexpr wchar_t k_window_class_name[] = L"pc_xbox360_players_met";
    constexpr int k_panel_left = 300;
    constexpr int k_panel_top = 60;
    constexpr int k_panel_right = 980;
    constexpr int k_panel_bottom = 640;

    constexpr int k_header_height = 52;
    constexpr int k_row_height = 56;
    constexpr int k_visible_rows = 8;
    constexpr int k_list_top = 130;
    struct s_player
    {
        std::wstring gamertag;
        presence presence_state = presence::offline;
        std::wstring game_met;      // game where you met them
        std::wstring when_met;      // "Today", "Yesterday", etc.
        bool is_friend = false;
        bool feedback_sent = false;
    };

    class c_players_met_window
    {
    public:
        c_players_met_window()
        {
            // Sample recent players from multiplayer sessions
            config cfg;
            if (cfg.load_beside_exe(L"players_met.ini"))
            {
                auto parse_pres = [](std::wstring t) {
                    for (auto& c : t) c = static_cast<wchar_t>(towlower(c));
                    if (t == L"online") return presence::online;
                    if (t == L"away") return presence::away;
                    if (t == L"busy") return presence::busy;
                    return presence::offline;
                };
                for (auto const& sec : cfg.sections_with_prefix(L"player."))
                {
                    s_player p;
                    p.gamertag = cfg.get(sec, L"gamertag", L"Unknown");
                    p.presence_state = parse_pres(cfg.get(sec, L"presence", L"offline"));
                    p.game_met = cfg.get(sec, L"game_met");
                    p.when_met = cfg.get(sec, L"when_met");
                    p.is_friend = cfg.get_bool(sec, L"is_friend");
                    p.feedback_sent = cfg.get_bool(sec, L"feedback_sent");
                    m_players.push_back(std::move(p));
                }
            }
            if (m_players.empty())
            {
                m_players = {
                    { L"Spartan117", presence::online, L"Halo 3", L"Today", false, false },
                };
            }
        }

        bool create()
        {
            bool result = false;

            WNDCLASSEXW window_class{};
            window_class.cbSize = sizeof(window_class);
            window_class.hInstance = GetModuleHandleW(nullptr);
            window_class.lpfnWndProc = &c_players_met_window::static_window_proc;
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
                    L"Players Met",
                    WS_POPUP,
                    0, 0, width, height,
                    nullptr, nullptr,
                    GetModuleHandleW(nullptr),
                    this);

                if (m_handle != nullptr)
                {
                    SetLayeredWindowAttributes(m_handle, RGB(255, 0, 255), 0, LWA_COLORKEY);
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
            c_players_met_window* instance =
                reinterpret_cast<c_players_met_window*>(GetWindowLongPtrW(window_handle, GWLP_USERDATA));

            if (message == WM_NCCREATE)
            {
                CREATESTRUCTW* create = reinterpret_cast<CREATESTRUCTW*>(l_param);
                instance = static_cast<c_players_met_window*>(create->lpCreateParams);
                SetWindowLongPtrW(window_handle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(instance));
                instance->m_handle = window_handle;
            }

            if (instance != nullptr)
                return instance->window_proc(message, w_param, l_param);

            return DefWindowProcW(window_handle, message, w_param, l_param);
        }

    private:

        void create_fonts()
        {
            int height = GetClientRectHeight();
            m_title_font  = make_font(scale_y(28, height), FW_SEMIBOLD);
            m_header_font = make_font(scale_y(15, height));
            m_row_font    = make_font(scale_y(17, height));
            m_meta_font   = make_font(scale_y(13, height));
            m_footer_font = make_font(scale_y(16, height));
            m_badge_font  = make_font(scale_y(11, height), FW_SEMIBOLD);
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

        void draw_face_badge(HDC dc, RECT rect, wchar_t letter, COLORREF fill)
        {
            draw_circle(dc, rect, fill);
            wchar_t buf[2]{ letter, L'\0' };
            draw_text(dc, m_badge_font, rect, buf, DT_CENTER | DT_VCENTER | DT_SINGLELINE, RGB(255, 255, 255));
        }

        void paint(HDC dc)
        {
            RECT client{};
            GetClientRect(m_handle, &client);
            int width = client.right;
            int height = client.bottom;

            fill_rect(dc, client, RGB(255, 0, 255));

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
                L"Players Met", DT_LEFT | DT_VCENTER | DT_SINGLELINE, RGB(240, 242, 244));

            wchar_t count_buf[32];
            swprintf_s(count_buf, L"%d Players", static_cast<int>(m_players.size()));
            draw_text(dc, m_header_font,
                RECT{ header.left + scale_x(20, width), header.top,
                      header.right - scale_x(20, width), header.bottom },
                count_buf, DT_RIGHT | DT_VCENTER | DT_SINGLELINE, RGB(160, 168, 176));

            // List
            int list_top = scale_y(k_list_top, height);
            int row_h = scale_y(k_row_height, height);
            int list_left = panel.left + scale_x(12, width);
            int list_right = panel.right - scale_x(12, width);

            int first = m_scroll_offset;
            int last = __min(static_cast<int>(m_players.size()), first + k_visible_rows);

            for (int i = first; i < last; ++i)
            {
                int row_y = list_top + (i - first) * row_h;
                RECT row{ list_left, row_y, list_right, row_y + row_h - scale_y(4, height) };

                bool selected = (i == m_selected);
                COLORREF row_fill = selected ? RGB(70, 110, 40) : RGB(44, 48, 52);
                COLORREF row_border = selected ? RGB(110, 160, 60) : RGB(60, 66, 72);
                fill_round_rect(dc, row, row_fill, row_border, round_radius(width, height));

                s_player const& p = m_players[i];

                // Presence dot
                int dot_size = scale_y(12, height);
                RECT dot{
                    row.left + scale_x(14, width),
                    row.top + (row.bottom - row.top - dot_size) / 2,
                    row.left + scale_x(14, width) + dot_size,
                    row.top + (row.bottom - row.top - dot_size) / 2 + dot_size
                };
                draw_circle(dc, dot, presence_color(p.presence_state));

                // Gamertag
                RECT tag_rect{
                    row.left + scale_x(36, width),
                    row.top + scale_y(4, height),
                    row.right - scale_x(160, width),
                    row.top + (row.bottom - row.top) / 2 + scale_y(2, height)
                };
                COLORREF tag_color = selected ? RGB(255, 255, 255) : RGB(230, 234, 238);
                draw_text(dc, m_row_font, tag_rect, p.gamertag.c_str(),
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, tag_color);

                // Met in game + when
                std::wstring meta = L"Met in " + p.game_met + L"  |  " + p.when_met;
                RECT meta_rect{
                    row.left + scale_x(36, width),
                    row.top + (row.bottom - row.top) / 2,
                    row.right - scale_x(160, width),
                    row.bottom - scale_y(4, height)
                };
                COLORREF meta_color = selected ? RGB(190, 210, 160) : RGB(130, 138, 146);
                draw_text(dc, m_meta_font, meta_rect, meta.c_str(),
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, meta_color);

                // Badges on the right: Friend / Feedback
                int badge_x = row.right - scale_x(12, width);
                if (p.is_friend)
                {
                    RECT fb{
                        badge_x - scale_x(70, width),
                        row.top + (row.bottom - row.top - scale_y(20, height)) / 2,
                        badge_x,
                        row.top + (row.bottom - row.top + scale_y(20, height)) / 2
                    };
                    fill_round_rect(dc, fb, RGB(50, 100, 160), RGB(70, 130, 200), scale_y(3, height));
                    draw_text(dc, m_badge_font, fb, L"FRIEND",
                        DT_CENTER | DT_VCENTER | DT_SINGLELINE, RGB(220, 240, 255));
                    badge_x = fb.left - scale_x(6, width);
                }
                if (p.feedback_sent)
                {
                    RECT fb{
                        badge_x - scale_x(80, width),
                        row.top + (row.bottom - row.top - scale_y(20, height)) / 2,
                        badge_x,
                        row.top + (row.bottom - row.top + scale_y(20, height)) / 2
                    };
                    fill_round_rect(dc, fb, RGB(80, 80, 50), RGB(120, 120, 70), scale_y(3, height));
                    draw_text(dc, m_badge_font, fb, L"FEEDBACK",
                        DT_CENTER | DT_VCENTER | DT_SINGLELINE, RGB(220, 220, 160));
                }
            }

            // Scrollbar
            if (static_cast<int>(m_players.size()) > k_visible_rows)
            {
                int track_top = list_top;
                int track_bottom = list_top + k_visible_rows * row_h;
                int track_h = track_bottom - track_top;
                int thumb_h = __max(scale_y(24, height),
                    track_h * k_visible_rows / static_cast<int>(m_players.size()));
                int max_scroll = static_cast<int>(m_players.size()) - k_visible_rows;
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
                    scale_x(x + 150, width), scale_y(700, height)
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

            draw_footer(300, L'A', RGB(90, 176, 54),  L"Add Friend");
            draw_footer(480, L'X', RGB(46, 113, 176), L"Feedback");
            draw_footer(660, L'B', RGB(196, 58, 48),  L"Back");
        
            m_msgbox.paint(dc, width, height, m_title_font, m_row_font);
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
            if (next >= static_cast<int>(m_players.size()))
                next = static_cast<int>(m_players.size()) - 1;
            if (next != m_selected)
            {
                m_selected = next;
                ensure_visible();
                InvalidateRect(m_handle, nullptr, FALSE);
            }
        }

        void add_friend()
        {
            if (m_selected < 0 || m_selected >= static_cast<int>(m_players.size()))
                return;
            s_player& p = m_players[m_selected];
            if (p.is_friend)
            {
                m_msgbox.show(
                    L"Players Met",
                    (p.gamertag + L" is already a friend.").c_str(),
                    controller_message_box::buttons::ok,
                    L"A  OK");
                InvalidateRect(m_handle, nullptr, FALSE);
            }
            else
            {
                p.is_friend = true;
                m_msgbox.show(
                    L"Players Met",
                    (L"Friend request sent to " + p.gamertag + L".").c_str(),
                    controller_message_box::buttons::ok,
                    L"A  OK");
                InvalidateRect(m_handle, nullptr, FALSE);
            }
        }

        void send_feedback()
        {
            if (m_selected < 0 || m_selected >= static_cast<int>(m_players.size()))
                return;
            s_player& p = m_players[m_selected];
            if (p.feedback_sent)
            {
                m_msgbox.show(
                    L"Players Met",
                    (L"Feedback already submitted for " + p.gamertag + L".").c_str(),
                    controller_message_box::buttons::ok,
                    L"A  OK");
                InvalidateRect(m_handle, nullptr, FALSE);
            }
            else
            {
                p.feedback_sent = true;
                m_msgbox.show(
                    L"Players Met",
                    (L"Player feedback submitted for " + p.gamertag + L".").c_str(),
                    controller_message_box::buttons::ok,
                    L"A  OK");
                InvalidateRect(m_handle, nullptr, FALSE);
            }
        }

        void poll_controller()
        {
            if (m_msgbox.visible())
            {
                XINPUT_STATE st{};
                if (XInputGetState(0, &st) == ERROR_SUCCESS)
                {
                    WORD buttons = st.Gamepad.wButtons;
                    static WORD s_prev = 0;
                    WORD pressed = static_cast<WORD>(buttons & ~s_prev);
                    s_prev = buttons;
                    if (m_msgbox.handle_controller(pressed, st.Gamepad.sThumbLX, GetTickCount()))
                        InvalidateRect(m_handle, nullptr, FALSE);
                }
                return;
            }

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
            if (pressed & XINPUT_GAMEPAD_A) add_friend();
            if (pressed & XINPUT_GAMEPAD_X) send_feedback();

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
                if (m_msgbox.visible())
                {
                    if (m_msgbox.handle_key(w_param))
                        InvalidateRect(m_handle, nullptr, FALSE);
                    return 0;
                }
                switch (w_param)
                {
                case VK_ESCAPE:
                case VK_BACK:
                    close();
                    break;
                case VK_RETURN:
                    add_friend();
                    break;
                case 'X':
                case 'x':
                    send_feedback();
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
                    m_selected = static_cast<int>(m_players.size()) - 1;
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
                    if (index >= 0 && index < static_cast<int>(m_players.size()))
                    {
                        m_selected = index;
                        InvalidateRect(m_handle, nullptr, FALSE);
                        add_friend();
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

        std::vector<s_player> m_players;
        int m_selected = 0;
        int m_scroll_offset = 0;
        controller_message_box m_msgbox;

        WORD m_previous_buttons = 0;
        DWORD m_last_nav_time = 0;
    };
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    c_players_met_window window;
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
