#pragma once
#include <kh/kh_actors.h>
#include <kh/kh_engine.h>
#include <kh/kh_input.h>

namespace rxmotion {
using Permitted = bool (*)(KhRef actor);
using HitObserver = void (*)(KhRef attacker, KhActor target, unsigned attack_serial,
                             unsigned frame, void* user);
using PacketPolicy = void (*)(KhRef attacker, KhRef enemy, KhRef packet, void* user);

struct Snapshot {
    KhRef actor = 0;
    KhActor intended_target = 0; // native pick resolved when this attack began
    KhActor target = 0; // confirmed contact for pursuit or Enemy Step
    unsigned attack_serial = 0;
    unsigned hit_frame = 0;
    int attack_id = -1;
    int group = 0;
    int damage = 0;
    bool confirmed_hit = false; // actual native HP loss, never a wall/guard/counter
    bool blocked = false;
    bool answered = false;
    bool launcher = false;
    bool launcher_confirmed = false; // target actually rose after the accepted hit
    bool chase_queued = false;
    bool chasing = false;
    bool enemy_step_queued = false;
};

bool attach(const KhEngine*, const KhActors*, const KhInput*, Permitted);
void detach();
void bind(KhRef actor);
void attack_started(KhRef actor, int attack_id, int group, bool launcher = false, KhActor intended_target = 0);
void state_changed(KhRef actor, int native_state);
bool request_chase(KhRef actor);
bool request_enemy_step(KhRef actor);
// Call only where the caller can leave its current action safely. Returns the
// native state routine reference to return from that action, not a state number.
KhRef step(KhRef actor);
Snapshot snapshot();
// Two independent listeners allow Drive and resource rules to share one hit hook.
void observe_hit(HitObserver, void* user = nullptr);
void observe_hit_secondary(HitObserver, void* user = nullptr);
void set_packet_policy(PacketPolicy, void* user = nullptr);
void reset();
}
