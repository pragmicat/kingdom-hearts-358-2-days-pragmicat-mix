#pragma once
#include <kh/kh_actors.h>
#include <kh/kh_engine.h>
#include <kh/kh_mod.h>
#include <kh/kh_net.h>

// Story-only, seat-local native Roxas actor replacement. frame() must be called
// after all actor updates, never from an attack/hit/actor-update callback.
namespace rxdrive {
enum class Phase { Normal, ActivationQueued, Active, RestorationQueued, Faulted };
struct Settings {
    unsigned capacity = 1000;
    unsigned charge_per_hit = 40;
    unsigned max_targets_per_attack = 3;
    unsigned duration_ticks = 900; // engine clock: 60 ticks/sec, excludes pause/cinematics
};
struct FrameContext {
    KhRef actor = 0;
    unsigned tick = 0;
    bool story = false;
    bool paused = false;
    bool cinematic = false;
    bool controllable = false;
    bool safe_to_replace = false; // caller: no finisher, active hit, chase, or forced state
};
struct View {
    Phase phase = Phase::Normal;
    unsigned charge = 0, capacity = 1000;
    unsigned remaining_ticks = 0, duration_ticks = 900;
    bool ready = false;
    bool available = false;
    const char* reason = nullptr;
};
struct Callbacks {
    // An additional caller veto before any native actor/resource is destroyed.
    bool (*before_replace)(KhRef old_actor, int new_kind, void* user) = nullptr;
    void (*after_replace)(KhRef old_actor, KhRef new_actor, int new_kind,
                          bool rolled_back, void* user) = nullptr;
    void (*log)(const char* message, void* user) = nullptr;
    void* user = nullptr;
};
bool init(const KhHost*, const KhEngine*, const KhActors*, const KhNet*,
          Callbacks = {}, Settings = {});
// Compatible with combat_motion's accepted-native-hit observer. No second hit hook.
void on_hit(KhRef attacker, KhActor target, unsigned attack_serial,
            unsigned tick, void* unused = nullptr);
bool request_activation(); // active form: request an early ordinary restoration
// Read-only: a pending transition can begin finishing the current melee action
// once the live Story actor's existing native spell channels are quiescent.
bool ready_for_safe_point(KhRef actor);
// One-shot practice setup; the caller must gate this to the isolated arena.
// Refuses queued/active forms, paused scenes, stale actors and network play.
bool prime_practice_meter(KhRef actor);
void frame(const FrameContext&);
View view();
bool is_active();
bool is_replacing();
// Call before a scene's native party teardown when possible. This does not force
// a reconstruction into a cinematic; party data already retains normal Roxas.
void reset();
void stop();
}
