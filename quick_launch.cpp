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
#include <shellapi.h>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "msimg32.lib")
#pragma comment(lib, "xinput.lib")

namespace
{
    using namespace xbox360_ui;

    constexpr wchar_t k_window_class_name[] = L"pc_xbox360_quick_launch";
    constexpr int k_panel_left = 320;
    constexpr int k_panel_top = 70;
    constexpr int k_panel_right = 960;
    constexpr int k_panel_bottom = 630;

    constexpr int k_header_height = 56;
    constexpr int k_row_height = 64;
    constexpr int k_visible_rows = 7;
    constexpr int k_list_top = 145;
    enum class e_game_type
    {
        disc,
        arcade,
        indie,
        demo,
        installed
    };

    struct s_game
    {
        std::wstring title;
        e_game_type type = e_game_type::installed;
        std::wstring last_played;
        int achievements_unlocked = 0;
        int achievements_total = 0;
        bool disc_in_tray = false;
        std::wstring path;          // executable or file to open
        std::wstring args;
        std::wstring working_dir;
        std::wstring icon_path;     // optional .ico / image; empty = extract from path
        HICON icon = nullptr;       // loaded at runtime
    };

    wchar_t const* type_label(e_game_type t)
    {
        switch (t)
        {
        case e_game_type::disc:      return L"Disc";
        case e_game_type::arcade:    return L"Arcade";
        case e_game_type::indie:     return L"Indie";
        case e_game_type::demo:      return L"Demo";
        default:                     return L"Installed";
        }
    }

    COLORREF type_color(e_game_type t)
    {
        switch (t)
        {
        case e_game_type::disc:      return RGB(90, 160, 220);
        case e_game_type::arcade:    return RGB(180, 120, 220);
        case e_game_type::indie:     return RGB(100, 180, 160);
        case e_game_type::demo:      return RGB(200, 160, 80);
        default:                     return RGB(140, 148, 156);
        }
    }

    class c_quick_launch_window
    {
    public:
        c_quick_launch_window()
        {
            load_games();
        }

        void load_games()
        {
            m_games.clear();
            config cfg;
            if (!cfg.load_beside_exe(L"quick_launch.ini"))
            {
                // No config file — leave list empty (user must provide quick_launch.ini)
                return;
            }

            auto parse_type = [](std::wstring const& t) -> e_game_type {
                std::wstring s = t;
                for (auto& c : s) c = static_cast<wchar_t>(towlower(c));
                if (s == L"disc") return e_game_type::disc;
                if (s == L"arcade") return e_game_type::arcade;
                if (s == L"indie") return e_game_type::indie;
                if (s == L"demo") return e_game_type::demo;
                return e_game_type::installed;
            };

            for (auto const& sec : cfg.sections_with_prefix(L"game."))
            {
                s_game g;
                g.title = cfg.get(sec, L"title", L"Unknown");
                g.type = parse_type(cfg.get(sec, L"type", L"installed"));
                g.last_played = cfg.get(sec, L"last_played", L"");
                g.achievements_unlocked = cfg.get_int(sec, L"achievements", 0);
                g.achievements_total = cfg.get_int(sec, L"achievements_total", 0);
                g.disc_in_tray = cfg.get_bool(sec, L"disc_in_tray", false);
                g.path = cfg.get(sec, L"path");
                g.args = cfg.get(sec, L"args");
                g.working_dir = cfg.get(sec, L"working_dir");
                g.icon_path = cfg.get(sec, L"icon");
                g.icon = load_game_icon(g.icon_path, g.path);
                m_games.push_back(std::move(g));
            }
        }

