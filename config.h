#pragma once

#include <string>
#include <vector>
#include <map>
#include <optional>

namespace xbox360_ui
{
    // Simple INI-style config:
    //   [section]
    //   key=value
    //   # comment
    // Values are trimmed. Sections and keys are case-insensitive for lookup.
    class config
    {
    public:
        // Load from path. Returns false if file missing or unreadable.
        bool load(std::wstring const& path);

        // Load from next to the running executable: <exe_dir>\<name>
        // e.g. load_beside_exe(L"quick_launch.ini")
        bool load_beside_exe(std::wstring const& filename);

        bool has_section(std::wstring const& section) const;
        std::vector<std::wstring> sections() const;

        std::wstring get(std::wstring const& section, std::wstring const& key,
                         std::wstring const& default_value = L"") const;
        int get_int(std::wstring const& section, std::wstring const& key, int default_value = 0) const;
        bool get_bool(std::wstring const& section, std::wstring const& key, bool default_value = false) const;

        // Sections whose names start with prefix (e.g. "game.")
        std::vector<std::wstring> sections_with_prefix(std::wstring const& prefix) const;

        static std::wstring exe_directory();

    private:
        static std::wstring normalize(std::wstring s);
        std::map<std::wstring, std::map<std::wstring, std::wstring>> m_data;
    };

    // Launch an application and focus its main window.
    // Returns true if CreateProcess succeeded. working_dir may be empty.
    bool launch_process(
        std::wstring const& path,
        std::wstring const& args = L"",
        std::wstring const& working_dir = L"");

    // Returns process id if an instance of this executable is already running, else 0.
    unsigned long find_running_process(std::wstring const& path);

    // Terminate a process by id. Returns true on success.
    bool terminate_process_id(unsigned long process_id);
}

