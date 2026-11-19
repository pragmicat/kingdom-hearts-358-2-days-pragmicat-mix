// Reproduce the native missing-tag underflow, then run the actual adapter.
#include "../src/weapon_animation.cpp"
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace test {
void check(bool yes, const char* message) { if (!yes) throw std::runtime_error(message); }
#define CHECK(x) ::test::check((x), #x)
constexpr KhRef actor = 0x1000, other = 0x5000, archive = 0x9000;
std::array<unsigned char, 0x200000> memory{};
std::map<KhRef, unsigned> allocations;
std::map<KhRef, KhRef> heaps;
std::map<KhRef, unsigned> file_tags;
std::map<KhRef, std::vector<KhRef>> table_parts;
std::map<std::string, void*> functions;
std::set<KhRef> rendered;
std::vector<unsigned> file_reads;
std::vector<std::string> logs;
KhEngine engine{}; KhActors actor_api{}; KhNet network{};
KhRef cursor, current_heap;
bool allowed, loaded, live, room, fail_file, fail_alloc;
int local_seat, native_loads, native_applies, native_frees, table_frees;
std::string missing, fail_hook;
void* pointer(KhRef ref) {
    if ((ref >= actor && ref < actor + 0x4000) ||
        (ref >= other && ref < other + 0x4000) || ref == archive) return memory.data() + ref;
    auto i = allocations.upper_bound(ref);
    if (i == allocations.begin()) return nullptr;
    --i;
    return ref >= i->first && ref < i->first + i->second ? memory.data() + ref : nullptr;
}
template<class T> void put(KhRef ref, unsigned offset, T value) {
    CHECK(pointer(ref)); std::memcpy(static_cast<unsigned char*>(pointer(ref)) + offset, &value, sizeof value);
}
template<class T> T get(KhRef ref, unsigned offset = 0) {
    CHECK(pointer(ref)); T value{}; std::memcpy(&value, static_cast<unsigned char*>(pointer(ref)) + offset, sizeof value); return value;
}
KhRef alloc(unsigned size) {
    if (fail_alloc) { fail_alloc = false; return 0; }
    const KhRef ref = cursor; cursor = (cursor + size + 15) & ~15u;
    CHECK(cursor < memory.size()); allocations[ref] = size; heaps[ref] = current_heap;
    std::memset(memory.data() + ref, 0, size); return ref;
}
void free_mem(KhRef ref) {
    CHECK(allocations.count(ref) == 1); CHECK(heaps.at(ref) == current_heap);
    CHECK(rendered.count(ref) == 0);
    allocations.erase(ref); heaps.erase(ref); file_tags.erase(ref);
}
void build_table(KhRef table) {
    for (unsigned track = 0; track < 5; ++track) {
        const KhRef list = alloc(4), object = alloc(32);
        CHECK(list && object);
        put(list, 0, object); put(table, track * 2, static_cast<std::uint16_t>(1));
        put(table, 0x10 + track * 4, list);
        table_parts[table].push_back(object); table_parts[table].push_back(list);
    }
}
void free_table(KhRef table) {
    CHECK(table_parts.count(table) == 1);
    for (const KhRef part : table_parts.at(table)) free_mem(part);
    table_parts.erase(table); ++table_frees;
}
KhRef read_file(KhRef packed, unsigned heap) {
    CHECK((packed & 0x80000000u) != 0); CHECK(heap == 7);
    const unsigned id = packed & 0x1ffu; CHECK(id >= 0x78 && id < 0x88);
    file_reads.push_back(id - 0x78);
    if (fail_file) { fail_file = false; return 0; }
    const KhRef result = alloc(32); CHECK(result); file_tags[result] = id - 0x78; return result;
}
int bind_file(KhRef table, KhRef model, KhRef file, int heap) {
    CHECK(model == actor + 0xf10); CHECK(heap == 7); CHECK(file_tags.count(file) == 1);
    build_table(table); return heap;
}
void native_load(KhRef model, KhRef state, int container, int slot, int base) {
    ++native_loads; CHECK(model == actor + 0xf0c); CHECK(state == actor + 0x2c54);
    CHECK(container == static_cast<int>(archive) && slot == 0 && base == 0x78);
    std::memset(pointer(state), 0, 0x54);
    std::memset(static_cast<unsigned char*>(pointer(state)) + 8, 0xff, 13);
    const KhRef tables = alloc(6 * 0x24); CHECK(tables);
    put(state, 0x4c, tables); put(state, 0x50, 6);
    for (unsigned tag = 0; tag < 6; ++tag) {
        put(state, 8 + tag, static_cast<signed char>(tag));
        const KhRef file = alloc(32); file_tags[file] = tag; put(state, 0x18 + 4 * tag, file);
        build_table(tables + 0x24 * tag);
    }
}
int native_remap(KhRef state, int mode) {
    if (mode >= 30 && mode < 46) {
        for (unsigned index = 0; index < 13; ++index) {
            const int id = get<signed char>(state, 8 + index);
            if (id == mode - 30) return static_cast<int>(index);
            if (id == -1) break;
        }
        return -1;
    }
    return mode >= 46 ? mode - 19 : mode;
}
void native_apply(KhRef model, KhRef state, int mode, int kind, int) {
    ++native_applies;
    if (!get<unsigned char>(model, 1) || get<signed char>(state) == mode) return;
    const int index = native_remap(state, mode);
    const KhRef table = mode >= 30 && mode < 46 && kind != 2 ?
        get<KhRef>(state, 0x4c) + static_cast<KhRef>(index * 0x24) : model + 0x13c;
    // Same unchecked native index: missing B006 gives pTables -0x24. Require a
    // real initialized candidate table; this is the crash regression's oracle.
    CHECK(table_parts.count(table) == 1);
    for (unsigned track = 0; track < 4; ++track) {
        const KhRef list = get<KhRef>(table, 0x10 + track * 4);
        const KhRef incoming = get<KhRef>(list);
        const KhRef outgoing = get<KhRef>(model + 4, 0xc + track * 4);
        if (outgoing) rendered.erase(outgoing);
        rendered.insert(incoming); put(model + 4, 0xc + track * 4, incoming);
        put(model + 4, 2 + track * 2, static_cast<std::int16_t>(0));
    }
    put(state, 0, static_cast<signed char>(mode)); put(state, 4, 1);
}
void native_free(KhRef state) {
    ++native_frees;
    const int count = get<int>(state, 0x50); const KhRef tables = get<KhRef>(state, 0x4c);
    for (int i = 0; i < count; ++i) {
        free_table(tables + static_cast<unsigned>(i) * 0x24);
        free_mem(get<KhRef>(state, 0x18 + static_cast<unsigned>(i) * 4));
    }
    if (count) free_mem(tables);
    put(state, 0x50, 0);
}
void remove(KhRef render, KhRef object) {
    CHECK(render == actor + 0xf0c + 4 + 0x20); CHECK(rendered.erase(object) == 1);
}
KhRef heap_get() { return current_heap; }
KhRef heap_set(KhRef heap) { KhRef previous = current_heap; current_heap = heap; return previous; }
bool permitted(KhRef ref) { return allowed && ref == actor; }
int module(int id) { return loaded && (id == 22 || id == 30); }
KhActor player(int seat) { return live && seat == 0 ? 1 : 0; }
KhRef ref(KhActor value) { return value == 1 ? actor : 0; }
int seat() { return local_seat; }
int in_room() { return room; }
void log(const char* message, void*) { logs.emplace_back(message); }
void* routine(const char* name, int*) { return missing == name || !functions.count(name) ? nullptr : functions.at(name); }
KhResult replace(const char* name, void* function, void** previous) {
    if (fail_hook == name || !functions.count(name)) return KH_ERROR;
    if (previous) *previous = functions.at(name);
    functions[name] = function; return KH_OK;
}
template<class T> T fn(const char* name) { return reinterpret_cast<T>(functions.at(name)); }
void fixture() {
    fail_hook.clear(); rxweapon::stop();
    memory.fill(0); allocations.clear(); heaps.clear(); file_tags.clear(); table_parts.clear(); functions.clear();
    rendered.clear(); file_reads.clear(); logs.clear();
    cursor = 0x10000; current_heap = 0x1234;
    allowed = loaded = live = true; room = fail_file = fail_alloc = false; local_seat = 0;
    native_loads = native_applies = native_frees = table_frees = 0; missing.clear();
    engine = {}; engine.size = sizeof engine; engine.pointer = &pointer; engine.module_loaded = &module;
    engine.routine = &routine; engine.replace = &replace;
    actor_api = {}; actor_api.size = sizeof actor_api; actor_api.player = &player; actor_api.ref = &ref; actor_api.local_seat = &seat;
    network = {}; network.size = sizeof network; network.in_room = &in_room;
    functions["Ov002_LoadAnimTables"] = reinterpret_cast<void*>(&native_load);
    functions["Ov002_ApplyAnimMode"] = reinterpret_cast<void*>(&native_apply);
    functions["Ov002_FreeResourceTables"] = reinterpret_cast<void*>(&native_free);
    functions["NNSi_FndAllocFromDefaultExpHeap"] = reinterpret_cast<void*>(&alloc);
    functions["NNSi_FndFreeFromDefaultHeap"] = reinterpret_cast<void*>(&free_mem);
    functions["Archive_LoadFile"] = reinterpret_cast<void*>(&read_file);
    functions["Resource_BindFileToSlot"] = reinterpret_cast<void*>(&bind_file);
    functions["FreeAllResourceTables"] = reinterpret_cast<void*>(&free_table);
    functions["NNS_G3dRenderObjRemoveAnmObj"] = reinterpret_cast<void*>(&remove);
    functions["Heap_GetCurrent"] = reinterpret_cast<void*>(&heap_get);
    functions["Heap_SetCurrent"] = reinterpret_cast<void*>(&heap_set);
    put(actor, 0xc, 0); put(actor + 0xf0c, 1, static_cast<unsigned char>(1));
    build_table(actor + 0xf0c + 0x13c);
}
void start() { CHECK(rxweapon::init(&engine, &actor_api, &network, &permitted, &log)); rxweapon::bind(actor); }
void load() { fn<rxweapon::Load>("Ov002_LoadAnimTables")(actor + 0xf0c, actor + 0x2c54, archive, 0, 0x78); }
void apply(int mode, int kind = 0) { fn<rxweapon::Apply>("Ov002_ApplyAnimMode")(actor + 0xf0c, actor + 0x2c54, mode, kind, 0); }
void free() { fn<rxweapon::Free>("Ov002_FreeResourceTables")(actor + 0x2c54); }
}

