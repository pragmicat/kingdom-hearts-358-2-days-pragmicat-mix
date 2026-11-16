#pragma once
#include <kh/kh_actors.h>
#include <kh/kh_engine.h>

namespace rxrules {
// The caller validates story scene, pause/cutscene/input gates and normal Roxas.
using Permitted = bool (*)(KhRef actor);
enum class Reaction { Native, Launcher, Carry };
struct Snapshot {
    KhRef actor = 0;
    unsigned attack_serial = 0;
    unsigned confirmed_actions = 0;
    unsigned restored_charges = 0;
    unsigned lift_requests = 0;
    int last_restored_spell = -1;
    bool guard_granted = false;
    bool dodge_granted = false;
};

// No collision hooks are installed here. combat_motion owns the single native
// hit pipeline and calls the policy/listener below through its adapters.
bool attach(const KhEngine*, const KhActors*, Permitted);
void detach();
void bind(KhRef actor);
// Call on the actor's owning heap after original effect/render loading. Only
// absent base Guard/Dodge Roll are built, using their original native builders.
void refresh_mobility(KhRef actor);
// Call before teardown, while the actor is still live. A retired handle is
// forgotten without dereferencing its old address or a replacement actor.
void forget(KhRef actor = 0);
void attack_started(KhRef actor, unsigned serial, Reaction = Reaction::Native);
// Packet policy: immediately before the ONE original enemy ApplyHit call.
void before_native_hit(KhRef attacker, KhRef enemy, KhRef packet, void* = nullptr);
// Listener: ONLY combat_motion's confirmed actual native HP-loss notification.
void on_confirmed_melee_hit(KhRef attacker, KhActor target, unsigned serial,
                            unsigned frame, void* = nullptr);
// Call after original TranslateCommandToAction and after the last active melee
// contact. Uses only a genuine native command8/pending4 spell request. Returns
// the native state-function reference the caller must return from actor step.
KhRef try_magic_cancel(KhRef actor, bool contacts_finished);
Snapshot snapshot();
}
