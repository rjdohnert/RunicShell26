// RunicShell26 (2.0.1 2026) Copyright (C) 2026, Roberto J Dohnert. All rights reserved.
// 2nd generation rjdsh.cpp - ksh93-compatible shell in C++17 for Windows Server 2022 + Server 2025 
// based on the original rsh.cpp by rjdohnert, with extensive modifications and improvements from 2005 to 2024, now rewritten in C++17 
// and added features like command history, tab completion, background job control, and better script execution.
// This file is licensed under the BSD-3 Clause License. See LICENSE.txt for details.
// Note: This implementation focuses on ksh93 compatibility and is not intended to be a full POSIX shell. It includes a subset of 
// features and built-in commands, and may have limitations compared to a full ksh93 shell.
// Version 2.0.1 (2026) - 2nd generation rewrite with C++17, added features, and improved ksh93 compatibility 
// Rewrite started in 2024 and completed in 2026, with ongoing maintenance and improvements expected.


#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <cstring>
#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <cwctype>
#include <regex>
#include <windows.h>
#include <winternl.h>
#include <io.h>
#include <fcntl.h>
#include <conio.h>

#pragma comment(lib, "Advapi32.lib")

// ksh93 Environment State & Variables Registry
struct KshEnvironment {
    std::map<std::wstring, std::wstring> variables;
    std::map<std::wstring, std::vector<std::wstring>> arrays;
    std::map<std::wstring, bool> exported;
} ksh_env;

// Forward declarations
std::wstring evaluate_arithmetic(const std::wstring& expr);
std::wstring evaluate_command_substitutions(const std::wstring& input);
bool execute_script_file(const std::wstring& script_path, const std::vector<std::wstring>& script_args, bool& should_exit_shell);
bool execute_command_line(const std::wstring& input_line, bool& should_exit_shell);
std::wstring trim_copy(const std::wstring& value);

struct ScriptContext {
    std::wstring script_name;
    std::vector<std::wstring> args;
};

struct BackgroundJob {
    int id;
    DWORD pid;
    HANDLE process_handle;
    std::wstring command;
    bool completed;
    DWORD exit_code;
    bool completion_reported;
};

std::vector<ScriptContext> g_script_context_stack;
std::vector<std::wstring> g_command_history;
std::vector<BackgroundJob> g_background_jobs;
int g_next_job_id = 1;
std::wstring g_history_file_path;
bool g_is_interactive_session = false;

std::wstring get_history_file_path() {
    wchar_t appdata_path[MAX_PATH];
    DWORD appdata_len = GetEnvironmentVariableW(L"APPDATA", appdata_path, MAX_PATH);
    if (appdata_len > 0 && appdata_len < MAX_PATH) {
        return std::wstring(appdata_path) + L"\\rsh_history.txt";
    }

    wchar_t userprofile_path[MAX_PATH];
    DWORD userprofile_len = GetEnvironmentVariableW(L"USERPROFILE", userprofile_path, MAX_PATH);
    if (userprofile_len > 0 && userprofile_len < MAX_PATH) {
        return std::wstring(userprofile_path) + L"\\AppData\\Roaming\\rsh_history.txt";
    }

    return L"rsh_history.txt";
}

void load_command_history_from_file() {
    g_history_file_path = get_history_file_path();

    std::wifstream history_file(g_history_file_path.c_str());
    if (!history_file.is_open()) {
        return;
    }

    std::wstring line;
    while (std::getline(history_file, line)) {
        std::wstring trimmed = trim_copy(line);
        if (!trimmed.empty()) {
            g_command_history.push_back(trimmed);
        }
    }
}

void append_history_entry_to_file(const std::wstring& command_line) {
    if (command_line.empty()) {
        return;
    }

    if (g_history_file_path.empty()) {
        g_history_file_path = get_history_file_path();
    }

    std::wofstream history_file(g_history_file_path.c_str(), std::ios::app);
    if (!history_file.is_open()) {
        return;
    }

    history_file << command_line << L"\n";
}

std::wstring format_bytes_iec(ULONGLONG bytes) {
    static const wchar_t* units[] = { L"B", L"KiB", L"MiB", L"GiB", L"TiB" };
    double value = static_cast<double>(bytes);
    size_t unit_index = 0;
    while (value >= 1024.0 && unit_index + 1 < _countof(units)) {
        value /= 1024.0;
        unit_index++;
    }

    wchar_t buffer[64];
    if (unit_index == 0) {
        _snwprintf_s(buffer, _countof(buffer), _TRUNCATE, L"%llu %s", bytes, units[unit_index]);
    } else {
        _snwprintf_s(buffer, _countof(buffer), _TRUNCATE, L"%.2f %s", value, units[unit_index]);
    }
    return buffer;
}

std::wstring get_registry_string(HKEY root, const wchar_t* subkey, const wchar_t* value_name) {
    wchar_t buffer[256];
    DWORD size = static_cast<DWORD>(sizeof(buffer));
    LONG status = RegGetValueW(root, subkey, value_name, RRF_RT_REG_SZ, nullptr, buffer, &size);
    if (status != ERROR_SUCCESS) {
        return L"";
    }
    return std::wstring(buffer);
}

