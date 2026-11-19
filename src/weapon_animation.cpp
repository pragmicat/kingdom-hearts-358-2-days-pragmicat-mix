#include "weapon_animation.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace rxweapon {
namespace {
constexpr unsigned kModel = 0xf0c, kState = 0x2c54, kStateSize = 0x54;
constexpr unsigned kTableSize = 0x24, kTagCount = 16, kNativeTagLimit = 13;
constexpr const char* kLoad = "Ov002_LoadAnimTables";
constexpr const char* kApply = "Ov002_ApplyAnimMode";
constexpr const char* kFree = "Ov002_FreeResourceTables";
using Load = void (*)(KhRef, KhRef, int, int, int);
using Apply = void (*)(KhRef, KhRef, int, int, int);
using Free = void (*)(KhRef);
using Alloc = KhRef (*)(unsigned);
using ReadFile = KhRef (*)(KhRef, unsigned);
using BindFile = int (*)(KhRef, KhRef, KhRef, int);
using HeapGet = KhRef (*)();
using HeapSet = KhRef (*)(KhRef);
using Remove = void (*)(KhRef, KhRef);
const KhEngine* api;
const KhActors* actors;
const KhNet* net;
Permitted permitted;
Log logger;
void* log_context;
Load original_load;
Apply original_apply;
Free original_free, free_table, free_memory;
Alloc allocate;
ReadFile read_file;
BindFile bind_file;
HeapGet heap_get;
HeapSet heap_set;
Remove remove_animation;
bool installed_load, installed_apply, installed_free, retiring;
KhRef owner, owner_heap;
std::array<unsigned char, kStateSize> stock{};
struct Supplement { KhRef table = 0, file = 0; };
std::array<Supplement, kTagCount> supplements{};
unsigned stock_count, supplement_count, warned;
bool captured, complete;

unsigned char* bytes(KhRef ref) {
    return api && ref ? static_cast<unsigned char*>(api->pointer(ref)) : nullptr;
}
template<class T> T read(KhRef ref, unsigned offset = 0) {
    T result{};
    if (auto* p = bytes(ref)) std::memcpy(&result, p + offset, sizeof result);
    return result;
}
template<class T> void write(KhRef ref, unsigned offset, T value) {
    if (auto* p = bytes(ref)) std::memcpy(p + offset, &value, sizeof value);
}
template<class T> T saved(unsigned offset) {
    T result{}; std::memcpy(&result, stock.data() + offset, sizeof result); return result;
}
void say(const char* message) { if (logger) logger(message, log_context); }
bool normal(KhRef actor) {
    return actor && api && api->module_loaded(22) && api->module_loaded(30) && bytes(actor) &&
        read<int>(actor, 0xc) == 0 && read<unsigned char>(actor, 8) == 0 &&
        read<unsigned char>(actor, 9) == 0 && net && !net->in_room() &&
        actors && actors->local_seat() == 0 && permitted && permitted(actor);
}
bool live_owner() {
    if (!normal(owner)) return false;
    const KhActor current = actors->player(0);
    return current && actors->ref(current) == owner;
}
bool exact(KhRef model, KhRef state) {
    return owner && model == owner + kModel && state == owner + kState;
}
bool table_valid(KhRef table) {
    if (!table || !bytes(table)) return false;
    for (unsigned track = 0; track < 5; ++track) {
        const auto count = read<std::uint16_t>(table, track * 2);
        if (count > 64) return false;
        const KhRef list = read<KhRef>(table, 0x10 + track * 4);
        if (count && (!list || !bytes(list))) return false;
        for (unsigned index = 0; index < count; ++index) {
            const KhRef object = read<KhRef>(list, index * 4);
            if (!object || !bytes(object)) return false;
        }
    }
    return true;
}
int stock_index(unsigned tag) {
    const int count = saved<int>(0x50);
    if (count < 0 || count > static_cast<int>(kNativeTagLimit)) return -1;
    for (int index = 0; index < count; ++index) {
        const int id = saved<signed char>(8 + static_cast<unsigned>(index));
        if (id == static_cast<int>(tag)) return index;
        if (id < 0) break;
    }
    return -1;
}
bool stock_has(unsigned tag) {
    const int index = stock_index(tag);
    const KhRef tables = saved<KhRef>(0x4c);
    return index >= 0 && tables && table_valid(tables + static_cast<unsigned>(index) * kTableSize);
}
bool owns_animation(KhRef object) {
    if (!object) return false;
    for (const auto& entry : supplements) {
        if (!entry.table || !bytes(entry.table)) continue;
        for (unsigned track = 0; track < 5; ++track) {
            const auto count = read<std::uint16_t>(entry.table, track * 2);
            const KhRef list = read<KhRef>(entry.table, 0x10 + track * 4);
            if (count > 64 || (count && (!list || !bytes(list)))) continue;
            for (unsigned index = 0; index < count; ++index)
                if (read<KhRef>(list, index * 4) == object) return true;
        }
    }
    return false;
}
void detach_owned_tracks() {
    const KhRef animation = owner + kModel + 4;
    const KhRef render_object = animation + 0x20;
    const KhRef pending = read<KhRef>(animation, 0xd4);
    const bool pending_owned = owns_animation(pending);
    const KhRef bound_zero = read<KhRef>(animation, 0xc);
    const bool bound_zero_owned = owns_animation(bound_zero);
    // Native fading adds its pending object to the render list before promotion.
    // Remove owned pending objects too, or a later draw would use freed memory.
    if (pending_owned) remove_animation(render_object, pending);
    for (unsigned track = 0; track < 5; ++track) {
        const KhRef bound = read<KhRef>(animation, 0xc + track * 4);
        if (!owns_animation(bound)) continue;
        remove_animation(render_object, bound);
        write<KhRef>(animation, 0xc + track * 4, 0);
        write<std::int16_t>(animation, 2 + track * 2, -1);
    }
    if (pending_owned || bound_zero_owned) {
        // Keep a newer stock pending object if another animation already began
        // fading away from this pool. It is already attached, so promote it.
        if (pending && !pending_owned) {
            write<KhRef>(animation, 0xc, pending);
            write<std::int16_t>(animation, 2, read<std::int16_t>(animation, 0xca));
            write<int>(pending, 4, 4096);
        }
        write<KhRef>(animation, 0xd4, 0);
        write<std::int16_t>(animation, 0xca, -1);
        write<std::uint16_t>(animation, 0, static_cast<std::uint16_t>(read<std::uint16_t>(animation) & ~4u));
        write<int>(animation, 0xd8, 4096);
    }
}
void release_pool() {
    for (auto& entry : supplements) {
        if (entry.table) {
            free_table(entry.table);
            free_memory(entry.table);
        }
        if (entry.file) free_memory(entry.file);
        entry = {};
    }
    supplement_count = 0;
}
void clear_owner() {
    owner = owner_heap = 0; stock = {}; supplements = {};
    stock_count = supplement_count = warned = 0; captured = complete = false;
}
void restore_and_release(bool trusted_teardown) {
    if (!captured) { clear_owner(); return; }
    if (!trusted_teardown && !live_owner()) {
        // The scene heap may already have been destroyed. Do not dereference or
        // free a recycled actor/heap; normal teardown is handled synchronously.
        clear_owner(); return;
    }
    retiring = true;
    const KhRef previous = heap_set(owner_heap);
    detach_owned_tracks();
    std::memcpy(bytes(owner + kState), stock.data(), stock.size());
    // Rebind the original weapon's inline idle tracks while all stock objects
    // are still alive. This also clears any interrupted fade in the native path.
    write<signed char>(owner + kState, 0, -1);
    original_apply(owner + kModel, owner + kState, 0, 2, 0);
    release_pool();
    heap_set(previous);
    retiring = false;
    clear_owner();
}
bool prepare_pool(KhRef model, int archive, int slot, int base) {
    stock_count = 0;
    for (unsigned tag = 0; tag < kTagCount; ++tag) if (stock_has(tag)) ++stock_count;
    if (!stock_count || slot != 0 || base != 0x78 || !archive || !bytes(static_cast<KhRef>(archive))) return false;
    for (unsigned tag = 0; tag < kTagCount; ++tag) {
        if (stock_has(tag)) continue;
        Supplement next;
        next.table = allocate(kTableSize);
        if (!next.table || !bytes(next.table)) { if (next.table) free_memory(next.table); release_pool(); return false; }
        std::memset(bytes(next.table), 0, kTableSize);
        const unsigned file_id = static_cast<unsigned>(base) + tag;
        const KhRef packed = (((static_cast<KhRef>(archive) + 0x8000u) & 0xfffffcu) << 7) |
            0x80000000u | (file_id & 0x1ffu);
        next.file = read_file(packed, static_cast<unsigned>(slot + 7));
        if (!next.file || !bytes(next.file)) {
            if (next.file) free_memory(next.file);
            free_memory(next.table); release_pool(); return false;
        }
        bind_file(next.table, model + 4, next.file, slot + 7);
        if (!table_valid(next.table)) {
            // A malformed native resource is never installed. The loader's
            // initialized table is still its cleanup owner's responsibility.
            free_table(next.table); free_memory(next.table); free_memory(next.file);
            release_pool(); return false;
        }
        supplements[tag] = next;
        ++supplement_count;
    }
    return true;
}
void load(KhRef model, KhRef state, int archive, int slot, int base) {
    const bool ours = !retiring && exact(model, state) && normal(owner);
    if (ours && captured) restore_and_release(true);
    // Keep the early BuildActionTable binding across a repeated stock setup.
    const KhRef actor = ours ? model - kModel : 0;
    original_load(model, state, archive, slot, base);
    if (!ours || !actor || !normal(actor)) return;
    owner = actor;
    owner_heap = heap_get();
    std::memcpy(stock.data(), bytes(state), stock.size());
    captured = true;
    complete = prepare_pool(model, archive, slot, base);
    char message[220];
    std::snprintf(message, sizeof message,
        "Kingdom Hearts 358/2 Days: weapon animation cache: %u stock tags, %u supplementary original tags; %s.",
        stock_count, supplement_count, complete ? "all 16 authored weapon animations available" : "supplement load refused; missing tags use safe stock weapon animation");
    say(message);
}
void install_state(unsigned tag) {
    const KhRef state = owner + kState;
    const signed char mode = read<signed char>(state);
    const int changed = read<int>(state, 4);
    if (stock_has(tag)) {
        std::memcpy(bytes(state), stock.data(), stock.size());
    } else {
        std::memset(bytes(state), 0, kStateSize);
        std::memset(bytes(state) + 8, 0xff, kNativeTagLimit);
        write<signed char>(state, 8, static_cast<signed char>(tag));
        write<KhRef>(state, 0x18, supplements[tag].file);
        write<KhRef>(state, 0x4c, supplements[tag].table);
        write<int>(state, 0x50, 1);
    }
    write<signed char>(state, 0, mode);
    write<int>(state, 4, changed);
}
void apply(KhRef model, KhRef state, int mode, int kind, int flags) {
    if (!retiring && captured && exact(model, state) && normal(owner) && mode >= 30 && mode < 46 && kind != 2) {
        const unsigned tag = static_cast<unsigned>(mode - 30);
        if (stock_has(tag) || (supplements[tag].table && table_valid(supplements[tag].table))) {
            install_state(tag);
        } else {
            // Only the weapon track degrades on allocation/resource failure.
            // The actor's authored body motion and contact graph stay intact.
            const unsigned bit = 1u << tag;
            if (!(warned & bit)) {
                warned |= bit;
                say("Kingdom Hearts 358/2 Days: refused missing weapon-animation tag; using a valid stock weapon track.");
            }
            for (unsigned fallback = 0; fallback < kTagCount; ++fallback) if (stock_has(fallback)) {
                install_state(fallback);
                original_apply(model, state, 30 + static_cast<int>(fallback), kind, 0);
                return;
            }
            return; // Do not call native's unchecked pTables[-1] path.
        }
    }
    original_apply(model, state, mode, kind, flags);
}
void free_state(KhRef state) {
    if (!retiring && captured && owner && state == owner + kState) restore_and_release(true);
    original_free(state);
}
template<class T> bool resolve(T& target, const char* name) {
    target = reinterpret_cast<T>(api->routine(name, nullptr)); return target != nullptr;
}
template<class T> bool hook(T& previous, const char* name, T replacement, bool& installed) {
    void* old = nullptr;
    if (api->replace(name, reinterpret_cast<void*>(replacement), &old) != KH_OK) return false;
    if (!old) return false;
    previous = reinterpret_cast<T>(old); installed = true; return true;
}
}
bool init(const KhEngine* engine, const KhActors* actor_api, const KhNet* network,
          Permitted scope, Log log, void* context) {
    if (installed_load || installed_apply || installed_free || !engine || !actor_api || !network || !scope ||
        engine->size < sizeof(KhEngine) || actor_api->size < sizeof(KhActors) || network->size < sizeof(KhNet) ||
        !engine->pointer || !engine->routine || !engine->replace || !engine->module_loaded ||
        !actor_api->player || !actor_api->ref || !actor_api->local_seat || !network->in_room) return false;
    api = engine; actors = actor_api; net = network; permitted = scope; logger = log; log_context = context;
    if (!resolve(allocate, "NNSi_FndAllocFromDefaultExpHeap") || !resolve(free_memory, "NNSi_FndFreeFromDefaultHeap") ||
        !resolve(read_file, "Archive_LoadFile") || !resolve(bind_file, "Resource_BindFileToSlot") ||
        !resolve(free_table, "FreeAllResourceTables") || !resolve(remove_animation, "NNS_G3dRenderObjRemoveAnmObj") ||
        !resolve(heap_get, "Heap_GetCurrent") || !resolve(heap_set, "Heap_SetCurrent") ||
        !resolve(original_load, kLoad) || !resolve(original_apply, kApply) || !resolve(original_free, kFree)) return false;
    if (!hook(original_load, kLoad, &load, installed_load) ||
        !hook(original_apply, kApply, &apply, installed_apply) ||
        !hook(original_free, kFree, &free_state, installed_free)) { stop(); return false; }
    clear_owner(); return true;
}
void bind(KhRef actor) {
    if (owner == actor) return;
    if (owner) forget(owner);
    if (normal(actor)) owner = actor;
}
void forget(KhRef actor) { if (!actor || actor == owner) restore_and_release(false); }
void stop() {
    forget();
    bool restored = true;
    if (installed_free && api->replace(kFree, reinterpret_cast<void*>(original_free), nullptr) == KH_OK) installed_free = false;
    else if (installed_free) restored = false;
    if (installed_apply && api->replace(kApply, reinterpret_cast<void*>(original_apply), nullptr) == KH_OK) installed_apply = false;
    else if (installed_apply) restored = false;
    if (installed_load && api->replace(kLoad, reinterpret_cast<void*>(original_load), nullptr) == KH_OK) installed_load = false;
    else if (installed_load) restored = false;
    if (restored) { api = nullptr; actors = nullptr; net = nullptr; permitted = nullptr; logger = nullptr; log_context = nullptr; }
}
View view() { return {owner, stock_count, supplement_count, captured, complete}; }
}
