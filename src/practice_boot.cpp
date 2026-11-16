#include "practice_boot.h"
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace rxpractice {
namespace {
constexpr std::uint16_t kPracticeFile = 20000;
constexpr std::uint16_t kPracticeMission = 3595;
constexpr unsigned kStateBytes = 0x1cac, kDeckOffset = 0xee0;
const KhHost* host;
const KhEngine* engine;
const KhActors* actors;
const KhNet* net;
bool requested, hooked, attempted, entered;
KhRef scene, mode, state_pointer, work_pointer, mission;
using StateFunction = KhRef (*)();
StateFunction original_title;
void (*new_game)(int);
void (*party_reset)();
void (*session_setup)(KhRef);
void (*slot_setup)(KhRef, int);
void (*session_create)();
void (*build_roxas)(int);
void (*set_field)(int, int, unsigned);
void (*request_scene)(int, int);
KhRef (*allocate)(unsigned);
void (*release)(KhRef);

unsigned char* bytes(KhRef ref) {
    return engine ? static_cast<unsigned char*>(engine->pointer(ref)) : nullptr;
}
bool range(KhRef ref, unsigned length) {
    return ref && length && ref <= UINT32_MAX - (length - 1) && bytes(ref) && bytes(ref + length - 1);
}
template<class T> T read(KhRef ref, unsigned offset = 0) {
    T value{};
    if (range(ref + offset, sizeof(T))) std::memcpy(&value, bytes(ref + offset), sizeof value);
    return value;
}
template<class T> void write(KhRef ref, unsigned offset, T value) {
    if (range(ref + offset, sizeof(T))) std::memcpy(bytes(ref + offset), &value, sizeof value);
}
void say(const char* message, KhLogLevel level = KH_LOG_INFO) {
    if (host && host->log) host->log(host->plugin, level, message);
}
template<class T> bool routine(T& destination, const char* name) {
    destination = reinterpret_cast<T>(engine->routine(name, nullptr));
    return destination != nullptr;
}
bool local() {
    return net && actors && !net->in_room() && actors->local_seat() == 0;
}
bool practice_requested() {
#if defined(_MSC_VER)
    size_t length = 0;
    if (getenv_s(&length, nullptr, 0, "PRAGMICAT_PRACTICE") != 0 || length != 2) return false;
    char value[2]{};
    return getenv_s(&length, value, sizeof value, "PRAGMICAT_PRACTICE") == 0 && std::strcmp(value, "1") == 0;
#else
    const char* value = std::getenv("PRAGMICAT_PRACTICE");
    return value && std::strcmp(value, "1") == 0;
#endif
}

bool bootstrap() {
    // These are title-idle prerequisites, before any live gameplay actor exists.
    const KhRef block = read<KhRef>(work_pointer);
    if (!range(block, 0x18 + kStateBytes) || !range(scene, 20) ||
        !range(mode, 8) || !range(mission, sizeof(std::uint16_t))) {
        say("Practice could not initialize: the native title state is unavailable.", KH_LOG_ERROR);
        return false;
    }
    const KhRef scratch = allocate(32);
    if (!range(scratch, 32)) {
        if (scratch) release(scratch);
        say("Practice could not initialize: no native setup memory is available.", KH_LOG_ERROR);
        return false;
    }
    std::memset(bytes(scratch), 0, 32);
    // Native routines take game references, including configuration arguments.
    write<int>(scratch, 0, 1);   // an offline session
    write<int>(scratch, 4, 1);   // one player
    write<int>(scratch, 12, 1);  // member zero only
    write<int>(scratch, 16, 1);  // native slot mode; same as Story's boot

    new_game(1);
    const KhRef state = read<KhRef>(state_pointer);
    if (state != block + 0x18 || !range(state, kStateBytes)) {
        release(scratch);
        say("Practice could not initialize: the new-game state was not published.", KH_LOG_ERROR);
        return false;
    }
    // A fresh, small deck. Let the game's own panel decoder and stat builder
    // create gear, spell charges and parameter records; no copied save is used.
    for (unsigned cell = 0; cell < 19; ++cell)
        write<std::uint16_t>(state, kDeckOffset + 2 * cell, 63); // Level Up
    constexpr std::array<std::uint16_t, 8> spells{93, 93, 96, 96, 99, 99, 105, 105};
    for (unsigned cell = 0; cell < spells.size(); ++cell)
        write<std::uint16_t>(state, kDeckOffset + 2 * (19 + cell), spells[cell]);
    session_setup(scratch);
    slot_setup(scratch + 16, 0);
    session_create();
    party_reset();
    build_roxas(0); // native new-game Roxas + full panel/deck reconstruction
    set_field(0, 9, 7); // ordinary playable Story-day context, not opening movies
    release(scratch);

    // First generated mission in the launcher's isolated two-mod profile.
    // Story flags retain the exact same normal-Roxas combat and Drive adapter.
    std::memset(bytes(mode), 0, 8);
    write<std::uint16_t>(mode, 2, kPracticeFile);
    write<std::uint16_t>(mission, 0, kPracticeMission);
    entered = true;
    request_scene(2, 0);
    say("Practice: a fresh Roxas deck is ready; entering the combat arena.");
    return true;
}

KhRef title_tick() {
    if (!requested || !hooked || attempted || !engine->on_game_thread() ||
        !engine->module_loaded(0) || read<int>(scene, 8) != 1 ||
        read<int>(scene, 12) != 0 || !local())
        return original_title ? original_title() : 0;
    attempted = true; // never reinitialize a title after returning from gameplay
    if (bootstrap()) return static_cast<KhRef>(-2); // native scene-exit sentinel
    return original_title ? original_title() : 0;
}
}

bool init(const KhHost* given_host, const KhEngine* given_engine,
          const KhActors* given_actors, const KhNet* given_net) {
    if (hooked || requested) return false;
    if (!practice_requested()) return true;
    if (!given_host || !given_engine || !given_actors || !given_net ||
        given_host->size < sizeof(KhHost) || given_host->abi_major != KH_ABI_MAJOR ||
        !given_host->port_version || std::strcmp(given_host->port_version, "0.1.5") != 0 ||
        given_engine->size < sizeof(KhEngine) || given_actors->size < sizeof(KhActors) ||
        given_net->size < sizeof(KhNet) || !given_engine->pointer || !given_engine->routine ||
        !given_engine->datum || !given_engine->replace || !given_engine->module_loaded ||
        !given_engine->on_game_thread || !given_actors->local_seat || !given_net->in_room)
        return false;
    host = given_host; engine = given_engine; actors = given_actors; net = given_net;
    scene = engine->datum("gSceneCtl");
    mode = engine->datum("data_0204c240");
    state_pointer = engine->datum("gGameState");
    work_pointer = engine->datum("data_0204be14");
    mission = engine->datum("gMissionNumber");
    if (!scene || !mode || !state_pointer || !work_pointer || !mission ||
        !routine(new_game, "Ov000_SetupWorkArea") ||
        !routine(party_reset, "PartyState_Reset") ||
        !routine(session_setup, "Session_StoreSetup") ||
        !routine(slot_setup, "CopyToSlotTable8") ||
        !routine(session_create, "EnsureServiceInstance") ||
        !routine(build_roxas, "Ov000_ResetPartyMemberAndLayout") ||
        !routine(set_field, "GameState_SetField") ||
        !routine(request_scene, "Scene_RequestPending") ||
        !routine(allocate, "NNSi_FndAllocFromDefaultExpHeap") ||
        !routine(release, "NNSi_FndFreeFromDefaultHeap")) return false;
    void* previous = nullptr;
    if (engine->replace("Ov000_TickMenuLoop", reinterpret_cast<void*>(&title_tick), &previous) != KH_OK || !previous)
        return false;
    original_title = reinterpret_cast<StateFunction>(previous);
    hooked = requested = true;
    attempted = entered = false;
    say("Practice direct entry armed for this isolated launch.");
    return true;
}
bool enabled() { return requested && hooked; }
bool active() {
    return enabled() && entered && engine->on_game_thread() && local() &&
        engine->module_loaded(22) && read<int>(scene, 8) == 2 &&
        read<unsigned char>(mode) == 0 && read<std::uint16_t>(mode, 2) == kPracticeFile &&
        read<std::uint16_t>(mission) == kPracticeMission;
}
void stop() {
    if (hooked && engine && engine->on_game_thread())
        engine->replace("Ov000_TickMenuLoop", reinterpret_cast<void*>(original_title), nullptr);
    requested = hooked = attempted = entered = false;
    host = nullptr; engine = nullptr; actors = nullptr; net = nullptr;
    original_title = nullptr;
}
}
