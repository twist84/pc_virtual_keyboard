#include "controller_message_box.h"
#include <xinput.h>

namespace xbox360_ui
{
    void controller_message_box::show(
        std::wstring title,
        std::wstring body,
        buttons button_style,
        wchar_t const* primary_label,
        wchar_t const* secondary_label,
        complete_fn on_complete)
    {
        m_visible = true;
        m_buttons = button_style;
        m_choice = 0;
        m_title = std::move(title);
        m_body = std::move(body);
        m_on_complete = std::move(on_complete);
        m_last_result = result::none;

        if (primary_label)
            m_primary_label = primary_label;
        else
            m_primary_label = (button_style == buttons::ok) ? L"A  OK" : L"A  Yes";

        if (secondary_label)
            m_secondary_label = secondary_label;
        else
            m_secondary_label = L"B  No";
    }

    void controller_message_box::hide()
    {
        m_visible = false;
        m_on_complete = nullptr;
    }

    void controller_message_box::finish(result r)
    {
        m_last_result = r;
        m_visible = false;
        complete_fn cb = std::move(m_on_complete);
        m_on_complete = nullptr;
        if (cb)
            cb(r);
    }

    void controller_message_box::paint(
        HDC dc,
        int client_width,
        int client_height,
        HFONT title_font,
        HFONT body_font) const
    {
        if (!m_visible)
            return;

        int w = client_width;
        int h = client_height;

        int mw = scale_x(420, w);
        int mh = scale_y(220, h);
        RECT card{
            (w - mw) / 2,
            (h - mh) / 2,
            (w + mw) / 2,
            (h + mh) / 2
        };

        fill_round_rect(dc, card, RGB(40, 44, 48), RGB(90, 140, 60), round_radius(w, h) * 2);

        RECT title_r{ card.left, card.top, card.right, card.top + scale_y(44, h) };
        fill_rect(dc, title_r, RGB(50, 70, 40));
        draw_text(dc, title_font, title_r, m_title.c_str(),
            DT_CENTER | DT_VCENTER | DT_SINGLELINE, RGB(220, 240, 180));

        RECT body_r{
            card.left + scale_x(20, w),
            title_r.bottom + scale_y(12, h),
            card.right - scale_x(20, w),
            card.bottom - scale_y(70, h)
        };
        draw_text(dc, body_font, body_r, m_body.c_str(),
            DT_CENTER | DT_WORDBREAK, RGB(220, 224, 228));

        int bw = scale_x(140, w);
        int bh = scale_y(40, h);
        int by = card.bottom - scale_y(56, h);
        int radius = round_radius(w, h);

        auto draw_btn = [&](RECT btn, bool selected, wchar_t const* label) {
            fill_round_rect(dc, btn,
                selected ? k_row_selected : RGB(50, 54, 58),
                selected ? k_row_sel_border : k_row_border,
                radius);
            draw_text(dc, body_font, btn, label,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE,
                selected ? k_text_selected : RGB(200, 206, 212));
        };

        if (m_buttons == buttons::yes_no)
        {
            int gap = scale_x(16, w);
            int total = 2 * bw + gap;
            int left = card.left + (card.right - card.left - total) / 2;
            RECT btn_primary{ left, by, left + bw, by + bh };
            RECT btn_secondary{ left + bw + gap, by, left + 2 * bw + gap, by + bh };
            draw_btn(btn_primary, m_choice == 0, m_primary_label.c_str());
            draw_btn(btn_secondary, m_choice == 1, m_secondary_label.c_str());
        }
        else
        {
            RECT btn_ok{
                card.left + (card.right - card.left - bw) / 2,
                by,
                card.left + (card.right - card.left - bw) / 2 + bw,
                by + bh
            };
            draw_btn(btn_ok, true, m_primary_label.c_str());
        }
    }

    bool controller_message_box::handle_key(WPARAM vk)
    {
        if (!m_visible)
            return false;

        switch (vk)
        {
        case VK_ESCAPE:
        case 'B': case 'b':
            if (m_buttons == buttons::yes_no)
            {
                m_choice = 1;
                finish(result::secondary);
            }
            else
            {
                finish(result::primary);
            }
            return true;

        case VK_RETURN:
        case 'A': case 'a':
            finish(m_choice == 0 ? result::primary : result::secondary);
            return true;

        case VK_LEFT:
            if (m_buttons == buttons::yes_no)
            {
                m_choice = 0;
                return true;
            }
            break;

        case VK_RIGHT:
            if (m_buttons == buttons::yes_no)
            {
                m_choice = 1;
                return true;
            }
            break;
        }
        return true; // consume other keys while open
    }

    bool controller_message_box::handle_controller(WORD pressed, SHORT stick_x, DWORD now_ms)
    {
        if (!m_visible)
            return false;

        if (pressed & XINPUT_GAMEPAD_B)
        {
            if (m_buttons == buttons::yes_no)
            {
                m_choice = 1;
                finish(result::secondary);
            }
            else
            {
                finish(result::primary);
            }
            return true;
        }

        if (pressed & XINPUT_GAMEPAD_A)
        {
            finish(m_choice == 0 ? result::primary : result::secondary);
            return true;
        }

        if (m_buttons == buttons::yes_no)
        {
            bool toggle = false;
            if (pressed & (XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_DPAD_RIGHT))
                toggle = true;
            else if (stick_x > k_stick_deadzone || stick_x < -k_stick_deadzone)
            {
                if (now_ms - m_last_nav_ms >= 180)
                {
                    toggle = true;
                    m_last_nav_ms = now_ms;
                }
            }
            if (toggle)
                m_choice = 1 - m_choice;
        }

        return true;
    }
}
