#pragma once

#include "xbox360_ui_common.h"

#include <string>
#include <functional>

namespace xbox360_ui
{
    // Controller-friendly modal dialog (replaces MessageBoxW for pad users).
    // A = primary, B = secondary/cancel, D-pad / stick Left-Right switches focus
    // when two buttons are shown.
    class controller_message_box
    {
    public:
        enum class result
        {
            none,       // still open / idle
            primary,    // A / Yes / OK / Reply
            secondary   // B / No / Close / Cancel
        };

        enum class buttons
        {
            ok,         // single "A  OK"
            yes_no      // "A  Yes"  +  "B  No"  (or custom labels)
        };

        using complete_fn = std::function<void(result)>;

        void show(
            std::wstring title,
            std::wstring body,
            buttons button_style = buttons::ok,
            wchar_t const* primary_label = nullptr,
            wchar_t const* secondary_label = nullptr,
            complete_fn on_complete = nullptr);

        void hide();
        bool visible() const { return m_visible; }

        // Call from host paint (after main UI). Uses fonts from host.
        void paint(
            HDC dc,
            int client_width,
            int client_height,
            HFONT title_font,
            HFONT body_font) const;

        // Returns true if the modal consumed the input.
        bool handle_key(WPARAM vk);
        bool handle_controller(WORD pressed, SHORT stick_x, DWORD now_ms);

        result last_result() const { return m_last_result; }

    private:
        void finish(result r);

        bool m_visible = false;
        buttons m_buttons = buttons::ok;
        int m_choice = 0; // 0 = primary, 1 = secondary
        std::wstring m_title;
        std::wstring m_body;
        std::wstring m_primary_label;
        std::wstring m_secondary_label;
        complete_fn m_on_complete;
        result m_last_result = result::none;
        DWORD m_last_nav_ms = 0;
    };
}