        bool create()
        {
            bool result = false;

            WNDCLASSEXW window_class{};
            window_class.cbSize = sizeof(window_class);
            window_class.hInstance = GetModuleHandleW(nullptr);
            window_class.lpfnWndProc = &c_quick_launch_window::static_window_proc;
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
                    L"Quick Launch",
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
            c_quick_launch_window* instance =
                reinterpret_cast<c_quick_launch_window*>(GetWindowLongPtrW(window_handle, GWLP_USERDATA));

            if (message == WM_NCCREATE)
            {
                CREATESTRUCTW* create = reinterpret_cast<CREATESTRUCTW*>(l_param);
                instance = static_cast<c_quick_launch_window*>(create->lpCreateParams);
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
            m_row_font    = make_font(scale_y(18, height));
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

        void draw_face_badge(HDC dc, RECT rect, wchar_t letter, COLORREF fill)
        {
            draw_circle(dc, rect, fill);
            wchar_t buf[2]{ letter, L'\0' };
            draw_text(dc, m_badge_font, rect, buf, DT_CENTER | DT_VCENTER | DT_SINGLELINE, RGB(255, 255, 255));
        }


        static HICON load_game_icon(std::wstring const& icon_path, std::wstring const& exe_path)
        {
            if (!icon_path.empty())
            {
                HICON icon = static_cast<HICON>(LoadImageW(
                    nullptr, icon_path.c_str(), IMAGE_ICON,
                    0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE));
                if (icon)
                    return icon;
            }

            if (exe_path.empty())
                return nullptr;

            SHFILEINFOW sfi{};
            if (SHGetFileInfoW(
                    exe_path.c_str(), 0, &sfi, sizeof(sfi),
                    SHGFI_ICON | SHGFI_LARGEICON))
            {
                return sfi.hIcon;
            }

            HICON large = nullptr;
            if (ExtractIconExW(exe_path.c_str(), 0, &large, nullptr, 1) > 0 && large)
                return large;

            return nullptr;
        }

        void release_icons()
        {
            for (auto& g : m_games)
            {
                if (g.icon)
                {
                    DestroyIcon(g.icon);
                    g.icon = nullptr;
                }
            }
        }

        // Box art: file icon when available, else initials
        void draw_box_art(HDC dc, RECT rect, s_game const& game, bool selected)
        {
            COLORREF bg = selected ? RGB(60, 90, 40) : RGB(50, 56, 62);
            COLORREF border = selected ? RGB(120, 170, 60) : RGB(80, 88, 96);
            fill_round_rect(dc, rect, bg, border, scale_y(3, GetClientRectHeight()));

            if (game.icon)
            {
                int pad = scale_y(4, GetClientRectHeight());
                int size = (rect.bottom - rect.top) - pad * 2;
                if (size < 8) size = 8;
                int x = rect.left + (rect.right - rect.left - size) / 2;
                int y = rect.top + pad;
                DrawIconEx(dc, x, y, game.icon, size, size, 0, nullptr, DI_NORMAL);
                return;
            }

            // Initials fallback when no icon
            wchar_t initials[3]{};
            size_t n = 0;
            for (wchar_t c : game.title)
            {
                if ((c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9'))
                {
                    initials[n++] = (c >= L'a' && c <= L'z') ? static_cast<wchar_t>(c - 32) : c;
                    if (n >= 2) break;
                }
            }
            draw_text(dc, m_row_font, rect, initials, DT_CENTER | DT_VCENTER | DT_SINGLELINE,
                selected ? RGB(220, 240, 180) : RGB(160, 170, 180));
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
                L"Quick Launch", DT_LEFT | DT_VCENTER | DT_SINGLELINE, RGB(240, 242, 244));

            wchar_t count_buf[64];
            swprintf_s(count_buf, L"%d Games", static_cast<int>(m_games.size()));
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
            int last = __min(static_cast<int>(m_games.size()), first + k_visible_rows);

            for (int i = first; i < last; ++i)
            {
                int row_y = list_top + (i - first) * row_h;
                RECT row{ list_left, row_y, list_right, row_y + row_h - scale_y(4, height) };

                bool selected = (i == m_selected);
                COLORREF row_fill = selected ? RGB(70, 110, 40) : RGB(44, 48, 52);
                COLORREF row_border = selected ? RGB(110, 160, 60) : RGB(60, 66, 72);
                fill_round_rect(dc, row, row_fill, row_border, round_radius(width, height));

                s_game const& g = m_games[i];

                // Box art placeholder
                int art_size = row.bottom - row.top - scale_y(10, height);
                RECT art{
                    row.left + scale_x(8, width),
                    row.top + scale_y(5, height),
                    row.left + scale_x(8, width) + art_size,
                    row.top + scale_y(5, height) + art_size
                };
                draw_box_art(dc, art, g, selected);

                // Title
                RECT title_rect{
                    art.right + scale_x(12, width),
                    row.top + scale_y(6, height),
                    row.right - scale_x(100, width),
                    row.top + (row.bottom - row.top) / 2
                };
                COLORREF title_color = selected ? RGB(255, 255, 255) : RGB(230, 234, 238);
                draw_text(dc, m_row_font, title_rect, g.title.c_str(),
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, title_color);

                // Meta line: type · last played · achievements
                wchar_t meta[128];
                if (g.achievements_total > 0)
                    swprintf_s(meta, L"%s  |  %s  |  %d/%d Achievements",
                        type_label(g.type), g.last_played.c_str(),
                        g.achievements_unlocked, g.achievements_total);
                else
                    swprintf_s(meta, L"%s  |  %s", type_label(g.type), g.last_played.c_str());

                RECT meta_rect{
                    art.right + scale_x(12, width),
                    row.top + (row.bottom - row.top) / 2,
                    row.right - scale_x(100, width),
                    row.bottom - scale_y(6, height)
                };
                COLORREF meta_color = selected ? RGB(190, 210, 160) : RGB(130, 138, 146);
                draw_text(dc, m_meta_font, meta_rect, meta,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, meta_color);

                // Disc-in-tray badge
                if (g.disc_in_tray)
                {
                    RECT disc_badge{
                        row.right - scale_x(90, width),
                        row.top + (row.bottom - row.top - scale_y(22, height)) / 2,
                        row.right - scale_x(12, width),
                        row.top + (row.bottom - row.top + scale_y(22, height)) / 2
                    };
                    fill_round_rect(dc, disc_badge, RGB(50, 100, 160), RGB(70, 130, 200), scale_y(3, height));
                    draw_text(dc, m_badge_font, disc_badge, L"DISC",
                        DT_CENTER | DT_VCENTER | DT_SINGLELINE, RGB(220, 240, 255));
                }
            }

            // Scrollbar
            if (static_cast<int>(m_games.size()) > k_visible_rows)
            {
                int track_top = list_top;
                int track_bottom = list_top + k_visible_rows * row_h;
                int track_h = track_bottom - track_top;
                int thumb_h = __max(scale_y(24, height),
                    track_h * k_visible_rows / static_cast<int>(m_games.size()));
                int max_scroll = static_cast<int>(m_games.size()) - k_visible_rows;
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

            RECT footer_a{
                scale_x(360, width), scale_y(650, height),
                scale_x(520, width), scale_y(690, height)
            };
            RECT a_badge{
                footer_a.left,
                footer_a.top + (footer_a.bottom - footer_a.top - badge_size) / 2,
                footer_a.left + badge_size,
                footer_a.top + (footer_a.bottom - footer_a.top - badge_size) / 2 + badge_size
            };
            draw_face_badge(dc, a_badge, L'A', RGB(90, 176, 54));
            RECT a_label = footer_a;
            a_label.left = a_badge.right + scale_x(8, width);
            draw_text(dc, m_footer_font, a_label, L"Launch", DT_LEFT | DT_VCENTER | DT_SINGLELINE, RGB(230, 232, 234));

            RECT footer_b{
                scale_x(540, width), scale_y(650, height),
                scale_x(680, width), scale_y(690, height)
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

            RECT footer_y{
                scale_x(700, width), scale_y(650, height),
                scale_x(880, width), scale_y(690, height)
            };
            RECT y_badge{
                footer_y.left,
                footer_y.top + (footer_y.bottom - footer_y.top - badge_size) / 2,
                footer_y.left + badge_size,
                footer_y.top + (footer_y.bottom - footer_y.top - badge_size) / 2 + badge_size
            };
            draw_face_badge(dc, y_badge, L'Y', RGB(214, 161, 28));
            RECT y_label = footer_y;
            y_label.left = y_badge.right + scale_x(8, width);
            draw_text(dc, m_footer_font, y_label, L"Details", DT_LEFT | DT_VCENTER | DT_SINGLELINE, RGB(230, 232, 234));
        
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
            if (next >= static_cast<int>(m_games.size()))
                next = static_cast<int>(m_games.size()) - 1;
            if (next != m_selected)
            {
                m_selected = next;
                ensure_visible();
                InvalidateRect(m_handle, nullptr, FALSE);
            }
        }

        void launch_selected()
        {
            if (m_selected < 0 || m_selected >= static_cast<int>(m_games.size()))
                return;

            s_game const& g = m_games[m_selected];
            if (g.path.empty())
            {
                m_msgbox.show(
                    L"Quick Launch",
                    L"Failed to launch:\n" + g.title,
                    controller_message_box::buttons::ok,
                    L"A  OK");
                InvalidateRect(m_handle, nullptr, FALSE);
                return;
            }

            unsigned long running = find_running_process(g.path);
            if (running != 0)
            {
                m_msgbox.show(
                    L"Quick Launch",
                    g.title + L" is already running.\n\nClose it?",
                    controller_message_box::buttons::yes_no,
                    L"A  Close",
                    L"B  Cancel",
                    [this, running, title = g.title](controller_message_box::result r) {
                        if (r == controller_message_box::result::primary)
                        {
                            if (!terminate_process_id(running))
                            {
                                m_msgbox.show(
                                    L"Quick Launch",
                                    L"Could not close:\n" + title,
                                    controller_message_box::buttons::ok,
                                    L"A  OK");
                            }
                        }
                        InvalidateRect(m_handle, nullptr, FALSE);
                    });
                InvalidateRect(m_handle, nullptr, FALSE);
                return;
            }

            if (!launch_process(g.path, g.args, g.working_dir))
            {
                m_msgbox.show(
                    L"Quick Launch",
                    L"Failed to launch:\n" + g.title + L"\n\n" + g.path,
                    controller_message_box::buttons::ok,
                    L"A  OK");
                InvalidateRect(m_handle, nullptr, FALSE);
            }
        }

        void show_details()
        {
            if (m_selected < 0 || m_selected >= static_cast<int>(m_games.size()))
                return;

            s_game const& g = m_games[m_selected];
            wchar_t buf[256];
            swprintf_s(buf,
                L"%s\n\nType: %s\nLast played: %s\nAchievements: %d / %d%s",
                g.title.c_str(),
                type_label(g.type),
                g.last_played.c_str(),
                g.achievements_unlocked,
                g.achievements_total,
                g.disc_in_tray ? L"\nDisc in tray" : L"");
            m_msgbox.show(
                    L"Game Details",
                    buf,
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
            if (pressed & XINPUT_GAMEPAD_A) launch_selected();
            if (pressed & XINPUT_GAMEPAD_Y) show_details();

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
                    launch_selected();
                    break;
                case 'Y':
                case 'y':
                    show_details();
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
                    m_selected = static_cast<int>(m_games.size()) - 1;
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
                    if (index >= 0 && index < static_cast<int>(m_games.size()))
                    {
                        m_selected = index;
                        InvalidateRect(m_handle, nullptr, FALSE);
                        launch_selected();
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
                release_icons();
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

        std::vector<s_game> m_games;
        int m_selected = 0;
        int m_scroll_offset = 0;
        controller_message_box m_msgbox;

        WORD m_previous_buttons = 0;
        DWORD m_last_nav_time = 0;
    };
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    c_quick_launch_window window;
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
