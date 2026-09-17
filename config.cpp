#include "config.h"

#include <windows.h>
#include <tlhelp32.h>
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

        AllowSetForegroundWindow(pi.dwProcessId);
        WaitForInputIdle(pi.hProcess, 5000);

        auto collect_pids = [](DWORD root_pid) {
            std::vector<DWORD> pids;
            pids.push_back(root_pid);
            HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            if (snap == INVALID_HANDLE_VALUE)
                return pids;
            PROCESSENTRY32W pe{};
            pe.dwSize = sizeof(pe);
            if (Process32FirstW(snap, &pe))
            {
                do
                {
                    if (pe.th32ParentProcessID == root_pid)
                        pids.push_back(pe.th32ProcessID);
                } while (Process32NextW(snap, &pe));
            }
            CloseHandle(snap);
            return pids;
        };

        struct enum_ctx
        {
            std::vector<DWORD> pids;
            HWND best_gui = nullptr;
            int best_gui_area = -1;
        };

        auto enum_cb = [](HWND hwnd, LPARAM lp) -> BOOL {
            auto* ctx = reinterpret_cast<enum_ctx*>(lp);
            DWORD wnd_pid = 0;
            GetWindowThreadProcessId(hwnd, &wnd_pid);
            bool match = false;
            for (DWORD p : ctx->pids)
            {
                if (p == wnd_pid)
                {
                    match = true;
                    break;
                }
            }
            if (!match)
                return TRUE;
            if (!IsWindowVisible(hwnd))
                return TRUE;
            if (GetWindow(hwnd, GW_OWNER) != nullptr)
                return TRUE;

            // Skip console host windows (associated console, not the game UI)
            wchar_t cls[64]{};
            GetClassNameW(hwnd, cls, 64);
            if (_wcsicmp(cls, L"ConsoleWindowClass") == 0)
                return TRUE;

            RECT rc{};
            GetWindowRect(hwnd, &rc);
            int area = (rc.right - rc.left) * (rc.bottom - rc.top);
            if (area < 200)
                return TRUE;

            if (area > ctx->best_gui_area)
            {
                ctx->best_gui_area = area;
                ctx->best_gui = hwnd;
            }
            return TRUE;
        };

        HWND best = nullptr;
        for (int attempt = 0; attempt < 40; ++attempt)
        {
            enum_ctx ctx;
            ctx.pids = collect_pids(pi.dwProcessId);
            EnumWindows(enum_cb, reinterpret_cast<LPARAM>(&ctx));
            if (ctx.best_gui != nullptr)
            {
                best = ctx.best_gui;
                break;
            }
            Sleep(50);
        }

        if (best != nullptr)
        {
            ShowWindow(best, SW_RESTORE);
            BringWindowToTop(best);
            SetForegroundWindow(best);
            SetActiveWindow(best);
        }

        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return true;
    }

    static std::wstring normalize_path(std::wstring p)
    {
        for (auto& c : p)
        {
            if (c == L'/') c = L'\\';
            c = static_cast<wchar_t>(towlower(c));
        }
        return p;
    }

    static std::wstring file_name_only(std::wstring const& path)
    {
        size_t slash = path.find_last_of(L"\\/");
        if (slash == std::wstring::npos)
            return normalize_path(path);
        return normalize_path(path.substr(slash + 1));
    }

    unsigned long find_running_process(std::wstring const& path)
    {
        if (path.empty())
            return 0;

        std::wstring target_full = normalize_path(path);
        std::wstring target_name = file_name_only(path);

        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE)
            return 0;

        PROCESSENTRY32W pe{};
        pe.dwSize = sizeof(pe);
        DWORD found = 0;
        DWORD self = GetCurrentProcessId();

        if (Process32FirstW(snap, &pe))
        {
            do
            {
                if (pe.th32ProcessID == self)
                    continue;

                std::wstring exe_name = normalize_path(pe.szExeFile);
                if (exe_name != target_name)
                    continue;

                HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
                if (!proc)
                {
                    // Name matched; accept if we cannot query full path
                    found = pe.th32ProcessID;
                    break;
                }

                wchar_t image[MAX_PATH]{};
                DWORD size = MAX_PATH;
                bool path_match = false;
                if (QueryFullProcessImageNameW(proc, 0, image, &size))
                    path_match = (normalize_path(image) == target_full);
                CloseHandle(proc);

                if (path_match || target_full.find(L'\\') == std::wstring::npos)
                {
                    found = pe.th32ProcessID;
                    break;
                }
            } while (Process32NextW(snap, &pe));
        }

        CloseHandle(snap);
        return found;
    }

    bool terminate_process_id(unsigned long process_id)
    {
        if (process_id == 0)
            return false;
        HANDLE proc = OpenProcess(PROCESS_TERMINATE, FALSE, process_id);
        if (!proc)
            return false;
        BOOL ok = TerminateProcess(proc, 1);
        CloseHandle(proc);
        return ok != FALSE;
    }

}
