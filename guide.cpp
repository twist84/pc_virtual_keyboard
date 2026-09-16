#include <windows.h>
#include <windowsx.h>
#include <xinput.h>
#include <wchar.h>
#include <algorithm>
#include <string>
#include <vector>

#include "virtual_keyboard.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "msimg32.lib")
#pragma comment(lib, "xinput.lib")

namespace
{
    constexpr wchar_t k_window_class_name[] = L"pc_xbox360_guide";
    constexpr int k_reference_width = 1280;
    constexpr int k_reference_height = 720;
    constexpr UINT k_controller_timer_id = 1;
    constexpr UINT k_controller_poll_ms = 16;
    constexpr SHORT k_stick_deadzone = 7849;

    // Guide sections (left blade)
    enum class e_section
    {
        home = 0,
        friends,
        messages,
        party,
        players,
        games,
        profile,
        count
    };

    wchar_t const* section_name(e_section s)
    {
        switch (s)
        {
        case e_section::home:     return L"Home";
        case e_section::friends:  return L"Friends";
        case e_section::messages: return L"Messages";
        case e_section::party:    return L"Party";
        case e_section::players:  return L"Players Met";
        case e_section::games:    return L"Quick Launch";
        case e_section::profile:  return L"Gamercard";
        default:                  return L"";
        }
    }

    // --- Shared data models ---
    enum class e_presence { online, away, busy, offline };

    struct s_friend {
        std::wstring gamertag;
        e_presence presence = e_presence::offline;
        std::wstring status;
        int gamerscore = 0;
        bool favorite = false;
    };

    enum class e_msg_type { text, voice, game_invite, friend_request, party_invite };
    struct s_message {
        e_msg_type type = e_msg_type::text;
        std::wstring from;
        std::wstring preview;
        std::wstring when;
        bool unread = true;
    };

    enum class e_member_status { leader, in_party, joining, invitable };
    struct s_member {
        std::wstring gamertag;
        e_member_status status = e_member_status::in_party;
        std::wstring activity;
        bool talking = false;
        bool muted = false;
    };

    struct s_player {
        std::wstring gamertag;
        e_presence presence = e_presence::offline;
        std::wstring game_met;
        std::wstring when_met;
        bool is_friend = false;
        bool feedback_sent = false;
    };

    enum class e_game_type { disc, arcade, indie, demo, installed };
    struct s_game {
        std::wstring title;
        e_game_type type = e_game_type::installed;
        std::wstring last_played;
        int achievements_unlocked = 0;
        int achievements_total = 0;
        bool disc_in_tray = false;
    };

    struct s_achievement {
        std::wstring name;
        std::wstring game;
        int gamerscore = 0;
    };

    // --- Scaling helpers ---
    int scale_x(int v, int w) { return MulDiv(v, w, k_reference_width); }
    int scale_y(int v, int h) { return MulDiv(v, h, k_reference_height); }
    RECT scale_rect(RECT r, int w, int h) {
        return RECT{ scale_x(r.left, w), scale_y(r.top, h), scale_x(r.right, w), scale_y(r.bottom, h) };
    }
    int round_radius(int w, int h) { return __max(2, scale_y(4, h)); }

    COLORREF presence_color(e_presence p) {
        switch (p) {
        case e_presence::online: return RGB(90, 176, 54);
        case e_presence::away:   return RGB(214, 161, 28);
        case e_presence::busy:   return RGB(196, 58, 48);
        default:                 return RGB(120, 124, 128);
        }
    }

    wchar_t const* msg_type_label(e_msg_type t) {
        switch (t) {
        case e_msg_type::text:           return L"Message";
        case e_msg_type::voice:          return L"Voice";
        case e_msg_type::game_invite:    return L"Game Invite";
        case e_msg_type::friend_request: return L"Friend Req";
        case e_msg_type::party_invite:   return L"Party Invite";
        default: return L"Message";
        }
    }

    COLORREF msg_type_color(e_msg_type t) {
        switch (t) {
        case e_msg_type::game_invite:    return RGB(90, 160, 220);
        case e_msg_type::friend_request: return RGB(90, 176, 54);
        case e_msg_type::party_invite:   return RGB(180, 120, 220);
        case e_msg_type::voice:          return RGB(214, 161, 28);
        default:                         return RGB(140, 148, 156);
        }
    }

    wchar_t const* game_type_label(e_game_type t) {
        switch (t) {
        case e_game_type::disc:   return L"Disc";
        case e_game_type::arcade: return L"Arcade";
        case e_game_type::indie:  return L"Indie";
        case e_game_type::demo:   return L"Demo";
        default:                  return L"Installed";
        }
    }

    class c_guide_window
    {
    public:
        c_guide_window()
        {
            // Friends
            m_friends = {
                { L"MajorNelson", e_presence::online, L"Playing Gears of War 2", 125480, true },
                { L"LarryHryb",   e_presence::online, L"Playing Halo 3", 98210, true },
                { L"Twister",     e_presence::online, L"Playing Left 4 Dead", 34200, true },
                { L"Rareware",    e_presence::online, L"Online", 45600, false },
                { L"Bungie",      e_presence::away,   L"Away", 87300, false },
                { L"EpicGames",   e_presence::busy,   L"Busy", 112000, false },
                { L"Arbiter",     e_presence::offline, L"Offline", 67000, false },
                { L"Cortana",     e_presence::offline, L"Last seen 2 hours ago", 18900, false },
            };

            // Messages
            m_messages = {
                { e_msg_type::game_invite, L"MajorNelson", L"Join Gears of War 2 multiplayer", L"Just now", true },
                { e_msg_type::party_invite, L"LarryHryb", L"Join my party", L"5 min ago", true },
                { e_msg_type::friend_request, L"Spartan117", L"Wants to be your friend", L"20 min ago", true },
                { e_msg_type::text, L"Zoey", L"GG on that last Left 4 Dead run!", L"1 hour ago", true },
                { e_msg_type::voice, L"MarcusFenix", L"Voice message (0:12)", L"2 hours ago", false },
                { e_msg_type::text, L"Rareware", L"New avatar items are up", L"Yesterday", false },
                { e_msg_type::game_invite, L"BlueKnight", L"Castle Crashers - 4 player co-op", L"Yesterday", false },
            };

            // Party
            m_members = {
                { L"Twister", e_member_status::leader, L"In party chat", true, false },
                { L"MajorNelson", e_member_status::in_party, L"Playing Gears of War 2", false, false },
                { L"LarryHryb", e_member_status::in_party, L"In party chat", true, false },
                { L"Rareware", e_member_status::joining, L"Connecting...", false, false },
                { L"Bungie", e_member_status::invitable, L"Online", false, false },
            };

            // Players met
            m_players = {
                { L"Spartan117", e_presence::online, L"Halo 3", L"Today", false, false },
                { L"MarcusFenix", e_presence::online, L"Gears of War 2", L"Today", false, false },
                { L"Zoey", e_presence::away, L"Left 4 Dead", L"Yesterday", true, false },
                { L"Bill", e_presence::offline, L"Left 4 Dead", L"Yesterday", false, false },
                { L"Francis", e_presence::offline, L"Left 4 Dead", L"Yesterday", false, true },
                { L"BlueKnight", e_presence::online, L"Castle Crashers", L"2 days ago", false, false },
                { L"DriftKing", e_presence::offline, L"Forza Motorsport 3", L"Last week", false, false },
            };

            // Games
            m_games = {
                { L"Halo 3", e_game_type::disc, L"Today", 42, 79, true },
                { L"Gears of War 2", e_game_type::installed, L"Yesterday", 38, 70, false },
                { L"Left 4 Dead", e_game_type::installed, L"2 days ago", 28, 50, false },
                { L"Castle Crashers", e_game_type::arcade, L"3 days ago", 12, 12, false },
                { L"Geometry Wars 2", e_game_type::arcade, L"Last week", 20, 20, false },
                { L"Braid", e_game_type::indie, L"Last week", 8, 12, false },
                { L"Shadow Complex", e_game_type::arcade, L"2 weeks ago", 12, 12, false },
            };

            // Profile
            m_gamertag = L"Twister";
            m_motto = L"Finish the fight.";
            m_location = L"Seattle, WA";
            m_bio = L"Xbox Live since day one. Halo, Gears, and too much Arcade.";
            m_gamerscore = 34200;
            m_reputation = L"Good";
            m_zone = L"Family";
            m_member_since = L"November 2005";
            m_games_played = 87;
            m_recent = {
                { L"Completed Campaign", L"Halo 3", 125 },
                { L"Around the World", L"Gears of War 2", 20 },
                { L"Zombie Genocide Master", L"Left 4 Dead", 50 },
                { L"The Final Showdown", L"Castle Crashers", 15 },
            };
        }

