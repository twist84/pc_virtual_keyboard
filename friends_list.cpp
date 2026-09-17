#include <windows.h>
#include <windowsx.h>
#include <xinput.h>
#include <wchar.h>
#include <algorithm>
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

    constexpr wchar_t k_window_class_name[] = L"pc_xbox360_friends_list";
    // Panel roughly matching NXE Guide friends blade proportions
    constexpr int k_panel_left = 340;
    constexpr int k_panel_top = 80;
    constexpr int k_panel_right = 940;
    constexpr int k_panel_bottom = 620;

    constexpr int k_header_height = 56;
    constexpr int k_row_height = 52;
    constexpr int k_visible_rows = 8;
    constexpr int k_list_top = 150;
    constexpr int k_list_bottom = 560;
    struct s_friend
    {
        std::wstring gamertag;
        presence presence_state = presence::offline;
        std::wstring status;   // e.g. "Playing Halo 3" or "Online"
        int gamerscore = 0;
        bool favorite = false;
    };

    class c_friends_list_window
    {
    public:
        c_friends_list_window()
        {
            // Sample data inspired by classic NXE / Guide friends list
            config cfg;
            if (cfg.load_beside_exe(L"friends_list.ini"))
            {
                auto parse_pres = [](std::wstring t) {
                    for (auto& c : t) c = static_cast<wchar_t>(towlower(c));
                    if (t == L"online") return presence::online;
                    if (t == L"away") return presence::away;
                    if (t == L"busy") return presence::busy;
                    return presence::offline;
                };
                for (auto const& sec : cfg.sections_with_prefix(L"friend."))
                {
                    s_friend f;
                    f.gamertag = cfg.get(sec, L"gamertag", L"Unknown");
                    f.presence_state = parse_pres(cfg.get(sec, L"presence", L"offline"));
                    f.status = cfg.get(sec, L"status");
                    f.gamerscore = cfg.get_int(sec, L"gamerscore");
                    f.favorite = cfg.get_bool(sec, L"favorite");
                    m_friends.push_back(std::move(f));
                }
            }
            if (m_friends.empty())
            {
                m_friends = {
                    { L"MajorNelson", presence::online, L"Online", 1000, true },
                };
            }

            // Online / away / busy first, then offline
            std::stable_sort(m_friends.begin(), m_friends.end(),
                [](s_friend const& a, s_friend const& b) {
                    auto rank = [](presence p) {
                        switch (p) {
                        case presence::online: return 0;
                        case presence::away:   return 1;
                        case presence::busy:   return 2;
                        default:                 return 3;
                        }
                    };
                    int ra = rank(a.presence_state), rb = rank(b.presence_state);
                    if (ra != rb) return ra < rb;
                    if (a.favorite != b.favorite) return a.favorite > b.favorite;
                    return a.gamertag < b.gamertag;
                });
        }

        bool create()
        {
            bool result = false;

            WNDCLASSEXW window_class{};
            window_class.cbSize = sizeof(window_class);
            window_class.hInstance = GetModuleHandleW(nullptr);
            window_class.lpfnWndProc = &c_friends_list_window::static_window_proc;
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
                    L"Friends",
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
            c_friends_list_window* instance =
                reinterpret_cast<c_friends_list_window*>(GetWindowLongPtrW(window_handle, GWLP_USERDATA));

            if (message == WM_NCCREATE)
            {
                CREATESTRUCTW* create = reinterpret_cast<CREATESTRUCTW*>(l_param);
                instance = static_cast<c_friends_list_window*>(create->lpCreateParams);
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
            m_title_font   = make_font(scale_y(28, height), FW_SEMIBOLD);
            m_header_font  = make_font(scale_y(16, height));
            m_row_font     = make_font(scale_y(18, height));
            m_status_font  = make_font(scale_y(14, height));
            m_footer_font  = make_font(scale_y(16, height));
            m_badge_font   = make_font(scale_y(12, height), FW_SEMIBOLD);
            m_section_font = make_font(scale_y(14, height), FW_SEMIBOLD);
        }

        void release_fonts()
        {
            HFONT fonts[] = {
                m_title_font, m_header_font, m_row_font,
                m_status_font, m_footer_font, m_badge_font, m_section_font
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

        RECT to_client_rect(RECT rect) const
        {
            RECT client{};
            GetClientRect(m_handle, &client);
            return scale_rect(rect, client.right, client.bottom);
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

            // Magenta color-key background
            fill_rect(dc, client, RGB(255, 0, 255));

            // Main panel
            RECT panel = scale_rect(
                RECT{ k_panel_left, k_panel_top, k_panel_right, k_panel_bottom },
                width, height);
            fill_round_rect(dc, panel, RGB(32, 36, 40), RGB(70, 76, 82), round_radius(width, height) * 2);

            // Header bar
            RECT header = panel;
            header.bottom = panel.top + scale_y(k_header_height, height);
            fill_rect(dc, header, RGB(48, 54, 60));

            draw_text(dc, m_title_font,
                RECT{ header.left + scale_x(20, width), header.top,
                      header.right - scale_x(20, width), header.bottom },
                L"Friends", DT_LEFT | DT_VCENTER | DT_SINGLELINE, RGB(240, 242, 244));

            // Online count on the right of header
            int online_count = 0;
            for (auto const& f : m_friends)
                if (f.presence_state == presence::online || f.presence_state == presence::away || f.presence_state == presence::busy)
                    ++online_count;

            wchar_t count_buf[64];
            swprintf_s(count_buf, L"%d Online", online_count);
            draw_text(dc, m_header_font,
                RECT{ header.left + scale_x(20, width), header.top,
                      header.right - scale_x(20, width), header.bottom },
                count_buf, DT_RIGHT | DT_VCENTER | DT_SINGLELINE, RGB(160, 168, 176));

            // List area
            int list_top = scale_y(k_list_top, height);
            int row_h = scale_y(k_row_height, height);
            int list_left = panel.left + scale_x(12, width);
            int list_right = panel.right - scale_x(12, width);

            int first = m_scroll_offset;
            int last = __min(static_cast<int>(m_friends.size()), first + k_visible_rows);

            for (int i = first; i < last; ++i)
            {
                int row_y = list_top + (i - first) * row_h;
                RECT row{ list_left, row_y, list_right, row_y + row_h - scale_y(4, height) };

                bool selected = (i == m_selected);
                COLORREF row_fill = selected ? RGB(70, 110, 40) : RGB(44, 48, 52);
                COLORREF row_border = selected ? RGB(110, 160, 60) : RGB(60, 66, 72);
                fill_round_rect(dc, row, row_fill, row_border, round_radius(width, height));

                s_friend const& f = m_friends[i];

                // Status dot
                int dot_size = scale_y(12, height);
                RECT dot{
                    row.left + scale_x(14, width),
                    row.top + (row.bottom - row.top - dot_size) / 2,
                    row.left + scale_x(14, width) + dot_size,
                    row.top + (row.bottom - row.top - dot_size) / 2 + dot_size
                };
                draw_circle(dc, dot, presence_color(f.presence_state));

                // Gamertag
                RECT tag_rect{
                    row.left + scale_x(36, width),
                    row.top,
                    row.right - scale_x(120, width),
                    row.top + (row.bottom - row.top) * 3 / 5
                };
                COLORREF tag_color = selected ? RGB(255, 255, 255) : RGB(230, 234, 238);
                // Favorite indicator (gold diamond) drawn before the gamertag
                RECT name_rect = tag_rect;
                if (f.favorite)
                {
                    int fav_size = scale_y(10, height);
                    int fav_y = tag_rect.top + (tag_rect.bottom - tag_rect.top - fav_size) / 2;
                    RECT fav{
                        tag_rect.left,
                        fav_y,
                        tag_rect.left + fav_size,
                        fav_y + fav_size
                    };
                    // Simple filled diamond for favorite
                    POINT diamond[4] = {
                        { (fav.left + fav.right) / 2, fav.top },
                        { fav.right, (fav.top + fav.bottom) / 2 },
                        { (fav.left + fav.right) / 2, fav.bottom },
                        { fav.left, (fav.top + fav.bottom) / 2 }
                    };
                    HBRUSH fav_brush = CreateSolidBrush(RGB(255, 200, 40));
                    HPEN fav_pen = CreatePen(PS_SOLID, 1, RGB(255, 200, 40));
                    HBRUSH old_b = static_cast<HBRUSH>(SelectObject(dc, fav_brush));
                    HPEN old_p = static_cast<HPEN>(SelectObject(dc, fav_pen));
                    Polygon(dc, diamond, 4);
                    SelectObject(dc, old_b);
                    SelectObject(dc, old_p);
                    DeleteObject(fav_pen);
                    DeleteObject(fav_brush);

                    name_rect.left = fav.right + scale_x(6, width);
                }

                draw_text(dc, m_row_font, name_rect, f.gamertag.c_str(),
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, tag_color);

                // Status / presence
                RECT status_rect{
                    row.left + scale_x(36, width),
                    row.top + (row.bottom - row.top) * 2 / 5,
                    row.right - scale_x(120, width),
                    row.bottom
                };
                COLORREF status_color = selected ? RGB(200, 220, 180) : RGB(140, 148, 156);
                draw_text(dc, m_status_font, status_rect, f.status.c_str(),
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, status_color);

                // Gamerscore
                wchar_t gs_buf[32];
                swprintf_s(gs_buf, L"%d G", f.gamerscore);
                RECT gs_rect{
                    row.right - scale_x(110, width),
                    row.top,
                    row.right - scale_x(12, width),
                    row.bottom
                };
                draw_text(dc, m_status_font, gs_rect, gs_buf,
                    DT_RIGHT | DT_VCENTER | DT_SINGLELINE, status_color);
            }

            // Scroll indicator if needed
            if (static_cast<int>(m_friends.size()) > k_visible_rows)
            {
                int track_top = list_top;
                int track_bottom = list_top + k_visible_rows * row_h;
                int track_h = track_bottom - track_top;
                int thumb_h = __max(scale_y(24, height),
                    track_h * k_visible_rows / static_cast<int>(m_friends.size()));
                int max_scroll = static_cast<int>(m_friends.size()) - k_visible_rows;
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
            RECT footer_a{
                scale_x(380, width),
                scale_y(640, height),
                scale_x(520, width),
                scale_y(680, height)
            };
            int badge_size = __max(18, scale_y(22, height));
            RECT a_badge{
                footer_a.left,
                footer_a.top + (footer_a.bottom - footer_a.top - badge_size) / 2,
                footer_a.left + badge_size,
                footer_a.top + (footer_a.bottom - footer_a.top - badge_size) / 2 + badge_size
            };
            draw_face_badge(dc, a_badge, L'A', RGB(90, 176, 54));
            RECT a_label = footer_a;
            a_label.left = a_badge.right + scale_x(8, width);
            draw_text(dc, m_footer_font, a_label, L"Select", DT_LEFT | DT_VCENTER | DT_SINGLELINE, RGB(230, 232, 234));

            RECT footer_b{
                scale_x(540, width),
                scale_y(640, height),
                scale_x(680, width),
                scale_y(680, height)
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
            if (next >= static_cast<int>(m_friends.size()))
                next = static_cast<int>(m_friends.size()) - 1;
            if (next != m_selected)
            {
                m_selected = next;
                ensure_visible();
                InvalidateRect(m_handle, nullptr, FALSE);
            }
        }

        void activate_selected()
        {
            // Placeholder: in a real integration this would open a context menu
            // (Invite, Message, Join Session, etc.)
            if (m_selected >= 0 && m_selected < static_cast<int>(m_friends.size()))
            {
                // Flash selection or show a simple message box for demo
                m_msgbox.show(
                    L"Friend Options",
                    (L"Selected: " + m_friends[m_selected].gamertag + L"\n" + m_friends[m_selected].status).c_str(),
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

            if (pressed & XINPUT_GAMEPAD_B)
            {
                close();
                return;
            }
            if (pressed & XINPUT_GAMEPAD_A)
                activate_selected();

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
                int w = client.right;
                int h = client.bottom;

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
                case VK_SPACE:
                    activate_selected();
                    break;
                case VK_UP:
                    move_selection(-1);
                    break;
                case VK_DOWN:
                    move_selection(1);
                    break;
                case VK_PRIOR: // Page Up
                    move_selection(-k_visible_rows);
                    break;
                case VK_NEXT:  // Page Down
                    move_selection(k_visible_rows);
                    break;
                case VK_HOME:
                    m_selected = 0;
                    ensure_visible();
                    InvalidateRect(m_handle, nullptr, FALSE);
                    break;
                case VK_END:
                    m_selected = static_cast<int>(m_friends.size()) - 1;
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
                    if (index >= 0 && index < static_cast<int>(m_friends.size()))
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
        HFONT m_status_font = nullptr;
        HFONT m_footer_font = nullptr;
        HFONT m_badge_font = nullptr;
        HFONT m_section_font = nullptr;

        std::vector<s_friend> m_friends;
        int m_selected = 0;
        int m_scroll_offset = 0;
        controller_message_box m_msgbox;

        WORD m_previous_buttons = 0;
        DWORD m_last_nav_time = 0;
    };
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    c_friends_list_window window;
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
