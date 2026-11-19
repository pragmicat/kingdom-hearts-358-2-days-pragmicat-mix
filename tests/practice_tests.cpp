// Actual practice adapter against a bounded native host. Never starts a game.
#include "../src/practice_boot.cpp"
#include <algorithm>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

int set_test_environment(const char* name, const char* value) {
#ifdef _WIN32
    return _putenv_s(name, value);
#else
    return *value ? setenv(name, value, 1) : unsetenv(name);
#endif
}

namespace practice_test {
void require(bool value, const char* expression) { if (!value) throw std::runtime_error(expression); }
#define REQUIRE(x) ::practice_test::require((x), #x)
std::vector<unsigned char> memory(65536);
std::map<KhRef, unsigned> allocations;
std::map<std::string, KhRef> globals;
std::map<std::string, void*> routines;
std::vector<std::string> calls;
std::set<int> modules;
KhHost host{};
KhEngine engine{};
KhActors actors{};
KhNet net{};
KhRef cursor, state_ptr, work_ptr, work, scene, mode, mission;
KhRef (*replacement)();
unsigned ordinary_calls, allocation_calls, frees, resolutions, replacements;
int seat, wanted_scene, wanted_arg;
bool room, game_thread, fail_allocation, fail_replace;

void* pointer(KhRef ref) {
    auto item = allocations.upper_bound(ref);
    if (item == allocations.begin()) return nullptr;
    --item;
    return ref >= item->first && ref < item->first + item->second ? memory.data() + ref : nullptr;
}
KhRef block(unsigned bytes) {
    const KhRef ref = cursor;
    cursor = (cursor + bytes + 15) & ~15u;
    REQUIRE(cursor < memory.size());
    allocations[ref] = bytes;
    std::memset(memory.data() + ref, 0, bytes);
    return ref;
}
template<class T> void put(KhRef ref, unsigned offset, T value) {
    REQUIRE(pointer(ref + offset));
    std::memcpy(pointer(ref + offset), &value, sizeof value);
}
template<class T> T get(KhRef ref, unsigned offset = 0) {
    T value{};
    REQUIRE(pointer(ref + offset));
    std::memcpy(&value, pointer(ref + offset), sizeof value);
    return value;
}
KhRef ordinary() { ++ordinary_calls; return 0x1234; }
KhRef allocate(unsigned size) {
    ++allocation_calls;
    if (fail_allocation) return 0;
    const KhRef ref = block(size);
    std::memset(pointer(ref), 0xcd, size);
    return ref;
}
void release(KhRef ref) { REQUIRE(allocations.erase(ref) == 1); ++frees; }
void fresh(int slot) {
    REQUIRE(slot == 1);
    calls.emplace_back("fresh");
    put(state_ptr, 0, work + 0x18);
    std::memset(pointer(work + 0x18), 0, 0x1cac);
}
void store(KhRef ref) {
    REQUIRE(get<int>(ref, 0) == 1 && get<int>(ref, 4) == 1);
    REQUIRE(get<int>(ref, 8) == 0 && get<int>(ref, 12) == 1);
    calls.emplace_back("offline setup");
}
void slot(KhRef ref, int number) {
    REQUIRE(number == 0 && get<int>(ref) == 1 && get<int>(ref, 4) == 0);
    calls.emplace_back("slot zero");
}
void session() { calls.emplace_back("service"); }
void reset_party() { calls.emplace_back("parameters and party"); }
void build(int kind) {
    REQUIRE(kind == 0);
    const KhRef state = get<KhRef>(state_ptr);
    // The panel grid supplied to the native new-game decoder is compact and
    // independently checked here, including untouched cells outside the deck.
    for (unsigned cell = 0; cell < 19; ++cell)
        REQUIRE(get<std::uint16_t>(state, 0xee0 + cell * 2) == 63);
    const std::array<std::uint16_t, 8> spell_ids{93,93,96,96,99,99,105,105};
    for (unsigned cell = 0; cell < spell_ids.size(); ++cell)
        REQUIRE(get<std::uint16_t>(state, 0xee0 + (19 + cell) * 2) == spell_ids[cell]);
    for (unsigned cell = 27; cell < 120; ++cell)
        REQUIRE(get<std::uint16_t>(state, 0xee0 + cell * 2) == 0);
    calls.emplace_back("native Roxas deck");
}
void field(int index, int bits, unsigned value) {
    REQUIRE(index == 0 && bits == 9 && value == 7);
    calls.emplace_back("playable day");
}
void change_scene(int target, int argument) {
    REQUIRE(get<unsigned char>(mode) == 0);
    REQUIRE(get<std::uint16_t>(mode, 2) == 20000);
    REQUIRE(get<std::uint16_t>(mode, 4) == 0);
    REQUIRE(get<std::uint16_t>(mission) == 3595);
    REQUIRE(allocation_calls == frees);
    wanted_scene = target; wanted_arg = argument;
    calls.emplace_back("field request");
}
void* find(const char* name, int*) { ++resolutions; auto it = routines.find(name); return it == routines.end() ? nullptr : it->second; }
KhRef datum(const char* name) { auto it = globals.find(name); return it == globals.end() ? 0 : it->second; }
int loaded(int module) { return modules.contains(module); }
KhResult replace(const char* name, void* fn, void** previous) {
    REQUIRE(std::string(name) == "Ov000_TickMenuLoop");
    ++replacements;
    if (fail_replace) return KH_ERROR;
    if (previous) *previous = reinterpret_cast<void*>(ordinary);
    replacement = reinterpret_cast<KhRef(*)()>(fn);
    return KH_OK;
}
void setup(bool enabled = true) {
    rxpractice::stop();
    set_test_environment("PRAGMICAT_PRACTICE", enabled ? "1" : "");
    allocations.clear(); globals.clear(); routines.clear(); calls.clear(); modules = {0};
    std::fill(memory.begin(), memory.end(), static_cast<unsigned char>(0));
    cursor = 0x100; room = false; game_thread = true; seat = 0;
    fail_allocation = fail_replace = false;
    ordinary_calls = allocation_calls = frees = resolutions = replacements = 0;
    wanted_scene = wanted_arg = -1; replacement = ordinary;
    state_ptr = block(4); work_ptr = block(4); work = block(0x18 + 0x1cac);
    scene = block(20); mode = block(8); mission = block(4);
    put(work_ptr,0,work); put(state_ptr,0,work + 0x18); put<int>(scene,8,1);
    globals = {{"gGameState",state_ptr},{"data_0204be14",work_ptr},{"gSceneCtl",scene},{"data_0204c240",mode},{"gMissionNumber",mission}};
    routines = {{"Ov000_SetupWorkArea",reinterpret_cast<void*>(fresh)},
                {"PartyState_Reset",reinterpret_cast<void*>(reset_party)},
                {"Session_StoreSetup",reinterpret_cast<void*>(store)},
                {"CopyToSlotTable8",reinterpret_cast<void*>(slot)},
                {"EnsureServiceInstance",reinterpret_cast<void*>(session)},
                {"Ov000_ResetPartyMemberAndLayout",reinterpret_cast<void*>(build)},
                {"GameState_SetField",reinterpret_cast<void*>(field)},
                {"Scene_RequestPending",reinterpret_cast<void*>(change_scene)},
                {"NNSi_FndAllocFromDefaultExpHeap",reinterpret_cast<void*>(allocate)},
                {"NNSi_FndFreeFromDefaultHeap",reinterpret_cast<void*>(release)}};
    host = {}; host.size = sizeof host; host.abi_major = KH_ABI_MAJOR; host.port_version = "0.1.5";
    engine = {}; engine.size = sizeof engine; engine.pointer = pointer; engine.routine = find;
    engine.datum = datum; engine.replace = replace; engine.module_loaded = loaded;
    engine.on_game_thread = [] { return game_thread ? 1 : 0; };
    actors = {}; actors.size = sizeof actors; actors.local_seat = [] { return seat; };
    net = {}; net.size = sizeof net; net.in_room = [] { return room ? 1 : 0; };
}
bool init() { return rxpractice::init(&host,&engine,&actors,&net); }
void arrive() { modules = {22}; put<int>(scene,8,2); }
}

