// Exercises the actual module against bounded native-memory and routine mocks.
#include "../src/combat_rules.cpp"
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>

namespace test {
#define CHECK(x) check((x), #x)
void check(bool ok, const char* msg) { if (!ok) throw std::runtime_error(msg); }
constexpr KhRef actor = 0x1000, enemy = 0x5000, packet = 0x6000, party = 0x7000;
std::array<unsigned char, 0x9000> memory{};
KhEngine api{};
KhActors actor_api{};
std::map<std::string, void*> routines;
KhActor owner = 1, target = 2;
unsigned tick, guard_calls, dodge_calls, destroy_calls, request_calls, pair_calls, clear_calls, stores;
bool permitted, loaded, slot_ok, cast_succeeds;
int enemy_kind;
std::array<int, 15> equipped{};
std::string absent;
void* pointer(KhRef ref) { return ref && ref < memory.size() ? memory.data() + ref : nullptr; }
template<class T> void put(KhRef ref, unsigned off, T v) { std::memcpy(memory.data() + ref + off, &v, sizeof v); }
template<class T> T get(KhRef ref, unsigned off = 0) { T v{}; std::memcpy(&v, memory.data() + ref + off, sizeof v); return v; }
KhActor player(int seat) { return seat == 0 ? owner : 0; }
KhRef ref(KhActor h) { return h == owner && owner ? actor : h == target && target ? enemy : 0; }
int kind(KhActor h) { return h == owner && owner ? KH_ACTOR_PLAYER : h == target && target ? KH_ACTOR_ENEMY : KH_ACTOR_NONE; }
KhActor of(KhRef r) { return r == actor ? owner : r == enemy ? target : 0; }
int character(KhActor h) { return h == target ? enemy_kind : 0; }
int module(int n) { return loaded && n == 22; }
unsigned frame() { return tick; }
bool allow(KhRef r) { return permitted && r == actor; }
void* routine(const char* name, int*) { return absent == name || !routines.count(name) ? nullptr : routines.at(name); }
KhRef datum(const char* name) { return std::strcmp(name, "gPartyMembers") == 0 ? party : 0; }
void guard(KhRef block, int level, KhRef r) {
    CHECK(block == actor + 0x1070 && level == 1 && r == actor); ++guard_calls;
    std::memset(memory.data() + block, 0x55, 0x11c); put(block, 0, static_cast<unsigned char>(1));
}
void dodge(KhRef block, int level, KhRef r, int extra) {
    CHECK(block == actor + 0x118c && level == 1 && r == actor && extra == 0); ++dodge_calls;
    std::memset(memory.data() + block, 0x66, 12); put(block, 0, static_cast<unsigned char>(1));
}
void destroy(KhRef block, unsigned, unsigned, unsigned) { CHECK(block == actor + 0x1070); ++destroy_calls; }
int count(int seat, int spell) { CHECK(seat == 0 && spell >= 0 && spell < 15); return get<unsigned char>(party, 0x9c + spell * 2); }
void set_count(int seat, int spell, int value) {
    CHECK(seat == 0); ++stores;
    value = std::clamp(value, 0, static_cast<int>(get<unsigned char>(party, 0x9d + spell * 2)));
    put(party, 0x9c + spell * 2, static_cast<unsigned char>(value));
}
int param(int seat, int what) { CHECK(seat == 0 && what >= 1 && what <= 15); return equipped[what - 1]; }
int slot(KhRef block, unsigned spell) { CHECK(block == actor + 0x2288 && spell < 15); return slot_ok && count(0, static_cast<int>(spell)) > 0; }
KhRef request(KhRef r, int n) {
    CHECK(r == actor && n == 0); ++request_calls;
    if (!cast_succeeds) return 0;
    const int spell = get<std::uint16_t>(actor, 0x4de);
    set_count(0, spell, count(0, spell) - 1); // mock the original cast's consumption
    return 0x0200abcd;
}
void pair(KhRef r) { CHECK(r == actor); ++pair_calls; }
void clear(KhRef r) { CHECK(r == actor + 0x910); ++clear_calls; }
void reset() {
    rxrules::detach(); memory.fill(0); routines.clear(); tick = 100;
    guard_calls = dodge_calls = destroy_calls = request_calls = pair_calls = clear_calls = stores = 0;
    permitted = loaded = slot_ok = cast_succeeds = true; owner = 1; target = 2; enemy_kind = 0; absent.clear(); equipped.fill(1);
    api = {}; api.size = sizeof(api); api.pointer = &pointer; api.routine = &routine; api.datum = &datum;
    api.module_loaded = &module; api.frame = &frame;
    actor_api = {}; actor_api.size = sizeof(actor_api); actor_api.player = &player; actor_api.ref = &ref;
    actor_api.kind = &kind; actor_api.of = &of; actor_api.character = &character;
    routines = {{"Ov022_SetUpTrailEffect", reinterpret_cast<void*>(&guard)},
                {"func_ov022_02092880", reinterpret_cast<void*>(&dodge)},
                {"Ov022_TeardownIfBit0", reinterpret_cast<void*>(&destroy)},
                {"Ov022_TryPendingAction", reinterpret_cast<void*>(&request)},
                {"Ov022_IsSlotActionAllowed", reinterpret_cast<void*>(&slot)},
                {"Load2DArrayU8", reinterpret_cast<void*>(&count)},
                {"ClampAndStoreLevelEntry", reinterpret_cast<void*>(&set_count)},
                {"Slot_EvalPackedParam", reinterpret_cast<void*>(&param)},
                {"func_ov022_0209c700", reinterpret_cast<void*>(&pair)},
                {"func_ov022_020b09c4", reinterpret_cast<void*>(&clear)}};
    put(actor, 0xc, 0); put(actor, 0x12, static_cast<std::uint16_t>(100));
    put(actor, 0x2290, 0x7fffu); put(actor, 0x2ba4, 0x8000u); put(actor, 0x2ba0, 0x8100u);
    for (int i = 0; i < 15; ++i) { put(party, 0x9c + i * 2, static_cast<unsigned char>(0)); put(party, 0x9d + i * 2, static_cast<unsigned char>(3)); }
    put(enemy, 0x50, 1); put(enemy, 0x60, static_cast<std::uint16_t>(1)); put(enemy, 0x21a, static_cast<std::int16_t>(100));
}
void attach() { CHECK(rxrules::attach(&api, &actor_api, &allow)); rxrules::bind(actor); }
void packet_defaults(unsigned flags = 4) {
    std::memset(memory.data() + packet, 0, 0x2c);
    put(packet, 0, static_cast<std::uint16_t>(flags)); put(packet, 2, static_cast<std::uint16_t>(0x4000));
    put(packet, 0x10, static_cast<std::int16_t>(27)); put(packet, 0x12, static_cast<std::int16_t>(4));
    put(packet, 0x14, static_cast<std::uint16_t>(0)); put(packet, 0x18, 0x8200u); put(packet, 0x1c, static_cast<std::int8_t>(17));
}
void hit(unsigned serial, unsigned flags = 4, bool confirmed = true) {
    rxrules::attack_started(actor, serial); packet_defaults(flags);
    rxrules::before_native_hit(actor, enemy, packet);
    if (confirmed) rxrules::on_confirmed_melee_hit(actor, target, serial, tick);
}
void prerequisites_and_scope() {
    reset(); absent = "ClampAndStoreLevelEntry"; CHECK(!rxrules::attach(&api, &actor_api, &allow)); CHECK(stores == 0);
    reset(); attach(); permitted = false; rxrules::refresh_mobility(actor); hit(1); CHECK(guard_calls == 0 && stores == 0);
    permitted = true; put(actor, 0xc, 19); rxrules::refresh_mobility(actor); CHECK(guard_calls == 0);
    put(actor, 0xc, 0); loaded = false; hit(2); CHECK(rxrules::snapshot().confirmed_actions == 0);
}
void mobility_native_build_and_restore() {
    reset(); attach(); std::array<unsigned char, 0x128> before{};
    std::memcpy(before.data(), memory.data() + actor + 0x1070, before.size());
    rxrules::refresh_mobility(actor); rxrules::refresh_mobility(actor);
    CHECK(guard_calls == 1 && dodge_calls == 1); CHECK(rxrules::snapshot().guard_granted && rxrules::snapshot().dodge_granted);
    CHECK(stores == 0); rxrules::forget(actor); CHECK(destroy_calls == 1);
    CHECK(std::memcmp(before.data(), memory.data() + actor + 0x1070, before.size()) == 0);
    reset(); attach(); put(actor, 0x1070, static_cast<unsigned char>(1)); put(actor, 0x118c, static_cast<unsigned char>(1));
    rxrules::refresh_mobility(actor); rxrules::forget(); CHECK(guard_calls == 0 && dodge_calls == 0 && destroy_calls == 0);
}
void retired_handles_never_restore_into_reuse() {
    reset(); attach(); rxrules::refresh_mobility(actor); owner = 9; // different generation at same actor address
    std::memset(memory.data() + actor + 0x1070, 0xa7, 0x128);
    rxrules::forget(); CHECK(destroy_calls == 0); CHECK(get<unsigned char>(actor, 0x1070) == 0xa7);
}
void native_packet_damage_and_immunity() {
    reset(); attach(); packet_defaults(); rxrules::attack_started(actor, 1, rxrules::Reaction::Launcher);
    std::array<unsigned char, 0x2c> before{}; std::memcpy(before.data(), memory.data() + packet, before.size());
    rxrules::before_native_hit(actor, enemy, packet);
    CHECK(get<std::uint16_t>(packet, 2) == 0x4004 && get<std::int8_t>(packet, 0x1c) == 100);
    before[2] = 4; before[3] = 0x40; before[0x1c] = 100;
    CHECK(std::memcmp(before.data(), memory.data() + packet, before.size()) == 0);
    CHECK(get<std::int16_t>(enemy, 0x21a) == 100); // policy never deals damage
    auto denied = [&](unsigned serial) { packet_defaults(); rxrules::attack_started(actor, serial, rxrules::Reaction::Launcher); rxrules::before_native_hit(actor, enemy, packet); CHECK(get<std::uint16_t>(packet, 2) == 0x4000); };
    tick += 100; enemy_kind = 83; denied(2); enemy_kind = 41; denied(3); enemy_kind = 0;
    put(enemy, 0x1ac, static_cast<std::uint16_t>(4)); denied(4); put(enemy, 0x1ac, static_cast<std::uint16_t>(0));
    put(enemy, 0x1b0, static_cast<std::uint16_t>(4)); denied(5); put(enemy, 0x1b0, static_cast<std::uint16_t>(0));
    put(enemy, 0x1c0, 4096); denied(6); put(enemy, 0x1c0, 0);
    put(enemy, 0x2e4, 1); denied(7); put(enemy, 0x2e4, 0);
    packet_defaults(12); rxrules::attack_started(actor, 8, rxrules::Reaction::Launcher); rxrules::before_native_hit(actor, enemy, packet); CHECK(get<std::uint16_t>(packet, 2) == 0x4000);
    packet_defaults(); put(packet, 2, static_cast<std::uint16_t>(0x4080)); rxrules::before_native_hit(actor, enemy, packet); CHECK(get<std::uint16_t>(packet, 2) == 0x4080);
}
void finite_launch_and_carry() {
    reset(); attach(); packet_defaults(); rxrules::attack_started(actor, 1, rxrules::Reaction::Launcher); rxrules::before_native_hit(actor, enemy, packet);
    tick += 1; packet_defaults(); rxrules::attack_started(actor, 2, rxrules::Reaction::Launcher); rxrules::before_native_hit(actor, enemy, packet); CHECK(get<std::uint16_t>(packet, 2) == 0x4000);
    put(enemy, 0x1c4, static_cast<unsigned char>(8)); tick += 12;
    packet_defaults(); rxrules::attack_started(actor, 3, rxrules::Reaction::Carry); rxrules::before_native_hit(actor, enemy, packet); CHECK(get<std::uint16_t>(packet, 2) == 0x4004);
    tick += 12; packet_defaults(); rxrules::attack_started(actor, 4, rxrules::Reaction::Carry); rxrules::before_native_hit(actor, enemy, packet); CHECK(get<std::uint16_t>(packet, 2) == 0x4000);
    put(enemy, 0x1c4, static_cast<unsigned char>(0)); tick += 100; packet_defaults(); rxrules::attack_started(actor, 5, rxrules::Reaction::Carry); rxrules::before_native_hit(actor, enemy, packet); CHECK(get<std::uint16_t>(packet, 2) == 0x4000);
}
void magic_native_validation_and_consumption() {
    reset(); attach(); put(actor, 0x1c, static_cast<std::uint16_t>(4)); put(actor, 0x4dc, static_cast<signed char>(8)); put(actor, 0x4de, static_cast<std::uint16_t>(2));
    put(party, 0x9c + 2 * 2, static_cast<unsigned char>(2));
    CHECK(rxrules::try_magic_cancel(actor, false) == 0 && request_calls == 0);
    slot_ok = false; CHECK(rxrules::try_magic_cancel(actor, true) == 0 && request_calls == 0); slot_ok = true;
    put(actor, 0x4dc, static_cast<signed char>(11)); CHECK(rxrules::try_magic_cancel(actor, true) == 0); put(actor, 0x4dc, static_cast<signed char>(8));
    cast_succeeds = false; CHECK(rxrules::try_magic_cancel(actor, true) == 0 && clear_calls == 0); cast_succeeds = true;
    CHECK(rxrules::try_magic_cancel(actor, true) == 0x0200abcd); CHECK(count(0, 2) == 1);
    CHECK(pair_calls == 1 && clear_calls == 1 && request_calls == 2);
    CHECK(get<std::uint16_t>(actor, 0x12) == 100 && rxrules::snapshot().attack_serial == 0);
}
void charges_require_six_real_unique_melee_actions() {
    reset(); attach();
    for (unsigned i = 1; i <= 5; ++i) hit(i); CHECK(stores == 0);
    rxrules::on_confirmed_melee_hit(actor, target, 5, tick); CHECK(rxrules::snapshot().confirmed_actions == 5);
    hit(6, 8); CHECK(stores == 0); hit(7, 4, false); CHECK(stores == 0); hit(8, 0x84);
    CHECK(stores == 1 && count(0, 0) == 1 && rxrules::snapshot().restored_charges == 1);
    for (int spell = 12; spell < 15; ++spell) CHECK(count(0, spell) == 0);
    for (unsigned i = 9; i < 40; ++i) hit(i); CHECK(stores == 1);
    tick += 180; hit(40); CHECK(stores == 2 && count(0, 1) == 1);
    permitted = false; tick += 180; for (unsigned i = 41; i <= 48; ++i) hit(i); CHECK(stores == 2);
}
void resource_caps_full_stocks_and_no_healing() {
    reset(); attach();
    for (int spell = 0; spell < 12; ++spell) put(party, 0x9c + spell * 2, static_cast<unsigned char>(3));
    for (unsigned i = 1; i <= 6; ++i) hit(i); CHECK(stores == 0 && rxrules::snapshot().confirmed_actions == 0);
    put(party, 0x9c, static_cast<unsigned char>(2)); hit(7); CHECK(stores == 0);
    for (unsigned i = 8; i <= 12; ++i) hit(i); CHECK(stores == 1 && count(0, 0) == 3);
    reset(); attach(); put(actor, 0x2290, 0x7000u); // only Cure family built
    for (unsigned i = 1; i <= 30; ++i) hit(i); CHECK(stores == 0);
    reset(); attach(); equipped.fill(0); for (unsigned i = 1; i <= 6; ++i) hit(i); CHECK(stores == 0);
    reset(); attach();
    unsigned serial = 0;
    for (unsigned batch = 0; batch < 7; ++batch) { tick = 100 + batch * 180; for (int i = 0; i < 6; ++i) hit(++serial); }
    CHECK(stores == 6); tick = 1900; hit(++serial); CHECK(stores == 7);
    for (int spell = 12; spell < 15; ++spell) CHECK(count(0, spell) == 0);
}
}
int main() {
    struct Case { const char* name; void (*run)(); };
    const Case cases[] = {
        {"prerequisites and story/paused gates", test::prerequisites_and_scope},
        {"native mobility setup and exact restoration", test::mobility_native_build_and_restore},
        {"retired handles cannot touch reused memory", test::retired_handles_never_restore_into_reuse},
        {"packet damage preservation and native immunity", test::native_packet_damage_and_immunity},
        {"bounded launch and one airborne carry", test::finite_launch_and_carry},
        {"native magic validation and charge consumption", test::magic_native_validation_and_consumption},
        {"six distinct confirmed melee actions per charge", test::charges_require_six_real_unique_melee_actions},
        {"resource caps, equipped spells and Cure exclusion", test::resource_caps_full_stocks_and_no_healing}
    };
    int failures = 0;
    for (const auto& c : cases) try { c.run(); std::cout << "PASS " << c.name << '\n'; }
    catch (const std::exception& e) { ++failures; std::cerr << "FAIL " << c.name << ": " << e.what() << '\n'; }
    rxrules::detach(); std::cout << (std::size(cases) - failures) << '/' << std::size(cases) << " rules tests passed\n";
    return failures ? 1 : 0;
}
