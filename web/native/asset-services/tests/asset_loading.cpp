/* Exercise the real texture registry, PNG codec and ROM VFS against a
 * synthetic image in the browser-local filesystem. */
#include "khdays/vfs/filesystem.h"
extern "C" {
#include "runtime/mods/textures.h"
}
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

int main()
{
    constexpr uint32_t color = 0xff7f4321u;
    std::array<uint32_t, 64> rgba;
    rgba.fill(color);
    khdays::vfs::set_mods_root("/fixture/mods");
    assert(khdays_textures_write_png("/fixture/mods/Synthetic/graphics/nested/layer.png",
                                     reinterpret_cast<const uint8_t *>(rgba.data()), 8, 8));

    const int loaded = khdays_textures_mod_replacement("nested/layer", 0);
    assert(loaded == 0);
    const auto *texture = khdays_textures_replacement(loaded);
    assert(texture != nullptr && texture->width == 8 && texture->height == 8);
    assert(texture->rgba[0] == color && texture->rgba[63] == color);
    assert(khdays_textures_mod_replacement("nested/layer", 0) == loaded);
    assert(khdays_textures_mod_replacement("missing", 0) == -1);
    assert(khdays_textures_version(loaded) == 0);

    // Fill the first block and step into the second; existing addresses
    // must remain valid when its renderer starts reading another block.
    for (int i = 1; i < 256; ++i) {
        const std::string name = "texture" + std::to_string(i);
        assert(khdays_textures_add_info(name.c_str(), 8, 8, rgba.data(), 0) == i);
        assert(std::strcmp(khdays_textures_name(i), name.c_str()) == 0);
    }
    const int changed = khdays_textures_add_changing("changing", 8, 8, 0);
    assert(changed == 256 && khdays_textures_replacements() == 257);
    assert(khdays_textures_version(changed) == 1);
    assert(khdays_textures_replacement(loaded) == texture);
    auto *write = khdays_textures_change_begin(changed);
    write[0] = color;
    khdays_textures_change_end(changed);
    assert(khdays_textures_version(changed) == 2);
    khdays_textures_read_begin(changed);
    assert(khdays_textures_replacement(changed)->rgba[0] == color);
    khdays_textures_read_end(changed);

    const uint8_t index = 0;
    const uint8_t palette[] = {0x21, 0x43, 0x7f, 0xff};
    assert(khdays_textures_write_indexed_png("/fixture/dumps/indexed/test.png", &index, 1, 1, palette, 1));
    assert(khdays_textures_replacement(-1) == nullptr && khdays_textures_replacement(257) == nullptr);
    assert(khdays_textures_version(257) == 0);

    std::puts("Native texture registry, PNG I/O and VFS passed in WebAssembly");
}
