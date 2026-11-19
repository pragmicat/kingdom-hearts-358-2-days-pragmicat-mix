// Normal-Roxas native Limit actions reused as bounded, health-independent finishers.
// This helper never synthesizes a hit, changes HP, or supplies a collision shape.
#pragma once
#include <kh/kh_engine.h>

namespace rxlimit {
enum class Finisher { Rush = 0, LightBurst = 1, Flurry = 2 };
struct Profile {
    Finisher kind;
    int native_variant;
    int native_state;
    int start_frame_fx;
    int end_frame_fx;
    const char* animation;
};
// Predicate must validate normal Roxas, local story actor and current scene lifetime.
// It must allow the actor while its native finisher state runs (not a pad-enabled test).
using Permitted = bool (*)(KhRef actor);
// Called at an authored exit window after a native timeline impact was stepped.
// Return0 to keep the action running. To exit, call cancel(actor), select/buffer
// the desired ordinary action, then return its actual native state routine.
using ExitHandler = KhRef (*)(KhRef actor, Finisher kind, int animation_frame_fx);

const Profile& profile(Finisher kind);
// Resolves and wraps native ro controllers/resource loading; rolls back partial install.
bool attach(const KhEngine* engine, Permitted permitted);
// Call from the normal actor update (on its owning heap) to queue original li resource
// loading early. Returns true only when both native asynchronous grid loads finished.
bool prepare(KhRef actor);
// The caller must return this native state-function reference from its actor step.
// 0 means no state change. HP/low-health permission is deliberately not consulted.
KhRef begin(KhRef actor, Finisher kind);
bool active(KhRef actor = 0);
void set_exit_handler(ExitHandler handler);
// The outer frame event must set this before actor controllers run. While
// suspended, managed controllers and their timeout stop; no exit input is read.
void set_suspended(bool suspended);
// Scale only managed native timeline damage, in20.12 fixed point. Bounded to
//1024..4096 (25%..100%); default4096 preserves the exact original move factors.
void set_damage_scale_fx(int scale);
// Returns an ordinary idle/fall native state-function reference, to return from actor step.
KhRef cancel(KhRef actor);
// Called before actor retirement/Drive replacement. Does not dereference retired actors.
void forget(KhRef actor = 0);
// Unwraps callbacks. Call forget before freeing the actor's resources.
void detach();
// A controller's ActorSetState wrapper can use this to suppress legacy hold-at-low-HP start.
bool suppress_legacy_start(KhRef actor, int state);
}
