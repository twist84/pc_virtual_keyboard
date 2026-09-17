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

    constexpr wchar_t k_window_class_name[] = L"pc_xbox360_message_center";
    constexpr int k_panel_left = 280;
    constexpr int k_panel_top = 50;
    constexpr int k_panel_right = 1000;
    constexpr int k_panel_bottom = 650;

    constexpr int k_header_height = 52;
    constexpr int k_row_height = 60;
    constexpr int k_visible_rows = 7;
    constexpr int k_list_top = 125;
    enum class e_msg_type
    {
        text,
        voice,
        game_invite,
        friend_request,
        party_invite
    };

    struct s_message
    {
        e_msg_type type = e_msg_type::text;
        std::wstring from;
        std::wstring preview;     // subject / first line
        std::wstring when;        // "Just now", "2 hours ago", etc.
        bool unread = true;
    };

    wchar_t const* type_label(e_msg_type t)
    {
        switch (t)
        {
        case e_msg_type::text:           return L"Message";
        case e_msg_type::voice:          return L"Voice Message";
        case e_msg_type::game_invite:    return L"Game Invite";
        case e_msg_type::friend_request: return L"Friend Request";
        case e_msg_type::party_invite:   return L"Party Invite";
        default:                         return L"Message";
        }
    }

    COLORREF type_color(e_msg_type t)
    {
        switch (t)
        {
        case e_msg_type::game_invite:    return RGB(90, 160, 220);
        case e_msg_type::friend_request: return RGB(90, 176, 54);
        case e_msg_type::party_invite:   return RGB(180, 120, 220);
        case e_msg_type::voice:          return RGB(214, 161, 28);
        default:                         return RGB(140, 148, 156);
        }
    }

    class c_message_center_window
    {
    public:
        c_message_center_window()
        {
            config cfg;
            if (cfg.load_beside_exe(L"message_center.ini"))
            {
                auto parse_type = [](std::wstring t) {
                    for (auto& c : t) c = static_cast<wchar_t>(towlower(c));
                    if (t == L"voice") return e_msg_type::voice;
                    if (t == L"game_invite") return e_msg_type::game_invite;
                    if (t == L"friend_request") return e_msg_type::friend_request;
                    if (t == L"party_invite") return e_msg_type::party_invite;
                    return e_msg_type::text;
                };
                for (auto const& sec : cfg.sections_with_prefix(L"message."))
                {
                    s_message msg;
                    msg.type = parse_type(cfg.get(sec, L"type", L"text"));
                    msg.from = cfg.get(sec, L"from", L"Unknown");
                    msg.preview = cfg.get(sec, L"preview");
                    msg.when = cfg.get(sec, L"when");
                    msg.unread = cfg.get_bool(sec, L"unread", true);
                    m_messages.push_back(std::move(msg));
                }
            }
            if (m_messages.empty())
            {
                m_messages = {
                    { e_msg_type::text, L"System", L"No messages in config.", L"Now", false },
                };
            }
        }

        bool create()
        {
            bool result = false;

            WNDCLASSEXW window_class{};
            window_class.cbSize = sizeof(window_class);
            window_class.hInstance = GetModuleHandleW(nullptr);
            window_class.lpfnWndProc = &c_message_center_window::static_window_proc;
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
                    L"Message Center",
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
            c_message_center_window* instance =
                reinterpret_cast<c_message_center_window*>(GetWindowLongPtrW(window_handle, GWLP_USERDATA));

            if (message == WM_NCCREATE)
            {
                CREATESTRUCTW* create = reinterpret_cast<CREATESTRUCTW*>(l_param);
                instance = static_cast<c_message_center_window*>(create->lpCreateParams);
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

        int unread_count() const
        {
            int n = 0;
            for (auto const& m : m_messages)
                if (m.unread) ++n;
            return n;
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
                L"Message Center", DT_LEFT | DT_VCENTER | DT_SINGLELINE, RGB(240, 242, 244));

            int unread = unread_count();
            wchar_t count_buf[48];
            if (unread > 0)
                swprintf_s(count_buf, L"%d Unread", unread);
            else
                swprintf_s(count_buf, L"%d Messages", static_cast<int>(m_messages.size()));
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
            int last = __min(static_cast<int>(m_messages.size()), first + k_visible_rows);

            for (int i = first; i < last; ++i)
            {
                int row_y = list_top + (i - first) * row_h;
                RECT row{ list_left, row_y, list_right, row_y + row_h - scale_y(4, height) };

                bool selected = (i == m_selected);
                s_message const& msg = m_messages[i];

                COLORREF row_fill = selected ? RGB(70, 110, 40) : (msg.unread ? RGB(48, 54, 58) : RGB(40, 44, 48));
                COLORREF row_border = selected ? RGB(110, 160, 60) : RGB(60, 66, 72);
                fill_round_rect(dc, row, row_fill, row_border, round_radius(width, height));

                // Unread indicator bar on left
                if (msg.unread)
                {
                    RECT bar{
                        row.left + scale_x(4, width),
                        row.top + scale_y(8, height),
                        row.left + scale_x(8, width),
                        row.bottom - scale_y(8, height)
                    };
                    fill_rect(dc, bar, RGB(90, 176, 54));
                }

                // Type badge
                RECT type_badge{
                    row.left + scale_x(16, width),
                    row.top + scale_y(8, height),
                    row.left + scale_x(120, width),
                    row.top + scale_y(28, height)
                };
                fill_round_rect(dc, type_badge, type_color(msg.type), type_color(msg.type), scale_y(3, height));
                draw_text(dc, m_badge_font, type_badge, type_label(msg.type),
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE, RGB(255, 255, 255));

                // From + time
                RECT from_rect{
                    type_badge.right + scale_x(10, width),
                    row.top + scale_y(6, height),
                    row.right - scale_x(100, width),
                    row.top + scale_y(28, height)
                };
                COLORREF from_color = selected ? RGB(255, 255, 255) : RGB(230, 234, 238);
                draw_text(dc, m_row_font, from_rect, msg.from.c_str(),
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, from_color);

                RECT when_rect{
                    row.right - scale_x(110, width),
                    row.top + scale_y(6, height),
                    row.right - scale_x(12, width),
                    row.top + scale_y(28, height)
                };
                draw_text(dc, m_meta_font, when_rect, msg.when.c_str(),
                    DT_RIGHT | DT_VCENTER | DT_SINGLELINE, RGB(140, 148, 156));

                // Preview
                RECT preview_rect{
                    row.left + scale_x(16, width),
                    row.top + scale_y(30, height),
                    row.right - scale_x(12, width),
                    row.bottom - scale_y(6, height)
                };
                COLORREF preview_color = selected ? RGB(190, 210, 160) : RGB(130, 138, 146);
                draw_text(dc, m_meta_font, preview_rect, msg.preview.c_str(),
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, preview_color);
            }

            // Scrollbar
            if (static_cast<int>(m_messages.size()) > k_visible_rows)
            {
                int track_top = list_top;
                int track_bottom = list_top + k_visible_rows * row_h;
                int track_h = track_bottom - track_top;
                int thumb_h = __max(scale_y(24, height),
                    track_h * k_visible_rows / static_cast<int>(m_messages.size()));
                int max_scroll = static_cast<int>(m_messages.size()) - k_visible_rows;
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
                    scale_x(x, width), scale_y(670, height),
                    scale_x(x + 150, width), scale_y(710, height)
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

            draw_footer(320, L'A', RGB(90, 176, 54),  L"Open");
            draw_footer(500, L'X', RGB(46, 113, 176), L"Delete");
            draw_footer(680, L'B', RGB(196, 58, 48),  L"Back");
        
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
            if (next >= static_cast<int>(m_messages.size()))
                next = static_cast<int>(m_messages.size()) - 1;
            if (next != m_selected)
            {
                m_selected = next;
                ensure_visible();
                InvalidateRect(m_handle, nullptr, FALSE);
            }
        }

        void open_selected()
        {
            if (m_selected < 0 || m_selected >= static_cast<int>(m_messages.size()))
                return;

            s_message& msg = m_messages[m_selected];
            msg.unread = false;

            std::wstring body = type_label(msg.type);
            body += L"\nFrom: ";
            body += msg.from;
            body += L"\n\n";
            body += msg.preview;
            body += L"\n\n(";
            body += msg.when;
            body += L")";

            m_msgbox.show(
                    L"Message",
                    body.c_str(),
                    controller_message_box::buttons::ok,
                    L"A  OK");
                InvalidateRect(m_handle, nullptr, FALSE);
        }

        void delete_selected()
        {
            if (m_selected < 0 || m_selected >= static_cast<int>(m_messages.size()))
                return;

            std::wstring name = m_messages[m_selected].from;
            m_messages.erase(m_messages.begin() + m_selected);
            if (m_selected >= static_cast<int>(m_messages.size()) && m_selected > 0)
                --m_selected;
            ensure_visible();
            InvalidateRect(m_handle, nullptr, FALSE);

            m_msgbox.show(
                    L"Message Center",
                    (L"Deleted message from " + name + L".").c_str(),
                    controller_message_box::buttons::ok,
                    L"A  OK");
                InvalidateRect(m_handle, nullptr, FALSE);
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
            if (pressed & XINPUT_GAMEPAD_A) open_selected();
            if (pressed & XINPUT_GAMEPAD_X) delete_selected();

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
                    open_selected();
                    break;
                case 'X':
                case 'x':
                case VK_DELETE:
                    delete_selected();
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
                    m_selected = static_cast<int>(m_messages.size()) - 1;
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
                    if (index >= 0 && index < static_cast<int>(m_messages.size()))
                    {
                        m_selected = index;
                        InvalidateRect(m_handle, nullptr, FALSE);
                        open_selected();
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

        std::vector<s_message> m_messages;
        int m_selected = 0;
        int m_scroll_offset = 0;
        controller_message_box m_msgbox;

        WORD m_previous_buttons = 0;
        DWORD m_last_nav_time = 0;
    };
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    c_message_center_window window;
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
