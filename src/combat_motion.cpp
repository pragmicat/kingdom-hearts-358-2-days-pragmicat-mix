#include "combat_motion.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace rxmotion {
namespace {
const KhEngine* engine;
const KhActors* actors;
const KhInput* input;
Permitted permitted;
using SendHit = int (*)(KhRef, KhRef, KhRef, KhRef, KhRef);
using ApplyHit = int (*)(KhRef, KhRef, KhRef);
using ActorStep = KhRef (*)(KhRef);
using SetState = KhRef (*)(KhRef, int);
SendHit original_send;
ApplyHit original_hit;
ActorStep original_jump;
SetState set_state;
bool installed;
Snapshot state;
KhActor player_handle;
unsigned serial_counter;
unsigned request_until, chase_start;
KhActor request_target;
KhRef scoped_attacker;
KhRef scoped_target;
float target_at_hit[3]{}, previous_player[3]{};
float last_distance = 0;
unsigned stalled_steps;
bool have_previous;
struct Listener { HitObserver callback = nullptr; void* user = nullptr; };
std::array<Listener, 2> listeners{};
PacketPolicy packet_policy;
void* packet_policy_user;
// Every distinct target in a multi-hit attack can feed resources once. This is
// bookkeeping only: it never changes native contact hit-id lists or damage.
std::array<KhActor, 16> notified{};
unsigned notified_count;
std::array<KhActor, 16> stepped_targets{};
unsigned stepped_count, last_step_frame;
bool step_cooldown, was_grounded = true;

unsigned char* bytes(KhRef ref) {
    return engine && ref ? static_cast<unsigned char*>(engine->pointer(ref)) : nullptr;
}
template<class T> T get(KhRef ref, unsigned offset = 0) {
    T value{};
    if (auto* p = bytes(ref)) std::memcpy(&value, p + offset, sizeof value);
    return value;
}
template<class T> void put(KhRef ref, unsigned offset, T value) {
    if (auto* p = bytes(ref)) std::memcpy(p + offset, &value, sizeof value);
}
unsigned now() { return input ? input->frame() : 0; }
bool finite(const float p[3]) { return std::isfinite(p[0]) && std::isfinite(p[1]) && std::isfinite(p[2]); }
bool live(KhRef actor) {
    if (!installed || !actor || actor != state.actor ||
        !engine->module_loaded(22) || !bytes(actor)) return false;
    const KhActor current = actors->player(0);
    return current && current == player_handle && actors->ref(current) == actor;
}
bool valid(KhRef actor) { return live(actor) && permitted && permitted(actor); }
bool target_position(float at[3]) {
    if (!state.target || actors->kind(state.target) != KH_ACTOR_ENEMY || !actors->ref(state.target)) return false;
    int hp = 0, maximum = 0;
    return actors->hp(state.target, &hp, &maximum) == KH_OK && hp > 0 &&
           actors->position(state.target, at) == KH_OK && finite(at);
}
void stop_chase() {
    state.chasing = false;
    state.chase_queued = false;
    state.enemy_step_queued = false;
    have_previous = false;
    stalled_steps = 0;
    request_until = 0;
    request_target = 0;
}
void clear_hit_confirmation() {
    state.target = 0;
    state.hit_frame = 0;
    state.damage = 0;
    state.confirmed_hit = state.blocked = state.answered = state.launcher_confirmed = false;
    stop_chase();
}
void track_landing() {
    if (!state.actor || !bytes(state.actor)) return;
    const bool ground = (get<unsigned>(state.actor, 0x24) & 4u) != 0;
    if (ground && !was_grounded) { stepped_targets = {}; stepped_count = 0; }
    was_grounded = ground;
}
bool step_target_eligible(float from[3], float target[3], bool fresh_hit = true) {
    if (!valid(state.actor) || !state.confirmed_hit || state.group != 1 ||
        (get<unsigned>(state.actor, 0x24) & 4u) != 0 ||
        (fresh_hit && static_cast<unsigned>(now() - state.hit_frame) > 24) ||
        (step_cooldown && static_cast<unsigned>(now() - last_step_frame) < 20) ||
        !target_position(target) || actors->position(player_handle, from) != KH_OK || !finite(from)) return false;
    const KhRef enemy = actors->ref(state.target);
    // Ov107_AiState_IntegrateVelocity sets +0x17a bit0 when its downward
    // ground ray meets the floor. The neighboring bit2 is a general contact
    // flag and must not be mistaken for an airborne/grounded test.
    if (!enemy || !bytes(enemy) || (get<unsigned char>(enemy, 0x17a) & 1u)) return false;
    for (unsigned n = 0; n < stepped_count; ++n) if (stepped_targets[n] == state.target) return false;
    if (stepped_count >= stepped_targets.size()) return false;
    const float dx = target[0] - from[0], dz = target[2] - from[2], dy = target[1] - from[1];
    return dx * dx + dz * dz <= 1.8F * 1.8F && std::abs(dy) <= 1.8F;
}
void refresh_launch() {
    if (!state.launcher || !state.confirmed_hit || state.launcher_confirmed ||
        static_cast<unsigned>(now() - state.hit_frame) > 36) return;
    float target[3];
    if (target_position(target) && target[1] - target_at_hit[1] >= 0.15F)
        state.launcher_confirmed = true;
}
void notify_hit(KhActor target) {
    for (unsigned i = 0; i < notified_count; ++i) if (notified[i] == target) return;
    if (notified_count >= notified.size()) return;
    notified[notified_count++] = target;
    // Listeners may update gauges; they must not move/create enemies during this
    // native collision traversal. The SDK's own CustomEnemy example defers that.
    for (const auto& listener : listeners)
        if (listener.callback) listener.callback(state.actor, target, state.attack_serial, state.hit_frame, listener.user);
}

int apply_hit(KhRef enemy, KhRef source, KhRef packet) {
    // All normal Roxas repertoire records use mode0 sweeps. A previous spell
    // can share the same player source/seat while a new melee action whiffs;
    // source identity alone must not confirm that new action or grant a toss.
    const KhRef attacker = state.actor;
    const KhActor attacker_handle = player_handle;
    const unsigned attack_serial = state.attack_serial;
    const bool owned = valid(attacker) && attack_serial && packet && bytes(packet) &&
        scoped_attacker == attacker && scoped_target == enemy;
    const KhActor target = owned ? actors->of(enemy) : 0;
    const bool enemy_kind = target && actors->kind(target) == KH_ACTOR_ENEMY;
    int before = 0, maximum = 0;
    float at[3]{};
    const bool measured = enemy_kind && actors->hp(target, &before, &maximum) == KH_OK &&
                          actors->position(target, at) == KH_OK && finite(at);
    if (measured && packet_policy) packet_policy(attacker, enemy, packet, packet_policy_user);
    const int result = original_hit(enemy, source, packet);
    // Policy and native hit callbacks may interrupt the action, start another
    // attack or retire/rebind the actor. The outgoing hit must not credit that
    // replacement action, even when its raw actor address is unchanged.
    if (!measured || state.actor != attacker || player_handle != attacker_handle ||
        state.attack_serial != attack_serial || !valid(attacker)) return result;
    const auto outcome = get<std::uint16_t>(packet, 0x24);
    // Packet result bit0 is an enemy answer/counter; bit1 is a native guard.
    // An accepted return can still be either, or the artificial 200-flag whiff.
    const bool answered = (outcome & 1u) != 0;
    const bool blocked = (outcome & 2u) != 0;
    int after = 0;
    if (actors->hp(target, &after, &maximum) != KH_OK) {
        // An SDK handle retires immediately when defeated. The native hp field
        // is still readable within its ApplyHit call, before enemy traversal ends.
        after = get<std::int16_t>(enemy, 0x21a);
    }
    const int damage = std::max(0, before - std::max(0, after));
    state.answered = state.answered || answered;
    state.blocked = state.blocked || blocked;
    if (result && !answered && !blocked && damage > 0) {
        state.confirmed_hit = true;
        state.hit_frame = now();
        state.damage += damage;
        // Keep the first successful target for this attack; multi-target sweeps
        // must not randomly redirect the chase as later collision slots run.
        if (!state.target || actors->kind(state.target) != KH_ACTOR_ENEMY) {
            state.target = target;
            std::copy(at, at + 3, target_at_hit);
            state.launcher_confirmed = false;
        }
        notify_hit(target);
    }
    return result;
}
int send_hit(KhRef actor, KhRef spec, KhRef enemy, KhRef node, KhRef hit) {
    const KhRef saved_actor = scoped_attacker, saved_target = scoped_target;
    if (actor == state.actor && valid(actor)) { scoped_attacker = actor; scoped_target = enemy; }
    else { scoped_attacker = 0; scoped_target = 0; }
    const int result = original_send(actor, spec, enemy, node, hit);
    scoped_attacker = saved_actor;
    scoped_target = saved_target;
    return result;
}

KhRef jump(KhRef actor) {
    if (!state.chasing || actor != state.actor) return original_jump(actor);
    if (!valid(actor) || static_cast<unsigned>(now() - chase_start) > 42) {
        stop_chase();
        return original_jump(actor);
    }
    float from[3], target[3];
    if (actors->position(player_handle, from) != KH_OK || !finite(from) || !target_position(target)) {
        stop_chase();
        return original_jump(actor);
    }
    const float dx = target[0] - from[0], dz = target[2] - from[2], dy = target[1] - from[1];
    const float distance = std::sqrt(dx * dx + dz * dz);
    if (distance > 14.0F || std::abs(dy) > 7.0F ||
        (distance < 1.25F && std::abs(dy) < 1.5F) ||
        (input->pressed() & (KH_BUTTON_Y | KH_BUTTON_X | KH_BUTTON_A))) {
        stop_chase();
        return original_jump(actor);
    }
    if (have_previous) {
        const float moved_x = from[0] - previous_player[0], moved_z = from[2] - previous_player[2];
        if (moved_x * moved_x + moved_z * moved_z < 0.0009F && distance >= last_distance - 0.03F)
            ++stalled_steps;
        else stalled_steps = 0;
        if (stalled_steps >= 4) {
            // A wall or an unreachable target ends pursuit; no position writes,
            // collision disabling, or repeated teleports are used to get past it.
            stop_chase();
            return original_jump(actor);
        }
    }
    std::copy(from, from + 3, previous_player);
    last_distance = distance;
    have_previous = true;
    const int old_x = get<int>(actor, 0x498), old_z = get<int>(actor, 0x4a0);
    const KhRef result = original_jump(actor);
    if (result || !valid(actor)) { stop_chase(); return result; }
    const float travel = std::min(0.65F, std::max(0.0F, distance - 1.05F));
    const float inv = distance > 0.0001F ? 1.0F / distance : 0.0F;
    const int step_x = static_cast<int>(dx * inv * travel * 4096.0F);
    const int step_z = static_cast<int>(dz * inv * travel * 4096.0F);
    // Replace only this jump state's horizontal contribution. External forces
    // already present in vecStep survive, and ActorUpdate still calls Mover_StepActor.
    put<int>(actor, 0x498, old_x + step_x);
    put<int>(actor, 0x4a0, old_z + step_z);
    put<int>(actor, 0x698, step_x);
    put<int>(actor, 0x6a0, step_z);
    put<int>(actor, 0x58, static_cast<int>(std::clamp(dy * 0.20F, -0.22F, 0.65F) * 4096.0F));
    return 0;
}
template<class T> T routine(const char* name) { return reinterpret_cast<T>(engine->routine(name, nullptr)); }
}