int main() {
    using namespace practice_test;
    int count = 0;
    auto test = [&](const char* name, auto body) {
        try { body(); ++count; std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) { std::cerr << "FAIL " << name << ": " << error.what() << '\n'; throw; }
    };
    try {
        test("campaign and nonexact flags install no practice hook", [] {
            setup(false); REQUIRE(rxpractice::init(nullptr,nullptr,nullptr,nullptr));
            REQUIRE(!rxpractice::enabled() && resolutions == 0 && replacements == 0);
            set_test_environment("PRAGMICAT_PRACTICE","true"); REQUIRE(init()); REQUIRE(replacements == 0);
        });
        test("unsupported native hosts fail before installing a hook", [] {
            setup(); host.port_version = "0.1.4"; REQUIRE(!init()); REQUIRE(replacements == 0);
            setup(); engine.size = 1; REQUIRE(!init()); REQUIRE(replacements == 0);
            setup(); net.in_room = nullptr; REQUIRE(!init()); REQUIRE(replacements == 0);
        });
        test("missing symbols and refused title replacement remain unarmed", [] {
            setup(); routines.erase("Ov000_ResetPartyMemberAndLayout"); REQUIRE(!init()); REQUIRE(!rxpractice::enabled());
            setup(); fail_replace = true; REQUIRE(!init()); REQUIRE(!rxpractice::enabled());
        });
        test("native bootstrap rebuilds a fresh deck without loading a save", [] {
            setup(); REQUIRE(init()); REQUIRE(!rxpractice::active());
            REQUIRE(replacement() == static_cast<KhRef>(-2));
            REQUIRE(wanted_scene == 2 && wanted_arg == 0 && ordinary_calls == 0);
            const std::vector<std::string> expected{"fresh","offline setup","slot zero","service","parameters and party","native Roxas deck","playable day","field request"};
            REQUIRE(calls == expected && frees == 1);
        });
        test("native mission halfword preserves its neighboring global", [] {
            setup(); REQUIRE(init());
            put<std::uint16_t>(mission, 2, 0xa55a);
            REQUIRE(replacement() == static_cast<KhRef>(-2));
            REQUIRE(get<std::uint16_t>(mission) == 3595);
            REQUIRE(get<std::uint16_t>(mission, 2) == 0xa55a);
            arrive(); REQUIRE(rxpractice::active());
        });
        test("a two-byte native mission global is sufficient for boot and ownership", [] {
            setup(); allocations[mission] = 2; REQUIRE(init());
            REQUIRE(replacement() == static_cast<KhRef>(-2));
            arrive(); REQUIRE(rxpractice::active());
        });
        test("network and foreign-seat titles cannot seed a practice game", [] {
            setup(); REQUIRE(init()); room = true; REQUIRE(replacement() == 0x1234); REQUIRE(calls.empty());
            room = false; seat = 1; REQUIRE(replacement() == 0x1234); REQUIRE(calls.empty());
            seat = 0; REQUIRE(replacement() == static_cast<KhRef>(-2));
        });
        test("scene pending, wrong overlay and wrong thread defer to native title", [] {
            setup(); REQUIRE(init()); put<int>(scene,12,7); REQUIRE(replacement() == 0x1234);
            put<int>(scene,12,0); modules.clear(); REQUIRE(replacement() == 0x1234);
            modules.insert(0); game_thread = false; REQUIRE(replacement() == 0x1234);
            game_thread = true; REQUIRE(calls.empty()); REQUIRE(replacement() == static_cast<KhRef>(-2));
        });
        test("bad native memory cannot alter the game or allocate setup data", [] {
            setup(); REQUIRE(init()); put<KhRef>(work_ptr,0,0);
            REQUIRE(replacement() == 0x1234 && calls.empty() && allocation_calls == 0);
        });
        test("native scratch failure preserves memory and never requests a field", [] {
            setup(); REQUIRE(init()); fail_allocation = true;
            const auto before = memory;
            REQUIRE(replacement() == 0x1234 && calls.empty() && wanted_scene == -1);
            REQUIRE(memory == before);
        });
        test("practice boot happens once even after returning to the title", [] {
            setup(); REQUIRE(init()); REQUIRE(replacement() == static_cast<KhRef>(-2));
            const auto total = calls.size(); REQUIRE(replacement() == 0x1234); REQUIRE(calls.size() == total);
        });
        test("active state is limited to the exact isolated local practice field", [] {
            setup(); REQUIRE(init()); replacement(); arrive(); REQUIRE(rxpractice::active());
            put<std::uint16_t>(mode,2,2); REQUIRE(!rxpractice::active());
            put<std::uint16_t>(mode,2,20000); put<unsigned char>(mode,0,4); REQUIRE(!rxpractice::active());
            put<unsigned char>(mode,0,0); put<std::uint16_t>(mission,0,1); REQUIRE(!rxpractice::active());
            put<std::uint16_t>(mission,0,3595); room = true; REQUIRE(!rxpractice::active());
            room = false; seat = 1; REQUIRE(!rxpractice::active()); seat = 0;
            REQUIRE(rxpractice::active());
        });
        test("stop restores the original title routine and clears practice ownership", [] {
            setup(); REQUIRE(init()); rxpractice::stop();
            REQUIRE(replacement == ordinary && !rxpractice::enabled() && !rxpractice::active());
        });
        rxpractice::stop(); set_test_environment("PRAGMICAT_PRACTICE","");
        std::cout << count << '/' << count << " practice tests passed\n";
        return 0;
    } catch (...) { rxpractice::stop(); set_test_environment("PRAGMICAT_PRACTICE",""); return 1; }
}
