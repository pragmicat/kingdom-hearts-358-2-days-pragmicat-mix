#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <filesystem>
#include <functional>
#include <string>

namespace zw {
namespace fs = std::filesystem;
inline constexpr wchar_t title[] = L"Kingdom Hearts 358/2 Days: Pragmicat Mix";
using Progress = std::function<void(const std::wstring&)>;
struct Seeded { bool save = false, settings = false; };
struct Prepared { fs::path root, profile, executable, source; Seeded seeded; size_t payload_files = 0; Seeded campaign_seeded, practice_seeded; };
struct Options { fs::path root, game, report; bool check = false, practice = false, self_test = false; };
std::string utf8(const std::wstring& text);
std::wstring wide(const std::string& text);
[[noreturn]] void fail(const std::wstring& message);
fs::path default_root();
bool installed(const fs::path& root);
bool supported_game(const fs::path& selected);
fs::path discover_game(const fs::path& root, const fs::path& own_folder);
Prepared prepare(const fs::path& root, fs::path selected, bool practice, const Progress& progress);
void launch_game(const Prepared& prepared, bool practice);
Options parse_options();
std::string quoted(const std::wstring& value);
void report(const fs::path& filename, const std::string& text);
void self_test(const fs::path& output);
} // namespace zw
