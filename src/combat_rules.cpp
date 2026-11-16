#include "combat_rules.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>

namespace rxrules {
namespace {
const KhEngine* engine;
const KhActors* actors;
Permitted permitted;
bool installed;
KhActor owner;
Snapshot state;
Reaction reaction;
using SetupGuard = void (*)(KhRef, int, KhRef);
using SetupDodge = void (*)(KhRef, int, KhRef, int);
using DestroyGuard = void (*)(KhRef, unsigned, unsigned, unsigned);
using NativeRequest = KhRef (*)(KhRef, int);
using SlotAllowed = int (*)(KhRef, unsigned);
using GetCount = int (*)(int, int);
using SetCount = void (*)(int, int, int);
using GetParam = int (*)(int, int);
using NativeCleanup = void (*)(KhRef);
SetupGuard setup_guard;
SetupDodge setup_dodge;
DestroyGuard destroy_guard;
NativeRequest try_request;
SlotAllowed slot_allowed;
GetCount get_count;
SetCount set_count;
GetParam get_param;
NativeCleanup process_pair, clear_combo;
KhRef party;
std::array<unsigned char, 0x11c> original_guard{};
std::array<unsigned char, 12> original_dodge{};
unsigned last_credit_serial, last_restore_frame, window_start, window_restores;
bool have_restore_frame, have_window;
int round_robin;
struct Contact {
    KhActor target = 0;
    unsigned serial = 0;
    unsigned frame = 0;
    bool physical = false;
};
std::array<Contact, 16> contacts{};
struct Lift {
    KhActor target = 0;
    unsigned frame = 0;
    unsigned serial = 0;
    unsigned last_request = 0;
    unsigned carries = 0;
    bool valid = false;
};
std::array<Lift, 32> lifts{};
unsigned lift_cursor;
constexpr unsigned kHitsPerCharge = 6;
constexpr unsigned kRestoreSpacing = 180; // 3 seconds, frame clock is 60 Hz
constexpr unsigned kWindow = 1800;       // at most six charges in 30 seconds
constexpr unsigned kRestoresPerWindow = 6;
constexpr unsigned kLiftSpacing = 90;    // no repeated lift requests for 1.5 s

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
unsigned now() { return engine ? engine->frame() : 0; }
bool live(KhRef actor) {
    if (!installed || !actors || !actor || actor != state.actor || !owner ||
        !engine->module_loaded(22) || !bytes(actor)) return false;
    return actors->player(0) == owner && actors->ref(owner) == actor &&
           actors->kind(owner) == KH_ACTOR_PLAYER;
}
bool allowed(KhRef actor) {
    return live(actor) && permitted && permitted(actor) && get<int>(actor, 0xc) == 0 &&
           get<unsigned char>(actor, 8) == 0 && get<unsigned char>(actor, 9) == 0 &&
           get<std::uint16_t>(actor, 0x12) > 0;
}
template<class T> T routine(const char* name) {
    return reinterpret_cast<T>(engine->routine(name, nullptr));
}
// Deliberately narrow: source-mapped small enemies only. Heavy/armored enemies,
// minigame props, special scripted actors and every boss keep native reactions.
bool light_kind(int kind) {
    switch (kind) {
    case 0: case 1: case 2: case 3: case 4: case 6: case 9: case 10:
    case 12: case 13: case 14: case 15: case 17: case 18: case 19:
    case 24: case 25: case 26: case 27: case 28: case 29: case 30:
    case 31: case 32: case 34: case 36: case 37: case 39: case 40:
    case 43: case 59: case 66: case 111:
        return true;
    default: return false;
    }
}
Contact* remember_contact(KhActor target, bool physical) {
    for (auto& c : contacts) if (c.target == target || !c.target) {
        c = {target, state.attack_serial, now(), physical}; return &c;
    }
    return nullptr;
}
bool lift_recent(KhActor target) {
    for (const auto& lift : lifts) if (lift.valid && lift.target == target) {
        if (lift.serial == state.attack_serial) return true;
        if (static_cast<unsigned>(now() - lift.frame) >= kLiftSpacing) return false;
        if (reaction == Reaction::Launcher || lift.carries >= 1 ||
            static_cast<unsigned>(now() - lift.last_request) < 12) return true;
    }
    return false;
}
void remember_lift(KhActor target) {
    for (auto& lift : lifts) if (lift.valid && lift.target == target) {
        if (static_cast<unsigned>(now() - lift.frame) >= kLiftSpacing) lift = {};
        if (!lift.valid || reaction == Reaction::Launcher) {
            lift = {target, now(), state.attack_serial, now(), 0, true};
        } else { lift.serial = state.attack_serial; lift.last_request = now(); }
        if (reaction == Reaction::Carry) ++lift.carries;
        return;
    }
    lifts[lift_cursor++ % lifts.size()] = {target, now(), state.attack_serial, now(),
                                         reaction == Reaction::Carry ? 1u : 0u, true};
}
bool restore_one() {
    // Only the twelve offensive spells already equipped AND built for this actor.
    // Healing (cu, slots12..14) never receives a free charge.
    const unsigned built = get<unsigned>(state.actor, 0x2290);
    const int seat = get<unsigned char>(state.actor, 9);
    for (int step = 0; step < 12; ++step) {
        const int spell = (round_robin + step) % 12;
        if (!(built & (1u << spell)) || get_param(seat, spell + 1) <= 0) continue;
        const KhRef entry = party + static_cast<unsigned>(seat) * 0x104 + 0x9c + static_cast<unsigned>(spell) * 2;
        const int cap = get<unsigned char>(entry, 1), current = get_count(seat, spell);
        if (cap <= 0 || current < 0 || current >= cap) continue;
        set_count(seat, spell, current + 1); // original clamping routine
        if (get_count(seat, spell) <= current) continue;
        put<std::uint64_t>(state.actor, 0x46c, get<std::uint64_t>(state.actor, 0x46c) | (1ull << 41));
        state.last_restored_spell = spell;
        ++state.restored_charges;
        round_robin = (spell + 1) % 12;
        return true;
    }
    return false;
}
}

bool attach(const KhEngine* e, const KhActors* a, Permitted allow) {
    if (installed || !e || !a || !allow || e->size < sizeof(KhEngine) ||
        a->size < sizeof(KhActors) || !e->pointer || !e->routine || !e->datum ||
        !e->module_loaded || !e->frame || !a->player || !a->ref || !a->kind ||
        !a->of || !a->character) return false;
    engine = e; actors = a; permitted = allow;
    setup_guard = routine<SetupGuard>("Ov022_SetUpTrailEffect");
    setup_dodge = routine<SetupDodge>("func_ov022_02092880");
    destroy_guard = routine<DestroyGuard>("Ov022_TeardownIfBit0");
    try_request = routine<NativeRequest>("Ov022_TryPendingAction");
    slot_allowed = routine<SlotAllowed>("Ov022_IsSlotActionAllowed");
    get_count = routine<GetCount>("Load2DArrayU8");
    set_count = routine<SetCount>("ClampAndStoreLevelEntry");
    get_param = routine<GetParam>("Slot_EvalPackedParam");
    process_pair = routine<NativeCleanup>("func_ov022_0209c700");
    clear_combo = routine<NativeCleanup>("func_ov022_020b09c4");
    party = e->datum("gPartyMembers");
    if (!setup_guard || !setup_dodge || !destroy_guard || !try_request || !slot_allowed ||
        !get_count || !set_count || !get_param || !process_pair || !clear_combo || !party) {
        engine = nullptr; actors = nullptr; permitted = nullptr; return false;
    }
    installed = true;
    return true;
}
void forget(KhRef actor) {
    if (actor && actor != state.actor) return;
    if (live(state.actor)) {
        if (state.guard_granted) {
            destroy_guard(state.actor + 0x1070, 0, 0, 0);
            std::memcpy(bytes(state.actor) + 0x1070, original_guard.data(), original_guard.size());
        }
        if (state.dodge_granted)
            std::memcpy(bytes(state.actor) + 0x118c, original_dodge.data(), original_dodge.size());
    }
    state = {}; owner = 0; contacts = {}; lifts = {}; reaction = Reaction::Native;
    last_credit_serial = last_restore_frame = window_start = window_restores = lift_cursor = 0;
    have_restore_frame = have_window = false; round_robin = 0;
}
void detach() {
    forget(); installed = false; engine = nullptr; actors = nullptr; permitted = nullptr;
}
void bind(KhRef actor) {
    if (actor == state.actor && live(actor)) return;
    forget();
    if (!installed || !actor) return;
    const KhActor current = actors->player(0);
    if (current && actors->ref(current) == actor && actors->kind(current) == KH_ACTOR_PLAYER) {
        owner = current; state.actor = actor;
    }
}
void refresh_mobility(KhRef actor) {
    if (!allowed(actor)) return;
    // Both animations are in the original effect files, loaded independently of
    // the panel abilities. Do not enable a resource-owning block by flipping a bit.
    if (!state.guard_granted && !(get<unsigned char>(actor, 0x1070) & 1u) &&
        get<KhRef>(actor, 0x2ba4)) {
        std::memcpy(original_guard.data(), bytes(actor) + 0x1070, original_guard.size());
        setup_guard(actor + 0x1070, 1, actor);
        state.guard_granted = (get<unsigned char>(actor, 0x1070) & 1u) != 0;
    }
    if (!state.dodge_granted && !(get<unsigned char>(actor, 0x118c) & 1u) && get<KhRef>(actor, 0x2ba0)) {
        std::memcpy(original_dodge.data(), bytes(actor) + 0x118c, original_dodge.size());
        setup_dodge(actor + 0x118c, 1, actor, 0);
        state.dodge_granted = (get<unsigned char>(actor, 0x118c) & 1u) != 0;
    }
}
void attack_started(KhRef actor, unsigned serial, Reaction style) {
    if (!allowed(actor) || !serial) return;
    state.attack_serial = serial; reaction = style; contacts = {};
}
void before_native_hit(KhRef attacker, KhRef enemy, KhRef packet, void*) {
    if (!allowed(attacker) || !state.attack_serial || !enemy || !bytes(enemy) || !packet || !bytes(packet)) return;
    const KhActor target = actors->of(enemy);
    if (!target || actors->kind(target) != KH_ACTOR_ENEMY || actors->ref(target) != enemy) return;
    const unsigned flags = get<std::uint16_t>(packet), kinds = get<std::uint16_t>(packet, 2);
    const bool physical = (flags & 4u) && !(flags & (8u | 0x10u)) && get<KhRef>(packet, 0x18);
    remember_contact(target, physical);
    if (!physical || (flags & 0x80u) || reaction == Reaction::Native ||
        !light_kind(actors->character(target)) || lift_recent(target)) return;
    // Preserve damage and all original packet fields. Only pure physical packets
    // without another status receive the native toss request. CalcHitDamage does
    // not use its high-kind resistance sum when low flag8 is clear.
    if ((kinds & 0x88cu) || !get<KhRef>(packet, 0x18) ||
        get<std::int16_t>(enemy, 0x21a) <= 0 || get<int>(enemy, 0x50) != 1 ||
        !(get<std::uint16_t>(enemy, 0x60) & 1u) || get<int>(enemy, 0x2e4) > 0 ||
        (get<std::uint16_t>(enemy, 0x1ac) & (1u | 4u | 0x20u | 0x40u | 0x100u)) ||
        (get<std::uint16_t>(enemy, 0x1b0) & 4u) || get<int>(enemy, 0x1c0) >= 4096 ||
        (get<unsigned char>(enemy, 0x1c4) & 0x10u)) return;
    const bool airborne = (get<unsigned char>(enemy, 0x1c4) & 8u) != 0;
    if ((reaction == Reaction::Carry && !airborne) || (reaction == Reaction::Launcher && airborne)) return;
    put<std::uint16_t>(packet, 2, static_cast<std::uint16_t>(kinds | 4u));
    put<std::int8_t>(packet, 0x1c, 100); // resistance still runs in original ApplyHit
    remember_lift(target); ++state.lift_requests;
}
void on_confirmed_melee_hit(KhRef attacker, KhActor target, unsigned serial, unsigned frame, void*) {
    if (!allowed(attacker) || !serial || serial != state.attack_serial || serial == last_credit_serial) return;
    bool physical = false;
    for (const auto& contact : contacts)
        if (contact.target == target && contact.serial == serial && contact.frame == frame && contact.physical)
            physical = true;
    if (!physical) return;
    last_credit_serial = serial;
    state.confirmed_actions = std::min(kHitsPerCharge, state.confirmed_actions + 1);
    if (state.confirmed_actions < kHitsPerCharge) return;
    if (have_restore_frame && static_cast<unsigned>(frame - last_restore_frame) < kRestoreSpacing) return;
    if (!have_window || static_cast<unsigned>(frame - window_start) >= kWindow) {
        have_window = true; window_start = frame; window_restores = 0;
    }
    if (window_restores >= kRestoresPerWindow) return;
    // No banking a full stockpile of six-hit refunds for later casts.
    state.confirmed_actions = 0;
    if (restore_one()) {
        ++window_restores; last_restore_frame = frame; have_restore_frame = true;
    }
}
KhRef try_magic_cancel(KhRef actor, bool contacts_finished) {
    if (!contacts_finished || !allowed(actor) || get<std::uint16_t>(actor, 0x1c) != 4 ||
        get<signed char>(actor, 0x4dc) != 8) return 0;
    const unsigned spell = get<std::uint16_t>(actor, 0x4de);
    if (spell >= 15 || !slot_allowed(actor + 0x2288, spell)) return 0;
    const KhRef result = try_request(actor, 0);
    if (!result || !live(actor)) return result;
    // Same exit cleanup as the original melee state; no fabricated state number,
    // projectile, contact volume, damage, target or resource deduction.
    put<std::uint64_t>(actor, 0x46c, get<std::uint64_t>(actor, 0x46c) & ~(1ull << 4));
    put<std::uint64_t>(actor, 0, (get<std::uint64_t>(actor) & ~(1ull << 32)) | (1ull << 29));
    process_pair(actor); clear_combo(actor + 0x910);
    state.attack_serial = 0; reaction = Reaction::Native; contacts = {};
    return result;
}
Snapshot snapshot() { return state; }
}
