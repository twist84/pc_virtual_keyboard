// Background controller listener: opens the Guide on Guide button or Start+Back.
// Run this (e.g. at login) from the same folder as pc_guide.exe.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <xinput.h>
#include <shellapi.h>

#include <string>
#include <vector>

#pragma comment(lib, "xinput.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")

namespace
{
    constexpr wchar_t k_guide_class[] = L"pc_xbox360_guide";
    constexpr wchar_t k_listener_class[] = L"pc_xbox360_guide_listener";
    constexpr UINT k_timer_id = 1;
    constexpr UINT k_poll_ms = 50;

    // Undocumented: XINPUT_GAMEPAD_GUIDE in the extended state
    constexpr WORD XINPUT_GAMEPAD_GUIDE = 0x0400;

    using XInputGetStateEx_t = DWORD (WINAPI*)(DWORD, XINPUT_STATE*);

    XInputGetStateEx_t g_get_state_ex = nullptr;
    WORD g_prev_buttons[4]{};
    bool g_guide_held[4]{};

    void load_xinput_ex()
    {
        if (g_get_state_ex)
            return;

        wchar_t const* dlls[] = {
            L"xinput1_4.dll",
            L"xinput1_3.dll",
            L"xinput9_1_0.dll",
            L"xinput1_2.dll",
            L"xinput1_1.dll",
        };

        for (auto name : dlls)
        {
            HMODULE mod = LoadLibraryW(name);
            if (!mod)
                continue;
            // Ordinal 100 = XInputGetStateEx on most builds
            auto fn = reinterpret_cast<XInputGetStateEx_t>(GetProcAddress(mod, reinterpret_cast<char const*>(100)));
            if (fn)
            {
                g_get_state_ex = fn;
                return;
            }
        }
    }

    bool get_pad_state(DWORD index, XINPUT_STATE& state)
    {
        if (g_get_state_ex)
        {
            if (g_get_state_ex(index, &state) == ERROR_SUCCESS)
                return true;
        }
        return XInputGetState(index, &state) == ERROR_SUCCESS;
    }

    std::wstring exe_dir()
    {
        wchar_t buf[MAX_PATH]{};
        DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
        if (n == 0 || n >= MAX_PATH)
            return L".";
        std::wstring path(buf, n);
        size_t slash = path.find_last_of(L"\\/");
        if (slash == std::wstring::npos)
            return L".";
        return path.substr(0, slash);
    }

    HWND find_guide_window()
    {
        return FindWindowW(k_guide_class, nullptr);
    }

    void focus_window(HWND hwnd)
    {
        if (!hwnd)
            return;
        if (IsIconic(hwnd))
            ShowWindow(hwnd, SW_RESTORE);
        ShowWindow(hwnd, SW_SHOW);
        BringWindowToTop(hwnd);
        SetForegroundWindow(hwnd);
    }

    bool launch_or_focus_guide()
    {
        HWND existing = find_guide_window();
        if (existing)
        {
            focus_window(existing);
            return true;
        }

        std::wstring path = exe_dir() + L"\\pc_guide.exe";
        if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
            return false;

        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        std::wstring cmd = L"\"" + path + L"\"";
        std::vector<wchar_t> cmd_buf(cmd.begin(), cmd.end());
        cmd_buf.push_back(L'\0');

        BOOL ok = CreateProcessW(
            path.c_str(),
            cmd_buf.data(),
            nullptr, nullptr, FALSE,
            0, nullptr,
            exe_dir().c_str(),
            &si, &pi);
        if (!ok)
            return false;

        AllowSetForegroundWindow(pi.dwProcessId);
        // Brief wait then try to focus when the window appears
        for (int i = 0; i < 40; ++i)
        {
            Sleep(50);
            HWND hwnd = find_guide_window();
            if (hwnd)
            {
                focus_window(hwnd);
                break;
            }
        }

        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return true;
    }

    bool edge_pressed(WORD prev, WORD now, WORD mask)
    {
        return (now & mask) == mask && (prev & mask) != mask;
    }

    void poll_controllers()
    {
        for (DWORD i = 0; i < 4; ++i)
        {
            XINPUT_STATE st{};
            if (!get_pad_state(i, st))
            {
                g_prev_buttons[i] = 0;
                g_guide_held[i] = false;
                continue;
            }

            WORD buttons = st.Gamepad.wButtons;
            WORD prev = g_prev_buttons[i];

            bool guide_down = (buttons & XINPUT_GAMEPAD_GUIDE) != 0;
            bool guide_edge = guide_down && !g_guide_held[i];
            g_guide_held[i] = guide_down;

            // Start + Back together (rising edge on the combo)
            bool combo_edge = edge_pressed(prev, buttons, XINPUT_GAMEPAD_START | XINPUT_GAMEPAD_BACK);

            g_prev_buttons[i] = buttons;

            if (guide_edge || combo_edge)
                launch_or_focus_guide();
        }
    }

    LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        switch (msg)
        {
        case WM_CREATE:
            SetTimer(hwnd, k_timer_id, k_poll_ms, nullptr);
            return 0;
        case WM_TIMER:
            if (wp == k_timer_id)
                poll_controllers();
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd, k_timer_id);
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    // Single instance
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\pc_xbox360_guide_listener");
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS)
        return 0;

    load_xinput_ex();

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = instance;
    wc.lpszClassName = k_listener_class;
    RegisterClassExW(&wc);

    // Message-only window (no UI)
    HWND hwnd = CreateWindowExW(
        0, k_listener_class, L"Guide Listener",
        0, 0, 0, 0, 0,
        HWND_MESSAGE, nullptr, instance, nullptr);
    if (!hwnd)
        return 1;

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
