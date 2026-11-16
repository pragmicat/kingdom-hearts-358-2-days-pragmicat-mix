#include "../src/combat_motion.cpp"
#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>

namespace motion_test {
void check(bool yes, const char* message) { if (!yes) throw std::runtime_error(message); }
#define CHECK(x) ::motion_test::check((x), #x)
std::array<unsigned char, 0x18000> memory{};
constexpr KhRef actor = 0x1000, other = 0x5000, enemy = 0x9000, enemy2 = 0xa000;
constexpr KhRef packet = 0xb000, source = 0xc000;
constexpr KhActor player_id = 101, target_id = 201, target2_id = 202;
KhEngine api{}; KhActors actor_api{}; KhInput input_api{};
std::map<std::string, void*> funcs;
unsigned frame_number, buttons, original_hits, original_sends, original_jumps, states;
unsigned notifications[2], policy_calls;
bool allowed, loaded, retired_player, retired_enemy, retired_enemy2;
int native_damage, native_result, native_outcome;
KhRef jump_result;
std::function<void()> during_native_hit, during_state_change, during_native_jump;
int last_state;
std::string absent, fail;
float player_pos[3], target_pos[3], target2_pos[3];
template<class T> void put(KhRef ref, unsigned offset, T value) { std::memcpy(memory.data() + ref + offset, &value, sizeof value); }
template<class T> T get(KhRef ref, unsigned offset = 0) { T value{}; std::memcpy(&value, memory.data() + ref + offset, sizeof value); return value; }
void* pointer(KhRef ref) { return ref && ref < memory.size() ? memory.data() + ref : nullptr; }
int module(int n) { return loaded && (n == 22 || n == 107); }
unsigned tick() { return frame_number; }
unsigned pressed() { return buttons; }
bool permitted(KhRef ref) { return allowed && ref == actor; }
KhActor player(int seat) { return seat == 0 && !retired_player ? player_id : 0; }
KhRef ref(KhActor id) {
    if (id == player_id && !retired_player) return actor;
    if (id == target_id && !retired_enemy) return enemy;
    if (id == target2_id && !retired_enemy2) return enemy2;
    return 0;
}
KhActor of(KhRef value) {
    if (value == actor && !retired_player) return player_id;
    if (value == enemy && !retired_enemy) return target_id;
    if (value == enemy2 && !retired_enemy2) return target2_id;
    return 0;
}
int kind(KhActor id) { return !ref(id) ? KH_ACTOR_NONE : id == player_id ? KH_ACTOR_PLAYER : KH_ACTOR_ENEMY; }
KhResult position(KhActor id, float* at) {
    if (!ref(id)) return KH_ERROR;
    const float* from = id == player_id ? player_pos : id == target_id ? target_pos : target2_pos;
    std::copy(from, from + 3, at); return KH_OK;
}
KhResult hp(KhActor id, int* now, int* most) {
    const KhRef object = ref(id);
    if (!object) return KH_ERROR;
    if (now) *now = id == player_id ? get<unsigned short>(object, 0x12) : get<short>(object, 0x21a);
    if (most) *most = 100;
    return KH_OK;
}
int native_hit(KhRef target, KhRef, KhRef hit) {
    ++original_hits;
    put(hit, 0x24, static_cast<unsigned short>(native_outcome));
    if (native_result && native_damage > 0) {
        const int remaining = std::max(0, static_cast<int>(get<short>(target, 0x21a)) - native_damage);
        put(target, 0x21a, static_cast<short>(remaining));
        if (!remaining) { if (target == enemy) retired_enemy = true; else retired_enemy2 = true; }
    }
    if (during_native_hit) during_native_hit();
    return native_result;
}
int native_send(KhRef striker, KhRef, KhRef target, KhRef, KhRef) {
    ++original_sends;
    put(packet, 0x14, static_cast<unsigned short>(get<unsigned char>(striker, 9)));
    const auto apply = reinterpret_cast<int (*)(KhRef, KhRef, KhRef)>(funcs.at("Ov107_AiState_ApplyHit"));
    return apply(target, get<KhRef>(striker, 0x4ec), packet);
}
KhRef native_jump(KhRef object) {
    ++original_jumps;
    put(object, 0x498, get<int>(object, 0x498) + 100);
    put(object, 0x4a0, get<int>(object, 0x4a0) + 200);
    if (during_native_jump) during_native_jump();
    return jump_result;
}
KhRef set_state(KhRef object, int state) {
    CHECK(object == actor); ++states; last_state = state;
    put(object, 0x2bb2, static_cast<signed char>(2));
    if (during_state_change) during_state_change();
    return 0x02000000u + 4u * static_cast<unsigned>(state);
}
void* routine(const char* name, int*) { return absent == name || !funcs.count(name) ? nullptr : funcs.at(name); }
KhResult replace(const char* name, void* replacement, void** previous) {
    if (fail == name || !funcs.count(name)) return KH_ERROR;
    if (previous) *previous = funcs.at(name);
    funcs[name] = replacement; return KH_OK;
}
void observe(KhRef striker, KhActor target, unsigned serial, unsigned frame, void* listener) {
    CHECK(striker == actor); CHECK(target == target_id || target == target2_id); CHECK(serial > 0); CHECK(frame == frame_number);
    ++notifications[static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(listener))];
}
void policy(KhRef striker, KhRef target, KhRef hit, void*) {
    CHECK(striker == actor); CHECK(target == enemy || target == enemy2); CHECK(hit == packet);
    ++policy_calls; put(hit, 2, static_cast<unsigned short>(get<unsigned short>(hit, 2) | 4u));
}
void reset() {
    rxmotion::detach(); memory.fill(0); funcs.clear(); frame_number = 100; buttons = 0;
    original_hits = original_sends = original_jumps = states = policy_calls = 0;
    notifications[0] = notifications[1] = 0;
    allowed = loaded = true; retired_player = retired_enemy = retired_enemy2 = false;
    native_damage = 10; native_result = 1; native_outcome = 0; jump_result = 0; last_state = -1;
    absent.clear(); fail.clear(); during_native_hit = {}; during_state_change = {}; during_native_jump = {};
    std::fill(player_pos, player_pos + 3, 0.0F);
    target_pos[0] = 5; target_pos[1] = 0; target_pos[2] = 0;
    target2_pos[0] = 5; target2_pos[1] = 0; target2_pos[2] = 1;
    api = {}; api.size = sizeof(api); api.pointer = &pointer; api.routine = &routine;
    api.replace = &replace; api.module_loaded = &module; api.frame = &tick;
    actor_api = {}; actor_api.size = sizeof(actor_api); actor_api.player = &player; actor_api.ref = &ref;
    actor_api.of = &of; actor_api.kind = &kind; actor_api.hp = &hp; actor_api.position = &position;
    input_api = {}; input_api.size = sizeof(input_api); input_api.frame = &tick; input_api.pressed = &pressed;
    funcs = {{"Ov022_SendSweepHit", reinterpret_cast<void*>(&native_send)},
             {"Ov107_AiState_ApplyHit", reinterpret_cast<void*>(&native_hit)},
             {"Ov022_StepJumpState", reinterpret_cast<void*>(&native_jump)},
             {"Ov022_ActorSetState", reinterpret_cast<void*>(&set_state)}};
    put(actor, 0x12, static_cast<unsigned short>(100)); put(actor, 9, static_cast<unsigned char>(0));
    put(actor, 0x4ec, source); put(other, 0x4ec, source + 0x100); put(other, 9, static_cast<unsigned char>(1));
    put(enemy, 0x21a, static_cast<short>(100)); put(enemy2, 0x21a, static_cast<short>(100));
}
void attach() { CHECK(rxmotion::attach(&api, &actor_api, &input_api, &permitted)); rxmotion::bind(actor); }
int hit(KhRef striker = actor, KhRef target = enemy) {
    return reinterpret_cast<int (*)(KhRef, KhRef, KhRef, KhRef, KhRef)>(funcs.at("Ov022_SendSweepHit"))(striker, 0, target, 0, 0);
}
KhRef jump() { return reinterpret_cast<KhRef (*)(KhRef)>(funcs.at("Ov022_StepJumpState"))(actor); }
void prerequisites_and_hook_rollback() {
    reset(); absent = "Ov022_StepJumpState"; CHECK(!rxmotion::attach(&api, &actor_api, &input_api, &permitted));
    CHECK(funcs.at("Ov022_SendSweepHit") == reinterpret_cast<void*>(&native_send));
    reset(); fail = "Ov022_StepJumpState"; CHECK(!rxmotion::attach(&api, &actor_api, &input_api, &permitted));
    CHECK(funcs.at("Ov022_SendSweepHit") == reinterpret_cast<void*>(&native_send));
    CHECK(funcs.at("Ov107_AiState_ApplyHit") == reinterpret_cast<void*>(&native_hit));
    reset(); attach(); rxmotion::detach();
    CHECK(funcs.at("Ov022_SendSweepHit") == reinterpret_cast<void*>(&native_send));
    CHECK(funcs.at("Ov107_AiState_ApplyHit") == reinterpret_cast<void*>(&native_hit));
    CHECK(funcs.at("Ov022_StepJumpState") == reinterpret_cast<void*>(&native_jump));
}
void actual_damage_is_correlated_to_attacker_and_one_observer_per_target() {
    reset(); attach(); rxmotion::observe_hit(&observe); rxmotion::observe_hit_secondary(&observe, reinterpret_cast<void*>(1));
    rxmotion::attack_started(actor, 11080, 0, true);
    CHECK(hit(other) == 1); CHECK(!rxmotion::snapshot().confirmed_hit); CHECK(notifications[0] == 0);
    CHECK(hit() == 1); auto s = rxmotion::snapshot(); CHECK(s.confirmed_hit); CHECK(s.damage == 10); CHECK(s.target == target_id);
    CHECK(s.launcher); CHECK(!s.launcher_confirmed); CHECK(notifications[0] == 1 && notifications[1] == 1);
    hit(); CHECK(rxmotion::snapshot().damage == 20); CHECK(notifications[0] == 1);
    hit(actor, enemy2); CHECK(notifications[0] == 2); CHECK(rxmotion::snapshot().target == target_id);
    const unsigned serial = s.attack_serial; rxmotion::attack_started(actor, 3, 1);
    CHECK(rxmotion::snapshot().attack_serial > serial); CHECK(!rxmotion::snapshot().confirmed_hit);
    hit(); CHECK(notifications[0] == 3);
}
void blocked_answered_rejected_and_zero_damage_do_not_confirm() {
    for (int outcome : {0, 1, 2}) {
        reset(); attach(); rxmotion::attack_started(actor, 11080, 0, true);
        native_outcome = outcome; native_damage = outcome ? 10 : 0; hit();
        const auto s = rxmotion::snapshot(); CHECK(!s.confirmed_hit); CHECK(!rxmotion::request_chase(actor));
        CHECK(s.answered == (outcome == 1)); CHECK(s.blocked == (outcome == 2));
    }
    reset(); attach(); rxmotion::attack_started(actor, 11080, 0, true); native_result = 0; hit();
    CHECK(!rxmotion::snapshot().confirmed_hit); CHECK(original_hits == 1);
}
void packet_policy_and_own_non_sweep_hits_share_the_native_call() {
    reset(); attach(); rxmotion::attack_started(actor, 11080, 0, true); rxmotion::set_packet_policy(&policy);
    hit(other); CHECK(policy_calls == 0); hit(); CHECK(policy_calls == 1); CHECK(get<unsigned short>(packet, 2) & 4u);
    rxmotion::attack_started(actor, 3, 1); put(packet, 0x14, static_cast<unsigned short>(0));
    auto apply = reinterpret_cast<int (*)(KhRef, KhRef, KhRef)>(funcs.at("Ov107_AiState_ApplyHit"));
    CHECK(apply(enemy, source + 1, packet)); CHECK(!rxmotion::snapshot().confirmed_hit);
    CHECK(apply(enemy, source, packet)); CHECK(!rxmotion::snapshot().confirmed_hit); CHECK(policy_calls == 1);
    CHECK(!rxmotion::request_enemy_step(actor));
    // A flying spell or counter with the same seat/source cannot become this
    // melee action's hit-confirm, launcher reaction or resource credit.
}
void killing_blow_counts_without_reusing_a_retired_target() {
    reset(); attach(); rxmotion::observe_hit(&observe); rxmotion::attack_started(actor, 11080, 0, true);
    native_damage = 120; hit(); const auto s = rxmotion::snapshot();
    CHECK(s.confirmed_hit); CHECK(s.damage == 100); CHECK(notifications[0] == 1); CHECK(retired_enemy);
    CHECK(!rxmotion::request_chase(actor)); CHECK(states == 0);
}
void launcher_requires_real_rise_and_request_expires() {
    reset(); attach(); rxmotion::attack_started(actor, 11080, 0, true); hit();
    CHECK(rxmotion::request_chase(actor)); CHECK(rxmotion::step(actor) == 0); CHECK(states == 0);
    frame_number += 19; target_pos[1] = 2; CHECK(rxmotion::step(actor) == 0); CHECK(!rxmotion::snapshot().chase_queued);
    reset(); attach(); rxmotion::attack_started(actor, 0, 0, false); hit(); target_pos[1] = 2;
    CHECK(!rxmotion::request_chase(actor)); CHECK(states == 0);
}
void chase_uses_native_state_reference_and_collision_step_without_position_writes() {
    reset(); attach(); rxmotion::attack_started(actor, 11080, 0, true); hit();
    CHECK(rxmotion::request_chase(actor)); target_pos[1] = 2;
    CHECK(rxmotion::step(actor) == 0x02000008u); CHECK(last_state == 2); CHECK(states == 1);
    CHECK(rxmotion::snapshot().chasing); CHECK(get<signed char>(actor, 0x2bb2) == 1); CHECK(get<int>(actor, 0x58) > 0);
    put(actor, 0x498, 1234); put(actor, 0x4a0, 5678);
    const auto original_memory = memory;
    CHECK(jump() == 0); CHECK(original_jumps == 1);
    CHECK(get<int>(actor, 0x498) == 1234 + static_cast<int>(0.65F * 4096)); CHECK(get<int>(actor, 0x4a0) == 5678);
    CHECK(player_pos[0] == 0 && player_pos[1] == 0 && player_pos[2] == 0);
    CHECK(std::memcmp(original_memory.data() + actor + 0x48c, memory.data() + actor + 0x48c, 12) == 0);
    CHECK(std::memcmp(original_memory.data() + enemy + 0x74, memory.data() + enemy + 0x74, 12) == 0);
    player_pos[0] = 4.3F; player_pos[1] = 1.5F; frame_number += 2;
    CHECK(jump() == 0); CHECK(!rxmotion::snapshot().chasing); CHECK(original_jumps == 2);
}
void collision_stall_timeout_and_attack_interrupt_end_chase() {
    auto begin = [] { reset(); attach(); rxmotion::attack_started(actor, 11080, 0, true); hit(); CHECK(rxmotion::request_chase(actor)); target_pos[1] = 2; CHECK(rxmotion::step(actor)); };
    begin(); for (int i = 0; i < 5; ++i) { frame_number += 2; jump(); }
    CHECK(!rxmotion::snapshot().chasing); CHECK(original_jumps == 5); // no movement through a mocked wall
    begin(); frame_number += 43; jump(); CHECK(!rxmotion::snapshot().chasing);
    begin(); buttons = KH_BUTTON_A; jump(); CHECK(!rxmotion::snapshot().chasing);
    begin(); rxmotion::state_changed(actor, 20); CHECK(!rxmotion::snapshot().chasing);
    begin(); rxmotion::attack_started(actor, 3, 1); CHECK(!rxmotion::snapshot().chasing);
}
void lifetime_distance_and_native_transition_guards() {
    auto begin = [] { reset(); attach(); rxmotion::attack_started(actor, 11080, 0, true); hit(); CHECK(rxmotion::request_chase(actor)); target_pos[1] = 2; };
    begin(); target_pos[0] = 30; CHECK(!rxmotion::step(actor)); CHECK(states == 0);
    begin(); retired_enemy = true; CHECK(!rxmotion::step(actor)); CHECK(states == 0);
    begin(); retired_player = true; CHECK(!rxmotion::step(actor)); CHECK(rxmotion::snapshot().actor == 0);
    begin(); CHECK(rxmotion::step(actor)); jump_result = 0x0200abcd; CHECK(jump() == jump_result); CHECK(!rxmotion::snapshot().chasing);
    begin(); CHECK(rxmotion::step(actor)); allowed = false; jump();
    CHECK(rxmotion::snapshot().actor == actor); CHECK(!rxmotion::snapshot().confirmed_hit);
    CHECK(!rxmotion::snapshot().chasing);
    begin(); rxmotion::reset(); CHECK(!rxmotion::step(actor)); CHECK(rxmotion::snapshot().actor == 0);
}
void prepare_air_step(KhRef target = enemy) {
    put(actor, 0x24, 0u); player_pos[1] = 2;
    target_pos[0] = target2_pos[0] = 1; target_pos[1] = target2_pos[1] = 2; target2_pos[2] = 0;
    rxmotion::attack_started(actor, 3, 1); hit(actor, target);
}
void enemy_step_is_native_mobility_with_no_extra_hit_or_camera_change() {
    reset(); attach(); prepare_air_step(); put(actor, 0x2bb0, static_cast<unsigned char>(2));
    const int old_hp = get<short>(enemy, 0x21a); const unsigned old_hits = original_hits;
    const auto before = memory;
    CHECK(rxmotion::request_enemy_step(actor)); CHECK(rxmotion::snapshot().enemy_step_queued);
    CHECK(rxmotion::step(actor) == 0x02000008u); CHECK(last_state == 2);
    CHECK(get<unsigned char>(actor, 0x2bb0) == 0); CHECK(get<signed char>(actor, 0x2bb2) == 1);
    CHECK(get<int>(actor, 0x58) == 2048); CHECK(!rxmotion::snapshot().enemy_step_queued);
    CHECK(original_hits == old_hits); CHECK(get<short>(enemy, 0x21a) == old_hp);
    for (unsigned i = 0; i < memory.size(); ++i) {
        const bool motion_field = (i >= actor + 0x58 && i < actor + 0x5c) ||
            i == actor + 0x2bb0 || i == actor + 0x2bb2 ||
            (i >= actor + 0x2bb4 && i < actor + 0x2bb8) || (i >= actor + 0x2bc0 && i < actor + 0x2bc4);
        CHECK(motion_field || memory[i] == before[i]);
    }
}
void interrupted_native_transitions_keep_the_replacement_motion() {
    for (bool chase : {false, true}) for (int interruption = 0; interruption < 5; ++interruption) {
        reset(); attach();
        if (chase) {
            rxmotion::attack_started(actor, 11080, 0, true); hit(); target_pos[1] = 2;
            CHECK(rxmotion::request_chase(actor));
        } else {
            prepare_air_step(); CHECK(rxmotion::request_enemy_step(actor));
        }
        during_state_change = [interruption] {
            if (interruption == 0) rxmotion::state_changed(actor, 20);
            if (interruption == 1) rxmotion::attack_started(actor, 5, 1);
            if (interruption == 2) retired_player = true;
            if (interruption == 3) allowed = false;
            if (interruption == 4) rxmotion::reset();
            put(actor, 0x58, 777); put(actor, 0x2bb2, static_cast<signed char>(7));
        };
        CHECK(rxmotion::step(actor) == 0x02000008u); // keep the native return
        CHECK(get<int>(actor, 0x58) == 777 && get<signed char>(actor, 0x2bb2) == 7);
        const auto s = rxmotion::snapshot();
        CHECK(!s.chasing && !s.chase_queued && !s.enemy_step_queued);
        if (interruption == 1) CHECK(s.attack_id == 5);
    }
}
void interrupted_native_jump_preserves_replacement_velocity() {
    for (int interruption = 0; interruption < 3; ++interruption) {
        reset(); attach(); rxmotion::attack_started(actor, 11080, 0, true); hit();
        target_pos[1] = 2; CHECK(rxmotion::request_chase(actor)); CHECK(rxmotion::step(actor));
        during_native_jump = [interruption] {
            if (interruption == 0) rxmotion::state_changed(actor, 20);
            if (interruption == 1) rxmotion::attack_started(actor, 5, 1);
            if (interruption == 2) rxmotion::reset();
            put(actor, 0x498, 888); put(actor, 0x4a0, 999); put(actor, 0x58, 777);
        };
        CHECK(jump() == 0);
        CHECK(get<int>(actor, 0x498) == 888 && get<int>(actor, 0x4a0) == 999 && get<int>(actor, 0x58) == 777);
        CHECK(!rxmotion::snapshot().chasing);
    }
}
void replacement_air_request_survives_the_outgoing_transition() {
    reset(); attach(); prepare_air_step(); CHECK(rxmotion::request_enemy_step(actor));
    during_state_change = [] { prepare_air_step(enemy2); CHECK(rxmotion::request_enemy_step(actor)); };
    CHECK(rxmotion::step(actor));
    const auto replacement = rxmotion::snapshot();
    CHECK(replacement.enemy_step_queued && replacement.target == target2_id);
    CHECK(get<signed char>(actor, 0x2bb2) == 2); // no outgoing impulse
    during_state_change = {};
    CHECK(rxmotion::step(actor)); CHECK(get<int>(actor, 0x58) == 2048);
    CHECK(!rxmotion::snapshot().enemy_step_queued);
}
void enemy_step_spending_survives_new_attacks_but_new_target_or_landing_refreshes() {
    reset(); attach(); prepare_air_step(); CHECK(rxmotion::request_enemy_step(actor)); CHECK(rxmotion::step(actor));
    CHECK(!rxmotion::request_enemy_step(actor));
    frame_number += 21; prepare_air_step(); CHECK(!rxmotion::request_enemy_step(actor)); // same target, another attack
    prepare_air_step(enemy2); CHECK(rxmotion::request_enemy_step(actor)); CHECK(rxmotion::step(actor));
    frame_number += 21; put(actor, 0x24, 4u); rxmotion::state_changed(actor, 0); // true landing
    rxmotion::state_changed(actor, 1); put(actor, 0x24, 0u); prepare_air_step();
    CHECK(rxmotion::request_enemy_step(actor)); CHECK(rxmotion::step(actor));
}
void enemy_step_spending_survives_temporary_control_loss() {
    reset(); attach(); prepare_air_step();
    CHECK(rxmotion::request_enemy_step(actor)); CHECK(rxmotion::step(actor));
    frame_number += 21; prepare_air_step(); CHECK(!rxmotion::request_enemy_step(actor));
    allowed = false; rxmotion::snapshot();
    allowed = true; prepare_air_step();
    CHECK(!rxmotion::request_enemy_step(actor)); // still airborne over the spent target
    prepare_air_step(enemy2); CHECK(rxmotion::request_enemy_step(actor)); CHECK(rxmotion::step(actor));
    // A real landing during suspension still starts another airborne route.
    frame_number += 21; allowed = false; put(actor, 0x24, 4u); rxmotion::snapshot();
    allowed = true; prepare_air_step();
    CHECK(rxmotion::request_enemy_step(actor)); CHECK(rxmotion::step(actor));
}
void temporary_control_loss_drops_requests_and_old_hit_confirms() {
    reset(); attach(); prepare_air_step(); CHECK(rxmotion::request_enemy_step(actor));
    allowed = false; auto suspended = rxmotion::snapshot();
    CHECK(suspended.actor == actor); CHECK(!suspended.confirmed_hit);
    CHECK(!suspended.enemy_step_queued); CHECK(!suspended.chase_queued); CHECK(!suspended.chasing);
    allowed = true;
    CHECK(!rxmotion::step(actor)); CHECK(!rxmotion::request_enemy_step(actor)); CHECK(states == 0);
    // Queued ground pursuit and an already active pursuit also cannot resume.
    for (bool active : {false, true}) {
        reset(); attach(); rxmotion::attack_started(actor, 11080, 0, true); hit();
        CHECK(rxmotion::request_chase(actor)); target_pos[1] = 2;
        if (active) CHECK(rxmotion::step(actor));
        allowed = false; suspended = rxmotion::snapshot();
        CHECK(!suspended.confirmed_hit); CHECK(!suspended.chase_queued); CHECK(!suspended.chasing);
        allowed = true; const unsigned old_states = states;
        CHECK(!rxmotion::step(actor)); CHECK(!rxmotion::request_chase(actor)); CHECK(states == old_states);
    }
}
void resumed_attack_can_confirm_new_contacts_without_restarting() {
    for (bool aerial : {false, true}) {
        reset(); attach(); rxmotion::observe_hit(&observe);
        if (aerial) {
            put(actor, 0x24, 0u); player_pos[1] = 2;
            target_pos[0] = 1; target_pos[1] = 2;
        }
        rxmotion::attack_started(actor, aerial ? 3 : 11080, aerial ? 1 : 0, !aerial);
        const auto started = rxmotion::snapshot();
        allowed = false; auto suspended = rxmotion::snapshot();
        CHECK(!suspended.confirmed_hit && !suspended.target);
        allowed = true; ++frame_number; hit();
        const auto resumed = rxmotion::snapshot();
        CHECK(resumed.attack_serial == started.attack_serial);
        CHECK(resumed.attack_id == started.attack_id && resumed.group == started.group);
        CHECK(resumed.confirmed_hit && resumed.target == target_id && notifications[0] == 1);
        if (aerial) CHECK(rxmotion::request_enemy_step(actor));
        else {
            CHECK(resumed.launcher); CHECK(rxmotion::request_chase(actor));
            CHECK(!rxmotion::step(actor)); target_pos[1] += 2;
            CHECK(rxmotion::step(actor));
        }
    }
}
void resumed_attack_preserves_per_target_observer_deduplication() {
    reset(); attach(); rxmotion::observe_hit(&observe);
    rxmotion::observe_hit_secondary(&observe, reinterpret_cast<void*>(1));
    rxmotion::attack_started(actor, 3, 1); hit();
    const auto serial = rxmotion::snapshot().attack_serial;
    CHECK(notifications[0] == 1 && notifications[1] == 1);
    allowed = false; rxmotion::snapshot(); ++frame_number; rxmotion::snapshot();
    allowed = true; hit();
    CHECK(rxmotion::snapshot().confirmed_hit && rxmotion::snapshot().attack_serial == serial);
    CHECK(notifications[0] == 1 && notifications[1] == 1); // no second credit for this attack/target
    hit(actor, enemy2); CHECK(notifications[0] == 2 && notifications[1] == 2);
    rxmotion::attack_started(actor, 12, 1); hit();
    CHECK(rxmotion::snapshot().attack_serial != serial);
    CHECK(notifications[0] == 3 && notifications[1] == 3); // a new attack earns its own credit
}
void forced_state_retires_the_old_attack_across_suspension() {
    for (int native_state : {13, 16, 20, 31}) {
        reset(); attach(); rxmotion::observe_hit(&observe);
        rxmotion::attack_started(actor, 3, 1);
        allowed = false; rxmotion::snapshot();
        rxmotion::state_changed(actor, native_state);
        allowed = true; hit();
        CHECK(get<short>(enemy, 0x21a) == 90); // native damage still executes
        CHECK(!rxmotion::snapshot().attack_serial && !rxmotion::snapshot().confirmed_hit);
        CHECK(notifications[0] == 0);
    }
    for (int native_state : {14, 15, 22}) {
        reset(); attach(); rxmotion::attack_started(actor, 3, 1);
        const auto serial = rxmotion::snapshot().attack_serial;
        rxmotion::state_changed(actor, native_state);
        CHECK(rxmotion::snapshot().attack_serial == serial); // ordinary native melee transitions
    }
}
void native_hit_reentrancy_cannot_confirm_an_interrupted_or_replaced_attack() {
    for (int interruption = 0; interruption < 3; ++interruption) {
        reset(); attach(); rxmotion::observe_hit(&observe);
        rxmotion::attack_started(actor, 3, 1);
        const auto old_serial = rxmotion::snapshot().attack_serial;
        during_native_hit = [interruption] {
            if (interruption == 0) rxmotion::state_changed(actor, 13);
            else if (interruption == 1) rxmotion::attack_started(actor, 12, 1);
            else retired_player = true;
        };
        hit(); during_native_hit = {};
        CHECK(get<short>(enemy, 0x21a) == 90); // original native damage is never suppressed
        CHECK(!rxmotion::snapshot().confirmed_hit && notifications[0] == 0);
        if (interruption == 1) {
            CHECK(rxmotion::snapshot().attack_serial != old_serial);
            hit(); CHECK(rxmotion::snapshot().confirmed_hit && notifications[0] == 1);
        }
    }
}
void interrupting_packet_policy(KhRef, KhRef, KhRef, void*) {
    rxmotion::state_changed(actor, 13);
}
void packet_policy_reentrancy_cannot_confirm_the_interrupted_attack() {
    reset(); attach(); rxmotion::observe_hit(&observe);
    rxmotion::attack_started(actor, 3, 1);
    rxmotion::set_packet_policy(&interrupting_packet_policy);
    hit(); CHECK(get<short>(enemy, 0x21a) == 90);
    CHECK(!rxmotion::snapshot().attack_serial && !rxmotion::snapshot().confirmed_hit);
    CHECK(notifications[0] == 0);
}
void suspended_actor_retirement_and_scene_unload_still_reset_binding() {
    reset(); attach(); prepare_air_step(); allowed = false; retired_player = true;
    CHECK(rxmotion::snapshot().actor == 0);
    reset(); attach(); prepare_air_step(); allowed = false; loaded = false;
    CHECK(rxmotion::snapshot().actor == 0);
}
void accepted_enemy_step_survives_recovery_until_its_fixed_deadline() {
    for (unsigned delay : {1u, 12u, 13u}) {
        reset(); attach(); prepare_air_step();
        frame_number += 24; CHECK(rxmotion::request_enemy_step(actor));
        frame_number += delay;
        const KhRef result = rxmotion::step(actor);
        CHECK((result != 0) == (delay <= 12)); CHECK(states == (delay <= 12 ? 1u : 0u));
        CHECK(!rxmotion::snapshot().enemy_step_queued);
        CHECK(!rxmotion::step(actor)); // consumption cannot execute the same request again
    }
    reset(); attach(); prepare_air_step(); frame_number += 25;
    CHECK(!rxmotion::request_enemy_step(actor)); // acceptance still requires a fresh hit
}
void repeated_motion_requests_do_not_extend_accepted_deadlines() {
    reset(); attach(); prepare_air_step(); CHECK(rxmotion::request_enemy_step(actor));
    frame_number += 10; CHECK(rxmotion::request_enemy_step(actor));
    frame_number += 3; CHECK(!rxmotion::step(actor)); CHECK(states == 0);
    CHECK(!rxmotion::snapshot().enemy_step_queued);
    reset(); attach(); rxmotion::attack_started(actor, 11080, 0, true); hit();
    CHECK(rxmotion::request_chase(actor)); target_pos[1] = 2;
    frame_number += 10; CHECK(rxmotion::request_chase(actor));
    frame_number += 9; CHECK(!rxmotion::step(actor)); CHECK(states == 0);
    CHECK(!rxmotion::snapshot().chase_queued);
}
void crowd_contacts_do_not_refresh_another_targets_mobility_window() {
    for (bool chase : {false, true}) {
        reset(); attach(); rxmotion::observe_hit(&observe);
        if (chase) {
            rxmotion::attack_started(actor, 11080, 0, true); hit();
        } else prepare_air_step();
        frame_number += chase ? 31 : 25;
        hit(actor, enemy2);
        const auto sweep = rxmotion::snapshot();
        CHECK(sweep.target == target_id && sweep.hit_frame == frame_number);
        CHECK(sweep.damage == 20 && notifications[0] == 2); // both contacts still count
        CHECK(!(chase ? rxmotion::request_chase(actor) : rxmotion::request_enemy_step(actor)));
        hit(); // a fresh contact with the actual target does renew its opportunity
        CHECK(chase ? rxmotion::request_chase(actor) : rxmotion::request_enemy_step(actor));
    }
}
void crowd_contacts_cannot_confirm_a_late_rise_for_an_old_launcher_target() {
    reset(); attach(); rxmotion::attack_started(actor, 11080, 0, true); hit();
    frame_number += 37; target_pos[1] = 2; hit(actor, enemy2);
    CHECK(!rxmotion::snapshot().launcher_confirmed);
    hit(); CHECK(rxmotion::snapshot().launcher_confirmed);
}
void accepted_crowd_mobility_keeps_its_original_target_and_deadline() {
    for (bool chase : {false, true}) for (bool expired : {false, true}) {
        reset(); attach();
        if (chase) {
            rxmotion::attack_started(actor, 11080, 0, true); hit(); target_pos[1] = 2;
            CHECK(rxmotion::request_chase(actor));
        } else { prepare_air_step(); CHECK(rxmotion::request_enemy_step(actor)); }
        frame_number += chase ? 18 : 12;
        hit(actor, enemy2);
        CHECK(rxmotion::snapshot().target == target_id);
        if (expired) ++frame_number;
        CHECK((rxmotion::step(actor) != 0) == !expired);
    }
}
void accepted_enemy_step_rechecks_live_execution_conditions() {
    for (int gate = 0; gate < 7; ++gate) {
        reset(); attach(); prepare_air_step(); frame_number += 24;
        CHECK(rxmotion::request_enemy_step(actor)); ++frame_number;
        if (gate == 0) target_pos[0] = 2;
        if (gate == 1) put(actor, 0x24, 4u);
        if (gate == 2) put(enemy, 0x17a, static_cast<unsigned char>(1));
        if (gate == 3) retired_enemy = true;
        if (gate == 4) retired_player = true;
        if (gate == 5) allowed = false;
        if (gate == 6) loaded = false;
        CHECK(!rxmotion::step(actor)); CHECK(states == 0);
        CHECK(!rxmotion::snapshot().enemy_step_queued);
    }
}
void accepted_motion_cannot_redirect_to_a_later_crowd_hit() {
    for (bool chase : {false, true}) {
        reset(); attach();
        if (chase) {
            rxmotion::attack_started(actor, 11080, 0, true); hit();
            CHECK(rxmotion::request_chase(actor));
        } else {
            prepare_air_step(); CHECK(rxmotion::request_enemy_step(actor));
        }
        retired_enemy = true; hit(actor, enemy2);
        CHECK(rxmotion::snapshot().target == target2_id);
        CHECK(!rxmotion::step(actor)); CHECK(states == 0);
        CHECK(!rxmotion::snapshot().enemy_step_queued && !rxmotion::snapshot().chase_queued);
    }
}
void enemy_step_rechecks_ground_range_cooldown_and_target_lifetime() {
    auto begin = [] { reset(); attach(); prepare_air_step(); };
    begin(); put(enemy, 0x17a, static_cast<unsigned char>(1)); CHECK(!rxmotion::request_enemy_step(actor));
    begin(); put(actor, 0x24, 4u); CHECK(!rxmotion::request_enemy_step(actor));
    begin(); target_pos[0] = 2; CHECK(!rxmotion::request_enemy_step(actor));
    begin(); target_pos[1] = 4; CHECK(!rxmotion::request_enemy_step(actor));
    begin(); CHECK(rxmotion::request_enemy_step(actor)); retired_enemy = true; CHECK(!rxmotion::step(actor)); CHECK(states == 0);
    begin(); CHECK(rxmotion::request_enemy_step(actor)); frame_number += 13; CHECK(!rxmotion::step(actor)); CHECK(states == 0);
    begin(); CHECK(rxmotion::request_enemy_step(actor)); CHECK(rxmotion::step(actor));
    prepare_air_step(enemy2); CHECK(!rxmotion::request_enemy_step(actor)); // another target still respects cooldown
    frame_number += 21; CHECK(rxmotion::request_enemy_step(actor));
}
}
int main() {
    const std::pair<const char*, std::function<void()>> cases[] = {
        {"motion prerequisites and native hook rollback", motion_test::prerequisites_and_hook_rollback},
        {"actual HP loss, attacker correlation, multi-target deduplicated observers", motion_test::actual_damage_is_correlated_to_attacker_and_one_observer_per_target},
        {"guard, counter, rejection and zero-damage cannot confirm", motion_test::blocked_answered_rejected_and_zero_damage_do_not_confirm},
        {"scoped melee attribution excludes prior spells with the same player source", motion_test::packet_policy_and_own_non_sweep_hits_share_the_native_call},
        {"killing blow retires target without losing hit credit", motion_test::killing_blow_counts_without_reusing_a_retired_target},
        {"launcher chase waits for actual rise and respects expiry", motion_test::launcher_requires_real_rise_and_request_expires},
        {"native chase state and collision step preserve coordinates and external forces", motion_test::chase_uses_native_state_reference_and_collision_step_without_position_writes},
        {"wall stall, timeout, new attack and forced-state cancellation", motion_test::collision_stall_timeout_and_attack_interrupt_end_chase},
        {"actor handles, target range, native return and reset guards", motion_test::lifetime_distance_and_native_transition_guards},
        {"Enemy Step uses native mobility without damage or camera writes", motion_test::enemy_step_is_native_mobility_with_no_extra_hit_or_camera_change},
        {"interrupted native transitions retain replacement motion", motion_test::interrupted_native_transitions_keep_the_replacement_motion},
        {"interrupted native jump retains replacement velocity", motion_test::interrupted_native_jump_preserves_replacement_velocity},
        {"replacement Enemy Step request survives outgoing transition", motion_test::replacement_air_request_survives_the_outgoing_transition},
        {"Enemy Step spending survives attacks, refreshes on new target or air route", motion_test::enemy_step_spending_survives_new_attacks_but_new_target_or_landing_refreshes},
        {"Enemy Step spending survives control loss until a real landing", motion_test::enemy_step_spending_survives_temporary_control_loss},
        {"control loss drops queued motion and old hit confirms", motion_test::temporary_control_loss_drops_requests_and_old_hit_confirms},
        {"resumed native attacks confirm fresh contacts without restart", motion_test::resumed_attack_can_confirm_new_contacts_without_restarting},
        {"resumed attacks preserve per-target observer credit", motion_test::resumed_attack_preserves_per_target_observer_deduplication},
        {"forced native states retire old attack identity across suspension", motion_test::forced_state_retires_the_old_attack_across_suspension},
        {"native hit reentrancy cannot credit interrupted or replaced attacks", motion_test::native_hit_reentrancy_cannot_confirm_an_interrupted_or_replaced_attack},
        {"packet policy reentrancy cannot credit an interrupted attack", motion_test::packet_policy_reentrancy_cannot_confirm_the_interrupted_attack},
        {"suspended actor retirement and scene unload reset motion", motion_test::suspended_actor_retirement_and_scene_unload_still_reset_binding},
        {"accepted Enemy Step survives recovery, expires and consumes once", motion_test::accepted_enemy_step_survives_recovery_until_its_fixed_deadline},
        {"repeated motion requests cannot renew accepted deadlines", motion_test::repeated_motion_requests_do_not_extend_accepted_deadlines},
        {"crowd contacts cannot refresh another target's mobility window", motion_test::crowd_contacts_do_not_refresh_another_targets_mobility_window},
        {"crowd contacts cannot confirm a late rise for an old launcher target", motion_test::crowd_contacts_cannot_confirm_a_late_rise_for_an_old_launcher_target},
        {"accepted crowd mobility keeps original target and deadline", motion_test::accepted_crowd_mobility_keeps_its_original_target_and_deadline},
        {"accepted Enemy Step rechecks live execution conditions", motion_test::accepted_enemy_step_rechecks_live_execution_conditions},
        {"accepted motion stays bound to its target across later crowd hits", motion_test::accepted_motion_cannot_redirect_to_a_later_crowd_hit},
        {"Enemy Step ground, range, cooldown and target lifetime guards", motion_test::enemy_step_rechecks_ground_range_cooldown_and_target_lifetime}
    };
    unsigned failed = 0;
    for (const auto& test : cases) try { test.second(); std::cout << "PASS " << test.first << '\n'; }
    catch (const std::exception& e) { ++failed; std::cout << "FAIL " << test.first << ": " << e.what() << '\n'; }
    rxmotion::detach();
    std::cout << (std::size(cases) - failed) << " passed, " << failed << " failed\n";
    return failed ? 1 : 0;
}
