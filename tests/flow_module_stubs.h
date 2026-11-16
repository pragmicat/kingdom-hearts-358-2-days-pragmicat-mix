#pragma once
#include "../src/combat_motion.h"
#include "../src/combat_rules.h"
#include "../src/drive_form.h"
#include "../src/animation_links.h"
#include "../src/drive_hud.h"
#include "../src/practice_boot.h"
#include "../src/weapon_animation.h"

// Controller tests keep companion systems observable but separate. Their real
// native hooks and physics/actor replacement have independent executable suites.
namespace flow_stub {
inline rxmotion::Snapshot motion;
inline rxmotion::HitObserver primary_hit, secondary_hit;
inline rxmotion::PacketPolicy packet_policy;
inline void *primary_ctx, *secondary_ctx, *policy_ctx;
inline rxdrive::View drive;
inline rxdrive::Settings drive_config;
inline rxdrive::Callbacks drive_callbacks;
inline rxdrive::FrameContext drive_frame;
inline rxrules::Snapshot rules;
inline rxrules::Reaction reaction;
inline unsigned motion_binds, motion_resets, motion_starts, motion_state_changes;
inline unsigned chase_requests, step_requests, motion_steps, serial;
inline unsigned rules_binds, rules_refreshes, rules_forgets, rules_starts, rules_hits, policy_calls;
inline unsigned anim_links, anim_resets, drive_frames, drive_requests, drive_hits, hud_updates;
inline KhRef native_motion_return, native_magic_return;
inline void (*during_motion_step)();
inline void (*during_magic_request)();
inline bool request_allowed, magic_contacts_finished, replacing;
inline bool motion_queue_rejected;
inline bool hud_running, hud_visible;
inline bool resources_settled;
inline bool practice_active;
inline unsigned practice_primes;
inline int anim_from, anim_to, anim_from_group, anim_to_group;
inline int motion_last_state;
inline void reset() {
    motion = {}; primary_hit = secondary_hit = nullptr; packet_policy = nullptr;
    primary_ctx = secondary_ctx = policy_ctx = nullptr;
    drive = {}; drive.available = true; drive_callbacks = {}; drive_frame = {};
    drive_config = {};
    rules = {}; reaction = rxrules::Reaction::Native;
    motion_binds = motion_resets = motion_starts = motion_state_changes = 0;
    chase_requests = step_requests = motion_steps = serial = 0;
    rules_binds = rules_refreshes = rules_forgets = rules_starts = rules_hits = policy_calls = 0;
    anim_links = anim_resets = drive_frames = drive_requests = drive_hits = hud_updates = 0;
    native_motion_return = native_magic_return = 0; request_allowed = true;
    during_motion_step = nullptr;
    during_magic_request = nullptr;
    motion_queue_rejected = false;
    magic_contacts_finished = replacing = false;
    hud_running = hud_visible = false;
    resources_settled = true;
    practice_active = false; practice_primes = 0;
    anim_from = anim_to = anim_from_group = anim_to_group = motion_last_state = -1;
}
inline void confirm(KhRef actor, int attack, unsigned tick, KhActor target = 9001) {
    motion.actor = actor; motion.attack_id = attack; motion.confirmed_hit = true;
    motion.target = target; motion.hit_frame = tick;
    if (primary_hit) primary_hit(actor, target, motion.attack_serial, tick, primary_ctx);
    if (secondary_hit) secondary_hit(actor, target, motion.attack_serial, tick, secondary_ctx);
}
}
namespace rxmotion {
bool attach(const KhEngine*, const KhActors*, const KhInput*, Permitted) { return true; }
void detach() { flow_stub::motion = {}; }
void bind(KhRef actor) { ++flow_stub::motion_binds; flow_stub::motion.actor = actor; }
void attack_started(KhRef actor, int id, int group, bool launcher) {
    ++flow_stub::motion_starts; flow_stub::motion = {};
    flow_stub::motion.actor = actor; flow_stub::motion.attack_id = id;
    flow_stub::motion.group = group; flow_stub::motion.launcher = launcher;
    flow_stub::motion.attack_serial = ++flow_stub::serial;
}
void state_changed(KhRef actor, int state) {
    ++flow_stub::motion_state_changes; flow_stub::motion_last_state = state;
    // Production retains pending motion only through ordinary jump/fall states.
    if(actor==flow_stub::motion.actor&&state!=1&&state!=2)
        flow_stub::motion.chase_queued=flow_stub::motion.enemy_step_queued=flow_stub::motion.chasing=false;
}
bool request_chase(KhRef) {
    ++flow_stub::chase_requests;
    if (!flow_stub::request_allowed || !flow_stub::motion.confirmed_hit) return false;
    flow_stub::motion.chase_queued = true; flow_stub::motion.enemy_step_queued = false;
    return true;
}
bool request_enemy_step(KhRef) {
    ++flow_stub::step_requests;
    if (!flow_stub::request_allowed || !flow_stub::motion.confirmed_hit) return false;
    flow_stub::motion.enemy_step_queued = true; flow_stub::motion.chase_queued = false;
    return true;
}
KhRef step(KhRef) {
    ++flow_stub::motion_steps;
    // Real motion keeps a waiting queue, but retires it after a transition or
    // rejection/expiry. Its independent suite verifies those native conditions.
    if (flow_stub::native_motion_return || flow_stub::motion_queue_rejected)
        flow_stub::motion.chase_queued = flow_stub::motion.enemy_step_queued = false;
    if (flow_stub::during_motion_step) flow_stub::during_motion_step();
    return flow_stub::native_motion_return;
}
Snapshot snapshot() { return flow_stub::motion; }
void observe_hit(HitObserver callback, void* context) { flow_stub::primary_hit = callback; flow_stub::primary_ctx = context; }
void observe_hit_secondary(HitObserver callback, void* context) { flow_stub::secondary_hit = callback; flow_stub::secondary_ctx = context; }
void set_packet_policy(PacketPolicy callback, void* context) { flow_stub::packet_policy = callback; flow_stub::policy_ctx = context; }
void reset() { ++flow_stub::motion_resets; flow_stub::motion = {}; }
}
namespace rxrules {
bool attach(const KhEngine*, const KhActors*, Permitted) { return true; }
void detach() { flow_stub::rules = {}; }
void bind(KhRef actor) { ++flow_stub::rules_binds; flow_stub::rules.actor = actor; }
void refresh_mobility(KhRef) { ++flow_stub::rules_refreshes; }
void forget(KhRef) { ++flow_stub::rules_forgets; flow_stub::rules = {}; }
void attack_started(KhRef actor, unsigned serial, Reaction reaction) {
    ++flow_stub::rules_starts; flow_stub::rules.actor = actor;
    flow_stub::rules.attack_serial = serial; flow_stub::reaction = reaction;
}
void before_native_hit(KhRef, KhRef, KhRef, void*) { ++flow_stub::policy_calls; }
void on_confirmed_melee_hit(KhRef, KhActor, unsigned, unsigned, void*) { ++flow_stub::rules_hits; }
KhRef try_magic_cancel(KhRef, bool finished) {
    flow_stub::magic_contacts_finished = finished;
    if (finished && flow_stub::during_magic_request) flow_stub::during_magic_request();
    return finished ? flow_stub::native_magic_return : 0;
}
Snapshot snapshot() { return flow_stub::rules; }
}
namespace rxanim {
bool attach(const KhEngine*, Permitted) { return true; }
void transition(KhRef, int from, int to, int from_group, int to_group, int, int) {
    ++flow_stub::anim_links; flow_stub::anim_from = from; flow_stub::anim_to = to;
    flow_stub::anim_from_group = from_group; flow_stub::anim_to_group = to_group;
}
void reset(KhRef) { ++flow_stub::anim_resets; }
void detach() {}
}
namespace rxdrive {
bool init(const KhHost*, const KhEngine*, const KhActors*, const KhNet*, Callbacks callbacks, Settings settings) {
    flow_stub::drive_config = settings;
    flow_stub::drive_callbacks = callbacks; return true;
}
void on_hit(KhRef, KhActor, unsigned, unsigned, void*) { ++flow_stub::drive_hits; }
bool request_activation() { ++flow_stub::drive_requests; return true; }
void frame(const FrameContext& context) { ++flow_stub::drive_frames; flow_stub::drive_frame = context; }
View view() { return flow_stub::drive; }
bool is_active() { return flow_stub::drive.phase == Phase::Active || flow_stub::drive.phase == Phase::RestorationQueued; }
bool is_replacing() { return flow_stub::replacing; }
bool ready_for_safe_point(KhRef actor) {
    return actor && flow_stub::resources_settled &&
        (flow_stub::drive.phase == Phase::ActivationQueued || flow_stub::drive.phase == Phase::RestorationQueued);
}
bool prime_practice_meter(KhRef actor) { ++flow_stub::practice_primes; return actor != 0; }
void reset() { flow_stub::drive = {}; flow_stub::drive.available = true; }
void stop() { flow_stub::drive = {}; }
}
namespace rxhud {
bool start(bool enabled) noexcept { flow_stub::hud_running = enabled; return true; }
void update(const rxdrive::View&, bool visible) noexcept { ++flow_stub::hud_updates; flow_stub::hud_visible = visible; }
void set_enabled(bool enabled) noexcept { flow_stub::hud_running = enabled; }
void stop() noexcept { flow_stub::hud_running = false; }
}
namespace rxpractice {
bool init(const KhHost*, const KhEngine*, const KhActors*, const KhNet*) { return true; }
bool enabled() { return flow_stub::practice_active; }
bool active() { return flow_stub::practice_active; }
void stop() { flow_stub::practice_active = false; }
}
namespace rxweapon {
bool init(const KhEngine*, const KhActors*, const KhNet*, Permitted, Log, void*) { return true; }
void bind(KhRef) {}
void forget(KhRef) {}
void stop() {}
}