        bool create()
        {
            WNDCLASSEXW wc{};
            wc.cbSize = sizeof(wc);
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.lpfnWndProc = &c_guide_window::static_window_proc;
            wc.lpszClassName = k_window_class_name;
            wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);

            if (RegisterClassExW(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
                return false;

            int width = GetSystemMetrics(SM_CXSCREEN);
            int height = GetSystemMetrics(SM_CYSCREEN);

            m_handle = CreateWindowExW(
                WS_EX_APPWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED,
                k_window_class_name, L"Xbox Guide",
                WS_POPUP, 0, 0, width, height,
                nullptr, nullptr, GetModuleHandleW(nullptr), this);

            if (!m_handle) return false;

            SetLayeredWindowAttributes(m_handle, RGB(0, 0, 0), 0, LWA_COLORKEY);
            create_fonts();
            ShowWindow(m_handle, SW_SHOW);
            UpdateWindow(m_handle);
            SetFocus(m_handle);
            SetTimer(m_handle, k_controller_timer_id, k_controller_poll_ms, nullptr);
            return true;
        }

        void close()
        {
            if (m_handle) { DestroyWindow(m_handle); PostQuitMessage(0); }
        }

        static LRESULT CALLBACK static_window_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
        {
            c_guide_window* self = reinterpret_cast<c_guide_window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (msg == WM_NCCREATE) {
                auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
                self = static_cast<c_guide_window*>(cs->lpCreateParams);
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
                self->m_handle = hwnd;
            }
            return self ? self->window_proc(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
        }

    private:
        // --- Drawing primitives ---
        HFONT make_font(int h, int weight = FW_NORMAL) {
            return CreateFontW(h, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        }

        void create_fonts() {
            int h = client_h();
            m_title_font = make_font(scale_y(26, h), FW_SEMIBOLD);
            m_section_font = make_font(scale_y(18, h));
            m_row_font = make_font(scale_y(16, h));
            m_meta_font = make_font(scale_y(13, h));
            m_footer_font = make_font(scale_y(15, h));
            m_badge_font = make_font(scale_y(11, h), FW_SEMIBOLD);
            m_small_font = make_font(scale_y(12, h));
        }

        void release_fonts() {
            HFONT fonts[] = { m_title_font, m_section_font, m_row_font, m_meta_font, m_footer_font, m_badge_font, m_small_font };
            for (HFONT f : fonts) if (f) DeleteObject(f);
        }

        int client_w() const { RECT r{}; GetClientRect(m_handle, &r); return r.right; }
        int client_h() const { RECT r{}; GetClientRect(m_handle, &r); return r.bottom; }

        void fill_rect(HDC dc, RECT r, COLORREF c) const {
            HBRUSH b = CreateSolidBrush(c); FillRect(dc, &r, b); DeleteObject(b);
        }

        void fill_round_rect(HDC dc, RECT r, COLORREF fill, COLORREF border, int radius) const {
            HBRUSH b = CreateSolidBrush(fill);
            HPEN p = CreatePen(PS_SOLID, 1, border);
            auto ob = static_cast<HBRUSH>(SelectObject(dc, b));
            auto op = static_cast<HPEN>(SelectObject(dc, p));
            RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
            SelectObject(dc, ob); SelectObject(dc, op);
            DeleteObject(p); DeleteObject(b);
        }

        void draw_text(HDC dc, HFONT font, RECT r, wchar_t const* text, UINT fmt, COLORREF color) const {
            auto old = static_cast<HFONT>(SelectObject(dc, font));
            COLORREF oc = SetTextColor(dc, color);
            int om = SetBkMode(dc, TRANSPARENT);
            DrawTextW(dc, text ? text : L"", -1, &r, fmt);
            SetBkMode(dc, om); SetTextColor(dc, oc); SelectObject(dc, old);
        }

        void draw_circle(HDC dc, RECT r, COLORREF fill) const {
            HBRUSH b = CreateSolidBrush(fill);
            HPEN p = CreatePen(PS_SOLID, 1, fill);
            auto ob = static_cast<HBRUSH>(SelectObject(dc, b));
            auto op = static_cast<HPEN>(SelectObject(dc, p));
            Ellipse(dc, r.left, r.top, r.right, r.bottom);
            SelectObject(dc, op); SelectObject(dc, ob);
            DeleteObject(p); DeleteObject(b);
        }

        void draw_face_badge(HDC dc, RECT r, wchar_t letter, COLORREF fill) {
            draw_circle(dc, r, fill);
            wchar_t buf[2]{ letter, 0 };
            draw_text(dc, m_badge_font, r, buf, DT_CENTER | DT_VCENTER | DT_SINGLELINE, RGB(255,255,255));
        }

        // --- Layout constants (reference space) ---
        static constexpr int k_blade_left = 40;
        static constexpr int k_blade_width = 220;
        static constexpr int k_content_left = 280;
        static constexpr int k_content_right = 1240;
        static constexpr int k_panel_top = 40;
        static constexpr int k_panel_bottom = 680;

        // --- Paint: Guide blade (left nav) ---
        void paint_blade(HDC dc, int w, int h)
        {
            RECT blade = scale_rect(RECT{ k_blade_left, k_panel_top, k_blade_left + k_blade_width, k_panel_bottom }, w, h);
            fill_round_rect(dc, blade, RGB(28, 32, 36), RGB(60, 66, 72), round_radius(w, h) * 2);

            // Xbox-style header on blade
            RECT hdr = blade;
            hdr.bottom = blade.top + scale_y(48, h);
            fill_rect(dc, hdr, RGB(40, 48, 40));
            draw_text(dc, m_title_font, hdr, L"Guide", DT_CENTER | DT_VCENTER | DT_SINGLELINE, RGB(140, 200, 80));

            int row_h = scale_y(44, h);
            int y0 = hdr.bottom + scale_y(8, h);

            for (int i = 0; i < static_cast<int>(e_section::count); ++i)
            {
                e_section s = static_cast<e_section>(i);
                RECT row{ blade.left + scale_x(6, w), y0 + i * row_h, blade.right - scale_x(6, w), y0 + (i + 1) * row_h - scale_y(4, h) };

                bool selected = (s == m_section);
                bool focused = (m_focus_blade && selected);

                if (focused)
                    fill_round_rect(dc, row, RGB(70, 110, 40), RGB(110, 160, 60), round_radius(w, h));
                else if (selected)
                    fill_round_rect(dc, row, RGB(50, 70, 40), RGB(80, 110, 50), round_radius(w, h));

                COLORREF tc = focused ? RGB(255,255,255) : (selected ? RGB(200, 230, 160) : RGB(180, 186, 192));
                draw_text(dc, m_section_font, row, section_name(s), DT_CENTER | DT_VCENTER | DT_SINGLELINE, tc);

                // Unread badge on Messages
                if (s == e_section::messages) {
                    int unread = 0;
                    for (auto const& m : m_messages) if (m.unread) ++unread;
                    if (unread > 0) {
                        RECT badge{ row.right - scale_x(28, w), row.top + scale_y(10, h), row.right - scale_x(8, w), row.bottom - scale_y(10, h) };
                        fill_round_rect(dc, badge, RGB(90, 176, 54), RGB(90, 176, 54), scale_y(8, h));
                        wchar_t buf[8]; swprintf_s(buf, L"%d", unread);
                        draw_text(dc, m_badge_font, badge, buf, DT_CENTER | DT_VCENTER | DT_SINGLELINE, RGB(255,255,255));
                    }
                }
            }
        }

        // --- Paint: content panels ---
        void paint_home(HDC dc, RECT content, int w, int h)
        {
            draw_text(dc, m_title_font,
                RECT{ content.left + scale_x(20, w), content.top + scale_y(16, h), content.right - scale_x(20, w), content.top + scale_y(56, h) },
                L"Home", DT_LEFT | DT_VCENTER | DT_SINGLELINE, RGB(240, 242, 244));

            // Summary cards
            int online = 0;
            for (auto const& f : m_friends)
                if (f.presence == e_presence::online || f.presence == e_presence::away || f.presence == e_presence::busy) ++online;
            int unread = 0;
            for (auto const& m : m_messages) if (m.unread) ++unread;
            int party_n = 0;
            for (auto const& m : m_members)
                if (m.status == e_member_status::leader || m.status == e_member_status::in_party || m.status == e_member_status::joining) ++party_n;

            struct card { wchar_t const* title; std::wstring value; };
            card cards[] = {
                { L"Gamertag", m_gamertag },
                { L"Gamerscore", std::to_wstring(m_gamerscore) + L" G" },
                { L"Friends Online", std::to_wstring(online) },
                { L"Unread Messages", std::to_wstring(unread) },
                { L"Party", std::to_wstring(party_n) + L" / 8" },
                { L"Games", std::to_wstring(m_games.size()) },
            };

            int card_w = scale_x(200, w);
            int card_h = scale_y(80, h);
            int gap = scale_x(12, w);
            int start_x = content.left + scale_x(20, w);
            int start_y = content.top + scale_y(70, h);

            for (int i = 0; i < 6; ++i) {
                int col = i % 3;
                int row = i / 3;
                RECT box{
                    start_x + col * (card_w + gap),
                    start_y + row * (card_h + scale_y(12, h)),
                    start_x + col * (card_w + gap) + card_w,
                    start_y + row * (card_h + scale_y(12, h)) + card_h
                };
                fill_round_rect(dc, box, RGB(44, 48, 52), RGB(60, 66, 72), round_radius(w, h));
                RECT label{ box.left, box.top + scale_y(10, h), box.right, box.top + scale_y(32, h) };
                draw_text(dc, m_meta_font, label, cards[i].title, DT_CENTER | DT_VCENTER | DT_SINGLELINE, RGB(140, 148, 156));
                RECT val{ box.left, box.top + scale_y(32, h), box.right, box.bottom - scale_y(10, h) };
                draw_text(dc, m_section_font, val, cards[i].value.c_str(), DT_CENTER | DT_VCENTER | DT_SINGLELINE, RGB(230, 234, 238));
            }

            // Hint
            RECT hint{ content.left + scale_x(20, w), content.bottom - scale_y(50, h), content.right - scale_x(20, w), content.bottom - scale_y(20, h) };
            draw_text(dc, m_meta_font, hint, L"Use D-pad Up/Down to switch sections  |  A to enter  |  B to close Guide",
                DT_CENTER | DT_VCENTER | DT_SINGLELINE, RGB(120, 128, 136));
        }

        void paint_list_header(HDC dc, RECT content, int w, int h, wchar_t const* title, wchar_t const* right)
        {
            RECT hdr{ content.left, content.top, content.right, content.top + scale_y(48, h) };
            fill_rect(dc, hdr, RGB(48, 54, 60));
            draw_text(dc, m_title_font,
                RECT{ hdr.left + scale_x(16, w), hdr.top, hdr.right - scale_x(16, w), hdr.bottom },
                title, DT_LEFT | DT_VCENTER | DT_SINGLELINE, RGB(240, 242, 244));
            if (right)
                draw_text(dc, m_meta_font,
                    RECT{ hdr.left + scale_x(16, w), hdr.top, hdr.right - scale_x(16, w), hdr.bottom },
                    right, DT_RIGHT | DT_VCENTER | DT_SINGLELINE, RGB(160, 168, 176));
        }

        void paint_friends(HDC dc, RECT content, int w, int h)
        {
            int online = 0;
            for (auto const& f : m_friends)
                if (f.presence != e_presence::offline) ++online;
            wchar_t right[32]; swprintf_s(right, L"%d Online", online);
            paint_list_header(dc, content, w, h, L"Friends", right);

            int list_top = content.top + scale_y(56, h);
            int row_h = scale_y(48, h);
            int visible = 9;
            int first = m_scroll;
            int last = __min(static_cast<int>(m_friends.size()), first + visible);

            for (int i = first; i < last; ++i) {
                int y = list_top + (i - first) * row_h;
                RECT row{ content.left + scale_x(10, w), y, content.right - scale_x(10, w), y + row_h - scale_y(4, h) };
                bool sel = (!m_focus_blade && i == m_selected);
                fill_round_rect(dc, row, sel ? RGB(70, 110, 40) : RGB(44, 48, 52),
                    sel ? RGB(110, 160, 60) : RGB(60, 66, 72), round_radius(w, h));

                auto const& f = m_friends[i];
                int ds = scale_y(11, h);
                RECT dot{ row.left + scale_x(12, w), row.top + (row.bottom - row.top - ds) / 2,
                          row.left + scale_x(12, w) + ds, row.top + (row.bottom - row.top - ds) / 2 + ds };
                draw_circle(dc, dot, presence_color(f.presence));

                RECT tag{ row.left + scale_x(32, w), row.top, row.right - scale_x(100, w), row.top + (row.bottom - row.top) * 3 / 5 };
                draw_text(dc, m_row_font, tag, f.gamertag.c_str(), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
                    sel ? RGB(255,255,255) : RGB(230, 234, 238));

                RECT st{ row.left + scale_x(32, w), row.top + (row.bottom - row.top) * 2 / 5, row.right - scale_x(100, w), row.bottom };
                draw_text(dc, m_meta_font, st, f.status.c_str(), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
                    sel ? RGB(190, 210, 160) : RGB(130, 138, 146));

                wchar_t gs[24]; swprintf_s(gs, L"%d G", f.gamerscore);
                RECT gsr{ row.right - scale_x(90, w), row.top, row.right - scale_x(10, w), row.bottom };
                draw_text(dc, m_meta_font, gsr, gs, DT_RIGHT | DT_VCENTER | DT_SINGLELINE, RGB(140, 148, 156));
            }
        }

        void paint_messages(HDC dc, RECT content, int w, int h)
        {
            int unread = 0;
            for (auto const& m : m_messages) if (m.unread) ++unread;
            wchar_t right[32];
            if (unread) swprintf_s(right, L"%d Unread", unread);
            else swprintf_s(right, L"%d Messages", static_cast<int>(m_messages.size()));
            paint_list_header(dc, content, w, h, L"Message Center", right);

            int list_top = content.top + scale_y(56, h);
            int row_h = scale_y(56, h);
            int visible = 8;
            int first = m_scroll;
            int last = __min(static_cast<int>(m_messages.size()), first + visible);

            for (int i = first; i < last; ++i) {
                int y = list_top + (i - first) * row_h;
                RECT row{ content.left + scale_x(10, w), y, content.right - scale_x(10, w), y + row_h - scale_y(4, h) };
                bool sel = (!m_focus_blade && i == m_selected);
                auto const& msg = m_messages[i];
                fill_round_rect(dc, row, sel ? RGB(70, 110, 40) : (msg.unread ? RGB(48, 54, 58) : RGB(40, 44, 48)),
                    sel ? RGB(110, 160, 60) : RGB(60, 66, 72), round_radius(w, h));

                if (msg.unread) {
                    RECT bar{ row.left + scale_x(3, w), row.top + scale_y(6, h), row.left + scale_x(7, w), row.bottom - scale_y(6, h) };
                    fill_rect(dc, bar, RGB(90, 176, 54));
                }

                RECT badge{ row.left + scale_x(14, w), row.top + scale_y(6, h), row.left + scale_x(110, w), row.top + scale_y(26, h) };
                fill_round_rect(dc, badge, msg_type_color(msg.type), msg_type_color(msg.type), scale_y(3, h));
                draw_text(dc, m_badge_font, badge, msg_type_label(msg.type), DT_CENTER | DT_VCENTER | DT_SINGLELINE, RGB(255,255,255));

                RECT from{ badge.right + scale_x(8, w), row.top + scale_y(4, h), row.right - scale_x(90, w), row.top + scale_y(26, h) };
                draw_text(dc, m_row_font, from, msg.from.c_str(), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
                    sel ? RGB(255,255,255) : RGB(230, 234, 238));

                RECT when{ row.right - scale_x(90, w), row.top + scale_y(4, h), row.right - scale_x(10, w), row.top + scale_y(26, h) };
                draw_text(dc, m_meta_font, when, msg.when.c_str(), DT_RIGHT | DT_VCENTER | DT_SINGLELINE, RGB(140, 148, 156));

                RECT prev{ row.left + scale_x(14, w), row.top + scale_y(28, h), row.right - scale_x(10, w), row.bottom - scale_y(4, h) };
                draw_text(dc, m_meta_font, prev, msg.preview.c_str(), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
                    sel ? RGB(190, 210, 160) : RGB(130, 138, 146));
            }
        }

        void paint_party(HDC dc, RECT content, int w, int h)
        {
            int n = 0;
            for (auto const& m : m_members)
                if (m.status != e_member_status::invitable) ++n;
            wchar_t right[16]; swprintf_s(right, L"%d / 8", n);
            paint_list_header(dc, content, w, h, L"Party", right);

            RECT banner{ content.left + scale_x(10, w), content.top + scale_y(56, h), content.right - scale_x(10, w), content.top + scale_y(96, h) };
            fill_round_rect(dc, banner, RGB(50, 80, 40), RGB(90, 140, 60), round_radius(w, h));
            draw_text(dc, m_row_font, banner, L"Party active  -  Chat open across games",
                DT_CENTER | DT_VCENTER | DT_SINGLELINE, RGB(200, 230, 160));

            int list_top = content.top + scale_y(108, h);
            int row_h = scale_y(48, h);
            int visible = 7;
            int first = m_scroll;
            int last = __min(static_cast<int>(m_members.size()), first + visible);

            for (int i = first; i < last; ++i) {
                int y = list_top + (i - first) * row_h;
                RECT row{ content.left + scale_x(10, w), y, content.right - scale_x(10, w), y + row_h - scale_y(4, h) };
                bool sel = (!m_focus_blade && i == m_selected);
                fill_round_rect(dc, row, sel ? RGB(70, 110, 40) : RGB(44, 48, 52),
                    sel ? RGB(110, 160, 60) : RGB(60, 66, 72), round_radius(w, h));

                auto const& m = m_members[i];
                int ds = scale_y(11, h);
                RECT dot{ row.left + scale_x(12, w), row.top + (row.bottom - row.top - ds) / 2,
                          row.left + scale_x(12, w) + ds, row.top + (row.bottom - row.top - ds) / 2 + ds };
                COLORREF dc_col = RGB(120, 124, 128);
                if (m.status == e_member_status::leader || m.status == e_member_status::in_party)
                    dc_col = m.talking ? RGB(90, 200, 90) : RGB(90, 160, 50);
                else if (m.status == e_member_status::joining)
                    dc_col = RGB(214, 161, 28);
                draw_circle(dc, dot, dc_col);

                RECT tag{ row.left + scale_x(32, w), row.top, row.right - scale_x(110, w), row.top + (row.bottom - row.top) * 3 / 5 };
                draw_text(dc, m_row_font, tag, m.gamertag.c_str(), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
                    sel ? RGB(255,255,255) : RGB(230, 234, 238));

                RECT act{ row.left + scale_x(32, w), row.top + (row.bottom - row.top) * 2 / 5, row.right - scale_x(110, w), row.bottom };
                draw_text(dc, m_meta_font, act, m.activity.c_str(), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
                    sel ? RGB(190, 210, 160) : RGB(130, 138, 146));

                wchar_t const* st =
                    m.status == e_member_status::leader ? L"Leader" :
                    m.status == e_member_status::in_party ? L"In Party" :
                    m.status == e_member_status::joining ? L"Joining" : L"Invite";
                RECT sr{ row.right - scale_x(100, w), row.top, row.right - scale_x(10, w), row.bottom };
                draw_text(dc, m_meta_font, sr, st, DT_RIGHT | DT_VCENTER | DT_SINGLELINE, RGB(140, 148, 156));
            }
        }

        void paint_players(HDC dc, RECT content, int w, int h)
        {
            wchar_t right[24]; swprintf_s(right, L"%d Players", static_cast<int>(m_players.size()));
            paint_list_header(dc, content, w, h, L"Players Met", right);

            int list_top = content.top + scale_y(56, h);
            int row_h = scale_y(48, h);
            int visible = 9;
            int first = m_scroll;
            int last = __min(static_cast<int>(m_players.size()), first + visible);

            for (int i = first; i < last; ++i) {
                int y = list_top + (i - first) * row_h;
                RECT row{ content.left + scale_x(10, w), y, content.right - scale_x(10, w), y + row_h - scale_y(4, h) };
                bool sel = (!m_focus_blade && i == m_selected);
                fill_round_rect(dc, row, sel ? RGB(70, 110, 40) : RGB(44, 48, 52),
                    sel ? RGB(110, 160, 60) : RGB(60, 66, 72), round_radius(w, h));

                auto const& p = m_players[i];
                int ds = scale_y(11, h);
                RECT dot{ row.left + scale_x(12, w), row.top + (row.bottom - row.top - ds) / 2,
                          row.left + scale_x(12, w) + ds, row.top + (row.bottom - row.top - ds) / 2 + ds };
                draw_circle(dc, dot, presence_color(p.presence));

                RECT tag{ row.left + scale_x(32, w), row.top, row.right - scale_x(150, w), row.top + (row.bottom - row.top) * 3 / 5 };
                draw_text(dc, m_row_font, tag, p.gamertag.c_str(), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
                    sel ? RGB(255,255,255) : RGB(230, 234, 238));

                std::wstring meta = L"Met in " + p.game_met + L"  |  " + p.when_met;
                RECT mr{ row.left + scale_x(32, w), row.top + (row.bottom - row.top) * 2 / 5, row.right - scale_x(150, w), row.bottom };
                draw_text(dc, m_meta_font, mr, meta.c_str(), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
                    sel ? RGB(190, 210, 160) : RGB(130, 138, 146));

                int bx = row.right - scale_x(10, w);
                if (p.is_friend) {
                    RECT b{ bx - scale_x(60, w), row.top + (row.bottom - row.top - scale_y(18, h)) / 2, bx, row.top + (row.bottom - row.top + scale_y(18, h)) / 2 };
                    fill_round_rect(dc, b, RGB(50, 100, 160), RGB(70, 130, 200), scale_y(3, h));
                    draw_text(dc, m_badge_font, b, L"FRIEND", DT_CENTER | DT_VCENTER | DT_SINGLELINE, RGB(220, 240, 255));
                    bx = b.left - scale_x(4, w);
                }
                if (p.feedback_sent) {
                    RECT b{ bx - scale_x(70, w), row.top + (row.bottom - row.top - scale_y(18, h)) / 2, bx, row.top + (row.bottom - row.top + scale_y(18, h)) / 2 };
                    fill_round_rect(dc, b, RGB(80, 80, 50), RGB(120, 120, 70), scale_y(3, h));
                    draw_text(dc, m_badge_font, b, L"FEEDBACK", DT_CENTER | DT_VCENTER | DT_SINGLELINE, RGB(220, 220, 160));
                }
            }
        }

        void paint_games(HDC dc, RECT content, int w, int h)
        {
            wchar_t right[24]; swprintf_s(right, L"%d Games", static_cast<int>(m_games.size()));
            paint_list_header(dc, content, w, h, L"Quick Launch", right);

            int list_top = content.top + scale_y(56, h);
            int row_h = scale_y(56, h);
            int visible = 8;
            int first = m_scroll;
            int last = __min(static_cast<int>(m_games.size()), first + visible);

            for (int i = first; i < last; ++i) {
                int y = list_top + (i - first) * row_h;
                RECT row{ content.left + scale_x(10, w), y, content.right - scale_x(10, w), y + row_h - scale_y(4, h) };
                bool sel = (!m_focus_blade && i == m_selected);
                fill_round_rect(dc, row, sel ? RGB(70, 110, 40) : RGB(44, 48, 52),
                    sel ? RGB(110, 160, 60) : RGB(60, 66, 72), round_radius(w, h));

                auto const& g = m_games[i];
                int art = row.bottom - row.top - scale_y(10, h);
                RECT box{ row.left + scale_x(8, w), row.top + scale_y(5, h), row.left + scale_x(8, w) + art, row.top + scale_y(5, h) + art };
                fill_round_rect(dc, box, sel ? RGB(60, 90, 40) : RGB(50, 56, 62), sel ? RGB(120, 170, 60) : RGB(80, 88, 96), scale_y(3, h));

                RECT title{ box.right + scale_x(10, w), row.top + scale_y(6, h), row.right - scale_x(80, w), row.top + (row.bottom - row.top) / 2 };
                draw_text(dc, m_row_font, title, g.title.c_str(), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
                    sel ? RGB(255,255,255) : RGB(230, 234, 238));

                wchar_t meta[96];
                if (g.achievements_total > 0)
                    swprintf_s(meta, L"%s  |  %s  |  %d/%d Achievements", game_type_label(g.type), g.last_played.c_str(), g.achievements_unlocked, g.achievements_total);
                else
                    swprintf_s(meta, L"%s  |  %s", game_type_label(g.type), g.last_played.c_str());
                RECT mr{ box.right + scale_x(10, w), row.top + (row.bottom - row.top) / 2, row.right - scale_x(80, w), row.bottom - scale_y(6, h) };
                draw_text(dc, m_meta_font, mr, meta, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
                    sel ? RGB(190, 210, 160) : RGB(130, 138, 146));

                if (g.disc_in_tray) {
                    RECT db{ row.right - scale_x(70, w), row.top + (row.bottom - row.top - scale_y(20, h)) / 2,
                             row.right - scale_x(10, w), row.top + (row.bottom - row.top + scale_y(20, h)) / 2 };
                    fill_round_rect(dc, db, RGB(50, 100, 160), RGB(70, 130, 200), scale_y(3, h));
                    draw_text(dc, m_badge_font, db, L"DISC", DT_CENTER | DT_VCENTER | DT_SINGLELINE, RGB(220, 240, 255));
                }
            }
        }

        void paint_profile(HDC dc, RECT content, int w, int h)
        {
            paint_list_header(dc, content, w, h, L"Gamercard", nullptr);

            int pic = scale_y(90, h);
            RECT pic_r{ content.left + scale_x(20, w), content.top + scale_y(64, h),
                        content.left + scale_x(20, w) + pic, content.top + scale_y(64, h) + pic };
            fill_round_rect(dc, pic_r, RGB(55, 85, 45), RGB(90, 140, 60), scale_y(6, h));
            draw_text(dc, m_title_font, pic_r, L"TW", DT_CENTER | DT_VCENTER | DT_SINGLELINE, RGB(200, 230, 160));

            RECT tag{ pic_r.right + scale_x(16, w), pic_r.top, content.right - scale_x(20, w), pic_r.top + scale_y(30, h) };
            draw_text(dc, m_title_font, tag, m_gamertag.c_str(), DT_LEFT | DT_VCENTER | DT_SINGLELINE, RGB(255,255,255));

            RECT motto{ pic_r.right + scale_x(16, w), tag.bottom, content.right - scale_x(20, w), tag.bottom + scale_y(22, h) };
            draw_text(dc, m_row_font, motto, m_motto.c_str(), DT_LEFT | DT_VCENTER | DT_SINGLELINE, RGB(180, 190, 160));

            RECT loc{ pic_r.right + scale_x(16, w), motto.bottom, content.right - scale_x(20, w), motto.bottom + scale_y(20, h) };
            std::wstring loc_s = m_location + L"  |  Member since " + m_member_since;
            draw_text(dc, m_meta_font, loc, loc_s.c_str(), DT_LEFT | DT_VCENTER | DT_SINGLELINE, RGB(140, 148, 156));

            // Stats
            int st_top = pic_r.bottom + scale_y(16, h);
            int st_h = scale_y(60, h);
            int st_gap = scale_x(10, w);
            int st_w = (content.right - content.left - scale_x(40, w) - 3 * st_gap) / 4;
            struct { wchar_t const* l; std::wstring v; } stats[] = {
                { L"Gamerscore", std::to_wstring(m_gamerscore) + L" G" },
                { L"Reputation", m_reputation },
                { L"Zone", m_zone },
                { L"Games", std::to_wstring(m_games_played) },
            };
            for (int i = 0; i < 4; ++i) {
                RECT box{ content.left + scale_x(20, w) + i * (st_w + st_gap), st_top,
                          content.left + scale_x(20, w) + i * (st_w + st_gap) + st_w, st_top + st_h };
                fill_round_rect(dc, box, RGB(44, 48, 52), RGB(60, 66, 72), round_radius(w, h));
                RECT lr{ box.left, box.top + scale_y(6, h), box.right, box.top + scale_y(24, h) };
                draw_text(dc, m_meta_font, lr, stats[i].l, DT_CENTER | DT_VCENTER | DT_SINGLELINE, RGB(140, 148, 156));
                RECT vr{ box.left, box.top + scale_y(24, h), box.right, box.bottom - scale_y(6, h) };
                draw_text(dc, m_row_font, vr, stats[i].v.c_str(), DT_CENTER | DT_VCENTER | DT_SINGLELINE, RGB(230, 234, 238));
            }

            // Bio
            int bio_y = st_top + st_h + scale_y(14, h);
            RECT bio_l{ content.left + scale_x(20, w), bio_y, content.right - scale_x(20, w), bio_y + scale_y(20, h) };
            draw_text(dc, m_badge_font, bio_l, L"BIO", DT_LEFT | DT_VCENTER | DT_SINGLELINE, RGB(120, 160, 80));
            RECT bio_b{ content.left + scale_x(20, w), bio_l.bottom + scale_y(2, h), content.right - scale_x(20, w), bio_l.bottom + scale_y(40, h) };
            draw_text(dc, m_row_font, bio_b, m_bio.c_str(), DT_LEFT | DT_WORDBREAK, RGB(200, 206, 212));

            // Recent achievements
            int ach_y = bio_b.bottom + scale_y(10, h);
            RECT ach_l{ content.left + scale_x(20, w), ach_y, content.right - scale_x(20, w), ach_y + scale_y(20, h) };
            draw_text(dc, m_badge_font, ach_l, L"RECENT ACHIEVEMENTS", DT_LEFT | DT_VCENTER | DT_SINGLELINE, RGB(120, 160, 80));

            int rh = scale_y(32, h);
            for (size_t i = 0; i < m_recent.size(); ++i) {
                int y = ach_l.bottom + scale_y(4, h) + static_cast<int>(i) * rh;
                RECT row{ content.left + scale_x(20, w), y, content.right - scale_x(20, w), y + rh - scale_y(3, h) };
                fill_round_rect(dc, row, RGB(44, 48, 52), RGB(60, 66, 72), round_radius(w, h));
                RECT nr{ row.left + scale_x(10, w), row.top, row.right - scale_x(140, w), row.bottom };
                draw_text(dc, m_meta_font, nr, m_recent[i].name.c_str(), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, RGB(230, 234, 238));
                wchar_t right[64]; swprintf_s(right, L"%s  +%d G", m_recent[i].game.c_str(), m_recent[i].gamerscore);
                RECT rr{ row.right - scale_x(180, w), row.top, row.right - scale_x(10, w), row.bottom };
                draw_text(dc, m_small_font, rr, right, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, RGB(140, 160, 120));
            }
        }

        void paint_footer(HDC dc, int w, int h)
        {
            int badge = __max(16, scale_y(20, h));
            auto foot = [&](int x, wchar_t letter, COLORREF col, wchar_t const* label) {
                RECT area{ scale_x(x, w), scale_y(690, h), scale_x(x + 130, w), scale_y(720, h) };
                RECT b{ area.left, area.top + (area.bottom - area.top - badge) / 2,
                        area.left + badge, area.top + (area.bottom - area.top - badge) / 2 + badge };
                draw_face_badge(dc, b, letter, col);
                RECT lbl = area; lbl.left = b.right + scale_x(6, w);
                draw_text(dc, m_footer_font, lbl, label, DT_LEFT | DT_VCENTER | DT_SINGLELINE, RGB(230, 232, 234));
            };

            if (m_focus_blade) {
                foot(300, L'A', RGB(90, 176, 54), L"Select");
                foot(460, L'B', RGB(196, 58, 48), L"Close Guide");
            } else {
                foot(280, L'A', RGB(90, 176, 54), L"Select");
                foot(440, L'B', RGB(196, 58, 48), L"Back");
                foot(580, L'Y', RGB(214, 161, 28), L"Blade");
            }
        }

        void paint(HDC dc)
        {
            int w = client_w(), h = client_h();
            RECT client{}; GetClientRect(m_handle, &client);
            fill_rect(dc, client, RGB(0, 0, 0));

            paint_blade(dc, w, h);

            RECT content = scale_rect(RECT{ k_content_left, k_panel_top, k_content_right, k_panel_bottom }, w, h);
            fill_round_rect(dc, content, RGB(32, 36, 40), RGB(70, 76, 82), round_radius(w, h) * 2);

            switch (m_section) {
            case e_section::home:     paint_home(dc, content, w, h); break;
            case e_section::friends:  paint_friends(dc, content, w, h); break;
            case e_section::messages: paint_messages(dc, content, w, h); break;
            case e_section::party:    paint_party(dc, content, w, h); break;
            case e_section::players:  paint_players(dc, content, w, h); break;
            case e_section::games:    paint_games(dc, content, w, h); break;
            case e_section::profile:  paint_profile(dc, content, w, h); break;
            default: break;
            }

            paint_footer(dc, w, h);
            paint_modal(dc, w, h);
        }

        void paint_modal(HDC dc, int w, int h)
        {
            if (m_modal == e_modal::none)
                return;

            RECT client{};
            GetClientRect(m_handle, &client);

            // Centered modal card (guide remains visible behind)
            int mw = scale_x(420, w);
            int mh = scale_y(220, h);
            RECT card{
                (client.right - mw) / 2,
                (client.bottom - mh) / 2,
                (client.right + mw) / 2,
                (client.bottom + mh) / 2
            };
            fill_round_rect(dc, card, RGB(40, 44, 48), RGB(90, 140, 60), round_radius(w, h) * 2);

            // Title bar
            RECT title_r{ card.left, card.top, card.right, card.top + scale_y(44, h) };
            fill_rect(dc, title_r, RGB(50, 70, 40));
            draw_text(dc, m_title_font, title_r, m_modal_title.c_str(),
                DT_CENTER | DT_VCENTER | DT_SINGLELINE, RGB(220, 240, 180));

            // Body
            RECT body_r{
                card.left + scale_x(20, w),
                title_r.bottom + scale_y(12, h),
                card.right - scale_x(20, w),
                card.bottom - scale_y(70, h)
            };
            draw_text(dc, m_row_font, body_r, m_modal_body.c_str(),
                DT_CENTER | DT_WORDBREAK, RGB(220, 224, 228));

            // Buttons
            int bw = scale_x(140, w);
            int bh = scale_y(40, h);
            int by = card.bottom - scale_y(56, h);

            if (m_modal == e_modal::message_actions)
            {
                int gap = scale_x(16, w);
                RECT btn_reply{
                    card.left + (card.right - card.left - 2 * bw - gap) / 2,
                    by,
                    card.left + (card.right - card.left - 2 * bw - gap) / 2 + bw,
                    by + bh
                };
                RECT btn_close{ btn_reply.right + gap, by, btn_reply.right + gap + bw, by + bh };

                bool reply_sel = (m_modal_choice == 0);
                bool close_sel = (m_modal_choice == 1);

                fill_round_rect(dc, btn_reply,
                    reply_sel ? RGB(70, 110, 40) : RGB(50, 54, 58),
                    reply_sel ? RGB(110, 160, 60) : RGB(70, 76, 82),
                    round_radius(w, h));
                fill_round_rect(dc, btn_close,
                    close_sel ? RGB(70, 110, 40) : RGB(50, 54, 58),
                    close_sel ? RGB(110, 160, 60) : RGB(70, 76, 82),
                    round_radius(w, h));

                draw_text(dc, m_row_font, btn_reply, L"A  Reply",
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE,
                    reply_sel ? RGB(255,255,255) : RGB(200, 206, 212));
                draw_text(dc, m_row_font, btn_close, L"B  Close",
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE,
                    close_sel ? RGB(255,255,255) : RGB(200, 206, 212));
            }
            else // info
            {
                RECT btn_ok{
                    card.left + (card.right - card.left - bw) / 2,
                    by,
                    card.left + (card.right - card.left - bw) / 2 + bw,
                    by + bh
                };
                fill_round_rect(dc, btn_ok, RGB(70, 110, 40), RGB(110, 160, 60), round_radius(w, h));
                draw_text(dc, m_row_font, btn_ok, L"A  OK",
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE, RGB(255,255,255));
            }
        }

        // --- Navigation ---
        int list_count() const
        {
            switch (m_section) {
            case e_section::friends:  return static_cast<int>(m_friends.size());
            case e_section::messages: return static_cast<int>(m_messages.size());
            case e_section::party:    return static_cast<int>(m_members.size());
            case e_section::players:  return static_cast<int>(m_players.size());
            case e_section::games:    return static_cast<int>(m_games.size());
            default: return 0;
            }
        }

        void ensure_visible(int visible)
        {
            if (m_selected < m_scroll) m_scroll = m_selected;
            else if (m_selected >= m_scroll + visible) m_scroll = m_selected - visible + 1;
        }

        void move_list(int delta)
        {
            int count = list_count();
            if (count <= 0) return;
            int next = m_selected + delta;
            if (next < 0) next = 0;
            if (next >= count) next = count - 1;
            if (next != m_selected) {
                m_selected = next;
                ensure_visible(8);
                InvalidateRect(m_handle, nullptr, FALSE);
            }
        }

        void move_blade(int delta)
        {
            int next = static_cast<int>(m_section) + delta;
            if (next < 0) next = 0;
            if (next >= static_cast<int>(e_section::count)) next = static_cast<int>(e_section::count) - 1;
            if (next != static_cast<int>(m_section)) {
                m_section = static_cast<e_section>(next);
                m_selected = 0;
                m_scroll = 0;
                InvalidateRect(m_handle, nullptr, FALSE);
            }
        }

        void activate()
        {
            if (m_focus_blade) {
                // Enter content
                if (m_section != e_section::home && m_section != e_section::profile) {
                    m_focus_blade = false;
                    m_selected = 0;
                    m_scroll = 0;
                    InvalidateRect(m_handle, nullptr, FALSE);
                }
                return;
            }

            // Content actions
            switch (m_section) {
            case e_section::friends:
                if (m_selected >= 0 && m_selected < static_cast<int>(m_friends.size()))
                    MessageBoxW(m_handle, (m_friends[m_selected].gamertag + L"\n" + m_friends[m_selected].status).c_str(), L"Friend", MB_OK);
                break;
            case e_section::messages:
                if (m_selected >= 0 && m_selected < static_cast<int>(m_messages.size())) {
                    m_messages[m_selected].unread = false;
                    s_message const& msg = m_messages[m_selected];
                    m_modal = e_modal::message_actions;
                    m_modal_title = L"Message";
                    m_modal_body = msg.from + L"\n" + msg.preview;
                    m_modal_choice = 0;
                    m_modal_msg_index = m_selected;
                    InvalidateRect(m_handle, nullptr, FALSE);
                }
                break;
            case e_section::party:
                if (m_selected >= 0 && m_selected < static_cast<int>(m_members.size())) {
                    auto& m = m_members[m_selected];
                    if (m.status == e_member_status::invitable) {
                        m.status = e_member_status::joining;
                        m.activity = L"Connecting...";
                        MessageBoxW(m_handle, (L"Inviting " + m.gamertag).c_str(), L"Party", MB_OK);
                        InvalidateRect(m_handle, nullptr, FALSE);
                    } else {
                        MessageBoxW(m_handle, (m.gamertag + L"\n" + m.activity).c_str(), L"Party Member", MB_OK);
                    }
                }
                break;
            case e_section::players:
                if (m_selected >= 0 && m_selected < static_cast<int>(m_players.size())) {
                    m_players[m_selected].is_friend = true;
                    MessageBoxW(m_handle, (L"Friend request sent to " + m_players[m_selected].gamertag).c_str(), L"Players Met", MB_OK);
                    InvalidateRect(m_handle, nullptr, FALSE);
                }
                break;
            case e_section::games:
                if (m_selected >= 0 && m_selected < static_cast<int>(m_games.size()))
                    MessageBoxW(m_handle, (L"Launching " + m_games[m_selected].title).c_str(), L"Quick Launch", MB_OK);
                break;
            default: break;
            }
        }

        void go_back()
        {
            if (!m_focus_blade) {
                m_focus_blade = true;
                InvalidateRect(m_handle, nullptr, FALSE);
            } else {
                close();
            }
        }

        void start_reply(std::wstring const& to)
        {
            if (m_awaiting_keyboard)
                return;

            m_reply_to = to;
            memset(&m_kb_overlapped, 0, sizeof(m_kb_overlapped));
            m_kb_result[0] = L'\0';

            std::wstring title = L"Reply to " + to;
            unsigned long r = online_guide_show_virtual_keyboard_ui(
                0,
                0,
                L"",
                title.c_str(),
                L"Type your message and press Done.",
                m_kb_result,
                static_cast<unsigned long>(sizeof(m_kb_result) / sizeof(m_kb_result[0])),
                &m_kb_overlapped);

            if (r == ERROR_IO_PENDING)
            {
                m_awaiting_keyboard = true;
                // Hide guide while keyboard is up
                ShowWindow(m_handle, SW_HIDE);
            }
            else
            {
                MessageBoxW(m_handle, L"Could not open virtual keyboard.", L"Message Center", MB_OK | MB_ICONWARNING);
            }
        }

        void poll_keyboard_completion()
        {
            if (!m_awaiting_keyboard)
                return;

            if (m_kb_overlapped.hEvent == nullptr)
                return;

            DWORD wait = WaitForSingleObject(m_kb_overlapped.hEvent, 0);
            if (wait != WAIT_OBJECT_0)
                return;

            m_awaiting_keyboard = false;
            DWORD status = static_cast<DWORD>(m_kb_overlapped.Internal);

            if (m_kb_overlapped.hEvent)
            {
                CloseHandle(m_kb_overlapped.hEvent);
                m_kb_overlapped.hEvent = nullptr;
            }

            ShowWindow(m_handle, SW_SHOW);
            SetFocus(m_handle);

            if (status == ERROR_SUCCESS && m_kb_result[0] != L'\0')
            {
                // Insert sent reply at top of message list
                s_message sent;
                sent.type = e_msg_type::text;
                sent.from = L"You -> " + m_reply_to;
                sent.preview = m_kb_result;
                sent.when = L"Just now";
                sent.unread = false;
                m_messages.insert(m_messages.begin(), sent);
                m_selected = 0;
                m_scroll = 0;

                m_modal = e_modal::info;
                m_modal_title = L"Message Sent";
                m_modal_body = L"To: " + m_reply_to + L"\n\n" + m_kb_result;
                m_modal_choice = 0;
            }

            m_reply_to.clear();
            m_kb_result[0] = L'\0';
            InvalidateRect(m_handle, nullptr, FALSE);
        }

        void close_modal()
        {
            m_modal = e_modal::none;
            m_modal_msg_index = -1;
            InvalidateRect(m_handle, nullptr, FALSE);
        }

        void confirm_modal()
        {
            if (m_modal == e_modal::message_actions)
            {
                if (m_modal_choice == 0 && m_modal_msg_index >= 0 &&
                    m_modal_msg_index < static_cast<int>(m_messages.size()))
                {
                    std::wstring to = m_messages[m_modal_msg_index].from;
                    close_modal();
                    start_reply(to);
                    return;
                }
                close_modal();
            }
            else if (m_modal == e_modal::info)
            {
                close_modal();
            }
        }

        void poll_controller()
        {
            poll_keyboard_completion();

            // While keyboard is open, do not process guide controller input
            if (m_awaiting_keyboard)
                return;

            XINPUT_STATE st{};
            if (XInputGetState(0, &st) != ERROR_SUCCESS) { m_prev_buttons = 0; return; }
            WORD buttons = st.Gamepad.wButtons;
            WORD pressed = static_cast<WORD>(buttons & ~m_prev_buttons);
            m_prev_buttons = buttons;

            // Modal takes input priority
            if (m_modal != e_modal::none)
            {
                if (pressed & XINPUT_GAMEPAD_B) {
                    if (m_modal == e_modal::message_actions) {
                        m_modal_choice = 1;
                        confirm_modal();
                    } else {
                        close_modal();
                    }
                    return;
                }
                if (pressed & XINPUT_GAMEPAD_A) {
                    confirm_modal();
                    return;
                }
                if (m_modal == e_modal::message_actions) {
                    if (pressed & (XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_DPAD_RIGHT)) {
                        m_modal_choice = 1 - m_modal_choice;
                        InvalidateRect(m_handle, nullptr, FALSE);
                    }
                    SHORT sx = st.Gamepad.sThumbLX;
                    if ((sx > k_stick_deadzone || sx < -k_stick_deadzone)) {
                        DWORD now = GetTickCount();
                        if (now - m_last_nav >= 180) {
                            m_modal_choice = 1 - m_modal_choice;
                            m_last_nav = now;
                            InvalidateRect(m_handle, nullptr, FALSE);
                        }
                    }
                }
                return;
            }

            if (pressed & XINPUT_GAMEPAD_B) { go_back(); return; }
            if (pressed & XINPUT_GAMEPAD_A) activate();
            if (pressed & XINPUT_GAMEPAD_Y) {
                m_focus_blade = !m_focus_blade;
                InvalidateRect(m_handle, nullptr, FALSE);
            }

            int dy = 0;
            if (pressed & XINPUT_GAMEPAD_DPAD_UP) dy = -1;
            if (pressed & XINPUT_GAMEPAD_DPAD_DOWN) dy = 1;
            SHORT sy = st.Gamepad.sThumbLY;
            if (dy == 0) {
                if (sy > k_stick_deadzone) dy = -1;
                if (sy < -k_stick_deadzone) dy = 1;
            }

            if (dy != 0) {
                DWORD now = GetTickCount();
                if (pressed != 0 || now - m_last_nav >= 120) {
                    if (m_focus_blade) move_blade(dy);
                    else move_list(dy);
                    m_last_nav = now;
                }
            }
        }

        LRESULT window_proc(UINT msg, WPARAM wp, LPARAM lp)
        {
            switch (msg) {
            case WM_PAINT: {
                PAINTSTRUCT ps{};
                HDC dc = BeginPaint(m_handle, &ps);
                RECT c{}; GetClientRect(m_handle, &c);
                HDC mem = CreateCompatibleDC(dc);
                HBITMAP bmp = CreateCompatibleBitmap(dc, c.right, c.bottom);
                auto old = static_cast<HBITMAP>(SelectObject(mem, bmp));
                paint(mem);
                BitBlt(dc, 0, 0, c.right, c.bottom, mem, 0, 0, SRCCOPY);
                SelectObject(mem, old); DeleteObject(bmp); DeleteDC(mem);
                EndPaint(m_handle, &ps);
                return 0;
            }
            case WM_ERASEBKGND: return TRUE;
            case WM_KEYDOWN:
                if (m_modal != e_modal::none) {
                    switch (wp) {
                    case VK_ESCAPE:
                    case 'B': case 'b':
                        if (m_modal == e_modal::message_actions) {
                            m_modal_choice = 1;
                            confirm_modal();
                        } else close_modal();
                        break;
                    case VK_RETURN:
                    case 'A': case 'a':
                        confirm_modal();
                        break;
                    case VK_LEFT:
                        if (m_modal == e_modal::message_actions) {
                            m_modal_choice = 0;
                            InvalidateRect(m_handle, nullptr, FALSE);
                        }
                        break;
                    case VK_RIGHT:
                        if (m_modal == e_modal::message_actions) {
                            m_modal_choice = 1;
                            InvalidateRect(m_handle, nullptr, FALSE);
                        }
                        break;
                    }
                    return 0;
                }
                switch (wp) {
                case VK_ESCAPE: go_back(); break;
                case VK_RETURN: activate(); break;
                case 'Y': case 'y':
                    m_focus_blade = !m_focus_blade;
                    InvalidateRect(m_handle, nullptr, FALSE);
                    break;
                case VK_UP:
                    if (m_focus_blade) move_blade(-1); else move_list(-1);
                    break;
                case VK_DOWN:
                    if (m_focus_blade) move_blade(1); else move_list(1);
                    break;
                case VK_LEFT: m_focus_blade = true; InvalidateRect(m_handle, nullptr, FALSE); break;
                case VK_RIGHT:
                    if (m_section != e_section::home && m_section != e_section::profile) {
                        m_focus_blade = false; m_selected = 0; m_scroll = 0;
                        InvalidateRect(m_handle, nullptr, FALSE);
                    }
                    break;
                }
                return 0;
            case WM_TIMER:
                if (wp == k_controller_timer_id) poll_controller();
                return 0;
            case WM_CLOSE: close(); return 0;
            case WM_NCDESTROY:
                KillTimer(m_handle, k_controller_timer_id);
                release_fonts();
                return 0;
            }
            return DefWindowProcW(m_handle, msg, wp, lp);
        }

        HWND m_handle = nullptr;
        HFONT m_title_font = nullptr, m_section_font = nullptr, m_row_font = nullptr;
        HFONT m_meta_font = nullptr, m_footer_font = nullptr, m_badge_font = nullptr, m_small_font = nullptr;

        e_section m_section = e_section::home;
        bool m_focus_blade = true;
        int m_selected = 0;
        int m_scroll = 0;

        std::vector<s_friend> m_friends;
        std::vector<s_message> m_messages;
        std::vector<s_member> m_members;
        std::vector<s_player> m_players;
        std::vector<s_game> m_games;
        std::vector<s_achievement> m_recent;

        std::wstring m_gamertag, m_motto, m_location, m_bio, m_reputation, m_zone, m_member_since;
        int m_gamerscore = 0, m_games_played = 0;

        // In-guide modal (controller-friendly, replaces MessageBox)
        enum class e_modal { none, message_actions, info };
        e_modal m_modal = e_modal::none;
        std::wstring m_modal_title;
        std::wstring m_modal_body;
        int m_modal_choice = 0; // 0 = primary (Reply/OK), 1 = secondary (Close)
        int m_modal_msg_index = -1;

        // Message compose via virtual keyboard
        bool m_awaiting_keyboard = false;
        OVERLAPPED m_kb_overlapped{};
        wchar_t m_kb_result[256]{};
        std::wstring m_reply_to;

        WORD m_prev_buttons = 0;
        DWORD m_last_nav = 0;
    };
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    c_guide_window guide;
    if (!guide.create()) return 1;

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
