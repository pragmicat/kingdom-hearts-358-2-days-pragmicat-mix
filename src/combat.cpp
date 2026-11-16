// Kingdom Hearts 358/2 Days: Pragmicat Mix: original Days attack graphs, animations and contact handling.
// ABI and offsets audited against khdays-port v0.1.5 / its matching decomp.
#include <kh/kh_actors.h>
#include <kh/kh_engine.h>
#include <kh/kh_events.h>
#include <kh/kh_input.h>
#include <kh/kh_net.h>
#include <kh/kh_parameters.h>
#include "roxas_catalogue.h"
#include "limit_finishers.h"
#include "move_routes.h"
#include "combat_motion.h"
#include "combat_rules.h"
#include "drive_form.h"
#include "animation_links.h"
#include "drive_hud.h"
#include "practice_boot.h"
#include "weapon_animation.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>

namespace {
constexpr unsigned kTableOffset = 0x910, kTableSize = 0x480;
// One verified wp.b row for each of the eighteen ci.b combo graphs.
constexpr int kWeaponRows[] = {0,1,3,23,14,16,19,18,25,31,33,35,37,40,5,39,46,47};
const KhHost* host;
const KhEngine* engine;
const KhActors* actors;
const KhInput* input;
const KhNet* net;
KhRef scene_ctl, cinematic_flag, boot_flags;
bool paused = false, building = false, stopping = false;
unsigned menu_depth = 0;
int buffer_ticks = 12, speed_fx = 5120, move_fx = 4710, turn_fx = 5325, style = 0;
bool defensive_cancels = true;
bool repertoire_debug = false;
bool drive_enabled = true, drive_meter = true;
int signature_damage_fx = 4096;
rxdrive::Settings drive_settings{1000, 25, 2, 900};
unsigned last_input_frame = ~0u;

using BuildFn = void (*)(KhRef, KhRef, int, int);
using TableFn = void (*)(KhRef);
using WalkFn = void (*)(KhRef, int, int);
using ActorFn = void (*)(KhRef);
using StepFn = int (*)(KhRef);
using AllocFn = KhRef (*)(unsigned);
using SpeedFn = void (*)(KhRef, int);
using SteerFn = int (*)(KhRef, int);
using StateFn = int (*)(KhRef, int);
using HeapGetFn = KhRef (*)();
using HeapSetFn = KhRef (*)(KhRef);
BuildFn game_build;
TableFn game_teardown, game_next_a, game_next_b, reset_grid;
WalkFn game_walk;
ActorFn game_translate;
StepFn game_step;
AllocFn game_alloc;
TableFn game_free;
SpeedFn set_speed;
SpeedFn set_animation;
SteerFn game_steer;
StateFn set_state;
StateFn game_set_state;
StateFn start_jump;
ActorFn process_pair;
TableFn clear_node;
int (*roll_allowed)(KhRef);
int (*guard_allowed)(KhRef);
HeapGetFn heap_get;
HeapSetFn heap_set;
int (*input_blocked)();
int (*heading_from_input)(KhRef);
int (*validate_target)(KhRef);
KhRef (*target_position)(KhRef);
ActorFn resolve_target_pick;

void say(const char* message, KhLogLevel level = KH_LOG_INFO) {
    if (host) host->log(host->plugin, level, message);
}
unsigned char* bytes(KhRef ref) { return static_cast<unsigned char*>(engine->pointer(ref)); }
template<class T> T read(KhRef ref, unsigned offset = 0) {
    T value{};
    if (auto* p = bytes(ref)) std::memcpy(&value, p + offset, sizeof value);
    return value;
}
template<class T> void write(KhRef ref, unsigned offset, T value) {
    if (auto* p = bytes(ref)) std::memcpy(p + offset, &value, sizeof value);
}
struct Move {
    int id, group, pool;
    KhRef node, record;
};
struct Bank {
    KhRef table = 0;
    int row = 0;
    std::vector<Move> moves;
};
struct TargetIntent {
    KhActor handle = 0;
    std::array<unsigned char, 8> identity{};
};
struct Player {
    KhRef actor = 0, table = 0;
    int kind = 0;
    KhRef heap = 0;
    std::array<unsigned char, kTableSize> original{};
    std::vector<Bank> banks;
    int queued = 0; // rxroutes::Action; one future action, never a queued macro
    int committed = 0; // accepted in the native window; survives buffer expiry
    rxroutes::Direction direction = rxroutes::Direction::Neutral;
    rxroutes::Direction committed_direction = rxroutes::Direction::Neutral;
    TargetIntent queued_target{}, committed_target{};
    rxroutes::Role role = rxroutes::Role::Pressure;
    unsigned queue_until = 0;
    unsigned light_cursor[2]{}, tech_cursor[2]{};
    unsigned depth = 0;
    unsigned techniques = 0;
    int active_bank = -1, active_move = -1;
    bool active_finisher = false;
    unsigned defense_until = 0;
    bool defense_queued = false;
    int pending_finisher = 0;
    int previous_move = -1;
    int previous_group = 0;
    bool blend_pending = false;
    bool jump_queued = false;
    unsigned jump_until = 0;
    unsigned jump_intent = 0, motion_jump_intent = 0;
    unsigned hit_serial = 0;
    int hit_animation_frame = -1;
    bool resume_route = false;
    unsigned resume_until = 0;
};
Player player;
bool practice_meter_primed = false;

bool known_player(KhRef actor) {
    const int kind = actor ? read<int>(actor, 0xc) : -1;
    if (!actor || !engine->module_loaded(22) || (kind != 0 && kind != 19) ||
        read<unsigned char>(actor, 8) != 0 || read<unsigned char>(actor, 9) != 0) return false;
    return !net->in_room() && actors->local_seat() == 0 &&
        (kind == 0 || rxdrive::is_active() || rxdrive::is_replacing());
}
bool live_local_actor(KhRef actor) {
    const KhActor current = actors->player(0);
    return current != 0 && actors->ref(current) == actor;
}
bool visible_for_input(KhRef actor) {
    // Apply the same enabled/suppression bits to ordinary and signature input.
    // Exempting native signature pad locks must not bypass this visibility gate.
    return (read<unsigned char>(actor, 0x694) & 9u) == 1u;
}
bool control_context_live(KhRef actor, bool ignore_input_lock = false) {
    if (stopping || !known_player(actor) || !live_local_actor(actor) || read<int>(scene_ctl, 8) != 2 ||
        (cinematic_flag && read<unsigned char>(cinematic_flag) != 0) || (!ignore_input_lock && input_blocked() != 0) ||
        read<std::uint16_t>(actor, 0x12) == 0) return false;
    const auto flags = read<std::uint64_t>(actor);
    constexpr std::uint64_t blocked = (1ull<<8)|(1ull<<16)|(1ull<<17)|(1ull<<24)|(1ull<<45);
    // +0x694 bit 0 is the actor's visible/enabled flag. Some reconstructed
    // structs call it bSuppressDraw, but SetVisible and PlayEntityVoice establish
    // the actual polarity. Invisible scripted actors must never consume input.
    return (flags & blocked) == 0 && visible_for_input(actor);
}
bool controllable(KhRef actor) { return !paused && control_context_live(actor); }
bool queue_live() {
    return player.queued != 0 && static_cast<int>(player.queue_until - input->frame()) >= 0;
}
void clear_queue() {
    player.queued = player.committed = 0;
    player.queued_target = player.committed_target = {};
}
int requested_action() { return player.committed ? player.committed : queue_live() ? player.queued : 0; }
rxroutes::Direction requested_direction() {
    return player.committed ? player.committed_direction : player.direction;
}

bool finisher_context_live(KhRef actor) {
    // Native finisher states temporarily lock pad input, so this deliberately
    // validates actor lifetime/scene instead of requiring controllable().
    if (actor != player.actor || player.kind != 0 || stopping ||
        !known_player(actor) || !live_local_actor(actor) || read<int>(scene_ctl, 8) != 2 ||
        (cinematic_flag && read<unsigned char>(cinematic_flag)) ||
        !read<std::uint16_t>(actor, 0x12)) return false;
    return visible_for_input(actor);
}
bool finisher_permitted(KhRef actor) { return !paused && finisher_context_live(actor); }

void discard_intentions(KhRef actor) {
    clear_queue();
    player.pending_finisher = 0;
    player.jump_queued = player.defense_queued = false;
    player.resume_route = player.blend_pending = false;
    // The accepted command also set native melee-pending fields. Leaving those
    // armed would let the original graph advance after the scripted lock ends.
    // A retired SDK handle must never write into its replacement's memory.
    if (actor && live_local_actor(actor) && bytes(player.table)) {
        write<int>(player.table, 0x47c, 0);
        if (read<std::uint16_t>(actor, 0x1c) == 1) write<std::uint16_t>(actor, 0x1c, 0);
    }
    input->hide(host->plugin, 0);
}
void discard_forced_intentions(KhRef actor) {
    // A native pause can itself block pad input. It suspends an already accepted
    // action; independent scene, actor and cinematic gates still invalidate it.
    if (!actor || actor != player.actor || control_context_live(actor, paused) ||
        (rxlimit::active(actor) && finisher_context_live(actor))) return;
    discard_intentions(actor);
}

int change_state(KhRef actor, int state) {
    if (rxlimit::suppress_legacy_start(actor, state)) {
        // Holding Attack at low HP no longer enters the old Limit mode.
        // The same attacks are available through ordinary combo finishers.
        return 0;
    }
    if (rxlimit::active(actor) && state != 34 && state != 35) rxlimit::forget(actor);
    rxmotion::state_changed(actor, state);
    // Receiving damage or entering a forced action invalidates a stored route.
    // States 14/15 are ordinary ground/air melee entry, whose native handler
    // calls StartGroupWalk. Keep the button intent that requested that entry.
    if (actor == player.actor && state >= 13 && state <= 31 &&
        state != 14 && state != 15 && state != 22) {
        clear_queue();
        player.resume_route = false;
        player.jump_queued = false;
        player.defense_queued = false;
        player.blend_pending = false;
    }
    if (actor == player.actor && player.resume_route && (state == 8 || state == 11))
        player.resume_route = false;
    return game_set_state(actor, state);
}

void scan_bank(Bank& bank) {
    for (int group = 0; group < 2; ++group) {
        const KhRef graph = bank.table + 0x28 + 0x30 * group;
        for (int pool = 0; pool < 3; ++pool) {
            const KhRef nodes = read<KhRef>(graph, 0x18 + 4 * pool);
            const unsigned count = read<unsigned>(graph, 0x24 + 4 * pool);
            if (!nodes || count > 64) continue;
            for (unsigned n = 0; n < count; ++n) {
                const KhRef node = nodes + 0x18 * n;
                const KhRef record = read<KhRef>(node, 0x14);
                if (!record || !bytes(record)) continue;
                bank.moves.push_back({read<std::uint16_t>(record), group, pool, node, record});
            }
        }
    }
}

void release_banks() {
    for (auto& bank : player.banks) {
        if (bank.table && bytes(bank.table)) {
            game_teardown(bank.table);
            game_free(bank.table);
        }
    }
    player.banks.clear();
}
void forget_rules(KhRef actor) {
    if (actor == player.actor && player.heap && live_local_actor(actor)) {
        const KhRef previous = heap_set(player.heap);
        rxrules::forget(actor);
        heap_set(previous);
    } else rxrules::forget(actor);
}

void build(KhRef table, KhRef actor, int kind, int row) {
    game_build(table, actor, kind, row);
    if (building || stopping || (kind != 0 && kind != 19) || !known_player(actor)) return;
    // The build occurs while the actor's weapon container is still alive.
    // Additional tables own their allocations; the actor's original is never freed here.
    if (player.actor) {
        say("An unexpected second Roxas table was built; preserving stock combat.", KH_LOG_WARNING);
        return;
    }
    player.actor = actor;
    player.table = table;
    player.heap = heap_get();
    player.kind = kind;
    rxweapon::bind(actor);
    std::memcpy(player.original.data(), bytes(table), kTableSize);
    building = true;
    try {
        const int bank_count = kind == 19 ? 1 : 18;
        player.banks.reserve(bank_count);
        for (int bank_index = 0; bank_index < bank_count; ++bank_index) {
            const int weapon_row = kWeaponRows[bank_index];
            const KhRef cache = game_alloc(kTableSize);
            if (!cache || !bytes(cache)) break;
            std::memset(bytes(cache), 0, kTableSize);
            game_build(cache, actor, kind, weapon_row);
            Bank bank;
            bank.table = cache;
            bank.row = weapon_row;
            try {
                bank.moves.reserve(64);
                scan_bank(bank);
                player.banks.push_back(std::move(bank));
            } catch (...) {
                // This bank has not transferred to player.banks yet.
                game_teardown(cache);
                game_free(cache);
                throw;
            }
        }
    } catch (...) {
        say("Could not allocate the complete move index; keeping completed banks.", KH_LOG_WARNING);
    }
    building = false;
    // Native IDs are uint16. Count without allocating across the plugin boundary.
    std::array<unsigned char, 8192> seen{};
    unsigned unique_ids = 0;
    for (const auto& bank : player.banks) for (const auto& move : bank.moves) {
        const unsigned id = static_cast<std::uint16_t>(move.id);
        const auto mask = static_cast<unsigned char>(1u << (id & 7u));
        if (!(seen[id >> 3] & mask)) { seen[id >> 3] |= mask; ++unique_ids; }
    }
    char log[180];
    std::snprintf(log, sizeof log, "Kingdom Hearts 358/2 Days: Pragmicat Mix: cached %zu original weapon graphs, %u unique graph-reachable attacks; original equipped wp row %d.", player.banks.size(), unique_ids, row);
    say(log);
    rxmotion::bind(actor);
    rxrules::bind(actor);
}

void teardown(KhRef table) {
    if (!building && table == player.table && player.actor) {
        rxweapon::forget(player.actor);
        rxlimit::forget(player.actor);
        rxanim::reset(player.actor);
        rxmotion::reset();
        forget_rules(player.actor);
        std::memcpy(bytes(table), player.original.data(), kTableSize);
        // Detach any animation objects owned by a cached bank before releasing it.
        if (set_animation && bytes(player.actor)) set_animation(player.actor, 0);
        release_banks();
        player = Player{};
    }
    game_teardown(table);
}

struct Choice { int bank = -1; const Move* move = nullptr; };
void apply_intended_target() {
    const auto& intent = player.committed ? player.committed_target : player.queued_target;
    if (!intent.handle) return; // Uncaptured inputs retain native targeting.
    // Clear cached native actor/part addresses even when the captured handle
    // has retired. An invalid buffered target must not leave a stale pick live.
    std::memset(bytes(player.actor) + 0x4f0, 0, 0x18);
    if (!actors->kind || !actors->ref || actors->kind(intent.handle) != KH_ACTOR_ENEMY) return;
    const KhRef expected = actors->ref(intent.handle);
    if (!expected || !bytes(expected)) return;
    // Store identity, not a borrowed collision-part pointer. Resolve the part
    // again through the game's live list and verify the generation-safe actor.
    std::memcpy(bytes(player.actor) + 0x4f0, intent.identity.data(), intent.identity.size());
    resolve_target_pick(player.actor);
    if (read<unsigned char>(player.actor, 0x4f0) != 1 || read<KhRef>(player.actor, 0x4fc) != expected)
        std::memset(bytes(player.actor) + 0x4f0, 0, 0x18);
}
Choice choose(int group, bool technique) {
    const bool finisher = player.depth >= 4;
    if (style == 0) {
        const auto action = requested_action() ? static_cast<rxroutes::Action>(requested_action()) :
            technique ? rxroutes::Action::Technique : rxroutes::Action::Light;
        const auto route = rxroutes::choose({group, player.kind, player.light_cursor[group], player.active_move},
                                             action, requested_direction());
        for (int b = 0; b < static_cast<int>(player.banks.size()); ++b)
            for (const auto& move : player.banks[b].moves)
                if (move.group == group && move.id == route.id) return {b, &move};
        return {}; // An unavailable authored role must not become a random move.
    }
    std::array<Choice, 1024> options{};
    unsigned option_count = 0;
    // Styles 1..18 focus a native graph. Style 19 surveys the entire atlas.
    for (int b = 0; b < static_cast<int>(player.banks.size()); ++b) {
        if (style > 0 && style < 19 && b != (style - 1) % static_cast<int>(player.banks.size())) continue;
        for (const auto& move : player.banks[b].moves) {
            if (move.group != group) continue;
            const bool wanted = finisher ? move.pool == 1 : technique ? move.pool != 0 : move.pool == 0;
            if (!wanted) continue;
            bool duplicate = false;
            for (unsigned option = 0; option < option_count; ++option)
                if (options[option].move->id == move.id) { duplicate = true; break; }
            if (!duplicate && option_count < options.size()) options[option_count++] = {b, &move};
        }
    }
    if (option_count == 0) {
        for (int b = 0; b < static_cast<int>(player.banks.size()); ++b)
            for (const auto& move : player.banks[b].moves)
                if (move.group == group && option_count < options.size() && (style == 0 || style == 19 || b == (style-1)%static_cast<int>(player.banks.size()))) options[option_count++] = {b, &move};
    }
    if (option_count == 0) return {};
    auto& cursor = technique ? player.tech_cursor[group] : player.light_cursor[group];
    const unsigned position = style == 19 ? cursor++ : technique ? player.techniques : player.depth;
    return options[position % option_count];
}

bool select_move(KhRef table, int group, bool technique) {
    if (player.banks.empty()) return false;
    const Choice choice = choose(group, technique);
    if (!choice.move) return false;
    apply_intended_target();
    const auto action = requested_action() ? static_cast<rxroutes::Action>(requested_action()) :
        technique ? rxroutes::Action::Technique : rxroutes::Action::Light;
    const auto route = rxroutes::choose({group, player.kind, player.light_cursor[group], player.active_move},
                                         action, requested_direction());
    player.previous_move = player.active_move;
    player.previous_group = (read<unsigned>(table) & 4u) ? 1 : 0;
    player.blend_pending = style == 0 && player.previous_move >= 0 && player.previous_group == group;
    const auto& bank = player.banks[choice.bank];
    // Keep the banks' resources at stable addresses: the attack's animation-row
    // pointers still refer to their bank. Only the active cursor lives in the actor.
    // The weapon block contains the equipped stat bonuses as well as reach and
    // graph selectors. Borrow only animations/graphs; never borrow another
    // Keyblade's strength/magic bonuses or overwrite a live stat effect.
    std::array<unsigned char, 0x20> equipped{};
    std::memcpy(equipped.data(), bytes(table) + 8, equipped.size());
    std::memcpy(bytes(table), bytes(bank.table), 0x468);
    std::memcpy(bytes(table) + 8, equipped.data(), equipped.size());
    const KhRef graph = table + 0x28 + 0x30 * group;
    write<KhRef>(table, 0x468, graph);
    write<KhRef>(table, 0x46c, graph + 0x18);
    write<KhRef>(table, 0x470, choice.move->node);
    write<int>(table, 0x474, 0);
    write<unsigned>(table, 0x478, 0);
    write<int>(table, 0x47c, 0);
    unsigned flags = read<unsigned>(table);
    flags = (flags & ~0x1eu) | (group ? 4u : 2u);
    write<unsigned>(table, 0, flags);
    reset_grid(choice.move->record);
    player.active_bank = choice.bank;
    player.active_move = choice.move->id;
    player.role = route.role;
    player.active_finisher = style == 0 ? route.ending : choice.move->pool == 1 || player.depth >= 4;
    if (action == rxroutes::Action::Light) ++player.light_cursor[group];
    if (technique) ++player.techniques;
    ++player.depth;
    clear_queue();
    player.hit_animation_frame = -1;
    rxmotion::attack_started(player.actor, choice.move->id, group, style == 0 && route.launcher);
    rxrules::attack_started(player.actor, rxmotion::snapshot().attack_serial,
        route.launcher ? rxrules::Reaction::Launcher : route.role == rxroutes::Role::Carry ?
        rxrules::Reaction::Carry : rxrules::Reaction::Native);
    if (style == 0 && player.kind == 0) {
        int first_contact = -1;
        for (const auto& move : kRxMoves)
            if (move.id == choice.move->id && move.group == group) { first_contact = move.first; break; }
        rxanim::transition(player.actor, player.previous_move, choice.move->id, player.previous_group,
                           group, read<int>(player.actor, 0x7b0), first_contact);
    }
    set_speed(player.actor, speed_fx);
    char log[180];
    std::snprintf(log, sizeof log, "Kingdom Hearts 358/2 Days: Pragmicat Mix: %s attack %d, native graph %d/wp row %d, %s, depth %u, style %d.", technique ? "technique" : "light", choice.move->id, choice.bank, bank.row, group ? "air" : "ground", player.depth, style);
    say(log);
    return true;
}

void walk(KhRef table, int reverse, int group) {
    game_walk(table, reverse, group);
    if (table != player.table || group < 0 || group > 1 || !controllable(player.actor)) return;
    const bool resume = player.resume_route && static_cast<int>(player.resume_until - input->frame()) >= 0;
    const int requested = requested_action();
    const auto direction = requested_direction();
    const auto target = player.committed ? player.committed_target : player.queued_target;
    if (!resume) {
        player.depth = 0;
        player.techniques = 0;
        player.light_cursor[0] = player.light_cursor[1] = 0;
        player.active_move = -1;
    }
    player.resume_route = false;
    const bool selected = select_move(table, group, requested == 2);
    // A neutral signature press starts a real short opener immediately, then
    // requests the signature at its first authorized transition.
    if (selected && requested >= 3 && player.kind == 0) {
        // The native command already accepted this deliberate chord and began
        // its opener. Its authored transition may exceed the raw press buffer.
        player.committed = requested;
        player.committed_direction = direction;
        player.committed_target = target;
    }
}
void next(KhRef table, bool branch) {
    if (table != player.table || !controllable(player.actor) || player.banks.empty()) {
        (branch ? game_next_b : game_next_a)(table);
        return;
    }
    const int group = (read<unsigned>(table) & 4u) ? 1 : 0;
    const int requested = requested_action();
    if (!requested) {
        (branch ? game_next_b : game_next_a)(table);
        return;
    }
    const bool technique = requested == 2;
    const int finisher = player.kind == 0 && style == 0 && requested >= 3 ? requested - 2 : 0;
    if (!select_move(table, group, technique))
        (branch ? game_next_b : game_next_a)(table);
    // Begin only after StepComboState returns from the original transition.
    // It must finish its native cleanup before we return a different state.
    player.pending_finisher = finisher;
}
void next_a(KhRef table) { next(table, false); }
void next_b(KhRef table) { next(table, true); }

rxroutes::Direction input_direction(KhRef actor) {
    const int heading = heading_from_input(actor);
    if (heading < 0) return rxroutes::Direction::Neutral;
    const int movement = (heading + read<std::int16_t>(actor, 0x478)) & 0xffff;
    int toward = read<int>(actor, 0x488) & 0xffff;
    if (validate_target(actor)) {
        const KhRef point = target_position(actor);
        float origin[3]{};
        const KhActor handle = actors->player(0);
        if (point && bytes(point) && handle && actors->position && actors->position(handle, origin) == KH_OK) {
            const double dx = read<int>(point) / 4096.0 - origin[0];
            const double dz = read<int>(point, 8) / 4096.0 - origin[2];
            if (std::isfinite(dx) && std::isfinite(dz) && dx * dx + dz * dz > 0.0001)
                toward = static_cast<int>(std::atan2(-dx, -dz) * (32768.0 / 3.14159265358979323846)) & 0xffff;
        }
    }
    return rxroutes::relative_direction(movement, toward);
}
bool form_transition_requested() {
    const auto phase = rxdrive::view().phase;
    return phase == rxdrive::Phase::ActivationQueued || phase == rxdrive::Phase::RestorationQueued;
}
bool form_transition_pending() {
    return form_transition_requested() && rxdrive::ready_for_safe_point(player.actor);
}
unsigned hidden_buttons() {
    return KH_BUTTON_X | KH_BUTTON_SELECT | (form_transition_pending() ? KH_BUTTON_A : 0u);
}
void sample_input(KhRef actor) {
    if (!controllable(actor) && !(rxlimit::active(actor) && finisher_permitted(actor))) {
        discard_forced_intentions(actor); input->hide(host->plugin, 0); return;
    }
    input->hide(host->plugin, hidden_buttons());
    const unsigned now = input->frame();
    if (now == last_input_frame) return;
    last_input_frame = now;
    const auto pressed = input->pressed();
    if (pressed & KH_BUTTON_B) {
        player.jump_queued = true;
        player.jump_until = now + buffer_ticks;
        ++player.jump_intent;
    } else if (player.jump_queued && static_cast<int>(player.jump_until - now) < 0) player.jump_queued = false;
    if (pressed & KH_BUTTON_Y) {
        player.defense_queued = true;
        player.defense_until = now + buffer_ticks;
    } else if (player.defense_queued && static_cast<int>(player.defense_until - now) < 0) {
        player.defense_queued = false;
    }
    if (repertoire_debug && (pressed & KH_BUTTON_SELECT)) {
        style = (style + 1) % 20;
        char log[100];
        std::snprintf(log, sizeof log, "Kingdom Hearts 358/2 Days: Pragmicat Mix style %d (%s).", style, style == 0 ? "curated deterministic flow" : style == 19 ? "complete repertoire survey" : "focused original weapon graph");
        say(log);
    } else if (drive_enabled && (pressed & KH_BUTTON_SELECT)) {
        rxdrive::request_activation();
    }
    // Let the current action finish, then transform/revert. New button presses
    // cannot extend a timed form indefinitely with a never-ending air string.
    if (form_transition_pending()) { clear_queue(); player.jump_queued = false; return; }
    if (pressed & (KH_BUTTON_A | KH_BUTTON_X)) {
        player.direction = input_direction(actor);
        player.queued_target = {};
        if (read<unsigned char>(actor, 0x4f0) == 1 && actors->of && actors->kind) {
            const KhActor target = actors->of(read<KhRef>(actor, 0x4fc));
            if (target && actors->kind(target) == KH_ACTOR_ENEMY) {
                player.queued_target.handle = target;
                std::memcpy(player.queued_target.identity.data(), bytes(actor) + 0x4f0, 8);
            }
        }
        const unsigned held = input->held();
        // A finisher is a fresh Technique press while Light is held. Returning
        // to Light with Technique still down must keep the normal attack chain.
        const bool chord = (pressed & KH_BUTTON_X) != 0 &&
            (held & (KH_BUTTON_A | KH_BUTTON_X)) == (KH_BUTTON_A | KH_BUTTON_X);
        player.queued = static_cast<int>(chord ? rxroutes::finisher(player.direction) :
            (pressed & KH_BUTTON_X) ? rxroutes::Action::Technique : rxroutes::Action::Light);
        player.queue_until = now + buffer_ticks;
    } else if (!queue_live()) {
        // Only an unaccepted button press expires. Native phase acceptance is a
        // commitment and can wait longer than the raw early-input buffer.
        player.queued = 0;
        player.queued_target = {};
    }
}

void translate(KhRef actor) {
    discard_forced_intentions(actor);
    if (actor != player.actor || !controllable(actor) || player.banks.empty()) {
        game_translate(actor);
        return;
    }
    // Preload the original finisher rig asynchronously on the actor's own heap.
    // An early request retains its ordinary attack fallback until loading ends.
    if (player.kind == 0) rxlimit::prepare(actor);
    rxmotion::bind(actor);
    rxrules::bind(actor);
    rxrules::refresh_mobility(actor);
    const bool defer_magic = form_transition_requested() && read<unsigned char>(actor, 0x4d6) == 8;
    sample_input(actor);
    if (defer_magic) {
        write<unsigned char>(actor, 0x4d6, 7);
        write<std::uint16_t>(actor, 0x18, static_cast<std::uint16_t>(read<std::uint16_t>(actor, 0x18) & ~1u));
        clear_queue();
    }
    if (form_transition_pending())
        write<std::uint16_t>(actor, 0x18, static_cast<std::uint16_t>(read<std::uint16_t>(actor, 0x18) & ~1u));
    // Ask the original command validator to start an attack. This retains its
    // checks for disabled actions, scripted states, guard, magic and item use.
    // Native command kind 9 is the melee path, not a newly invented state.
    const bool offer = queue_live() && read<unsigned char>(actor, 0x4d6) == 7;
    if (offer) {
        write<unsigned char>(actor, 0x4d6, 9);
        write<std::uint16_t>(actor, 0x18, read<std::uint16_t>(actor, 0x18) | 1u);
    }
    game_translate(actor);
}

void combo_exit(KhRef actor, bool preserve_route) {
    // Match the native StepComboState done path. No old melee contacts can run
    // after the state routine returned by this exit is installed by ActorUpdate.
    write<std::uint64_t>(actor, 0x46c, read<std::uint64_t>(actor, 0x46c) & ~(1ull << 4));
    write<std::uint64_t>(actor, 0, (read<std::uint64_t>(actor) & ~(1ull << 32)) | (1ull << 29));
    process_pair(actor);
    clear_node(actor + kTableOffset);
    player.resume_route = preserve_route;
    player.resume_until = input->frame() + 90; // one roll/guard plus a short re-entry
    player.blend_pending = false;
    rxanim::reset(actor);
}
void mobility_consumed() {
    player.committed = 0;
    player.committed_target = {};
    // Keep a new air attack pressed together with/after Jump, but discard an
    // older accepted attack which the deliberate movement replaced.
    if (player.queued && static_cast<int>(player.queue_until - player.jump_until) < 0) {
        player.queued = 0;
        player.queued_target = {};
    }
    player.jump_queued = false;
    player.defense_queued = false;
}
void defense_consumed() {
    // A deliberate defense replaces accepted/older offense, but a later tap
    // still belongs to the player's next action. Keep its target and deadline.
    player.committed = 0;
    player.committed_target = {};
    player.pending_finisher = 0;
    if (player.queued && (!queue_live() || static_cast<int>(player.queue_until - player.defense_until) <= 0)) {
        player.queued = 0;
        player.queued_target = {};
    }
    if (player.jump_queued && (static_cast<int>(player.jump_until - player.defense_until) <= 0 ||
        static_cast<int>(player.jump_until - input->frame()) < 0)) player.jump_queued = false;
    player.defense_queued = false;
}
bool cancel_open(KhRef actor, KhRef record) {
    if (!record || read<unsigned char>(record, 2) != 0) return false;
    const auto hit = rxmotion::snapshot();
    const int frame = read<int>(actor, 0x7b0);
    if (hit.actor == actor && hit.confirmed_hit && hit.attack_id == player.active_move &&
        player.hit_animation_frame >= 0)
        return frame >= player.hit_animation_frame + rxroutes::hit_cancel_delay(player.role) * 4096;
    const int contacts = read<std::int16_t>(record, 0x30);
    const KhRef channels = read<KhRef>(record, 0x34);
    if (contacts < 0 || contacts > 128 || (contacts && !channels)) return false;
    int last_contact = 0;
    for (int c = 0; c < contacts; ++c) {
        if (!bytes(channels + 0x48u * c)) return false;
        last_contact = std::max(last_contact, read<int>(channels + 0x48u * c, 8));
    }
    return frame >= last_contact + rxroutes::recovery_frames(player.role) * 4096;
}
int post_contact_defense(KhRef actor, KhRef record) {
    if (!defensive_cancels || !record || !player.defense_queued ||
        static_cast<int>(player.defense_until - input->frame()) < 0 ||
        (read<unsigned>(actor, 0x24) & 4u) == 0) return 0;
    if (!cancel_open(actor, record)) return 0;
    const auto flags = read<std::uint64_t>(actor);
    const auto flags2 = read<std::uint64_t>(actor, 0x464);
    if ((flags & (1ull << 4)) || ((flags & (1ull << 36)) && (flags2 & (1ull << 7)))) return 0;
    int state = -1;
    if (read<int>(actor, 0x2abc) != -1 && roll_allowed(actor + 0x118c)) state = 8;
    else if (guard_allowed(actor + 0x1070)) state = 11;
    if (state < 0) return 0;
    const int result = set_state(actor, state);
    if (!result) return 0;
    // This cached native setter bypasses change_state. Notify motion only once
    // the defense was accepted, so an old pursuit cannot outlive its Jump tap.
    rxmotion::state_changed(actor, state);
    combo_exit(actor, true);
    defense_consumed();
    say(state == 8 ? "Kingdom Hearts 358/2 Days: Pragmicat Mix: dodge cancel; combo route preserved." : "Kingdom Hearts 358/2 Days: Pragmicat Mix: guard cancel; combo route preserved.");
    return result;
}

int steer(KhRef actor, int aim) {
    if (actor != player.actor || !controllable(actor)) return game_steer(actor, aim);
    const int speed = read<int>(actor, 0x4b8), turn = read<int>(actor, 0x4b0);
    const bool moving = aim >= 0 && (read<std::uint64_t>(actor, 0x464) & (1ull << 19)) != 0;
    if (moving) {
        write<int>(actor, 0x4b8, static_cast<int>((static_cast<std::int64_t>(speed) * move_fx) >> 12));
        write<int>(actor, 0x4b0, static_cast<int>((static_cast<std::int64_t>(turn) * turn_fx) >> 12));
    }
    const int result = game_steer(actor, aim);
    // These are temporary inputs to the original collision-respecting mover.
    // Restore the authored values so successive frames cannot compound the boost.
    write<int>(actor, 0x4b8, speed);
    write<int>(actor, 0x4b0, turn);
    if (!result && moving) {
        const int original_scale = read<int>(actor, 0x458);
        set_speed(actor, static_cast<int>((static_cast<std::int64_t>(original_scale) * move_fx) >> 12));
    }
    return result;
}

int step(KhRef actor) {
    discard_forced_intentions(actor);
    if (actor != player.actor || !controllable(actor) || player.banks.empty()) return game_step(actor);
    sample_input(actor);
    if (player.defense_queued)
        write<std::uint16_t>(actor, 0x18, static_cast<std::uint16_t>(read<std::uint16_t>(actor, 0x18) & ~0x800u));
    if (form_transition_pending()) {
        write<std::uint16_t>(actor, 0x18, static_cast<std::uint16_t>(read<std::uint16_t>(actor, 0x18) & ~1u));
        write<int>(actor + kTableOffset, 0x47c, 0);
        if (read<std::uint16_t>(actor, 0x1c) == 1) write<std::uint16_t>(actor, 0x1c, 0);
    }
    const KhRef table = actor + kTableOffset;
    const KhRef node = read<KhRef>(table, 0x470);
    const KhRef record = node ? read<KhRef>(node, 0x14) : 0;
    if (const int defense = post_contact_defense(actor, record)) return defense;
    const bool can_cancel = cancel_open(actor, record);
    if (form_transition_requested() && read<std::uint16_t>(actor, 0x1c) == 4 &&
        read<signed char>(actor, 0x4dc) == 8) write<std::uint16_t>(actor, 0x1c, 0);
    if (form_transition_pending() && can_cancel) {
        const int state = (read<unsigned>(actor, 0x24) & 4u) ? 0 : 2;
        if (const int result = set_state(actor, state)) {
            combo_exit(actor, false);
            clear_queue(); player.jump_queued = false;
            return result;
        }
    }
    if (const KhRef magic = rxrules::try_magic_cancel(actor, can_cancel)) {
        player.resume_route = true;
        player.resume_until = input->frame() + 90;
        clear_queue(); player.jump_queued = false;
        return static_cast<int>(magic);
    }
    const bool jump_live = player.jump_queued && static_cast<int>(player.jump_until - input->frame()) >= 0;
    const auto hit = rxmotion::snapshot();
    const bool motion_pending = hit.chase_queued || hit.enemy_step_queued;
    if (jump_live || motion_pending) {
        // Acceptance belongs to the motion module's bounded queue. Do not
        // require the original hit/tap to be fresh again or renew its deadline
        // while the current attack waits for its safe exit.
        bool requested = motion_pending;
        if (jump_live && !requested) {
            if (hit.launcher) requested = rxmotion::request_chase(actor);
            else if ((read<unsigned>(table) & 4u) != 0) requested = rxmotion::request_enemy_step(actor);
            if (requested) player.motion_jump_intent = player.jump_intent;
        }
        if (requested && can_cancel) {
            if (const KhRef chase = rxmotion::step(actor)) {
                combo_exit(actor, true);
                mobility_consumed();
                return static_cast<int>(chase);
            }
            const auto remaining = rxmotion::snapshot();
            if (!remaining.chase_queued && !remaining.enemy_step_queued &&
                player.jump_intent == player.motion_jump_intent)
                player.jump_queued = false; // a rejected/expired accepted tap cannot renew itself
        }
        const bool awaiting_rise = jump_live && hit.launcher && requested && !hit.launcher_confirmed &&
            static_cast<unsigned>(input->frame() - hit.hit_frame) <= 4;
        const auto flags = read<std::uint64_t>(actor);
        const bool held = (flags & (1ull << 36)) && (read<std::uint64_t>(actor, 0x464) & (1ull << 7));
        if (jump_live && can_cancel && !awaiting_rise && (read<unsigned>(actor, 0x24) & 4u) && !(flags & (1ull << 4)) && !held) {
            const int horizontal_x = read<int>(actor, 0x698), horizontal_z = read<int>(actor, 0x6a0);
            if (const int jumped = start_jump(actor, 0)) {
                // Same native launch preparation and momentum retention as
                // StepComboState's jump phase; no synthetic flight or invulnerability.
                write<int>(actor, 0x698, horizontal_x);
                write<int>(actor, 0x6a0, horizontal_z);
                combo_exit(actor, true);
                mobility_consumed();
                return jumped;
            }
        }
        if (awaiting_rise)
            write<std::uint16_t>(actor, 0x18, static_cast<std::uint16_t>(read<std::uint16_t>(actor, 0x18) & ~2u));
    }
    if (record && requested_action() && (!player.active_finisher || requested_action() >= 3)) {
        const int frame = read<int>(actor, 0x7b0);
        const int window_lo = read<int>(record, 0x10);
        const int window_hi = read<int>(record, 0x14);
        // Buffering can select a follow-up only in the move's authored combo
        // window. The native state still finishes its contacts before advancing.
        if (frame >= window_lo && (window_hi == -0x1000 || frame < window_hi)) {
            if (!player.committed) {
                player.committed = player.queued;
                player.committed_direction = player.direction;
                player.committed_target = player.queued_target;
            }
            write<int>(table, 0x47c, 1);
            write<std::uint16_t>(actor, 0x1c, 1);
        }
    }
    if (player.active_finisher && requested_action() < 3) {
        // A finisher must recover instead of becoming an endless stun loop.
        write<int>(table, 0x47c, 0);
        write<std::uint16_t>(actor, 0x1c, 0);
    }
    const int result = game_step(actor);
    if (!result && player.pending_finisher) {
        const auto kind = player.pending_finisher == 2 ? rxlimit::Finisher::LightBurst :
            player.pending_finisher == 3 ? rxlimit::Finisher::Flurry : rxlimit::Finisher::Rush;
        player.pending_finisher = 0;
        if (const KhRef changed = rxlimit::begin(actor, kind)) {
            clear_queue();
            player.defense_queued = false;
            say(kind == rxlimit::Finisher::Rush ? "Kingdom Hearts 358/2 Days: Pragmicat Mix: ordinary Event Horizon rush finisher." :
                kind == rxlimit::Finisher::Flurry ? "Kingdom Hearts 358/2 Days: Pragmicat Mix: ordinary Event Horizon flurry finisher." :
                "Kingdom Hearts 358/2 Days: Pragmicat Mix: ordinary Event Horizon light-burst finisher.");
            return static_cast<int>(changed);
        }
    } else if (result) player.pending_finisher = 0;
    // Original per-attack resets restore normal animation speed; keep only this
    // actor's native combo animations at the configured multiplier.
    if (result == 0 && controllable(actor)) set_speed(actor, speed_fx);
    return result;
}

void accepted_hit(KhRef attacker, KhActor target, unsigned serial, unsigned tick, void*) {
    if (attacker == player.actor && serial != player.hit_serial) {
        player.hit_serial = serial;
        player.hit_animation_frame = read<int>(attacker, 0x7b0);
    }
    rxdrive::on_hit(attacker, target, serial, tick);
}
bool before_form_replace(KhRef actor, int, void*) {
    if (actor != player.actor || rxlimit::active(actor) || rxmotion::snapshot().chasing) return false;
    rxanim::reset(actor);
    forget_rules(actor);
    rxmotion::reset();
    clear_queue();
    return true;
}
void after_form_replace(KhRef, KhRef actor, int, bool, void*) {
    if (actor == player.actor && bytes(player.table)) {
        // Constructor graph ownership was captured by build(); the adapter then
        // restores the player's actual equipped numeric weapon stats.
        std::memcpy(player.original.data() + 8, bytes(player.table) + 8, 0x20);
        rxmotion::bind(actor);
        rxrules::bind(actor);
        last_input_frame = input->frame();
        player.resume_route = false;
    }
}
void drive_log(const char* message, void*) { say(message); }
KhRef signature_exit(KhRef actor, rxlimit::Finisher, int) {
    sample_input(actor);
    if (form_transition_pending() && !player.defense_queued) {
        if (const KhRef result = rxlimit::cancel(actor)) {
            combo_exit(actor, false); clear_queue(); player.jump_queued = false;
            return result;
        }
    }
    if (!player.defense_queued || static_cast<int>(player.defense_until - input->frame()) < 0) return 0;
    const bool ground = (read<unsigned>(actor, 0x24) & 4u) != 0;
    int state = -1;
    if (ground && read<int>(actor, 0x2abc) != -1 && roll_allowed(actor + 0x118c)) state = 8;
    else if (ground && guard_allowed(actor + 0x1070)) state = 11;
    else if (!ground) state = 2; // an air break, with native fall and no extra invulnerability
    if (state < 0) return 0;
    const KhRef recovered = rxlimit::cancel(actor);
    if (!recovered) return 0;
    const KhRef result = static_cast<KhRef>(set_state(actor, state));
    combo_exit(actor, true);
    defense_consumed();
    return result ? result : recovered;
}
void frame_event(const KhEvent* event, void*) {
    paused = event->b != 0 || menu_depth != 0;
    rxlimit::set_suspended(paused || (cinematic_flag && read<unsigned char>(cinematic_flag)));
    const bool scene_live = engine->module_loaded(22) && read<int>(scene_ctl, 8) == 2;
    const bool story = scene_live && boot_flags && (read<unsigned char>(boot_flags) & 4u) == 0;
    const KhRef actor = player.actor;
    const bool live = actor && live_local_actor(actor);
    const bool cinematic = cinematic_flag && read<unsigned char>(cinematic_flag);
    discard_forced_intentions(actor);
    const auto motion = rxmotion::snapshot();
    const bool safe = live && controllable(actor) && !rxlimit::active(actor) && !motion.chasing &&
        read<int>(actor, 0x6bc) >= 0 && read<int>(actor, 0x6bc) <= 4;
    rxdrive::frame({actor, input->frame(), story && drive_enabled, paused, cinematic,
                    live && controllable(actor), safe});
    if (!practice_meter_primed && drive_enabled && rxpractice::active() &&
        live && controllable(actor))
        practice_meter_primed = rxdrive::prime_practice_meter(actor);
    rxhud::update(rxdrive::view(), drive_meter && drive_enabled && story && live &&
        known_player(player.actor) && !paused && !cinematic);
    if (player.actor && controllable(player.actor)) {
        // The frame event still runs when an actor update is skipped. Capture
        // brief taps here; sample_input's frame stamp prevents double sampling
        // when translate/step already handled this pad read.
        sample_input(player.actor);
    } else if (rxlimit::active(player.actor) && finisher_permitted(player.actor)) {
        sample_input(player.actor);
    } else input->hide(host->plugin, 0);
}
void menu_open(const KhEvent*, void*) {
    ++menu_depth; paused = true; discard_intentions(player.actor);
    rxlimit::set_suspended(true); rxanim::reset(); input->hide(host->plugin, 0);
}
void menu_close(const KhEvent*, void*) { if (menu_depth) --menu_depth; paused = menu_depth != 0; }
void scene_event(const KhEvent*, void*) {
    practice_meter_primed = false;
    clear_queue(); player.defense_queued = false; player.pending_finisher = 0;
    player.jump_queued = player.resume_route = false; menu_depth = 0;
    rxlimit::forget(); rxanim::reset(); rxmotion::reset(); rxdrive::reset(); input->hide(host->plugin, 0);
}

template<class T> bool routine(T& target, const char* name) {
    target = reinterpret_cast<T>(engine->routine(name, nullptr));
    if (!target) { char log[180]; std::snprintf(log, sizeof log, "Required native routine missing: %s.", name); say(log, KH_LOG_ERROR); }
    return target != nullptr;
}
template<class T> bool replace(T& previous, const char* name, T replacement) {
    void* old = nullptr;
    if (engine->replace(name, reinterpret_cast<void*>(replacement), &old) != KH_OK) return false;
    previous = reinterpret_cast<T>(old);
    return true;
}
KhResult start(const KhHost* given) {
    if (!given || given->size < sizeof(KhHost) || given->abi_major != KH_ABI_MAJOR ||
        given->abi_minor < KH_ABI_MINOR || !given->find || !given->log ||
        !given->port_version || std::strcmp(given->port_version, "0.1.5") != 0) return KH_UNSUPPORTED;
    host = given;
    engine = static_cast<const KhEngine*>(host->find(host->plugin, KH_ENGINE, KH_ENGINE_VERSION));
    actors = static_cast<const KhActors*>(host->find(host->plugin, KH_ACTORS, KH_ACTORS_VERSION));
    input = static_cast<const KhInput*>(host->find(host->plugin, KH_INPUT, KH_INPUT_VERSION));
    net = static_cast<const KhNet*>(host->find(host->plugin, KH_NET, KH_NET_VERSION));
    auto* events = static_cast<const KhEvents*>(host->find(host->plugin, KH_EVENTS, KH_EVENTS_VERSION));
    auto* parameters = static_cast<const KhParameters*>(host->find(host->plugin, KH_PARAMETERS, KH_PARAMETERS_VERSION));
    if (!engine || !actors || !input || !net || !events || engine->size < sizeof(KhEngine) ||
        actors->size < sizeof(KhActors) || input->size < sizeof(KhInput) || net->size < sizeof(KhNet) ||
        events->size < sizeof(KhEvents) || !engine->pointer || !engine->routine || !engine->datum ||
        !engine->module_loaded || !engine->replace || !actors->local_seat || !actors->player || !actors->ref || !input->hide || !input->frame ||
        !input->pressed || !input->held || !actors->position || !net->in_room || !events->subscribe) return KH_UNSUPPORTED;
    if (parameters && parameters->size >= sizeof(KhParameters) && parameters->integer && parameters->number) {
        buffer_ticks = std::clamp(parameters->integer(host->plugin, "buffer_ms", 200), 50, 400) * 60 / 1000;
        speed_fx = static_cast<int>(std::clamp(parameters->number(host->plugin, "attack_speed", 1.25), 1.0, 1.6) * 4096);
        move_fx = static_cast<int>(std::clamp(parameters->number(host->plugin, "movement_speed", 1.15), 1.0, 1.4) * 4096);
        turn_fx = static_cast<int>(std::clamp(parameters->number(host->plugin, "turn_speed", 1.3), 1.0, 1.6) * 4096);
        defensive_cancels = parameters->integer(host->plugin, "defensive_cancels", 1) != 0;
        repertoire_debug = parameters->integer(host->plugin, "repertoire_debug", 0) != 0;
        drive_enabled = parameters->integer(host->plugin, "drive_enabled", 1) != 0;
        drive_meter = parameters->integer(host->plugin, "drive_meter", 1) != 0;
        const int hits = std::clamp(parameters->integer(host->plugin, "drive_charge_hits", 40), 10, 80);
        drive_settings.charge_per_hit = static_cast<unsigned>(1000 / hits);
        drive_settings.duration_ticks = static_cast<unsigned>(std::clamp(parameters->integer(host->plugin, "drive_seconds", 15), 8, 30) * 60);
        signature_damage_fx = static_cast<int>(std::clamp(parameters->number(host->plugin, "signature_damage", 1.0), .25, 1.0) * 4096);
    }
    scene_ctl = engine->datum("gSceneCtl");
    cinematic_flag = engine->datum("data_0204be04");
    boot_flags = engine->datum("data_0204c240");
    if (!scene_ctl || !boot_flags || !routine(game_alloc, "NNSi_FndAllocFromDefaultExpHeap") ||
        !routine(game_free, "NNSi_FndFreeFromDefaultHeap") || !routine(heap_get, "Heap_GetCurrent") ||
        !routine(heap_set, "Heap_SetCurrent") || !routine(reset_grid, "Ov022_ResetChannelGrid") ||
        !routine(set_animation, "Ov022_SetAnimState") || !routine(set_state, "Ov022_ActorSetState") ||
        !routine(start_jump, "Ov022_ResetTimersAndMaybeSignal") ||
        !routine(process_pair, "func_ov022_0209c700") || !routine(clear_node, "func_ov022_020b09c4") ||
        !routine(roll_allowed, "Ov022_IsBit0Set_2") || !routine(guard_allowed, "Ov022_IsBit0Set") ||
        !routine(heading_from_input, "Ov022_HeadingFromHeldDpad") || !routine(validate_target, "Ov022_ValidateTargetRef") ||
        !routine(target_position, "func_ov022_020ad0c0") ||
        !routine(resolve_target_pick, "Ov022_ResolveTargetPick") ||
        !routine(set_speed, "Ov022_SetAnimSpeed") || !routine(input_blocked, "Ov022_IsInputAllowedForActiveSlot")) return KH_UNSUPPORTED;
    if (!replace(game_build, "Ov022_BuildActionTable", &build) ||
        !replace(game_teardown, "Ov022_TearDownActionTable", &teardown) ||
        !replace(game_walk, "Ov022_StartGroupWalk", &walk) ||
        !replace(game_next_a, "func_ov022_020b09e0", &next_a) ||
        !replace(game_next_b, "func_ov022_020b0a54", &next_b) ||
        !replace(game_translate, "Ov022_TranslateCommandToAction", &translate) ||
        !replace(game_steer, "Ov022_SteerIdleByAim", &steer) ||
        !replace(game_set_state, "Ov022_ActorSetState", &change_state) ||
        !replace(game_step, "Ov022_StepComboState", &step)) return KH_UNSUPPORTED;
    if (!rxlimit::attach(engine, &finisher_permitted)) return KH_UNSUPPORTED;
    if (!rxweapon::init(engine, actors, net, &known_player, &drive_log) ||
        !rxrules::attach(engine, actors, &controllable) ||
        !rxmotion::attach(engine, actors, input, &controllable) ||
        !rxanim::attach(engine, &controllable) ||
        !rxdrive::init(host, engine, actors, net, {&before_form_replace, &after_form_replace, &drive_log, nullptr}, drive_settings) ||
        !rxpractice::init(host, engine, actors, net))
        return KH_UNSUPPORTED;
    practice_meter_primed = false;
    rxmotion::observe_hit(&accepted_hit);
    rxmotion::observe_hit_secondary(&rxrules::on_confirmed_melee_hit);
    rxmotion::set_packet_policy(&rxrules::before_native_hit);
    rxlimit::set_exit_handler(&signature_exit);
    rxlimit::set_damage_scale_fx(signature_damage_fx);
    if (!rxhud::start(drive_meter && drive_enabled)) say("Drive display could not start; combat remains enabled.", KH_LOG_WARNING);
    events->subscribe(host->plugin, "frame", &frame_event, nullptr);
    events->subscribe(host->plugin, "scene", &scene_event, nullptr);
    events->subscribe(host->plugin, "menu_open", &menu_open, nullptr);
    events->subscribe(host->plugin, "menu_close", &menu_close, nullptr);
    say("Kingdom Hearts 358/2 Days: Pragmicat Mix ready: directional techniques, chosen signature finishers, hit-confirm exits, launch pursuit, Enemy Step, magic weave and timed native dual-wield Drive. Single-player campaign; no scripted controls.");
    return KH_OK;
}
void stop() {
    rxpractice::stop();
    practice_meter_primed = false;
    rxhud::stop();
    rxdrive::stop();
    rxweapon::stop();
    stopping = true;
    rxanim::detach();
    rxmotion::detach();
    forget_rules(player.actor);
    rxrules::detach();
    rxlimit::forget();
    rxlimit::detach();
    input->hide(host->plugin, 0);
    if (player.actor && engine->module_loaded(22) && live_local_actor(player.actor) && bytes(player.table)) {
        const KhRef previous_heap = heap_set(player.heap);
        std::memcpy(bytes(player.table), player.original.data(), kTableSize);
        if (set_animation && bytes(player.actor)) set_animation(player.actor, 0);
        release_banks();
        heap_set(previous_heap);
    }
    player = Player{};
    say("Kingdom Hearts 358/2 Days: Pragmicat Mix stopped; original action-table ownership restored.");
}
const KhPluginDescriptor descriptor = KH_PLUGIN_DESCRIPTOR("Kingdom Hearts 358/2 Days: Pragmicat Mix", &start, &stop);
}
extern "C" KH_PLUGIN_EXPORT const KhPluginDescriptor* kh_plugin_query() { return &descriptor; }