bool attach(const KhEngine* e, const KhActors* a, const KhInput* i, Permitted allow) {
    if (installed || !e || !a || !i || !allow || e->size < sizeof(KhEngine) ||
        a->size < sizeof(KhActors) || i->size < sizeof(KhInput) ||
        !e->pointer || !e->routine || !e->replace || !e->module_loaded ||
        !a->player || !a->ref || !a->of || !a->kind || !a->hp || !a->position ||
        !i->frame || !i->pressed) return false;
    engine = e; actors = a; input = i; permitted = allow;
    original_send = routine<SendHit>("Ov022_SendSweepHit");
    original_hit = routine<ApplyHit>("Ov107_AiState_ApplyHit");
    original_jump = routine<ActorStep>("Ov022_StepJumpState");
    set_state = routine<SetState>("Ov022_ActorSetState");
    if (!original_send || !original_hit || !original_jump || !set_state) { engine = nullptr; return false; }
    void* previous = nullptr;
    if (e->replace("Ov022_SendSweepHit", reinterpret_cast<void*>(&send_hit), &previous) != KH_OK) { engine = nullptr; return false; }
    original_send = reinterpret_cast<SendHit>(previous);
    if (e->replace("Ov107_AiState_ApplyHit", reinterpret_cast<void*>(&apply_hit), &previous) != KH_OK) {
        e->replace("Ov022_SendSweepHit", reinterpret_cast<void*>(original_send), nullptr);
        engine = nullptr; return false;
    }
    original_hit = reinterpret_cast<ApplyHit>(previous);
    if (e->replace("Ov022_StepJumpState", reinterpret_cast<void*>(&jump), &previous) != KH_OK) {
        e->replace("Ov107_AiState_ApplyHit", reinterpret_cast<void*>(original_hit), nullptr);
        e->replace("Ov022_SendSweepHit", reinterpret_cast<void*>(original_send), nullptr);
        engine = nullptr; return false;
    }
    original_jump = reinterpret_cast<ActorStep>(previous);
    installed = true;
    reset();
    return true;
}
void detach() {
    reset();
    if (installed && engine) {
        engine->replace("Ov022_StepJumpState", reinterpret_cast<void*>(original_jump), nullptr);
        engine->replace("Ov107_AiState_ApplyHit", reinterpret_cast<void*>(original_hit), nullptr);
        engine->replace("Ov022_SendSweepHit", reinterpret_cast<void*>(original_send), nullptr);
    }
    installed = false; engine = nullptr; actors = nullptr; input = nullptr; permitted = nullptr;
    listeners = {};
    packet_policy = nullptr; packet_policy_user = nullptr;
}
void bind(KhRef actor) {
    if (actor == state.actor && actor && player_handle && actors && actors->ref(player_handle) == actor) return;
    reset();
    if (!installed || !actor) return;
    const KhActor current = actors->player(0);
    if (current && actors->ref(current) == actor) { state.actor = actor; player_handle = current; }
}
void attack_started(KhRef actor, int id, int group, bool launcher) {
    bind(actor);
    if (!valid(actor)) return;
    track_landing();
    stop_chase();
    const unsigned serial = ++serial_counter ? serial_counter : ++serial_counter;
    state = Snapshot{};
    state.actor = actor;
    state.attack_serial = serial;
    state.attack_id = id;
    state.group = group;
    state.launcher = launcher;
    notified = {}; notified_count = 0;
}
void state_changed(KhRef actor, int native_state) {
    if (actor == state.actor) track_landing();
    if (actor == state.actor && native_state == 1 && (get<unsigned>(actor, 0x24) & 4u)) {
        // A new native jump from the floor starts another route. Ordinary
        // airborne attacks and state2 transitions never replenish Enemy Step.
        stepped_targets = {}; stepped_count = 0;
    }
    // Match the controller's audited forced-action rule. A suspended attack may
    // resume, but damage/forced states must not attribute later contacts to it.
    // Ordinary melee entry14/15 and state22 retain their existing identity.
    if (actor == state.actor && native_state >= 13 && native_state <= 31 &&
        native_state != 14 && native_state != 15 && native_state != 22) {
        clear_hit_confirmation();
        state.attack_serial = 0;
        state.attack_id = -1;
        state.group = 0;
        state.launcher = false;
        notified = {}; notified_count = 0;
    }
    if (actor == state.actor && native_state != 1 && native_state != 2) stop_chase();
}
bool request_chase(KhRef actor) {
    if (!valid(actor) || !state.confirmed_hit || !state.launcher || !state.target ||
        (!state.chase_queued && static_cast<unsigned>(now() - state.hit_frame) > 30)) return false;
    float target[3];
    if (!target_position(target)) return false;
    if (state.chase_queued) {
        if (state.target != request_target || static_cast<int>(request_until - now()) < 0) {
            stop_chase(); return false;
        }
        return true; // Rechecking a buffered press never renews its deadline.
    }
    state.chase_queued = true;
    state.enemy_step_queued = false;
    request_until = now() + 18;
    request_target = state.target;
    return true;
}
bool request_enemy_step(KhRef actor) {
    if (actor != state.actor) return false;
    track_landing();
    const bool queued = state.enemy_step_queued;
    if (queued && (state.target != request_target || static_cast<int>(request_until - now()) < 0)) {
        stop_chase(); return false;
    }
    float from[3], target[3];
    if (!step_target_eligible(from, target, !queued)) return false;
    if (queued) return true;
    state.enemy_step_queued = true;
    state.chase_queued = false;
    request_until = now() + 12;
    request_target = state.target;
    return true;
}
KhRef step(KhRef actor) {
    if (actor != state.actor || (!state.chase_queued && !state.enemy_step_queued)) return 0;
    if (!valid(actor) || state.target != request_target ||
        static_cast<int>(request_until - now()) < 0) { stop_chase(); return 0; }
    track_landing();
    if (state.enemy_step_queued) {
        float from[3], target[3];
        // Hit freshness accepted this input; recovery consumes its fixed queue
        // window. All current geometry, lifetime and spending checks still apply.
        if (!step_target_eligible(from, target, false)) { stop_chase(); return 0; }
        const KhActor used_target = state.target;
        const KhRef result = set_state(actor, 2);
        if (!result) { stop_chase(); return 0; }
        put<signed char>(actor, 0x2bb2, 1);
        put<unsigned char>(actor, 0x2bb0, 0);
        put<int>(actor, 0x2bb4, 0);
        put<int>(actor, 0x2bc0, 0);
        put<int>(actor, 0x58, 2048); // ordinary half-unit rising impulse
        stepped_targets[stepped_count++] = used_target;
        last_step_frame = now(); step_cooldown = true;
        stop_chase();
        was_grounded = false;
        return result;
    }
    refresh_launch();
    if (!state.launcher_confirmed) return 0;
    float from[3], target[3];
    if (!target_position(target) || actors->position(player_handle, from) != KH_OK || !finite(from)) { stop_chase(); return 0; }
    const float dx = target[0] - from[0], dz = target[2] - from[2], dy = target[1] - from[1];
    if (dx * dx + dz * dz > 144.0F || dy < -1.5F || dy > 6.0F) { stop_chase(); return 0; }
    // State2 builds the game's ordinary falling/jump controller through the
    // actor's own pre-draw hook, preserving each Roxas overlay. Change its jump
    // phase to rising after initialization; physics and collision still run.
    const KhRef result = set_state(actor, 2);
    if (!result) { stop_chase(); return 0; }
    put<signed char>(actor, 0x2bb2, 1);
    put<int>(actor, 0x2bb4, 0);
    put<int>(actor, 0x58, static_cast<int>(std::clamp(dy * 0.20F, 0.25F, 0.65F) * 4096.0F));
    state.chasing = true;
    state.chase_queued = false;
    chase_start = now();
    have_previous = false;
    stalled_steps = 0;
    return result;
}
Snapshot snapshot() {
    if (state.actor && !live(state.actor)) { reset(); return state; }
    track_landing();
    if (state.actor && (!permitted || !permitted(state.actor))) {
        // Suspension drops stale movement/hit intent, not the native attack
        // still running after resume. Preserve its identity and per-target
        // observer ledger so new contacts confirm without duplicate credit.
        // Spent Enemy Step targets still last until landing or actor retirement.
        clear_hit_confirmation();
        return state;
    }
    refresh_launch();
    return state;
}
void observe_hit(HitObserver callback, void* user) { listeners[0] = {callback, user}; }
void observe_hit_secondary(HitObserver callback, void* user) { listeners[1] = {callback, user}; }
void set_packet_policy(PacketPolicy policy, void* user) { packet_policy = policy; packet_policy_user = user; }
void reset() {
    state = Snapshot{};
    player_handle = 0;
    scoped_attacker = scoped_target = 0;
    notified = {}; notified_count = 0;
    stepped_targets = {}; stepped_count = 0; last_step_frame = 0;
    step_cooldown = false; was_grounded = true;
    stop_chase();
}
}
