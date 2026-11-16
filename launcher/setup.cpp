#include "setup.h"
#include <bcrypt.h>
#include <shlobj.h>
#include <shellapi.h>
#include <winioctl.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <chrono>
#include <fstream>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>
#include "payload_entries.h"

namespace zw {
namespace {
constexpr char owner[] = "Kingdom Hearts 358/2 Days: Pragmicat Mix\nprofile-format=1\n";
constexpr char runtime_hash[] = "383ccd23526d92f1d4ab6fb6a4c1bccc8d87f8b66ffc7af455cf21943350dc29";
struct Handle {
    HANDLE value;
    explicit Handle(HANDLE h = INVALID_HANDLE_VALUE) : value(h) {}
    ~Handle() { if (value != INVALID_HANDLE_VALUE && value) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
std::wstring error_text(DWORD code = GetLastError()) {
    wchar_t* message = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, code, 0, reinterpret_cast<wchar_t*>(&message), 0, nullptr);
    std::wstring result = message ? message : L"Windows could not complete this operation.";
    if (message) LocalFree(message);
    while (!result.empty() && (result.back() == L'\r' || result.back() == L'\n')) result.pop_back();
    return result;
}
std::wstring lower(std::wstring text) { if (!text.empty()) CharLowerBuffW(text.data(), static_cast<DWORD>(text.size())); return text; }
bool is_link(const fs::path& path) { const auto a = GetFileAttributesW(path.c_str()); return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_REPARSE_POINT) != 0; }
bool file(const fs::path& path) { std::error_code e; return fs::is_regular_file(path, e); }
bool directory(const fs::path& path) { std::error_code e; return fs::is_directory(path, e); }
std::string read_text(const fs::path& path) { std::ifstream in(path, std::ios::binary); return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()}; }
fs::path normalized(const fs::path& path) { return fs::weakly_canonical(fs::absolute(path)); }
bool within(const fs::path& child, const fs::path& parent) {
    const auto c = lower(normalized(child).wstring()), p = lower(normalized(parent).wstring());
    return c == p || (c.size() > p.size() && c.compare(0, p.size(), p) == 0 && (c[p.size()] == L'\\' || c[p.size()] == L'/'));
}
bool safe_relative(const fs::path& path) {
    if (path.empty() || path.is_absolute() || path.has_root_name()) return false;
    for (const auto& part : path) if (part == L".." || part == L"." || part.wstring().find(L':') != std::wstring::npos) return false;
    return true;
}
void make_directory(const fs::path& root, const fs::path& relative) {
    if (!safe_relative(relative)) fail(L"An installation path is not valid.");
    fs::path current = root;
    for (const auto& part : relative) {
        current /= part;
        if (is_link(current)) fail(L"A setup folder points outside this project. Choose a fresh Kingdom Hearts 358/2 Days: Pragmicat Mix folder.");
        if (fs::exists(current) && !directory(current)) fail(L"A file is blocking a setup folder.");
        fs::create_directory(current);
    }
}
bool same_contents(const fs::path& path, const void* data, size_t size) {
    std::error_code error;
    if (!file(path) || fs::file_size(path, error) != size || error) return false;
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    std::array<char, 65536> buffer{};
    const auto* expected = static_cast<const unsigned char*>(data);
    for (size_t offset = 0; offset < size;) {
        const auto count = std::min(buffer.size(), size - offset);
        input.read(buffer.data(), static_cast<std::streamsize>(count));
        if (input.gcount() != static_cast<std::streamsize>(count) ||
            std::memcmp(buffer.data(), expected + offset, count) != 0) return false;
        offset += count;
    }
    return true;
}
void write_owned(const fs::path& root, const fs::path& relative, const void* data, size_t size) {
    if (!safe_relative(relative)) fail(L"An installation file path is not valid.");
    if (!relative.parent_path().empty()) make_directory(root, relative.parent_path());
    const fs::path destination = root / relative, temporary = destination.wstring() + L".pragmicat-new";
    if (is_link(destination) || is_link(temporary)) fail(L"A setup file points outside this project.");
    if (same_contents(destination, data, size)) return;
    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size)); out.close();
    if (!out || !MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        fail(L"Could not save a Kingdom Hearts 358/2 Days: Pragmicat Mix file.\n\n" + error_text());
}
void write_owned(const fs::path& root, const fs::path& relative, const std::string& text) { write_owned(root, relative, text.data(), text.size()); }
void copy_owned(const fs::path& source, const fs::path& root, const fs::path& relative, bool missing_only = false) {
    const auto destination = root / relative;
    if (missing_only && file(destination)) return;
    if (!safe_relative(relative)) fail(L"A copied file path is not valid.");
    if (!relative.parent_path().empty()) make_directory(root, relative.parent_path());
    const fs::path temporary = destination.wstring() + L".pragmicat-new";
    if (is_link(destination) || is_link(temporary)) fail(L"A copied file points outside this project.");
    if (!CopyFileW(source.c_str(), temporary.c_str(), FALSE) || !MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        fail(L"Could not copy a game file into Kingdom Hearts 358/2 Days: Pragmicat Mix.\n\n" + error_text());
}
std::string sha256(const fs::path& path) {
    Handle input(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (input.value == INVALID_HANDLE_VALUE) return {};
    BCRYPT_ALG_HANDLE algorithm = nullptr; BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return {};
    DWORD object_size = 0, returned = 0;
    bool ok = BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&object_size), sizeof(object_size), &returned, 0) >= 0;
    std::vector<unsigned char> object(object_size);
    ok = ok && BCryptCreateHash(algorithm, &hash, object.data(), object_size, nullptr, 0, 0) >= 0;
    std::array<unsigned char, 65536> buffer{}; DWORD count = 0;
    while (ok) {
        if (!ReadFile(input.value, buffer.data(), static_cast<DWORD>(buffer.size()), &count, nullptr)) { ok = false; break; }
        if (!count) break;
        ok = BCryptHashData(hash, buffer.data(), count, 0) >= 0;
    }
    std::array<unsigned char, 32> digest{};
    ok = ok && BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) >= 0;
    if (hash) BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!ok) return {};
    std::string result;
    for (const auto byte : digest) { result += "0123456789abcdef"[byte >> 4]; result += "0123456789abcdef"[byte & 15]; }
    return result;
}
fs::path extracted_game(const fs::path& extracted) {
    if (!directory(extracted)) return {};
    std::error_code error;
    for (const auto& entry : fs::directory_iterator(extracted, error)) {
        const auto candidate = entry.path(), header = candidate / L"system/header.bin";
        if (candidate.extension() != L".partial" && directory(candidate / L"nitrofs") && file(header) && fs::file_size(header, error) == 512) return candidate;
    }
    return {};
}
fs::path game_executable(const fs::path& selected) { return directory(selected) ? selected / L"khdays-native.exe" : selected; }
fs::path known_folder(REFKNOWNFOLDERID id) {
    wchar_t* result = nullptr;
    if (FAILED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &result))) fail(L"Could not locate your Windows folders.");
    fs::path path(result); CoTaskMemFree(result); return path;
}
bool owned_root(const fs::path& root) { return !is_link(root) && read_text(root / L".pragmicat-mix-owned") == owner; }
void take_root(const fs::path& root) {
    if (is_link(root)) fail(L"The Kingdom Hearts 358/2 Days: Pragmicat Mix folder is a link. Choose a fresh folder for this project.");
    if (fs::exists(root) && !owned_root(root) && (!directory(root) || !fs::is_empty(root))) fail(L"That folder already contains another project. Kingdom Hearts 358/2 Days: Pragmicat Mix will not change it. Choose an empty folder.");
    fs::create_directories(root); write_owned(root, L".pragmicat-mix-owned", std::string(owner));
}
fs::path remembered_game(const fs::path& root) {
    if (!owned_root(root)) return {};
    std::array<wchar_t, 32768> value{};
    GetPrivateProfileStringW(L"launcher", L"game", L"", value.data(), static_cast<DWORD>(value.size()), (root / L"launcher.ini").c_str());
    return fs::path(value.data());
}
std::wstring final_directory(const fs::path& path) {
    Handle h(CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
    if (h.value == INVALID_HANDLE_VALUE) return {};
    std::vector<wchar_t> buffer(32768);
    const auto length = GetFinalPathNameByHandleW(h.value, buffer.data(), static_cast<DWORD>(buffer.size()), FILE_NAME_NORMALIZED);
    return length && length < buffer.size() ? lower(std::wstring(buffer.data(), length)) : std::wstring{};
}
void data_link(const fs::path& profile, const fs::path& target) {
    const auto link = profile / L"data";
    if (fs::exists(link)) {
        const auto current = final_directory(link);
        if (!current.empty() && current == final_directory(target)) return;
        if (is_link(link) || !directory(link) || !fs::is_empty(link)) fail(L"A profile's data folder points somewhere else. Kingdom Hearts 358/2 Days: Pragmicat Mix will not change it.");
    } else fs::create_directory(link);
    const auto print = normalized(target).wstring();
    const std::wstring substitute = print.rfind(L"\\\\", 0) == 0 ? L"\\??\\UNC\\" + print.substr(2) : L"\\??\\" + print;
    struct Junction { DWORD tag; WORD length, reserved, sub_offset, sub_length, print_offset, print_length; wchar_t paths[1]; };
    const size_t bytes = offsetof(Junction, paths) + (substitute.size() + print.size() + 2) * sizeof(wchar_t);
    if (bytes > MAXIMUM_REPARSE_DATA_BUFFER_SIZE) fail(L"The setup folder path is too long.");
    std::vector<unsigned char> storage(bytes); auto* data = reinterpret_cast<Junction*>(storage.data());
    data->tag = IO_REPARSE_TAG_MOUNT_POINT; data->length = static_cast<WORD>(bytes - 8);
    data->sub_length = static_cast<WORD>(substitute.size() * sizeof(wchar_t));
    data->print_offset = static_cast<WORD>((substitute.size() + 1) * sizeof(wchar_t)); data->print_length = static_cast<WORD>(print.size() * sizeof(wchar_t));
    memcpy(data->paths, substitute.c_str(), (substitute.size() + 1) * sizeof(wchar_t));
    memcpy(reinterpret_cast<unsigned char*>(data->paths) + data->print_offset, print.c_str(), (print.size() + 1) * sizeof(wchar_t));
    Handle handle(CreateFileW(link.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr));
    DWORD returned = 0;
    if (handle.value == INVALID_HANDLE_VALUE || !DeviceIoControl(handle.value, FSCTL_SET_REPARSE_POINT, data, static_cast<DWORD>(bytes), nullptr, 0, &returned, nullptr)) fail(L"Could not connect this profile to its own copied game data.\n\n" + error_text());
}
std::wstring lock_name(const fs::path& root, const wchar_t* suffix) {
    std::uint64_t hash = 1469598103934665603ull;
    for (const auto c : lower(normalized(root).wstring())) hash = (hash ^ static_cast<unsigned>(c)) * 1099511628211ull;
    return L"Local\\PragmicatMix-" + std::to_wstring(hash) + L"-" + suffix;
}
struct SetupLock {
    Handle handle;
    explicit SetupLock(const fs::path& root) : handle(CreateMutexW(nullptr, FALSE, lock_name(root, L"setup").c_str())) {
        const DWORD acquired = handle.value ? WaitForSingleObject(handle.value, 0) : WAIT_FAILED;
        if (acquired != WAIT_OBJECT_0 && acquired != WAIT_ABANDONED) fail(L"Another Kingdom Hearts 358/2 Days: Pragmicat Mix window is setting up the game. Let it finish first.");
    }
    ~SetupLock() { if (handle.value) ReleaseMutex(handle.value); }
};
bool game_data_inventory(const fs::path& path, std::map<std::wstring, std::string>& inventory, const Progress& progress) {
    if (is_link(path) || !directory(path)) return false;
    // A marker repair must have bounded work even if the selected folders are wrong.
    constexpr size_t maximum_entries = 100000;
    constexpr size_t maximum_path_characters = 4 * 1024 * 1024;
    constexpr std::uintmax_t maximum_bytes = 2ull * 1024 * 1024 * 1024;
    std::uintmax_t bytes = 0;
    size_t path_characters = 0;
    for (const auto& entry : fs::recursive_directory_iterator(path)) {
        if (inventory.size() >= maximum_entries || is_link(entry.path())) return false;
        auto relative = lower(entry.path().lexically_relative(path).wstring());
        if (relative.size() > maximum_path_characters - path_characters) return false;
        path_characters += relative.size();
        std::string signature;
        if (entry.is_directory()) signature = "directory";
        else if (entry.is_regular_file()) {
            const auto size = entry.file_size();
            if (size > maximum_bytes - bytes) return false;
            bytes += size;
            signature = sha256(entry.path());
            if (signature.empty()) return false;
        } else return false;
        if (!inventory.emplace(std::move(relative), std::move(signature)).second) return false;
        if (inventory.size() % 200 == 0) progress(L"Checking your existing game-data copy... " + std::to_wstring(inventory.size()) + L" entries");
    }
    return true;
}
bool matching_game_data(const fs::path& source, const fs::path& completed, const Progress& progress) {
    std::map<std::wstring, std::string> original, copied;
    return game_data_inventory(source, original, progress) &&
        game_data_inventory(completed, copied, progress) && original == copied;
}
void copy_game_data(const fs::path& original, const fs::path& root, const Progress& progress) {
    const auto source = extracted_game(original / L"data/extracted");
    if (source.empty()) fail(L"The selected game has no complete extracted data. Finish its normal setup first.");
    make_directory(root, L"game-data/extracted");
    const auto existing = extracted_game(root / L"game-data/extracted");
    if (!existing.empty() && lower(existing.filename().wstring()) != lower(source.filename().wstring()))
        fail(L"The existing game-data copy belongs to another extraction. It has been preserved. Choose its matching original installation or a fresh Kingdom Hearts 358/2 Days: Pragmicat Mix profile folder.");
    const auto completed = root / L"game-data/extracted" / source.filename();
    if (fs::exists(completed)) {
        progress(L"Checking your existing game-data copy before restoring its setup marker...");
        if (!matching_game_data(source, completed, progress))
            fail(L"The existing game-data copy differs from the selected game. It has been preserved. Choose the matching original installation or a fresh Kingdom Hearts 358/2 Days: Pragmicat Mix profile folder.");
        write_owned(root, L"game-data/.copy-complete", std::string(runtime_hash) + "\n");
        return;
    }
    const auto relative = fs::path(L"game-data/extracted") / (source.filename().wstring() + L".partial");
    make_directory(root, relative); unsigned files = 0;
    progress(L"Copying your game data once. Your original installation stays unchanged...");
    for (const auto& entry : fs::recursive_directory_iterator(source)) {
        if (is_link(entry.path())) fail(L"The extracted data contains a linked subfolder. Choose the original game folder instead.");
        const auto destination = relative / fs::relative(entry.path(), source);
        if (entry.is_directory()) make_directory(root, destination);
        else if (entry.is_regular_file()) { copy_owned(entry.path(), root, destination); if (++files % 200 == 0) progress(L"Copying your game data once... " + std::to_wstring(files) + L" files"); }
    }
    fs::rename(root / relative, completed);
    write_owned(root, L"game-data/.copy-complete", std::string(runtime_hash) + "\n");
}
void prepare_game_files(const fs::path& selected, const fs::path& root, const Progress& progress) {
    // Verify marker recovery before replacing any already copied engine files.
    if (!file(root / L"game-data/.copy-complete") || extracted_game(root / L"game-data/extracted").empty()) copy_game_data(selected.parent_path(), root, progress);
    progress(L"Preparing your separate Kingdom Hearts 358/2 Days: Pragmicat Mix game engine..."); copy_owned(selected, root, L"runtime/khdays-native.exe");
    for (const auto* name : {L"msvcp140.dll", L"msvcp140_2.dll", L"vcruntime140.dll", L"vcruntime140_1.dll"}) if (file(selected.parent_path() / name)) copy_owned(selected.parent_path() / name, root, fs::path(L"runtime") / name);
}
Seeded seed_profile(const fs::path& original, const fs::path& root, bool practice) {
    const fs::path relative = practice ? L"practice" : L"campaign";
    make_directory(root, relative / L"saves"); if (file(root / relative / L".initialised")) return {};
    Seeded seeded;
    const auto settings = practice && file(root / L"campaign/saves/settings.ini") ? root / L"campaign/saves/settings.ini" : original / L"saves/settings.ini";
    std::string text = "preset = kh2\n";
    if (file(settings) && fs::file_size(settings) <= 1024 * 1024) { text = read_text(settings); seeded.settings = true; }
    text += "\n[mods]\nnative_code = on\n";
    if (practice) text += "\n[save]\nautosave = off\n";
    write_owned(root, relative / L"saves/settings.ini", text);
    const auto save = original / L"saves/khdays.sav";
    if (!practice && file(save) && fs::file_size(save) <= 2 * 1024 * 1024 && !file(root / relative / L"saves/khdays.sav")) { copy_owned(save, root, relative / L"saves/khdays.sav", true); seeded.save = true; }
    if (!practice && directory(original / L"saves/slots")) for (const auto& entry : fs::directory_iterator(original / L"saves/slots"))
        if (!is_link(entry.path()) && entry.is_regular_file() && entry.path().extension() == L".sav" && entry.file_size() <= 2 * 1024 * 1024) copy_owned(entry.path(), root, relative / L"saves/slots" / entry.path().filename(), true);
    write_owned(root, relative / L".initialised", "Campaign and practice use separate saves.\n"); return seeded;
}
size_t deploy_payload(const fs::path& root, bool practice) {
    const fs::path profile = practice ? L"practice" : L"campaign"; size_t files = 0;
    for (const auto& entry : kPayloadEntries) {
        if (entry.practice_only && !practice) continue;
        const auto resource = FindResourceW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(entry.id), RT_RCDATA);
        const auto loaded = resource ? LoadResource(GetModuleHandleW(nullptr), resource) : nullptr;
        const DWORD size = resource ? SizeofResource(GetModuleHandleW(nullptr), resource) : 0;
        const void* bytes = loaded ? LockResource(loaded) : nullptr;
        if (!bytes || !size) fail(L"This launcher download is incomplete. Download the current Kingdom Hearts 358/2 Days: Pragmicat Mix launcher again.");
        write_owned(root, profile / entry.relative, bytes, size); ++files;
    }
    const auto order = profile / L"saves/mods/order.ini";
    if (!file(root / order)) write_owned(root, order, practice ? "Pragmicat Mix = on\nPragmicat Practice = on\n" : "Pragmicat Mix = on\n");
    return files;
}
bool starts(const std::wstring& text, const wchar_t* prefix) { return lower(text).rfind(lower(prefix), 0) == 0; }
std::vector<wchar_t> child_environment(const Prepared& prepared, bool practice) {
    std::vector<std::wstring> lines; wchar_t* environment = GetEnvironmentStringsW();
    if (!environment) fail(L"Could not read the Windows environment.");
    for (const wchar_t* entry = environment; *entry; entry += wcslen(entry) + 1) { const std::wstring line(entry); if (!starts(line, L"KHDAYS_") && !starts(line, L"PRAGMICAT_")) lines.push_back(line); }
    FreeEnvironmentStringsW(environment);
    lines.push_back(L"KHDAYS_SETTINGS=" + (prepared.profile / L"saves/settings.ini").wstring()); lines.push_back(L"KHDAYS_PLAYER_SLOT=1"); lines.push_back(L"KHDAYS_UPDATES=off");
    if (practice) lines.push_back(L"PRAGMICAT_PRACTICE=1");
    std::sort(lines.begin(), lines.end(), [](const auto& a, const auto& b) { return lower(a) < lower(b); });
    std::vector<wchar_t> block; for (const auto& line : lines) { block.insert(block.end(), line.begin(), line.end()); block.push_back(0); } block.push_back(0); return block;
}
} // namespace