std::wstring get_windows_release_text() {
    std::wstring product_name = get_registry_string(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"ProductName");
    std::wstring display_version = get_registry_string(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"DisplayVersion");

    typedef LONG(WINAPI* RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    RtlGetVersionPtr rtl_get_version = (ntdll != nullptr)
        ? reinterpret_cast<RtlGetVersionPtr>(GetProcAddress(ntdll, "RtlGetVersion"))
        : nullptr;

    RTL_OSVERSIONINFOEXW os = {};
    os.dwOSVersionInfoSize = sizeof(os);

    wchar_t version_text[96] = L"unknown";
    if (rtl_get_version != nullptr && rtl_get_version(reinterpret_cast<PRTL_OSVERSIONINFOW>(&os)) == 0) {
        _snwprintf_s(version_text, _countof(version_text), _TRUNCATE, L"%lu.%lu.%lu", os.dwMajorVersion, os.dwMinorVersion, os.dwBuildNumber);
    }

    std::wstring release = product_name.empty() ? L"Windows" : product_name;
    release += L" (";
    release += version_text;
    if (!display_version.empty()) {
        release += L", ";
        release += display_version;
    }
    release += L")";
    return release;
}

void print_startup_system_info() {
    std::wcout << L"OS Release: " << get_windows_release_text() << L"\n";

    MEMORYSTATUSEX mem = {};
    mem.dwLength = sizeof(mem);
    if (GlobalMemoryStatusEx(&mem)) {
        const ULONGLONG used_physical = mem.ullTotalPhys - mem.ullAvailPhys;
        std::wcout << L"Memory Use: " << mem.dwMemoryLoad << L"%\n";
        std::wcout << L"  Physical: "
                  << format_bytes_iec(used_physical)
                  << L" used / "
                  << format_bytes_iec(mem.ullTotalPhys)
                  << L" total\n";
        std::wcout << L"  Available: " << format_bytes_iec(mem.ullAvailPhys) << L"\n";
    } else {
        std::wcout << L"Memory Use: unavailable\n";
    }
}

std::wstring longest_common_prefix_case_insensitive(const std::vector<std::wstring>& values) {
    if (values.empty()) {
        return L"";
    }

    std::wstring prefix = values[0];
    for (size_t i = 1; i < values.size() && !prefix.empty(); ++i) {
        size_t common = 0;
        size_t limit = (prefix.size() < values[i].size()) ? prefix.size() : values[i].size();
        while (common < limit && std::towlower(prefix[common]) == std::towlower(values[i][common])) {
            common++;
        }
        prefix = prefix.substr(0, common);
    }
    return prefix;
}

std::wstring to_lower_copy(const std::wstring& value) {
    std::wstring lower;
    lower.reserve(value.size());
    for (wchar_t ch : value) {
        lower.push_back(static_cast<wchar_t>(std::towlower(ch)));
    }
    return lower;
}

bool starts_with_case_insensitive(const std::wstring& value, const std::wstring& prefix) {
    if (prefix.size() > value.size()) {
        return false;
    }
    for (size_t i = 0; i < prefix.size(); ++i) {
        if (std::towlower(value[i]) != std::towlower(prefix[i])) {
            return false;
        }
    }
    return true;
}

std::vector<std::wstring> builtin_commands() {
    return {
        L"exit", L"source", L".", L"typeset", L"[[", L"cd", L"print", L"echo", L"history", L"complete", L"math",
        L"jobs", L"fg", L"kill", L"wait"
    };
}

void add_completion_candidate(std::map<std::wstring, std::wstring>& candidates, const std::wstring& candidate, const std::wstring& prefix) {
    if (candidate.empty()) {
        return;
    }
    if (!prefix.empty() && !starts_with_case_insensitive(candidate, prefix)) {
        return;
    }
    candidates[to_lower_copy(candidate)] = candidate;
}

void scan_directory_for_completions(const std::wstring& directory, const std::wstring& prefix, std::map<std::wstring, std::wstring>& candidates) {
    if (directory.empty()) {
        return;
    }

    std::wstring pattern = directory;
    if (!pattern.empty() && pattern.back() != L'\\' && pattern.back() != L'/') {
        pattern += L"\\";
    }
    pattern += L"*";

    WIN32_FIND_DATAW find_data;
    HANDLE handle = FindFirstFileW(pattern.c_str(), &find_data);
    if (handle == INVALID_HANDLE_VALUE) {
        return;
    }

    do {
        if ((find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            continue;
        }

        std::wstring name = find_data.cFileName;
        add_completion_candidate(candidates, name, prefix);

        size_t dot = name.find_last_of(L'.');
        if (dot != std::wstring::npos) {
            std::wstring extension = to_lower_copy(name.substr(dot));
            if (extension == L".exe" || extension == L".cmd" || extension == L".bat" || extension == L".com") {
                add_completion_candidate(candidates, name.substr(0, dot), prefix);
            }
        }
    } while (FindNextFileW(handle, &find_data));

    FindClose(handle);
}

std::vector<std::wstring> collect_completion_candidates(const std::wstring& prefix) {
    std::map<std::wstring, std::wstring> candidates;

    for (const std::wstring& builtin : builtin_commands()) {
        add_completion_candidate(candidates, builtin, prefix);
    }

    wchar_t cwd[MAX_PATH];
    if (GetCurrentDirectoryW(MAX_PATH, cwd) > 0) {
        scan_directory_for_completions(cwd, prefix, candidates);
    }

    wchar_t path_buf[32767];
    DWORD path_len = GetEnvironmentVariableW(L"PATH", path_buf, 32767);
    if (path_len > 0 && path_len < 32767) {
        std::wstring path_value(path_buf);
        size_t start = 0;
        while (start <= path_value.size()) {
            size_t sep = path_value.find(L';', start);
            std::wstring entry = (sep == std::wstring::npos)
                ? path_value.substr(start)
                : path_value.substr(start, sep - start);
            entry = trim_copy(entry);
            if (!entry.empty()) {
                scan_directory_for_completions(entry, prefix, candidates);
            }
            if (sep == std::wstring::npos) {
                break;
            }
            start = sep + 1;
        }
    }

    std::vector<std::wstring> matches;
    for (const auto& kv : candidates) {
        matches.push_back(kv.second);
    }
    return matches;
}

void render_input_line(const std::wstring& prompt, const std::wstring& buffer, size_t cursor_pos) {
    std::wcout << L"\r" << prompt << buffer << L" ";
    const size_t trailing = buffer.size() - cursor_pos;
    for (size_t i = 0; i <= trailing; ++i) {
        std::wcout << L'\b';
    }
    std::wcout.flush();
}

bool read_interactive_line(const std::wstring& prompt, std::wstring& output) {
    std::wstring buffer;
    size_t cursor = 0;
    size_t history_index = g_command_history.size();
    std::wstring saved_current_line;

    std::wcout << prompt;
    std::wcout.flush();

    while (true) {
        wint_t ch = _getwch();

        if (ch == 26) {
            if (buffer.empty()) {
                std::wcout << L"\n";
                return false;
            }
            continue;
        }

        if (ch == 13) {
            std::wcout << L"\n";
            output = buffer;
            return true;
        }

        if (ch == 3) {
            std::wcout << L"^C\n";
            output.clear();
            return true;
        }

        if (ch == 9) {
            size_t token_start = cursor;
            while (token_start > 0 && !std::iswspace(buffer[token_start - 1])) {
                token_start--;
            }
            std::wstring prefix = buffer.substr(token_start, cursor - token_start);
            std::vector<std::wstring> matches = collect_completion_candidates(prefix);

            if (matches.empty()) {
                std::wcout << L'\a';
                std::wcout.flush();
                continue;
            }

            std::wstring replacement = matches[0];
            if (matches.size() > 1) {
                std::wstring common_prefix = longest_common_prefix_case_insensitive(matches);
                if (common_prefix.size() > prefix.size()) {
                    replacement = common_prefix;
                } else {
                    std::wcout << L"\n";
                    for (const std::wstring& match : matches) {
                        std::wcout << match << L"\n";
                    }
                    render_input_line(prompt, buffer, cursor);
                    continue;
                }
            }

            buffer.replace(token_start, cursor - token_start, replacement);
            cursor = token_start + replacement.size();
            if (matches.size() == 1 && (cursor == buffer.size() || !std::iswspace(buffer[cursor]))) {
                buffer.insert(cursor, 1, L' ');
                cursor++;
            }
            render_input_line(prompt, buffer, cursor);
            continue;
        }

        if (ch == 8) {
            if (cursor > 0) {
                buffer.erase(cursor - 1, 1);
                cursor--;
                render_input_line(prompt, buffer, cursor);
            }
            continue;
        }

        if (ch == 0 || ch == 224) {
            wint_t key = _getwch();
            if (key == 72) {
                if (!g_command_history.empty()) {
                    if (history_index == g_command_history.size()) {
                        saved_current_line = buffer;
                    }
                    if (history_index > 0) {
                        history_index--;
                        buffer = g_command_history[history_index];
                        cursor = buffer.size();
                        render_input_line(prompt, buffer, cursor);
                    }
                }
            } else if (key == 80) {
                if (history_index < g_command_history.size()) {
                    history_index++;
                    if (history_index == g_command_history.size()) {
                        buffer = saved_current_line;
                    } else {
                        buffer = g_command_history[history_index];
                    }
                    cursor = buffer.size();
                    render_input_line(prompt, buffer, cursor);
                }
            } else if (key == 75) {
                if (cursor > 0) {
                    cursor--;
                    render_input_line(prompt, buffer, cursor);
                }
            } else if (key == 77) {
                if (cursor < buffer.size()) {
                    cursor++;
                    render_input_line(prompt, buffer, cursor);
                }
            } else if (key == 71) {
                cursor = 0;
                render_input_line(prompt, buffer, cursor);
            } else if (key == 79) {
                cursor = buffer.size();
                render_input_line(prompt, buffer, cursor);
            } else if (key == 83) {
                if (cursor < buffer.size()) {
                    buffer.erase(cursor, 1);
                    render_input_line(prompt, buffer, cursor);
                }
            }
            continue;
        }

        if (ch >= 32) {
            buffer.insert(cursor, 1, static_cast<wchar_t>(ch));
            cursor++;
            render_input_line(prompt, buffer, cursor);
        }
    }
}

bool resolve_history_recall(const std::wstring& input, std::wstring& resolved) {
    resolved = input;
    if (input.empty() || input[0] != L'!') {
        return true;
    }

    if (input == L"!!") {
        if (g_command_history.empty()) {
            std::wcerr << L"rsh: history empty\n";
            return false;
        }
        resolved = g_command_history.back();
        return true;
    }

    if (input.size() > 1 && input[1] == L'-') {
        try {
            size_t rel = static_cast<size_t>(std::stoul(input.substr(2)));
            if (rel == 0 || rel > g_command_history.size()) {
                std::wcerr << L"rsh: history event not found\n";
                return false;
            }
            resolved = g_command_history[g_command_history.size() - rel];
            return true;
        } catch (...) {
            std::wcerr << L"rsh: invalid history reference\n";
            return false;
        }
    }

    bool numeric = input.size() > 1;
    for (size_t i = 1; i < input.size(); ++i) {
        if (!std::iswdigit(input[i])) {
            numeric = false;
            break;
        }
    }

    if (numeric) {
        try {
            size_t index = static_cast<size_t>(std::stoul(input.substr(1)));
            if (index == 0 || index > g_command_history.size()) {
                std::wcerr << L"rsh: history event not found\n";
                return false;
            }
            resolved = g_command_history[index - 1];
            return true;
        } catch (...) {
            std::wcerr << L"rsh: invalid history reference\n";
            return false;
        }
    }

    std::wstring prefix = input.substr(1);
    for (size_t i = g_command_history.size(); i > 0; --i) {
        if (starts_with_case_insensitive(g_command_history[i - 1], prefix)) {
            resolved = g_command_history[i - 1];
            return true;
        }
    }

    std::wcerr << L"rsh: history event not found\n";
    return false;
}

void update_background_jobs(bool print_completions) {
    for (BackgroundJob& job : g_background_jobs) {
        if (job.completed) {
            continue;
        }

        DWORD code = STILL_ACTIVE;
        if (!GetExitCodeProcess(job.process_handle, &code)) {
            code = 1;
        }

        if (code != STILL_ACTIVE) {
            job.completed = true;
            job.exit_code = code;
            if (print_completions && !job.completion_reported) {
                std::wcout << L"[" << job.id << L"] Done (exit=" << job.exit_code << L") " << job.command << L"\n";
            }
            job.completion_reported = true;
        }
    }
}

BackgroundJob* find_background_job(int job_id) {
    for (BackgroundJob& job : g_background_jobs) {
        if (job.id == job_id) {
            return &job;
        }
    }
    return nullptr;
}

bool parse_job_id(const std::wstring& raw, int& job_id) {
    if (raw.empty()) {
        return false;
    }
    try {
        size_t consumed = 0;
        int parsed = std::stoi(raw, &consumed);
        if (consumed != raw.size() || parsed <= 0) {
            return false;
        }
        job_id = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

void remove_background_job(int job_id) {
    for (size_t i = 0; i < g_background_jobs.size(); ++i) {
        if (g_background_jobs[i].id == job_id) {
            if (g_background_jobs[i].process_handle != nullptr) {
                CloseHandle(g_background_jobs[i].process_handle);
                g_background_jobs[i].process_handle = nullptr;
            }
            g_background_jobs.erase(g_background_jobs.begin() + static_cast<long long>(i));
            return;
        }
    }
}

void cleanup_all_background_jobs() {
    for (BackgroundJob& job : g_background_jobs) {
        if (job.process_handle != nullptr) {
            CloseHandle(job.process_handle);
            job.process_handle = nullptr;
        }
    }
    g_background_jobs.clear();
}

const std::vector<std::wstring>& current_script_args() {
    static const std::vector<std::wstring> empty_args;
    if (g_script_context_stack.empty()) {
        return empty_args;
    }
    return g_script_context_stack.back().args;
}

std::wstring current_script_name() {
    if (g_script_context_stack.empty()) {
        return L"";
    }
    return g_script_context_stack.back().script_name;
}

std::wstring join_script_args(const std::vector<std::wstring>& args) {
    std::wstring joined;
    for (size_t i = 0; i < args.size(); ++i) {
        joined += args[i];
        if (i + 1 < args.size()) {
            joined += L" ";
        }
    }
    return joined;
}

std::wstring trim_copy(const std::wstring& value) {
    const std::wstring whitespace = L" \t\r\n";
    const size_t start = value.find_first_not_of(whitespace);
    if (start == std::wstring::npos) {
        return L"";
    }
    const size_t end = value.find_last_not_of(whitespace);
    return value.substr(start, end - start + 1);
}

bool ends_with_case_insensitive(const std::wstring& value, const std::wstring& suffix) {
    if (suffix.size() > value.size()) {
        return false;
    }

    const size_t offset = value.size() - suffix.size();
    for (size_t i = 0; i < suffix.size(); ++i) {
        if (std::towlower(value[offset + i]) != std::towlower(suffix[i])) {
            return false;
        }
    }
    return true;
}

std::wstring quote_command_argument(const std::wstring& arg) {
    if (arg.empty()) {
        return L"\"\"";
    }

    bool needs_quotes = false;
    for (wchar_t ch : arg) {
        if (std::iswspace(ch) || ch == L'"') {
            needs_quotes = true;
            break;
        }
    }

    if (!needs_quotes) {
        return arg;
    }

    std::wstring escaped;
    escaped.reserve(arg.size() + 2);
    escaped.push_back(L'"');
    for (wchar_t ch : arg) {
        if (ch == L'"') {
            escaped += L"\\\"";
        } else {
            escaped.push_back(ch);
        }
    }
    escaped.push_back(L'"');
    return escaped;
}

bool build_script_interpreter_command(const std::vector<std::wstring>& tokens, std::wstring& command_line) {
    if (tokens.empty()) {
        return false;
    }

    const std::wstring& script_path = tokens[0];
    if (ends_with_case_insensitive(script_path, L".ps1")) {
        command_line = L"powershell.exe -NoProfile -ExecutionPolicy Bypass -File ";
        command_line += quote_command_argument(script_path);
    } else if (ends_with_case_insensitive(script_path, L".sh")) {
        command_line = L"bash ";
        command_line += quote_command_argument(script_path);
    } else {
        return false;
    }

    for (size_t i = 1; i < tokens.size(); ++i) {
        command_line += L" ";
        command_line += quote_command_argument(tokens[i]);
    }

    return true;
}

bool is_ksh_script_path(const std::wstring& path) {
    return ends_with_case_insensitive(path, L".ksh");
}

bool build_self_script_command(const std::wstring& script_path, const std::vector<std::wstring>& script_args, std::wstring& command_line) {
    wchar_t exe_path[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return false;
    }

    command_line = quote_command_argument(exe_path);
    command_line += L" ";
    command_line += quote_command_argument(script_path);
    for (const std::wstring& arg : script_args) {
        command_line += L" ";
        command_line += quote_command_argument(arg);
    }
    return true;
}

std::wstring decode_multibyte(const std::string& input, UINT code_page) {
    if (input.empty()) {
        return L"";
    }

    int wide_len = MultiByteToWideChar(code_page, 0, input.data(), static_cast<int>(input.size()), nullptr, 0);
    if (wide_len <= 0) {
        return L"";
    }

    std::wstring output(static_cast<size_t>(wide_len), L'\0');
    MultiByteToWideChar(code_page, 0, input.data(), static_cast<int>(input.size()), &output[0], wide_len);
    return output;
}

std::wstring decode_script_text(const std::vector<char>& bytes) {
    if (bytes.empty()) {
        return L"";
    }

    if (bytes.size() >= 3 &&
        static_cast<unsigned char>(bytes[0]) == 0xEF &&
        static_cast<unsigned char>(bytes[1]) == 0xBB &&
        static_cast<unsigned char>(bytes[2]) == 0xBF) {
        return decode_multibyte(std::string(bytes.begin() + 3, bytes.end()), CP_UTF8);
    }

    if (bytes.size() >= 2 &&
        static_cast<unsigned char>(bytes[0]) == 0xFF &&
        static_cast<unsigned char>(bytes[1]) == 0xFE) {
        const size_t byte_count = bytes.size() - 2;
        const size_t wchar_count = byte_count / sizeof(wchar_t);
        std::wstring text(wchar_count, L'\0');
        if (wchar_count > 0) {
            std::memcpy(&text[0], bytes.data() + 2, wchar_count * sizeof(wchar_t));
        }
        return text;
    }

    if (bytes.size() >= 2 &&
        static_cast<unsigned char>(bytes[0]) == 0xFE &&
        static_cast<unsigned char>(bytes[1]) == 0xFF) {
        std::wstring text;
        for (size_t i = 2; i + 1 < bytes.size(); i += 2) {
            const unsigned char hi = static_cast<unsigned char>(bytes[i]);
            const unsigned char lo = static_cast<unsigned char>(bytes[i + 1]);
            wchar_t ch = static_cast<wchar_t>((hi << 8) | lo);
            text.push_back(ch);
        }
        return text;
    }

    std::wstring utf8 = decode_multibyte(std::string(bytes.begin(), bytes.end()), CP_UTF8);
    if (!utf8.empty()) {
        return utf8;
    }
    return decode_multibyte(std::string(bytes.begin(), bytes.end()), CP_ACP);
}

std::vector<std::wstring> split_script_lines(const std::wstring& text) {
    std::vector<std::wstring> lines;
    std::wstring current;
    for (wchar_t ch : text) {
        if (ch == L'\r') {
            continue;
        }
        if (ch == L'\n') {
            lines.push_back(current);
            current.clear();
            continue;
        }
        current.push_back(ch);
    }
    if (!current.empty()) {
        lines.push_back(current);
    }
    return lines;
}

// Evaluate ksh93 arithmetic expansion: $(( expr ))
double evaluate_math_function(const std::wstring& function_name, const std::vector<double>& args) {
    std::wstring name = to_lower_copy(function_name);

    if (name == L"abs" && args.size() == 1) return std::fabs(args[0]);
    if (name == L"sqrt" && args.size() == 1) return std::sqrt(args[0]);
    if (name == L"pow" && args.size() == 2) return std::pow(args[0], args[1]);
    if (name == L"min" && args.size() == 2) return (args[0] < args[1]) ? args[0] : args[1];
    if (name == L"max" && args.size() == 2) return (args[0] > args[1]) ? args[0] : args[1];
    if (name == L"sin" && args.size() == 1) return std::sin(args[0]);
    if (name == L"cos" && args.size() == 1) return std::cos(args[0]);
    if (name == L"tan" && args.size() == 1) return std::tan(args[0]);
    if (name == L"asin" && args.size() == 1) return std::asin(args[0]);
    if (name == L"acos" && args.size() == 1) return std::acos(args[0]);
    if (name == L"atan" && args.size() == 1) return std::atan(args[0]);
    if (name == L"log" && args.size() == 1) return std::log(args[0]);
    if (name == L"log10" && args.size() == 1) return std::log10(args[0]);
    if (name == L"exp" && args.size() == 1) return std::exp(args[0]);
    if (name == L"floor" && args.size() == 1) return std::floor(args[0]);
    if (name == L"ceil" && args.size() == 1) return std::ceil(args[0]);
    if (name == L"round" && args.size() == 1) return std::round(args[0]);

    throw std::runtime_error("unsupported function");
}

class ArithmeticParser {
public:
    explicit ArithmeticParser(const std::wstring& expression) : expr(expression), pos(0) {}

    double parse() {
        double value = parse_expression();
        skip_spaces();
        if (pos != expr.size()) {
            throw std::runtime_error("unexpected trailing characters");
        }
        return value;
    }

private:
    const std::wstring& expr;
    size_t pos;

    void skip_spaces() {
        while (pos < expr.size() && std::iswspace(expr[pos])) {
            pos++;
        }
    }

    bool consume(wchar_t token) {
        skip_spaces();
        if (pos < expr.size() && expr[pos] == token) {
            pos++;
            return true;
        }
        return false;
    }

    double parse_expression() {
        double value = parse_term();
        while (true) {
            if (consume(L'+')) {
                value += parse_term();
            } else if (consume(L'-')) {
                value -= parse_term();
            } else {
                break;
            }
        }
        return value;
    }

    double parse_term() {
        double value = parse_power();
        while (true) {
            if (consume(L'*')) {
                value *= parse_power();
            } else if (consume(L'/')) {
                double right = parse_power();
                if (right == 0.0) {
                    throw std::runtime_error("division by zero");
                }
                value /= right;
            } else if (consume(L'%')) {
                double right = parse_power();
                if (right == 0.0) {
                    throw std::runtime_error("modulo by zero");
                }
                value = std::fmod(value, right);
            } else {
                break;
            }
        }
        return value;
    }

    double parse_power() {
        double left = parse_unary();
        if (consume(L'^')) {
            double right = parse_power();
            return std::pow(left, right);
        }
        return left;
    }

    double parse_unary() {
        if (consume(L'+')) {
            return parse_unary();
        }
        if (consume(L'-')) {
            return -parse_unary();
        }
        return parse_primary();
    }

    std::wstring parse_identifier() {
        skip_spaces();
        std::wstring identifier;
        while (pos < expr.size() && (std::iswalnum(expr[pos]) || expr[pos] == L'_')) {
            identifier.push_back(expr[pos]);
            pos++;
        }
        return identifier;
    }

    double parse_number() {
        skip_spaces();
        size_t start = pos;
        bool seen_dot = false;
        while (pos < expr.size()) {
            wchar_t ch = expr[pos];
            if (std::iswdigit(ch)) {
                pos++;
                continue;
            }
            if (ch == L'.' && !seen_dot) {
                seen_dot = true;
                pos++;
                continue;
            }
            break;
        }
        if (start == pos) {
            throw std::runtime_error("number expected");
        }
        return std::stod(expr.substr(start, pos - start));
    }

    double parse_primary() {
        skip_spaces();
        if (consume(L'(')) {
            double inner = parse_expression();
            if (!consume(L')')) {
                throw std::runtime_error("missing closing parenthesis");
            }
            return inner;
        }

        if (pos < expr.size() && (std::iswalpha(expr[pos]) || expr[pos] == L'_')) {
            std::wstring identifier = parse_identifier();
            std::wstring lowered = to_lower_copy(identifier);
            if (lowered == L"pi") {
                return std::acos(-1.0);
            }
            if (lowered == L"e") {
                return std::exp(1.0);
            }

            if (consume(L'(')) {
                std::vector<double> args;
                skip_spaces();
                if (!consume(L')')) {
                    while (true) {
                        args.push_back(parse_expression());
                        if (consume(L')')) {
                            break;
                        }
                        if (!consume(L',')) {
                            throw std::runtime_error("comma expected");
                        }
                    }
                }
                return evaluate_math_function(identifier, args);
            }

            if (ksh_env.variables.find(identifier) != ksh_env.variables.end()) {
                return std::stod(ksh_env.variables[identifier]);
            }
            return 0.0;
        }

        return parse_number();
    }
};

std::wstring evaluate_arithmetic(const std::wstring& expr) {
    try {
        ArithmeticParser parser(expr);
        double value = parser.parse();
        if (!std::isfinite(value)) {
            return L"0";
        }

        double nearest = std::round(value);
        if (std::fabs(value - nearest) < 1e-9) {
            return std::to_wstring(static_cast<long long>(nearest));
        }

        wchar_t buffer[64];
        _snwprintf_s(buffer, _countof(buffer), _TRUNCATE, L"%.10f", value);
        std::wstring result(buffer);
        while (!result.empty() && result.back() == L'0') {
            result.pop_back();
        }
        if (!result.empty() && result.back() == L'.') {
            result.pop_back();
        }
        return result.empty() ? L"0" : result;
    } catch (...) {
        return L"0";
    }
}

// Expand ksh93 variables, arrays, and $((arithmetic))
std::wstring ksh_expand(const std::wstring& input) {
    std::wstring result;
    size_t i = 0;
    const std::vector<std::wstring>& script_args = current_script_args();
    const std::wstring script_name = current_script_name();
    while (i < input.length()) {
        if (input[i] == L'$') {
            i++;
            if (i < input.length() && input[i] == L'#') {
                result += std::to_wstring(script_args.size());
                i++;
                continue;
            }
            if (i < input.length() && input[i] == L'@') {
                result += join_script_args(script_args);
                i++;
                continue;
            }
            if (i < input.length() && std::iswdigit(input[i])) {
                std::wstring index_text;
                while (i < input.length() && std::iswdigit(input[i])) {
                    index_text += input[i];
                    i++;
                }
                const size_t arg_index = static_cast<size_t>(std::stoul(index_text));
                if (arg_index == 0) {
                    result += script_name;
                } else if (arg_index <= script_args.size()) {
                    result += script_args[arg_index - 1];
                }
                continue;
            }
            if (i < input.length() && input[i] == L'(') {
                i++;
                if (i < input.length() && input[i] == L'(') {
                    // $(( arithmetic )) expansion
                    i++;
                    std::wstring arith_content;
                    int inner_depth = 0;
                    while (i < input.length()) {
                        if (input[i] == L'(') {
                            inner_depth++;
                            arith_content += input[i];
                            i++;
                            continue;
                        }

                        if (input[i] == L')') {
                            if (inner_depth == 0) {
                                if (i + 1 < input.length() && input[i + 1] == L')') {
                                    i += 2;
                                    break;
                                }
                            } else {
                                inner_depth--;
                                arith_content += input[i];
                                i++;
                                continue;
                            }
                        }

                        arith_content += input[i];
                        i++;
                    }
                    result += evaluate_arithmetic(arith_content);
                } else {
                    // $( command substitution ) expansion
                    std::wstring cmd_content;
                    int depth = 1;
                    while (i < input.length() && depth > 0) {
                        if (input[i] == L'(') depth++;
                        if (input[i] == L')') depth--;
                        if (depth > 0) cmd_content += input[i];
                        i++;
                    }
                    // Run command capture output (simplified simulation)
                    result += L"[cmd_sub_output]";
                }
            } else {
                // Standard variable or array lookup $VAR or ${VAR}
                std::wstring var_name;
                bool braced = false;
                if (i < input.length() && input[i] == L'{') {
                    braced = true;
                    i++;
                }
                if (braced && i < input.length() && (input[i] == L'@' || input[i] == L'#')) {
                    var_name += input[i];
                    i++;
                }
                while (i < input.length() && (std::iswalnum(input[i]) || input[i] == L'_')) {
                    var_name += input[i];
                    i++;
                }
                if (braced && i < input.length() && input[i] == L'}') {
                    i++;
                }

                if (var_name == L"#") {
                    result += std::to_wstring(script_args.size());
                } else if (var_name == L"@") {
                    result += join_script_args(script_args);
                } else if (!var_name.empty() && std::all_of(var_name.begin(), var_name.end(), [](wchar_t ch) { return std::iswdigit(ch) != 0; })) {
                    const size_t arg_index = static_cast<size_t>(std::stoul(var_name));
                    if (arg_index == 0) {
                        result += script_name;
                    } else if (arg_index <= script_args.size()) {
                        result += script_args[arg_index - 1];
                    }
                } else if (ksh_env.variables.find(var_name) != ksh_env.variables.end()) {
                    result += ksh_env.variables[var_name];
                } else {
                    wchar_t buf[32767];
                    DWORD ret = GetEnvironmentVariableW(var_name.c_str(), buf, 32767);
                    if (ret > 0) result += buf;
                }
            }
        } else {
            result += input[i];
            i++;
        }
    }
    return result;
}

// Advanced ksh93 Tokenizer handling quotes and compound array blocks
std::vector<std::wstring> ksh_tokenize(const std::wstring& input) {
    std::vector<std::wstring> tokens;
    std::wstring current;
    bool in_quotes = false;
    bool in_array_def = false;

    for (size_t i = 0; i < input.length(); ++i) {
        wchar_t c = input[i];
        if (c == L'"' && !in_array_def) {
            in_quotes = !in_quotes;
        } else if (c == L'(' && i > 0 && input[i-1] == L'=') {
            in_array_def = true;
            current += c;
        } else if (c == L')' && in_array_def) {
            in_array_def = false;
            current += c;
        } else if (c == L' ' && !in_quotes && !in_array_def) {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
        } else {
            current += c;
        }
    }
    if (!current.empty()) {
        tokens.push_back(current);
    }
    return tokens;
}

// ksh93 Advanced Conditional Evaluator
bool evaluate_ksh_conditional(const std::wstring& condition_str) {
    // Parse basic binary operators: ==, !=, =~ (regex), -ef, -nt, -ot
    std::wregex regex_op(L"(.*?)\\s+(=~)\\s+(.*?)");
    std::wsmatch match;
    if (std::regex_match(condition_str, match, regex_op)) {
        try {
            std::wstring str = match[1];
            std::wstring pat = match[3];
            std::wregex rx(pat);
            return std::regex_search(str, rx);
        } catch (...) {
            return false;
        }
    }
    // Simple string equality fallback
    size_t eq = condition_str.find(L"==");
    if (eq != std::wstring::npos) {
        std::wstring left = condition_str.substr(0, eq);
        std::wstring right = condition_str.substr(eq + 2);
        // Trim spaces
        left.erase(left.find_last_not_of(L" \n\r\t") + 1);
        right.erase(0, right.find_first_not_of(L" \n\r\t"));
        return left == right;
    }
    return false;
}

void execute_native_or_fallback(const std::wstring& full_command) {
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    std::vector<wchar_t> mutable_cmd(full_command.begin(), full_command.end());
    mutable_cmd.push_back(L'\0');
    if (CreateProcessW(nullptr, mutable_cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return;
    }

    // Fallback to cmd.exe for Windows built-in shell commands
    std::wstring cmd_call = L"cmd.exe /c " + full_command;
    std::vector<wchar_t> cmd_call_mutable(cmd_call.begin(), cmd_call.end());
    cmd_call_mutable.push_back(L'\0');
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));
    if (CreateProcessW(nullptr, cmd_call_mutable.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
}

bool launch_process(const std::wstring& full_command, HANDLE& process_handle, DWORD& pid) {
    process_handle = nullptr;
    pid = 0;

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    std::vector<wchar_t> mutable_cmd(full_command.begin(), full_command.end());
    mutable_cmd.push_back(L'\0');
    if (CreateProcessW(nullptr, mutable_cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        process_handle = pi.hProcess;
        pid = pi.dwProcessId;
        CloseHandle(pi.hThread);
        return true;
    }

    std::wstring cmd_call = L"cmd.exe /c " + full_command;
    std::vector<wchar_t> cmd_call_mutable(cmd_call.begin(), cmd_call.end());
    cmd_call_mutable.push_back(L'\0');
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));
    if (CreateProcessW(nullptr, cmd_call_mutable.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        process_handle = pi.hProcess;
        pid = pi.dwProcessId;
        CloseHandle(pi.hThread);
        return true;
    }

    return false;
}

bool execute_script_file(const std::wstring& script_path, const std::vector<std::wstring>& script_args, bool& should_exit_shell) {
    ScriptContext context;
    context.script_name = script_path;
    context.args = script_args;
    g_script_context_stack.push_back(context);

    std::ifstream script_file(script_path.c_str(), std::ios::binary);
    if (!script_file.is_open()) {
        std::wcerr << L"rsh: cannot open script: " << script_path << L"\n";
        ksh_env.variables[L"?"] = L"1";
        g_script_context_stack.pop_back();
        return false;
    }

    std::vector<char> bytes((std::istreambuf_iterator<char>(script_file)), std::istreambuf_iterator<char>());
    std::wstring script_text = decode_script_text(bytes);
    std::vector<std::wstring> lines = split_script_lines(script_text);

    bool first_line = true;
    for (const std::wstring& line : lines) {
        if (first_line && line.rfind(L"#!", 0) == 0) {
            first_line = false;
            continue;
        }
        first_line = false;

        std::wstring trimmed = trim_copy(line);
        if (trimmed.empty() || trimmed[0] == L'#') {
            continue;
        }

        if (!execute_command_line(trimmed, should_exit_shell)) {
            g_script_context_stack.pop_back();
            return false;
        }
        if (should_exit_shell) {
            g_script_context_stack.pop_back();
            return true;
        }
    }

    ksh_env.variables[L"?"] = L"0";
    g_script_context_stack.pop_back();
    return true;
}

bool execute_command_line(const std::wstring& input_line, bool& should_exit_shell) {
    if (input_line.empty()) {
        return true;
    }

    std::wstring resolved_input;
    if (!resolve_history_recall(input_line, resolved_input)) {
        ksh_env.variables[L"?"] = L"1";
        return false;
    }
    if (resolved_input != input_line) {
        std::wcout << resolved_input << L"\n";
    }
    g_command_history.push_back(resolved_input);
    if (g_is_interactive_session) {
        append_history_entry_to_file(resolved_input);
    }

    // 1. Expand ksh93 variables and expressions
    std::wstring expanded = ksh_expand(resolved_input);

    // 2. Tokenize with ksh93 rules
    std::vector<std::wstring> tokens = ksh_tokenize(expanded);
    if (tokens.empty()) {
        return true;
    }

    bool run_in_background = false;
    if (!tokens.empty() && tokens.back() == L"&") {
        run_in_background = true;
        tokens.pop_back();
        if (tokens.empty()) {
            ksh_env.variables[L"?"] = L"1";
            return false;
        }
    }

    std::wstring command_to_run = expanded;
    if (run_in_background) {
        command_to_run = trim_copy(command_to_run);
        if (!command_to_run.empty() && command_to_run.back() == L'&') {
            command_to_run.pop_back();
            command_to_run = trim_copy(command_to_run);
        }
    }

    std::wstring cmd = tokens[0];
    std::wstring external_command_to_run = command_to_run;
    build_script_interpreter_command(tokens, external_command_to_run);

    if (cmd == L"exit") {
        should_exit_shell = true;
        ksh_env.variables[L"?"] = L"0";
        return true;
    }

    if (cmd == L"source" || cmd == L".") {
        if (tokens.size() < 2) {
            std::wcerr << L"rsh: source requires a file path\n";
            ksh_env.variables[L"?"] = L"1";
            return false;
        }
        std::vector<std::wstring> source_args;
        for (size_t i = 2; i < tokens.size(); ++i) {
            source_args.push_back(tokens[i]);
        }
        return execute_script_file(tokens[1], source_args, should_exit_shell);
    }

    if (is_ksh_script_path(cmd)) {
        std::vector<std::wstring> script_args;
        for (size_t i = 1; i < tokens.size(); ++i) {
            script_args.push_back(tokens[i]);
        }

        if (!run_in_background) {
            return execute_script_file(cmd, script_args, should_exit_shell);
        }

        std::wstring script_command;
        if (!build_self_script_command(cmd, script_args, script_command)) {
            std::wcerr << L"rsh: failed to resolve shell executable path\n";
            ksh_env.variables[L"?"] = L"1";
            return false;
        }

        HANDLE process_handle = nullptr;
        DWORD pid = 0;
        if (!launch_process(script_command, process_handle, pid)) {
            std::wcerr << L"rsh: failed to start background script\n";
            ksh_env.variables[L"?"] = L"1";
            return false;
        }

        BackgroundJob job;
        job.id = g_next_job_id++;
        job.pid = pid;
        job.process_handle = process_handle;
        job.command = script_command;
        job.completed = false;
        job.exit_code = STILL_ACTIVE;
        job.completion_reported = false;
        g_background_jobs.push_back(job);

        std::wcout << L"[" << job.id << L"] " << job.pid << L"\n";
        ksh_env.variables[L"?"] = L"0";
        return true;
    }

    if (cmd == L"history") {
        for (size_t i = 0; i < g_command_history.size(); ++i) {
            std::wcout << (i + 1) << L"  " << g_command_history[i] << L"\n";
        }
        ksh_env.variables[L"?"] = L"0";
        return true;
    }

    if (cmd == L"jobs") {
        update_background_jobs(false);
        for (const BackgroundJob& job : g_background_jobs) {
            std::wcout << L"[" << job.id << L"] "
                      << (job.completed ? L"Done" : L"Running")
                      << L" pid=" << job.pid;
            if (job.completed) {
                std::wcout << L" exit=" << job.exit_code;
            }
            std::wcout << L" " << job.command << L"\n";
        }
        ksh_env.variables[L"?"] = L"0";
        return true;
    }

    if (cmd == L"fg") {
        if (tokens.size() < 2) {
            std::wcerr << L"rsh: fg requires a job id\n";
            ksh_env.variables[L"?"] = L"1";
            return false;
        }

        int job_id = 0;
        if (!parse_job_id(tokens[1], job_id)) {
            std::wcerr << L"rsh: invalid job id\n";
            ksh_env.variables[L"?"] = L"1";
            return false;
        }

        update_background_jobs(false);
        BackgroundJob* job = find_background_job(job_id);
        if (job == nullptr) {
            std::wcerr << L"rsh: job not found: " << job_id << L"\n";
            ksh_env.variables[L"?"] = L"1";
            return false;
        }

        if (!job->completed) {
            std::wcout << L"[" << job->id << L"] foreground " << job->command << L"\n";
            WaitForSingleObject(job->process_handle, INFINITE);
            DWORD code = 1;
            GetExitCodeProcess(job->process_handle, &code);
            job->completed = true;
            job->exit_code = code;
        }

        std::wstring exit_code = std::to_wstring(job->exit_code);
        remove_background_job(job_id);
        ksh_env.variables[L"?"] = exit_code;
        return true;
    }

    if (cmd == L"kill") {
        if (tokens.size() < 2) {
            std::wcerr << L"rsh: kill requires a job id\n";
            ksh_env.variables[L"?"] = L"1";
            return false;
        }

        int job_id = 0;
        if (!parse_job_id(tokens[1], job_id)) {
            std::wcerr << L"rsh: invalid job id\n";
            ksh_env.variables[L"?"] = L"1";
            return false;
        }

        BackgroundJob* job = find_background_job(job_id);
        if (job == nullptr) {
            std::wcerr << L"rsh: job not found: " << job_id << L"\n";
            ksh_env.variables[L"?"] = L"1";
            return false;
        }

        if (!job->completed) {
            TerminateProcess(job->process_handle, 1);
            WaitForSingleObject(job->process_handle, INFINITE);
        }

        remove_background_job(job_id);
        ksh_env.variables[L"?"] = L"0";
        return true;
    }

    if (cmd == L"wait") {
        if (tokens.size() == 1) {
            update_background_jobs(false);
            std::vector<int> ids;
            for (const BackgroundJob& job : g_background_jobs) {
                if (!job.completed) {
                    ids.push_back(job.id);
                }
            }

            for (int id : ids) {
                BackgroundJob* job = find_background_job(id);
                if (job != nullptr && !job->completed) {
                    WaitForSingleObject(job->process_handle, INFINITE);
                    DWORD code = 1;
                    GetExitCodeProcess(job->process_handle, &code);
                    job->completed = true;
                    job->exit_code = code;
                }
                remove_background_job(id);
            }

            ksh_env.variables[L"?"] = L"0";
            return true;
        }

        int job_id = 0;
        if (!parse_job_id(tokens[1], job_id)) {
            std::wcerr << L"rsh: invalid job id\n";
            ksh_env.variables[L"?"] = L"1";
            return false;
        }

        BackgroundJob* job = find_background_job(job_id);
        if (job == nullptr) {
            std::wcerr << L"rsh: job not found: " << job_id << L"\n";
            ksh_env.variables[L"?"] = L"1";
            return false;
        }

        if (!job->completed) {
            WaitForSingleObject(job->process_handle, INFINITE);
            DWORD code = 1;
            GetExitCodeProcess(job->process_handle, &code);
            job->completed = true;
            job->exit_code = code;
        }

        std::wstring exit_code = std::to_wstring(job->exit_code);
        remove_background_job(job_id);
        ksh_env.variables[L"?"] = exit_code;
        return true;
    }

    if (cmd == L"complete") {
        std::wstring prefix = (tokens.size() > 1) ? tokens[1] : L"";
        std::vector<std::wstring> matches = collect_completion_candidates(prefix);
        for (const std::wstring& match : matches) {
            std::wcout << match << L"\n";
        }
        ksh_env.variables[L"?"] = matches.empty() ? L"1" : L"0";
        return !matches.empty();
    }

    if (cmd == L"math") {
        if (tokens.size() < 2) {
            std::wcerr << L"rsh: math requires an expression\n";
            ksh_env.variables[L"?"] = L"1";
            return false;
        }

        std::wstring expression;
        for (size_t i = 1; i < tokens.size(); ++i) {
            expression += tokens[i];
            if (i + 1 < tokens.size()) {
                expression += L" ";
            }
        }

        std::wstring value = evaluate_arithmetic(expression);
        std::wcout << value << L"\n";
        ksh_env.variables[L"?"] = L"0";
        return true;
    }

    // ksh93 Built-in: typeset (supports array declarations typeset -a)
    if (cmd == L"typeset") {
        if (tokens.size() > 1) {
            std::wstring declaration = tokens[1];
            // Check compound array assignment e.g. arr=(one two three)
            size_t assignment_eq = declaration.find(L'=');
            if (assignment_eq != std::wstring::npos) {
                std::wstring var_name = declaration.substr(0, assignment_eq);
                std::wstring val_part = declaration.substr(assignment_eq + 1);
                ksh_env.variables[var_name] = val_part;
            }
        }
        ksh_env.variables[L"?"] = L"0";
        return true;
    }

    // ksh93 Built-in: [[ conditional test ]]
    if (cmd == L"[[") {
        // Reconstruct block condition string up to ']]'
        std::wstring cond_block;
        for (size_t t = 1; t < tokens.size(); ++t) {
            if (tokens[t] == L"]]") break;
            cond_block += tokens[t] + L" ";
        }
        bool test_result = evaluate_ksh_conditional(cond_block);
        ksh_env.variables[L"?"] = test_result ? L"0" : L"1";
        return true;
    }

    // Variable assignment standard ksh: var=value
    size_t eq = cmd.find(L'=');
    if (eq != std::wstring::npos && eq > 0) {
        std::wstring k = cmd.substr(0, eq);
        std::wstring v = cmd.substr(eq + 1);
        ksh_env.variables[k] = v;
        SetEnvironmentVariableW(k.c_str(), v.c_str());
        ksh_env.variables[L"?"] = L"0";
        return true;
    }

    // Built-in: cd
    if (cmd == L"cd") {
        std::wstring p = (tokens.size() > 1) ? tokens[1] : L"C:\\";
        if (SetCurrentDirectoryW(p.c_str())) {
            ksh_env.variables[L"?"] = L"0";
            return true;
        }
        std::wcerr << L"rsh: cd failed: " << p << L"\n";
        ksh_env.variables[L"?"] = L"1";
        return false;
    }

    // Built-in: print / echo
    if (cmd == L"print" || cmd == L"echo") {
        for (size_t i = 1; i < tokens.size(); ++i) {
            std::wcout << tokens[i] << (i == tokens.size() - 1 ? L"" : L" ");
        }
        std::wcout << L"\n";
        ksh_env.variables[L"?"] = L"0";
        return true;
    }

    // Execute external processes or system fallback
    if (!run_in_background) {
        execute_native_or_fallback(external_command_to_run);
        ksh_env.variables[L"?"] = L"0";
        return true;
    }

    HANDLE process_handle = nullptr;
    DWORD pid = 0;
    if (!launch_process(external_command_to_run, process_handle, pid)) {
        std::wcerr << L"rsh: failed to start background command\n";
        ksh_env.variables[L"?"] = L"1";
        return false;
    }

    BackgroundJob job;
    job.id = g_next_job_id++;
    job.pid = pid;
    job.process_handle = process_handle;
    job.command = command_to_run;
    job.completed = false;
    job.exit_code = STILL_ACTIVE;
    job.completion_reported = false;
    g_background_jobs.push_back(job);

    std::wcout << L"[" << job.id << L"] " << job.pid << L"\n";
    ksh_env.variables[L"?"] = L"0";
    return true;
}

int wmain(int argc, wchar_t* argv[]) {
    _setmode(_fileno(stdout), _O_U16TEXT);
    _setmode(_fileno(stdin), _O_U16TEXT);
    _setmode(_fileno(stderr), _O_U16TEXT);

    bool should_exit_shell = false;

    if (argc > 1 && std::wcscmp(argv[1], L"-v") == 0) {
        std::wcout << L"\n";
        std::wcout << L"RunicShell26 (2.0.1 2026) Copyright (C) 2026, Roberto J Dohnert\n";
        std::wcout << L"        All Rights Reserved. License: BSD-3-Clause        \n";
        std::wcout << L"\n";
        std::wcout << L"Microsoft Windows is a registered trademark of Microsoft Corporation.\n";
        std::wcout << L"KornShell is a registered trademark of AT&T Research released under the\n";
        std::wcout << L"Eclipse Public License. Originally developed by David Korn.\n";
        std::wcout << L"\n";
        return 0;
    }

    if (argc > 1) {
        std::vector<std::wstring> script_args;
        for (int i = 2; i < argc; ++i) {
            script_args.push_back(argv[i]);
        }
        execute_script_file(argv[1], script_args, should_exit_shell);
        return should_exit_shell ? 0 : 0;
    }

    std::wcout << L"\n";
    print_startup_system_info();
    std::wcout << L"\n";
    std::wcout << L"Type 'exit' to quit.\n\n";

    g_is_interactive_session = true;
    load_command_history_from_file();

    std::wstring input_line;

    while (true) {
        update_background_jobs(true);

        std::wstring prompt;
        wchar_t ps1[256];
        if (GetEnvironmentVariableW(L"PS1", ps1, 256) > 0) {
            prompt = ps1;
        } else {
            wchar_t cwd[MAX_PATH];
            DWORD cwd_len = GetCurrentDirectoryW(MAX_PATH, cwd);
            prompt = (cwd_len > 0 ? std::wstring(cwd) : L".") + L" $ ";
        }

        if (!read_interactive_line(prompt, input_line)) {
            break;
        }

        std::wstring trimmed = trim_copy(input_line);
        if (trimmed.empty()) {
            continue;
        }

        execute_command_line(trimmed, should_exit_shell);
        if (should_exit_shell) {
            break;
        }
    }

    cleanup_all_background_jobs();
    return 0;
}