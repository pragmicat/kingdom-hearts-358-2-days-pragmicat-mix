#include "khdays/vfs/filesystem.h"
#include "runtime/core/rom.h"
#include "runtime/rooms/added.h"

extern "C" {
#include "host/app/host.h"
#include "host/app/instance.h"
#include "runtime/core/origin.h"
#include "runtime/core/session_log.h"
#include "runtime/menus/saveworld.h"
#include "runtime/mods/list.h"
}

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <cstring>

// Native game services outside the cartridge and mod-discovery scope.
extern "C" const char *khdays_host_user_folder() { return "saves"; }
extern "C" void khdays_log(const char *, ...) {}
extern "C" void khdays_origin_want() {}
extern "C" void khdays_saveworld_card_read(u32) {}
extern "C" void khdays_rooms_load() {}
bool khdays_rooms_serves(const std::string &) { return false; }
bool khdays_rooms_merge(const std::string &, const std::vector<unsigned char> &,
                        std::vector<unsigned char> &, const KhdaysGameFiles &)
{
    return false;
}

extern "C" void khdays_host_tell_no_data(const char *where, const char *problem)
{
    std::fprintf(stderr, "rom fixture failed in %s: %s\n", where, problem ? problem : "missing data");
    std::abort();
}

static u32 le32(const u8 *bytes)
{
    return static_cast<u32>(bytes[0]) | static_cast<u32>(bytes[1]) << 8 |
           static_cast<u32>(bytes[2]) << 16 | static_cast<u32>(bytes[3]) << 24;
}

int main()
{
    assert(khdays_rom_init());
    const auto &root = khdays::vfs::data_root();
    assert(root.parent_path() == "data/extracted");
    assert(root.filename().string().size() == 16 && root.extension().empty());
    assert(khdays_mods_started_count() == 1);
    assert(std::strcmp(khdays_mods_started_folder(0), "Enabled") == 0);
    assert(std::memcmp(khdays_rom_header(), "SYNTHETIC   ", 12) == 0);
    assert(khdays_rom_file_count() == 3);

    auto *reader = khdays_rom_reader_new();
    std::array<u8, 32> bytes{};
    khdays_rom_reader_read(reader, 0x4000, bytes.data(), 4);
    assert(bytes[0] == 0xaa && bytes[3] == 0xdd);
    khdays_rom_reader_read(reader, 0x4100, bytes.data(), 24);
    // The new mod file is longer, so native init rewrites its FAT entry and
    // exposes the relocated bytes through both the read and find APIs.
    assert(le32(bytes.data() + 8) == 0x4800);
    assert(le32(bytes.data() + 12) == 0x4812);
    u32 offset = 0, length = 0;
    assert(khdays_rom_find("folder/note.bin", &offset, &length));
    assert(offset == 0x4800 && length == 18);
    khdays_rom_reader_read(reader, offset, bytes.data(), length);
    assert(std::memcmp(bytes.data(), "longer mod payload", length) == 0);

    char name[64];
    assert(khdays_rom_file_info(2, name, sizeof(name), &offset, &length));
    assert(std::strcmp(name, "_unnamed/file_00002.bin") == 0);
    assert(offset == 0x4600 && length == 3);
    khdays_rom_reader_read(reader, offset, bytes.data(), length);
    assert(bytes[0] == 'R' && bytes[1] == 'A' && bytes[2] == 'W');
    khdays_rom_reader_read(reader, 0x4500, bytes.data(), bytes.size());
    assert(std::all_of(bytes.begin(), bytes.end(), [](u8 value) { return value == 0xff; }));

    u8 *decoded = nullptr;
    assert(khdays_rom_file_data(reader, "compressed.z", -1, &decoded, &length));
    assert(length == 3 && std::memcmp(decoded, "ABC", length) == 0);
    std::free(decoded);
    std::puts("Native Days cartridge init, raw/LZ11 reads, mod relocation and FNT/FAT passed");
}