std::string utf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (!length) throw std::runtime_error("A folder name cannot be read as Unicode.");
    std::string out(static_cast<size_t>(length), '\0'); WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), out.data(), length, nullptr, nullptr); return out;
}
std::wstring wide(const std::string& text) {
    if (text.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(length), L'\0'); MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), length); return out;
}
[[noreturn]] void fail(const std::wstring& message) { throw std::runtime_error(utf8(message)); }
fs::path default_root() { return known_folder(FOLDERID_LocalAppData) / L"Kingdom Hearts 358-2 Days - Pragmicat Mix"; }
bool installed(const fs::path& root) { return owned_root(root) && sha256(root / L"runtime/khdays-native.exe") == runtime_hash && file(root / L"game-data/.copy-complete") && !extracted_game(root / L"game-data/extracted").empty(); }
bool supported_game(const fs::path& selected) {
    const auto exe = game_executable(selected); return file(exe) && !extracted_game(exe.parent_path() / L"data/extracted").empty() && sha256(exe) == runtime_hash;
}
fs::path discover_game(const fs::path& root, const fs::path& own_folder) {
    for (const auto& candidate : {remembered_game(root), own_folder}) if (!candidate.empty() && supported_game(candidate)) return game_executable(candidate);
    std::vector<fs::path> containers;
    const auto documents = known_folder(FOLDERID_Documents), profile = known_folder(FOLDERID_Profile);
    containers.insert(containers.end(), {documents / L"Game", documents / L"Games", profile / L"Games", profile / L"OneDrive/Documents/Game"});
    for (wchar_t drive = L'C'; drive <= L'Z'; ++drive) { const std::wstring volume{drive, L':', L'\\'}; if (GetDriveTypeW(volume.c_str()) == DRIVE_FIXED) for (const auto* sub : {L"Game/Quiver/Apps", L"Game/Recompilations", L"Games"}) containers.emplace_back(fs::path(volume) / sub); }
    unsigned examined = 0;
    for (const auto& container : containers) { std::error_code error; if (!directory(container)) continue; for (const auto& entry : fs::directory_iterator(container, error)) {
        if (++examined > 512) return {}; if (!entry.is_directory(error)) continue;
        const auto name = lower(entry.path().filename().wstring());
        if (name.find(L"days") == std::wstring::npos && name.find(L"358") == std::wstring::npos && name.find(L"kingdom hearts") == std::wstring::npos) continue;
        if (supported_game(entry.path())) return entry.path() / L"khdays-native.exe";
    }} return {};
}
Prepared prepare(const fs::path& root, fs::path selected, bool practice, const Progress& progress) {
    SetupLock lock(root); const bool ready = installed(root);
    Handle running(OpenMutexW(SYNCHRONIZE, FALSE, lock_name(root, practice ? L"practice" : L"campaign").c_str()));
    if (running.value) fail(L"This Kingdom Hearts 358/2 Days: Pragmicat Mix session is already running. Return to its game window first.");
    if (!ready) {
        if (selected.empty() || !supported_game(selected)) fail(L"Choose khdays-native.exe from your native Days PC port 0.1.5 installation. Kingdom Hearts 358/2 Days: Pragmicat Mix will make its own copy.");
        selected = normalized(game_executable(selected));
        if (within(selected.parent_path(), root) || within(root, selected.parent_path())) fail(L"Keep the Kingdom Hearts 358/2 Days: Pragmicat Mix profile folder separate from the original game installation.");
    }
    take_root(root);
    if (!ready) prepare_game_files(selected, root, progress);
    if (selected.empty()) selected = remembered_game(root);
    if (!selected.empty() && !WritePrivateProfileStringW(L"launcher", L"game", game_executable(selected).c_str(), (root / L"launcher.ini").c_str())) fail(L"Could not remember the selected game folder.\n\n" + error_text());
    const fs::path profile = root / (practice ? L"practice" : L"campaign");
    const auto campaign_seeded = seed_profile(game_executable(selected).parent_path(), root, false);
    const auto practice_seeded = seed_profile(game_executable(selected).parent_path(), root, true);
    data_link(root / L"campaign", root / L"game-data"); data_link(root / L"practice", root / L"game-data");
    progress(L"Installing the bundled combat mod..."); const auto count = deploy_payload(root, practice);
    return {root, profile, root / L"runtime/khdays-native.exe", selected, practice ? practice_seeded : campaign_seeded, count, campaign_seeded, practice_seeded};
}
void launch_game(const Prepared& prepared, bool practice) {
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    Handle lock(CreateMutexW(&security, FALSE, lock_name(prepared.root, practice ? L"practice" : L"campaign").c_str()));
    if (!lock.value) fail(L"Could not reserve this game profile.");
    if (GetLastError() == ERROR_ALREADY_EXISTS) fail(L"This Kingdom Hearts 358/2 Days: Pragmicat Mix session is already running. Return to its game window first.");
    SIZE_T bytes = 0; InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes); std::vector<unsigned char> storage(bytes);
    auto* attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if (!InitializeProcThreadAttributeList(attributes, 1, 0, &bytes)) fail(L"Could not prepare this game session.");
    const BOOL inherited = UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, &lock.value, sizeof(lock.value), nullptr, nullptr);
    STARTUPINFOEXW startup{}; startup.StartupInfo.cb = sizeof(startup); startup.lpAttributeList = attributes;
    PROCESS_INFORMATION process{}; auto environment = child_environment(prepared, practice); std::wstring command = L"\"" + prepared.executable.wstring() + L"\"";
    const BOOL created = inherited && CreateProcessW(prepared.executable.c_str(), command.data(), nullptr, nullptr, TRUE, EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT, environment.data(), prepared.profile.c_str(), &startup.StartupInfo, &process);
    const DWORD error = GetLastError(); DeleteProcThreadAttributeList(attributes);
    if (!created) fail(L"Could not start Kingdom Hearts 358/2 Days: Pragmicat Mix.\n\n" + error_text(error));
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
}
std::string quoted(const std::wstring& value) {
    std::string output = "\"";
    for (const unsigned char c : utf8(value)) { if (c == '\\' || c == '"') output += '\\'; if (c == '\n') output += "\\n"; else if (c == '\r') output += "\\r"; else if (c >= 32) output += static_cast<char>(c); }
    return output + '"';
}
void report(const fs::path& filename, const std::string& text) {
    if (!filename.empty()) { std::ofstream out(filename, std::ios::binary); out << text << '\n'; if (!out) fail(L"Could not write the setup report."); }
    else { DWORD written = 0; const auto line = text + "\n"; WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), line.data(), static_cast<DWORD>(line.size()), &written, nullptr); }
}
Options parse_options() {
    int count = 0; wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &count); if (!arguments) fail(L"Could not read launcher options."); Options result;
    try { for (int i = 1; i < count; ++i) {
        const std::wstring option = arguments[i];
        if (option == L"--check-setup") result.check = true; else if (option == L"--practice") result.practice = true; else if (option == L"--self-test") result.self_test = true;
        else if (option == L"--game" || option == L"--profile-root" || option == L"--report") { if (++i == count) fail(L"A launcher option needs a folder or filename."); if (option == L"--game") result.game = arguments[i]; else if (option == L"--profile-root") result.root = normalized(arguments[i]); else result.report = normalized(arguments[i]); }
        else fail(L"Unknown launcher option: " + option);
    }} catch (...) { LocalFree(arguments); throw; } LocalFree(arguments); return result;
}
void self_test(const fs::path& output) {
    std::array<wchar_t, 32768> temporary{}; if (!GetTempPathW(static_cast<DWORD>(temporary.size()), temporary.data())) fail(L"Could not create launcher test files.");
    const fs::path root = fs::path(temporary.data()) / (L"PragmicatMixLauncher-test-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64())); take_root(root);
    unsigned groups = 0; const auto check = [&](bool ok) { if (!ok) fail(L"A launcher self-check failed."); ++groups; };
    write_owned(root, L"hash.txt", "abc"); check(sha256(root / L"hash.txt") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    check(safe_relative(L"mods/Pragmicat Mix/mod.ini") && !safe_relative(L"../outside") && !safe_relative(L"C:/outside") && !safe_relative(L"mods/file:stream"));
    check(within(root / L"campaign/saves", root) && !within(root.wstring() + L"-other", root));
    write_owned(root, L"original/saves/khdays.sav", "original-save"); write_owned(root, L"original/saves/settings.ini", "preset = kh2\n");
    const auto seeded = seed_profile(root / L"original", root, false); write_owned(root, L"campaign/saves/khdays.sav", "campaign-progress"); seed_profile(root / L"original", root, false);
    check(seeded.save && seeded.settings && read_text(root / L"campaign/saves/khdays.sav") == "campaign-progress" && read_text(root / L"original/saves/khdays.sav") == "original-save" && read_text(root / L"campaign/saves/settings.ini").find("native_code = on") != std::string::npos);
    seed_profile(root / L"original", root, true); check(!file(root / L"practice/saves/khdays.sav") && read_text(root / L"practice/saves/settings.ini").find("autosave = off") != std::string::npos);
    check(deploy_payload(root, true) >= 7 && read_text(root / L"practice/mods/Pragmicat Mix/native/windows-x64/pragmicat_mix.dll").rfind("MZ", 0) == 0 && file(root / L"practice/mods/Pragmicat Practice/mod.ini"));
    const fs::path deployed = root / L"practice/mods/Pragmicat Mix/native/windows-x64/pragmicat_mix.dll";
    const auto deployed_hash = sha256(deployed);
    fs::last_write_time(deployed, fs::file_time_type::clock::now() - std::chrono::hours(24));
    const auto deployed_time = fs::last_write_time(deployed);
    deploy_payload(root, true);
    check(fs::last_write_time(deployed) == deployed_time && sha256(deployed) == deployed_hash);
    write_owned(root, L"practice/mods/Pragmicat Mix/native/windows-x64/pragmicat_mix.dll", "damaged");
    deploy_payload(root, true);
    check(sha256(deployed) == deployed_hash && !file(deployed.wstring() + L".pragmicat-new"));
    // Small, non-game fixtures exercise the same copy/recovery path as setup.
    const fs::path fixture = L"original/data/extracted/test-game";
    const fs::path copied = L"game-data/extracted/test-game";
    const auto ignore_progress = [](const std::wstring&) {};
    write_owned(root, fixture / L"system/header.bin", std::string(512, 'H'));
    write_owned(root, fixture / L"nitrofs/sample.bin", "original-data");
    copy_game_data(root / L"original", root, ignore_progress);
    check(file(root / L"game-data/.copy-complete") && read_text(root / copied / L"nitrofs/sample.bin") == "original-data");
    const auto sample = root / copied / L"nitrofs/sample.bin";
    fs::last_write_time(sample, fs::file_time_type::clock::now() - std::chrono::hours(24));
    const auto sample_time = fs::last_write_time(sample);
    const auto campaign_settings = read_text(root / L"campaign/saves/settings.ini");
    fs::remove(root / L"game-data/.copy-complete");
    copy_game_data(root / L"original", root, ignore_progress);
    check(read_text(root / L"game-data/.copy-complete") == std::string(runtime_hash) + "\n" &&
        fs::last_write_time(sample) == sample_time && !fs::exists(root / L"game-data/extracted/test-game.partial") &&
        read_text(root / L"campaign/saves/khdays.sav") == "campaign-progress" &&
        read_text(root / L"campaign/saves/settings.ini") == campaign_settings &&
        read_text(root / fixture / L"nitrofs/sample.bin") == "original-data");
    fs::remove(root / L"game-data/.copy-complete");
    const auto refuses_recovery = [&] {
        bool refused = false;
        try { copy_game_data(root / L"original", root, ignore_progress); } catch (const std::exception&) { refused = true; }
        check(refused && !file(root / L"game-data/.copy-complete") && !fs::exists(root / L"game-data/extracted/test-game.partial"));
    };
    write_owned(root, copied / L"nitrofs/sample.bin", "modified-data");
    refuses_recovery(); check(read_text(sample) == "modified-data");
    write_owned(root, L"original/khdays-native.exe", "replacement-runtime");
    write_owned(root, L"runtime/khdays-native.exe", "existing-runtime");
    const auto runtime_time = fs::last_write_time(root / L"runtime/khdays-native.exe");
    bool engine_repair_refused = false;
    try { prepare_game_files(root / L"original/khdays-native.exe", root, ignore_progress); } catch (const std::exception&) { engine_repair_refused = true; }
    check(engine_repair_refused && read_text(root / L"runtime/khdays-native.exe") == "existing-runtime" &&
        fs::last_write_time(root / L"runtime/khdays-native.exe") == runtime_time &&
        read_text(root / L"campaign/saves/khdays.sav") == "campaign-progress");
    fs::remove(sample); refuses_recovery(); check(!file(sample));
    write_owned(root, copied / L"nitrofs/sample.bin", "original-data");
    write_owned(root, copied / L"nitrofs/extra.bin", "unexpected");
    refuses_recovery(); check(read_text(root / copied / L"nitrofs/extra.bin") == "unexpected");
    fs::remove(root / copied / L"nitrofs/extra.bin");
    fs::remove(root / copied / L"system/header.bin"); refuses_recovery();
    write_owned(root, copied / L"system/header.bin", std::string(512, 'H'));
    data_link(root / copied / L"nitrofs", root / L"original"); refuses_recovery();
    check(read_text(root / fixture / L"nitrofs/sample.bin") == "original-data");
    fs::remove(root / copied / L"nitrofs/data");
    write_owned(root, L"other-original/data/extracted/another-game/system/header.bin", std::string(512, 'H'));
    write_owned(root, L"other-original/data/extracted/another-game/nitrofs/sample.bin", "original-data");
    bool wrong_extraction = false;
    try { copy_game_data(root / L"other-original", root, ignore_progress); } catch (const std::exception&) { wrong_extraction = true; }
    check(wrong_extraction && !file(root / L"game-data/.copy-complete") &&
        !fs::exists(root / L"game-data/extracted/another-game.partial") && read_text(sample) == "original-data");
    copy_game_data(root / L"original", root, ignore_progress);
    check(file(root / L"game-data/.copy-complete") && read_text(root / L"campaign/saves/khdays.sav") == "campaign-progress");
    make_directory(root, L"game-data/extracted"); data_link(root / L"campaign", root / L"game-data"); data_link(root / L"campaign", root / L"game-data"); check(final_directory(root / L"campaign/data") == final_directory(root / L"game-data"));
    bool rejected = false; try { take_root(root / L"original"); } catch (const std::exception&) { rejected = true; } check(rejected && !supported_game(root / L"hash.txt"));
    Prepared prepared{root, root / L"practice", {}, {}, {}, 0}; const auto environment = child_environment(prepared, true); std::wstring joined; for (const auto c : environment) joined += c ? c : L'\n';
    check(joined.find(L"KHDAYS_INPUT=") == std::wstring::npos && joined.find(L"KHDAYS_HEADLESS=") == std::wstring::npos && joined.find(L"PRAGMICAT_PRACTICE=1") != std::wstring::npos && joined.find(L"KHDAYS_PLAYER_SLOT=1") != std::wstring::npos);
    report(output, "{\"ok\":true,\"test_groups\":" + std::to_string(groups) + ",\"game_launched\":false,\"test_root\":" + quoted(root.wstring()) + "}");
}
} // namespace zw