int main() {
    using namespace test;
    unsigned passed = 0;
    const auto run = [&](const char* name, const std::function<void()>& test) {
        try { fixture(); test(); ++passed; std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) { std::cerr << "FAIL " << name << ": " << error.what() << '\n'; throw; }
    };
    try {
        run("native B000 to B006 reproduces missing-tag underflow without adapter", [] {
            native_load(actor + 0xf0c, actor + 0x2c54, archive, 0, 0x78);
            native_apply(actor + 0xf0c, actor + 0x2c54, 30, 0, 0);
            bool rejected = false;
            try { native_apply(actor + 0xf0c, actor + 0x2c54, 36, 0, 0); } catch (const std::runtime_error&) { rejected = true; }
            CHECK(rejected); CHECK(native_remap(actor + 0x2c54, 36) == -1);
        });
        run("B000 to B006 binds the authored weapon clip from the original model archive", [] {
            start(); load(); CHECK(rxweapon::view().complete);
            CHECK(rxweapon::view().stock_tags == 6 && rxweapon::view().supplementary_tags == 10);
            CHECK(file_reads.size() == 10); for (unsigned i = 0; i < 10; ++i) CHECK(file_reads[i] == i + 6);
            apply(30); apply(36); CHECK(get<signed char>(actor + 0x2c54, 8) == 6);
            CHECK(native_remap(actor + 0x2c54, 36) == 0);
            const KhRef file = get<KhRef>(actor + 0x2c54, 0x18); CHECK(file_tags.at(file) == 6);
        });
        run("existing tags reuse stock allocations and preserve the equipped model", [] {
            start(); load(); const KhRef stock_tables = get<KhRef>(actor + 0x2c54, 0x4c);
            apply(36); apply(31); CHECK(get<KhRef>(actor + 0x2c54, 0x4c) == stock_tables);
            CHECK(file_reads.size() == 10); CHECK(get<int>(actor, 0xc) == 0);
        });
        run("all sixteen authored tags bind safely and repeated presses allocate nothing", [] {
            start(); load(); const auto count = allocations.size();
            for (int lap = 0; lap < 3; ++lap) for (int tag = 0; tag < 16; ++tag) apply(30 + tag);
            CHECK(allocations.size() == count); CHECK(file_reads.size() == 10);
            CHECK(get<signed char>(actor + 0x2c54) == 45);
        });
        run("teardown detaches owned tracks before freeing and stock frees exactly once", [] {
            start(); load(); apply(36); const auto prior = allocations.size(); CHECK(prior > 10);
            free(); CHECK(native_frees == 1); CHECK(table_frees == 16); CHECK(!rxweapon::view().actor);
            CHECK(table_parts.size() == 1); CHECK(allocations.size() == 10); // Inline table's five pairs remain.
        });
        run("pending native fades are detached before their supplemental objects are freed", [] {
            start(); load(); apply(36);
            const KhRef table = rxweapon::supplements[7].table;
            const KhRef pending = get<KhRef>(get<KhRef>(table, 0x10)); rendered.insert(pending);
            put(actor + 0xf10, 0xd4, pending); put(actor + 0xf10, 0, static_cast<std::uint16_t>(4));
            free(); CHECK(table_parts.size() == 1); CHECK(get<KhRef>(actor + 0xf10, 0xd4) == 0);
        });
        run("allocation failure rolls back the pool and missing tags use valid stock clips", [] {
            start(); fail_alloc = false;
            // Fail the first supplemental file; the stock loader has already completed.
            fail_file = true; load(); CHECK(!rxweapon::view().complete); CHECK(rxweapon::view().supplementary_tags == 0);
            apply(36); CHECK(get<signed char>(actor + 0x2c54) == 30);
            CHECK(logs.size() == 2); apply(36); CHECK(logs.size() == 2);
            free(); CHECK(table_parts.size() == 1);
        });
        run("retired SDK handles still clean through the synchronous native teardown hook", [] {
            start(); load(); apply(36); live = false; free();
            CHECK(native_frees == 1 && table_parts.size() == 1);
        });
        run("explicit stop restores original state and leaves stock resource ownership intact", [] {
            start(); load(); apply(36); const auto old_heap = current_heap; current_heap = 0x7777;
            rxweapon::stop(); CHECK(current_heap == 0x7777); current_heap = old_heap;
            CHECK(table_frees == 10); CHECK(get<int>(actor + 0x2c54, 0x50) == 6);
            CHECK(functions.at("Ov002_ApplyAnimMode") == reinterpret_cast<void*>(&native_apply));
            native_free(actor + 0x2c54); CHECK(table_parts.size() == 1);
        });
        run("dual wield, other seats and room actors cannot arm a normal-Roxas pool", [] {
            start(); put(actor, 0xc, 19); rxweapon::bind(other); CHECK(rxweapon::view().actor == 0);
            put(actor, 0xc, 0); room = true; rxweapon::bind(actor); CHECK(rxweapon::view().actor == 0);
            room = false; local_seat = 1; rxweapon::bind(actor); CHECK(rxweapon::view().actor == 0);
        });
        run("missing native routines and refused hooks restore earlier replacements", [] {
            missing = "Archive_LoadFile"; CHECK(!rxweapon::init(&engine, &actor_api, &network, &permitted));
            missing.clear(); fail_hook = "Ov002_ApplyAnimMode";
            CHECK(!rxweapon::init(&engine, &actor_api, &network, &permitted));
            CHECK(functions.at("Ov002_LoadAnimTables") == reinterpret_cast<void*>(&native_load));
        });
    } catch (...) { return 1; }
    std::cout << passed << '/' << passed << " weapon animation tests passed\n";
    return 0;
}
