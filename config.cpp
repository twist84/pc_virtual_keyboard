#include "config.h"

#include <windows.h>
#include <shellapi.h>
#pragma comment(lib, "shell32.lib")
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace xbox360_ui
{
    namespace
    {
        std::wstring trim(std::wstring s)
        {
            auto not_space = [](wchar_t c) { return !iswspace(c); };
            s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
            s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
            return s;
        }

        std::wstring to_lower(std::wstring s)
        {
            for (auto& c : s)
                c = static_cast<wchar_t>(towlower(c));
            return s;
        }
    }

    std::wstring config::normalize(std::wstring s)
    {
        return to_lower(trim(std::move(s)));
    }

    std::wstring config::exe_directory()
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

    bool config::load(std::wstring const& path)
    {
        m_data.clear();
        std::ifstream file(path);
        if (!file)
            return false;

        // Read as UTF-8 / ANSI bytes then convert roughly via MultiByte
        std::string narrow((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        if (narrow.size() >= 3 &&
            static_cast<unsigned char>(narrow[0]) == 0xEF &&
            static_cast<unsigned char>(narrow[1]) == 0xBB &&
            static_cast<unsigned char>(narrow[2]) == 0xBF)
        {
            narrow.erase(0, 3);
        }

        int wide_len = MultiByteToWideChar(CP_UTF8, 0, narrow.data(), static_cast<int>(narrow.size()), nullptr, 0);
        std::wstring content;
        if (wide_len > 0)
        {
            content.resize(static_cast<size_t>(wide_len));
            MultiByteToWideChar(CP_UTF8, 0, narrow.data(), static_cast<int>(narrow.size()), content.data(), wide_len);
        }
        else
        {
            content.assign(narrow.begin(), narrow.end());
        }

        std::wstring section;
        std::wstringstream ss(content);
        std::wstring line;
        while (std::getline(ss, line))
        {
            if (!line.empty() && line.back() == L'\r')
                line.pop_back();
            line = trim(line);
            if (line.empty() || line[0] == L'#' || line[0] == L';')
                continue;

            if (line.front() == L'[' && line.back() == L']')
            {
                section = normalize(line.substr(1, line.size() - 2));
                m_data[section];
                continue;
            }

            size_t eq = line.find(L'=');
            if (eq == std::wstring::npos || section.empty())
                continue;

            std::wstring key = normalize(line.substr(0, eq));
            std::wstring value = trim(line.substr(eq + 1));
            // Strip optional quotes
            if (value.size() >= 2 &&
                ((value.front() == L'"' && value.back() == L'"') ||
                 (value.front() == L'\'' && value.back() == L'\'')))
            {
                value = value.substr(1, value.size() - 2);
            }
            m_data[section][key] = value;
        }
        return true;
    }

    bool config::load_beside_exe(std::wstring const& filename)
    {
        return load(exe_directory() + L"\\" + filename);
    }

    bool config::has_section(std::wstring const& section) const
    {
        return m_data.find(normalize(section)) != m_data.end();
    }

    std::vector<std::wstring> config::sections() const
    {
        std::vector<std::wstring> out;
        out.reserve(m_data.size());
        for (auto const& kv : m_data)
            out.push_back(kv.first);
        return out;
    }

    std::vector<std::wstring> config::sections_with_prefix(std::wstring const& prefix) const
    {
        std::wstring p = normalize(prefix);
        std::vector<std::wstring> out;
        for (auto const& kv : m_data)
            if (kv.first.size() >= p.size() && kv.first.compare(0, p.size(), p) == 0)
                out.push_back(kv.first);
        std::sort(out.begin(), out.end());
        return out;
    }

    std::wstring config::get(std::wstring const& section, std::wstring const& key,
                             std::wstring const& default_value) const
    {
        auto sit = m_data.find(normalize(section));
        if (sit == m_data.end())
            return default_value;
        auto kit = sit->second.find(normalize(key));
        if (kit == sit->second.end())
            return default_value;
        return kit->second;
    }

    int config::get_int(std::wstring const& section, std::wstring const& key, int default_value) const
    {
        std::wstring v = get(section, key);
        if (v.empty())
            return default_value;
        return _wtoi(v.c_str());
    }

    bool config::get_bool(std::wstring const& section, std::wstring const& key, bool default_value) const
    {
        std::wstring v = to_lower(get(section, key));
        if (v.empty())
            return default_value;
        return v == L"1" || v == L"true" || v == L"yes" || v == L"on";
    }

    bool launch_process(
        std::wstring const& path,
        std::wstring const& args,
        std::wstring const& working_dir)
    {
        if (path.empty())
            return false;

        DWORD attrs = GetFileAttributesW(path.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES)
            return false;

        std::wstring dir = working_dir;
        if (dir.empty())
        {
            size_t slash = path.find_last_of(L"\\/");
            if (slash != std::wstring::npos)
                dir = path.substr(0, slash);
        }

        std::wstring cmd = L"\"" + path + L"\"";
        if (!args.empty())
        {
            cmd += L" ";
            cmd += args;
        }
        std::vector<wchar_t> cmd_buf(cmd.begin(), cmd.end());
        cmd_buf.push_back(L'\0');

        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        BOOL ok = CreateProcessW(
            path.c_str(),
            cmd_buf.data(),
            nullptr, nullptr, FALSE,
            0, nullptr,
            dir.empty() ? nullptr : dir.c_str(),
            &si, &pi);
        if (!ok)
            return false;

        // Allow the new process to take foreground, then focus its main window
        AllowSetForegroundWindow(pi.dwProcessId);
        WaitForInputIdle(pi.hProcess, 5000);

        struct find_data
        {
            DWORD pid = 0;
            HWND hwnd = nullptr;
        };

        find_data data;
        data.pid = pi.dwProcessId;

        auto enum_proc = [](HWND hwnd, LPARAM lp) -> BOOL {
            auto* fd = reinterpret_cast<find_data*>(lp);
            DWORD wnd_pid = 0;
            GetWindowThreadProcessId(hwnd, &wnd_pid);
            if (wnd_pid != fd->pid)
                return TRUE;
            if (!IsWindowVisible(hwnd))
                return TRUE;
            if (GetWindow(hwnd, GW_OWNER) != nullptr)
                return TRUE;
            wchar_t title[4]{};
            GetWindowTextW(hwnd, title, 4);
            // Prefer windows with a title (skip tool-only)
            if (title[0] == L'\0')
                return TRUE;
            fd->hwnd = hwnd;
            return FALSE;
        };

        // Retry briefly — some apps create the window after InputIdle
        for (int attempt = 0; attempt < 20 && data.hwnd == nullptr; ++attempt)
        {
            EnumWindows(enum_proc, reinterpret_cast<LPARAM>(&data));
            if (data.hwnd == nullptr)
                Sleep(50);
        }

        if (data.hwnd != nullptr)
        {
            ShowWindow(data.hwnd, SW_RESTORE);
            BringWindowToTop(data.hwnd);
            SetForegroundWindow(data.hwnd);
            SetActiveWindow(data.hwnd);
        }

        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return true;
    }
}
