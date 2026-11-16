#include "drive_form.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace rxdrive {
namespace {
constexpr unsigned kPartySize = 4 * 0x104;
constexpr unsigned kKind = 0xc, kHp = 0x12, kMaxHp = 0x16;
constexpr unsigned kPosition = 0x48c, kTarget = 0x4f0;
constexpr unsigned kChargeStatus = 0x2770, kRegen = 0x2bf0, kDot = 0x2c00;
const KhHost* host;
const KhEngine* engine;
const KhActors* actors;
const KhNet* net;
Callbacks callbacks;
Settings settings;
View state;
FrameContext last_context;
bool initialized = false, replacing = false, unload_hooked = false;
unsigned last_tick = 0, seen_serial = ~0u, serial_targets = 0;
std::array<KhActor, 16> targets{};
int normal_overlay = -1, drive_overlay = -1;
struct Appearance { int alternate = 0, group = 0; bool valid = false; } normal_appearance;
KhRef loader, party, scene, mode, overlay_slots, overlay_groups;
using Ref0 = KhRef (*)();
using Ref1 = KhRef (*)(KhRef);
using Ref2 = KhRef (*)(KhRef, KhRef);
using Int2 = int (*)(int, int);
using Int0 = int (*)();
using Void0 = void (*)();
using VoidInt = void (*)(int);
using VoidRef = void (*)(KhRef);
using VoidRefInt = void (*)(KhRef, int);
using SeatGroup = void (*)(int, int);
using Place = void (*)(int, KhRef);
Ref0 heap_get;
Ref1 heap_set, destroy_task;
Ref2 instantiate;
Int2 acquire_overlay;
Int0 party_count, language;
VoidInt release_overlay, cleanup_entry, hide_entry, stop_voice;
VoidRef resolve_target;
VoidRefInt set_visible, set_hp;
Place place;
SeatGroup show_group;
KhRef (*allocate)(unsigned);
VoidRef free_block;
Void0 original_unload_party;

unsigned char* ptr(KhRef ref) { return engine ? static_cast<unsigned char*>(engine->pointer(ref)) : nullptr; }
bool range(KhRef ref, unsigned size) {
    return ref && size && ref <= UINT32_MAX - (size - 1) && ptr(ref) && ptr(ref + size - 1);
}
template<class T> T read(KhRef ref, unsigned offset = 0) {
    T value{};
    if (range(ref + offset, sizeof(T))) std::memcpy(&value, ptr(ref + offset), sizeof value);
    return value;
}
template<class T> void write(KhRef ref, unsigned offset, T value) {
    if (range(ref + offset, sizeof(T))) std::memcpy(ptr(ref + offset), &value, sizeof value);
}
void log(const char* message) {
    if (callbacks.log) callbacks.log(message, callbacks.user);
    else if (host && host->log) host->log(host->plugin, KH_LOG_INFO, message);
}
void reason(const char* text) { state.reason = text; }
bool live(KhRef actor) {
    if (!actors || !actor || actors->local_seat() != 0) return false;
    const KhActor handle = actors->player(0);
    return handle && actors->ref(handle) == actor && range(actor, 0x2c28);
}
bool story(KhRef actor) {
    return live(actor) && !net->in_room() && engine->module_loaded(22) && engine->module_loaded(29) &&
           read<int>(scene, 8) == 2 && (read<unsigned char>(mode) & 4u) == 0 &&
           read<unsigned char>(actor, 8) == 0 && read<unsigned char>(actor, 9) == 0;
}
const char* descriptor_name(int overlay) {
    switch (overlay) {
    case 30: return "gOv030RoxasClass";
    case 49: return "gOv049RoxasDualClass";
    case 68: return "gOv068RoxasDualClass";
    case 87: return "gOv087RoxasDualClass";
    case 104: return "gOv104RoxasDualClass";
    default: return nullptr;
    }
}
KhRef descriptor(int overlay) {
    const char* name = descriptor_name(overlay);
    if (!name || !engine->module_loaded(overlay)) return 0;
    const KhRef desc = engine->datum(name);
    const unsigned size = read<unsigned>(desc, 0xc);
    return range(desc, 20) && read<unsigned short>(desc) == 10 &&
           read<KhRef>(desc, 4) && read<KhRef>(desc, 8) &&
           size == (overlay == 30 ? 14952u : 12168u) ? desc : 0;
}
bool free_slot_for(int kind) {
    if (!range(overlay_slots, 16) || !range(overlay_groups + 16u * kind, 16)) return false;
    for (int i = 3; i >= 0; --i)
        if (read<int>(overlay_slots, 4u*i) == -1 && read<int>(overlay_groups, 16u*kind+4u*i) != -1) return true;
    return false;
}

struct Snapshot {
    KhRef actor = 0, root = 0, task = 0, heap = 0;
    int overlay = -1, kind = 0;
    std::int16_t group = -1;
    std::uint16_t hp = 0, hp_max = 0, angle = 0;
    std::uint64_t persistent_flags = 0;
    int interrupt_charge = 0;
    unsigned char aim_slot = 0;
    std::array<unsigned char, kPartySize> party_records{};
    std::array<unsigned char, 0x44 * 4> metadata{};
    std::array<unsigned char, 12> position{};
    std::array<unsigned char, 8> target_identity{};
    std::array<unsigned char, 0x18> status{};
    std::array<unsigned char, 0x10> regen{};
    std::array<unsigned char, 0x20> dot{};
    std::array<unsigned char, 0x20> weapon{};
    std::array<KhRef, 3> allies{};
    std::array<int, 8> config{};
    unsigned root_metadata = 0;
    KhRef callback = 0;
};
KhRef player_metadata;

bool snapshot(KhRef actor, Snapshot& out) {
    if (!story(actor) || !range(party, kPartySize) || !range(loader, 40) ||
        !range(player_metadata, static_cast<unsigned>(out.metadata.size()))) { reason("Drive requires a live Story battle."); return false; }
    out.root = read<KhRef>(loader, 4);
    if (!range(out.root, 0x40)) { reason("Party root is not ready."); return false; }
    out.task = read<KhRef>(out.root, 4);
    out.overlay = read<int>(out.root, 8);
    if (!range(out.task, 0x2c) || read<KhRef>(out.task, 0x20) != actor || !descriptor(out.overlay)) {
        reason("Player task does not match the audited native Roxas class."); return false;
    }
    out.actor = actor;
    out.kind = read<int>(actor, kKind);
    if (out.kind != 0 && out.kind != 19) { reason("Drive applies only to Roxas."); return false; }
    // Room membership is assigned after the class constructor by the native
    // party placement path. Reconstructing the class alone leaves it unassigned.
    // Target collection and collision use this group to index the room's lists.
    out.group = read<std::int16_t>(actor, 0x66);
    if (out.group < 0) { reason("Roxas must belong to a live room before transforming."); return false; }
    out.hp = read<std::uint16_t>(actor, kHp);
    out.hp_max = read<std::uint16_t>(actor, kMaxHp);
    if (!out.hp || out.hp > out.hp_max) { reason("Roxas must be alive to transform."); return false; }
    out.heap = read<KhRef>(out.task, 0x1c);
    out.angle = read<std::uint16_t>(read<KhRef>(actor, 0x20), 0x80);
    std::memcpy(out.position.data(), ptr(actor + kPosition), out.position.size());
    std::memcpy(out.target_identity.data(), ptr(actor + kTarget), out.target_identity.size());
    std::memcpy(out.status.data(), ptr(actor + kChargeStatus), out.status.size());
    std::memcpy(out.regen.data(), ptr(actor + kRegen), out.regen.size());
    std::memcpy(out.dot.data(), ptr(actor + kDot), out.dot.size());
    std::memcpy(out.weapon.data(), ptr(actor + 0x918), out.weapon.size());
    std::memcpy(out.party_records.data(), ptr(party), kPartySize);
    std::memcpy(out.metadata.data(), ptr(player_metadata), out.metadata.size());
    const auto flags = read<std::uint64_t>(actor);
    // The native area snapshot explicitly persists these two one-use survival
    // flags. Reconstructing must not refresh Second Chance/Auto-Life uses.
    out.persistent_flags = flags & 0x6000000000ull;
    out.interrupt_charge = read<int>(actor, 0x4b4);
    out.aim_slot = read<unsigned char>(actor, 0x2bb0);
    // Reproduce FillSelectorStates' full eight-word constructor argument. Word6
    // selects the alternate archive; word7 selects normal Roxas's name family.
    // Keep the original normal appearance across Drive, whose constructor does
    // not expose the normal-only family selector on its actor.
    const unsigned special = read<unsigned char>(party, 3);
    const Appearance appearance = out.kind == 19 && normal_appearance.valid ? normal_appearance :
        Appearance{party_count() > 2 || language() == 0x2a ? 0 : 1,
                   special == 0x14 || special == 0x15 ? 1 : 0, true};
    out.config = {read<unsigned char>(actor, 9), read<signed char>(actor, 0x4bc),
                  read<unsigned char>(actor, 8), (flags & 0x20) ? 1 : 0,
                  (flags & 0x10000) ? 1 : 0, (flags & (1ull << 36)) ? 1 : 0,
                  appearance.alternate, appearance.group};
    if (out.config[1] < 0) { reason("The original player entity slot is being released."); return false; }
    out.root_metadata = read<unsigned>(out.root, 0xc);
    out.callback = read<KhRef>(loader);
    for (int seat = 1; seat < 4; ++seat) out.allies[seat-1] = read<KhRef>(out.root, 4u + 12u*seat);
    return true;
}
bool unchanged_allies(const Snapshot& saved) {
    for (int seat = 1; seat < 4; ++seat)
        if (saved.allies[seat-1] != read<KhRef>(saved.root, 4u + 12u*seat)) return false;
    return true;
}
void restore_globals(const Snapshot& saved) {
    std::memcpy(ptr(party), saved.party_records.data(), kPartySize);
    std::memcpy(ptr(player_metadata), saved.metadata.data(), saved.metadata.size());
    write<KhRef>(loader, 0, saved.callback);
}

// Construct the original native class. No memcpy of an actor, animation bind,
// magic object, task object, or model is used anywhere in this adapter.
KhRef construct(const Snapshot& saved, int kind, int overlay, KhRef args) {
    const KhRef desc = descriptor(overlay);
    if (!desc) return 0;
    restore_globals(saved);
    write<unsigned char>(party, 3, static_cast<unsigned char>(kind));
    if (kind == 19) write<unsigned char>(party, 4, 1); // Story mode7's own dual weapon row
    // The native allocator does not zero scratch allocations. In particular,
    // CreateActor reads word7 (+0x1c); leaving it unspecified can select no path
    // and pass an uninitialized archive filename to the native loader.
    std::memset(ptr(args), 0, 48);
    std::memcpy(ptr(args), saved.config.data(), sizeof(saved.config));
    const KhRef task = instantiate(desc, args);
    // Construction may read party flags/counts, but may not refill or persist them.
    restore_globals(saved);
    if (!task || !range(task, 0x2c)) return 0;
    write<KhRef>(saved.root, 4, task);
    write<int>(saved.root, 8, overlay);
    const KhRef actor = read<KhRef>(task, 0x20);
    if (!range(actor, 0x2c28) || read<int>(actor, kKind) != kind ||
        !range(read<KhRef>(actor, 0x20), 0x14c)) {
        destroy_task(task);
        write<KhRef>(saved.root, 4, 0);
        return 0;
    }
    write<unsigned char>(saved.root, 0xc, static_cast<unsigned char>(kind));
    // Match the native room-placement order: submit the new entity to its
    // current room, then place it. The actor's constructor does not do this;
    // merely making its model visible leaves lock-on scanning group -1.
    show_group(0, saved.group);
    // The native setter resolves seat0 again and resets actor/sub-body placement.
    std::memcpy(ptr(args + 32), saved.position.data(), saved.position.size());
    place(0, args + 32);
    // wp.b's first two bytes select the form's animation/model resources and
    // bytes16..19 select its graph/kinds. Preserve those from the new form;
    // retain every numeric equipped-gear bonus from the outgoing live actor.
    std::memcpy(ptr(actor + 0x91a), saved.weapon.data() + 2, 14);
    std::memcpy(ptr(actor + 0x92c), saved.weapon.data() + 20, 12);
    write<std::uint16_t>(actor, kMaxHp, saved.hp_max);
    set_hp(actor, saved.hp);
    std::memcpy(ptr(actor + kChargeStatus), saved.status.data(), saved.status.size());
    std::memcpy(ptr(actor + kRegen), saved.regen.data(), saved.regen.size());
    std::memcpy(ptr(actor + kDot), saved.dot.data(), saved.dot.size());
    write<std::uint64_t>(actor, 0, (read<std::uint64_t>(actor) & ~0x6000000000ull) | saved.persistent_flags);
    write<int>(actor, 0x4b4, saved.interrupt_charge);
    write<unsigned char>(actor, 0x2bb0, saved.aim_slot);
    const KhRef node = read<KhRef>(actor, 0x20);
    write<std::uint16_t>(node, 0x80, saved.angle);
    write<std::uint16_t>(node, 4, read<std::uint16_t>(node, 4) | 0x20u);
    write<unsigned>(actor, 0x488, static_cast<std::uint16_t>(saved.angle - 0x8000u));
    std::memset(ptr(actor + kTarget), 0, 0x18);
    std::memcpy(ptr(actor + kTarget), saved.target_identity.data(), saved.target_identity.size());
    // Kind3 is another player's seat; avoid the original resolver's null-entry path.
    if (read<unsigned char>(actor, kTarget) == 3) {
        const int seat = read<unsigned char>(actor, kTarget + 1);
        if (seat > 3 || !actors->player(seat)) write<unsigned char>(actor, kTarget, 0);
    }
    if (read<unsigned char>(actor, kTarget)) resolve_target(actor);
    set_visible(actor, 1);
    restore_globals(saved);
    if (!unchanged_allies(saved)) {
        destroy_task(task);
        write<KhRef>(saved.root, 4, 0);
        return 0;
    }
    return actor;
}

bool transform(KhRef old_actor, int new_kind) {
    Snapshot saved;
    if (!snapshot(old_actor, saved)) return false;
    // The normal overlay is kept resident while driving. Restoration and rollback
    // never compete with allies for the normal Roxas overlay's only slot.
    int target_overlay = new_kind == 0 ? normal_overlay : -1;
    bool acquired = false;
    if (new_kind == 19) {
        if (!free_slot_for(19)) { reason("No spare native player overlay slot; gauge retained."); return false; }
        target_overlay = acquire_overlay(0, 19);
        acquired = true;
        write<KhRef>(loader, 0, saved.callback);
    }
    if (target_overlay < 0 || !descriptor(target_overlay)) {
        if (acquired && target_overlay >= 0) release_overlay(target_overlay);
        reason("Dual-wield native class did not load; original actor retained."); return false;
    }
    const KhRef previous_heap = heap_set(saved.heap);
    const KhRef args = allocate(48);
    if (!args || !range(args, 48)) {
        if (args) free_block(args);
        heap_set(previous_heap);
        if (acquired) release_overlay(target_overlay);
        reason("Native game heap has no room for transformation arguments."); return false;
    }
    // Preflight must finish before the owner detaches its combat bindings. A
    // retained actor never loses those bindings merely because loading failed.
    if (callbacks.before_replace && !callbacks.before_replace(old_actor, new_kind, callbacks.user)) {
        free_block(args);
        heap_set(previous_heap);
        if (acquired) release_overlay(target_overlay);
        reason("Waiting for the current combat action to finish."); return false;
    }
    replacing = true;
    cleanup_entry(0);
    hide_entry(0);
    stop_voice(saved.config[1]);
    // Match UnloadParty: the outgoing task remains registered through its
    // destructor, whose cleanup can legitimately resolve its seat again.
    destroy_task(saved.task); // original actor destructor and core bank teardown run here
    write<KhRef>(saved.root, 4, 0);
    KhRef made = construct(saved, new_kind, target_overlay, args);
    bool rolled_back = false;
    if (!made) {
        // The previous overlay was deliberately not released. Rebuild its exact
        // original class, restore current inventory/HP/status, and retain gauge.
        made = construct(saved, saved.kind, saved.overlay, args);
        rolled_back = true;
    }
    if (made) {
        if (!rolled_back && new_kind == 19) {
            normal_overlay = saved.overlay;
            drive_overlay = target_overlay;
            normal_appearance = {saved.config[6], saved.config[7], true};
        } else if (!rolled_back && new_kind == 0) {
            write<unsigned>(saved.root, 0xc, saved.root_metadata & ~0xffu);
            release_overlay(drive_overlay);
            drive_overlay = -1;
            normal_overlay = -1; // native party now owns the restored normal overlay
            normal_appearance = {};
        } else if (acquired) release_overlay(target_overlay);
        if (callbacks.after_replace) callbacks.after_replace(old_actor, made,
            rolled_back ? saved.kind : new_kind, rolled_back, callbacks.user);
    } else {
        restore_globals(saved);
        state.phase = Phase::Faulted;
        reason("Native reconstruction and normal rollback both failed; actor lifecycle fault.");
        log(state.reason);
    }
    free_block(args);
    heap_set(previous_heap);
    replacing = false;
    if (rolled_back) { reason("Transformation rolled back; original state and gauge restored."); return false; }
    if (!made) return false;
    reason(nullptr);
    last_context.actor = made;
    seen_serial = ~0u; serial_targets = 0; targets.fill(0);
    log(new_kind == 19 ? "Drive: native dual-wield Roxas entered; original inventory/HP preserved."
                       : "Drive: normal Story Roxas restored; inventory/HP preserved.");
    return true;
}

bool owned_spells_finished(KhRef actor) {
    // The native destructor owns these channels, including projectiles which can
    // outlive the casting animation. Never tear down an idle caster with live
    // parts. Common channels use BindSlotParts/StepSlotParts; reaction channels
    // 5 and 11..14 have a smaller allocation and use their own state at +0x118.
    // +0x118 in the common layout is a fixed spell-kind tag, not a live state.
    const KhRef pool = actor + 0x2288;
    const unsigned mask = read<unsigned>(pool, 8);
    if (mask & ~0x7fffu) return false;
    for (unsigned kind = 0; kind < 15; ++kind) {
        if (!(mask & (1u << kind))) continue;
        const KhRef slot = read<KhRef>(pool, 0x18 + 4u * kind);
        if (!range(slot, 0x119) || read<unsigned char>(slot) != kind) return false;
        if (kind == 5 || kind >= 11) {
            if (read<unsigned char>(slot, 0x118)) return false;
            continue;
        }
        if (!range(slot, 0x174) || read<int>(slot, 0x170)) return false;
        const unsigned count = read<unsigned char>(slot, 1);
        const KhRef parts = read<KhRef>(slot, 0x168);
        if (!count || count > 5 || !range(parts, count * 0x150u)) return false;
        for (unsigned part = 0; part < count; ++part)
            if (read<unsigned char>(parts, part * 0x150u + 0x14c)) return false;
    }
    return true;
}

bool safe(const FrameContext& context) {
    if (!context.story || context.paused || context.cinematic || !context.controllable ||
        !context.safe_to_replace || !story(context.actor)) return false;
    // Only grounded ordinary idle/run/fall poses are lifecycle replacement points.
    // Hit reactions, original Limit actions and authored attack contacts finish first.
    const int animation = read<int>(context.actor, 0x6bc);
    if (animation < 0 || animation > 4 || (read<unsigned>(context.actor, 0x24) & 4u) == 0) return false;
    const auto flags = read<std::uint64_t>(context.actor);
    if (flags & ((1ull<<8)|(1ull<<13)|(1ull<<16)|(1ull<<17)|(1ull<<19)|(1ull<<23)|(1ull<<24)|(1ull<<28)|(1ull<<45))) return false;
    const auto visible = read<unsigned char>(context.actor, 0x694);
    if (!(visible & 1u) || (visible & 8u)) return false;
    if (!owned_spells_finished(context.actor)) {
        reason("Waiting for Roxas's active magic to finish.");
        return false;
    }
    return true;
}
void party_unload() {
    // An incomplete installation may await restoration. Keep its pre-resolved
    // native chain callable without touching Drive state or overlay ownership.
    if (!initialized) { original_unload_party(); return; }
    // The party destructor will release its current dual overlay. Our reserved,
    // actor-free normal overlay must be released BEFORE ov029 itself unloads.
    if (normal_overlay >= 0 && engine->module_loaded(29)) release_overlay(normal_overlay);
    normal_overlay = drive_overlay = -1;
    normal_appearance = {};
    state.phase = Phase::Normal;
    state.charge = state.remaining_ticks = 0;
    state.ready = false;
    last_context = {};
    original_unload_party();
}
template<class T> bool get_routine(T& value, const char* name) {
    value = reinterpret_cast<T>(engine->routine(name, nullptr));
    return value != nullptr;
}
}

bool init(const KhHost* given_host, const KhEngine* given_engine, const KhActors* given_actors,
          const KhNet* given_net, Callbacks given_callbacks, Settings given_settings) {
    if (initialized || unload_hooked || !given_host || !given_engine || !given_actors || !given_net ||
        given_host->size < sizeof(KhHost) || given_host->abi_major != KH_ABI_MAJOR ||
        given_engine->size < sizeof(KhEngine) || given_actors->size < sizeof(KhActors) ||
        given_net->size < sizeof(KhNet) || !given_host->port_version ||
        std::strcmp(given_host->port_version, "0.1.5") != 0 || !given_engine->pointer ||
        !given_engine->routine || !given_engine->datum || !given_engine->replace ||
        !given_engine->module_loaded || !given_engine->on_game_thread || !given_actors->player || !given_actors->ref ||
        !given_actors->local_seat || !given_net->in_room) return false;
    host = given_host; engine = given_engine; actors = given_actors; net = given_net;
    callbacks = given_callbacks; settings = given_settings;
    settings.capacity = std::clamp(settings.capacity, 100u, 10000u);
    settings.charge_per_hit = std::clamp(settings.charge_per_hit, 1u, settings.capacity);
    settings.max_targets_per_attack = std::clamp(settings.max_targets_per_attack, 1u, 16u);
    settings.duration_ticks = std::clamp(settings.duration_ticks, 60u, 3600u);
    state = {}; state.capacity = settings.capacity; state.duration_ticks = settings.duration_ticks;
    loader = engine->datum("data_ov022_020b2e78");
    party = engine->datum("gPartyMembers");
    player_metadata = engine->datum("data_0204c3d8");
    scene = engine->datum("gSceneCtl");
    mode = engine->datum("data_0204c240");
    overlay_slots = engine->datum("data_ov029_020b3200");
    overlay_groups = engine->datum("data_ov029_020b30b0");
    if (!loader || !party || !player_metadata || !scene || !mode || !overlay_slots || !overlay_groups ||
        !get_routine(heap_get,"Heap_GetCurrent") || !get_routine(heap_set,"Heap_SetCurrent") ||
        !get_routine(destroy_task,"VeneerTo_Obj_Destroy") || !get_routine(instantiate,"InstantiateClass") ||
        !get_routine(acquire_overlay,"Ov029_AcquireOverlaySlot") || !get_routine(release_overlay,"Ov029_ReleaseOverlaySlot") ||
        !get_routine(party_count,"func_ov022_020882f8") || !get_routine(language,"LoadGlobalU16At0") ||
        !get_routine(cleanup_entry,"Ov022_CleanupEntry") || !get_routine(hide_entry,"func_ov022_02087298") ||
        !get_routine(stop_voice,"SNDi_ProcessEntry") || !get_routine(resolve_target,"Ov022_ResolveTargetPick") ||
        !get_routine(set_visible,"func_ov022_020a0678") || !get_routine(set_hp,"Ov022_ActorSetHp") ||
        !get_routine(place,"func_ov022_02088218") || !get_routine(show_group,"func_ov022_02088428") ||
        !get_routine(allocate,"NNSi_FndAllocFromDefaultExpHeap") ||
        !get_routine(free_block,"NNSi_FndFreeFromDefaultHeap") ||
        !get_routine(original_unload_party,"Ov022_UnloadParty")) {
        reason("Audited native Drive lifecycle routines are unavailable."); return false;
    }
    void* previous = nullptr;
    if (engine->replace("Ov022_UnloadParty", reinterpret_cast<void*>(&party_unload), &previous) != KH_OK) {
        reason("Native party-teardown guard could not be installed."); return false;
    }
    unload_hooked = true;
    if (!previous) {
        reason("Native party-teardown guard did not return its predecessor.");
        stop(); // Retryable restoration; the resolved chain remains a pass-through.
        return false;
    }
    original_unload_party = reinterpret_cast<Void0>(previous);
    initialized = true; state.available = true;
    last_tick = 0; seen_serial = ~0u; serial_targets = 0; targets.fill(0);
    normal_overlay = drive_overlay = -1;
    normal_appearance = {};
    log("Drive native seat-local adapter initialized; combat charge starts empty.");
    return true;
}
void on_hit(KhRef attacker, KhActor target, unsigned attack_serial, unsigned, void*) {
    if (!initialized || replacing || !target || state.phase == Phase::Active ||
        state.phase == Phase::RestorationQueued || state.phase == Phase::Faulted ||
        !last_context.story || last_context.paused || last_context.cinematic ||
        attacker != last_context.actor || !story(attacker) || read<int>(attacker, kKind) != 0) return;
    if (seen_serial != attack_serial) { seen_serial = attack_serial; serial_targets = 0; targets.fill(0); }
    for (unsigned i = 0; i < serial_targets; ++i) if (targets[i] == target) return;
    if (serial_targets >= settings.max_targets_per_attack) return;
    targets[serial_targets++] = target;
    state.charge = std::min(settings.capacity, state.charge + settings.charge_per_hit);
    state.ready = state.charge == settings.capacity;
}
bool request_activation() {
    if (!initialized || replacing || state.phase == Phase::Faulted) return false;
    if (state.phase == Phase::ActivationQueued) {
        state.phase = Phase::Normal;
        reason(nullptr);
        return true; // cancel only the uncommitted request; retain full charge
    }
    if (state.phase == Phase::Active) { state.phase = Phase::RestorationQueued; return true; }
    if (state.phase != Phase::Normal || state.charge < settings.capacity) { reason("Drive gauge is not full."); return false; }
    state.phase = Phase::ActivationQueued; reason("Drive activation queued until the current action finishes."); return true;
}
bool ready_for_safe_point(KhRef actor) {
    return initialized && !replacing &&
        (state.phase == Phase::ActivationQueued || state.phase == Phase::RestorationQueued) &&
        story(actor) && owned_spells_finished(actor);
}
bool prime_practice_meter(KhRef actor) {
    if (!initialized || replacing || !engine->on_game_thread() ||
        state.phase != Phase::Normal || !last_context.story || last_context.paused ||
        last_context.cinematic || actor != last_context.actor || !story(actor) ||
        read<int>(actor, kKind) != 0) return false;
    state.charge = settings.capacity;
    state.ready = true;
    reason(nullptr);
    return true;
}
void frame(const FrameContext& context) {
    if (!initialized || replacing || !engine->on_game_thread()) return;
    // The interval begins at the previous sample. A paused/cinematic sample
    // cannot become active play time merely because the next callback resumes.
    const bool was_suspended = last_context.paused || last_context.cinematic;
    const unsigned delta = last_tick && !was_suspended ? context.tick - last_tick : 0;
    last_tick = context.tick;
    last_context = context;
    if (state.phase == Phase::Faulted) return;
    if (!context.story || !story(context.actor)) {
        if (!is_active()) { state.phase = Phase::Normal; state.ready = false; state.charge = 0; }
        reason("Drive is available only in a live Story Roxas battle."); return;
    }
    if (state.phase == Phase::Active && !context.paused && !context.cinematic) {
        const unsigned elapsed = std::min(delta, state.remaining_ticks);
        state.remaining_ticks -= elapsed;
        if (!state.remaining_ticks) state.phase = Phase::RestorationQueued;
    }
    if (!safe(context)) return;
    if (state.phase == Phase::ActivationQueued) {
        if (read<int>(context.actor, kKind) != 0) { state.phase = Phase::Normal; reason("Story already controls this Roxas form."); return; }
        if (transform(context.actor,19)) {
            state.phase = Phase::Active; state.charge = 0; state.ready = false;
            state.remaining_ticks = settings.duration_ticks;
        } else if (state.phase != Phase::Faulted) state.phase = Phase::Normal;
    } else if (state.phase == Phase::RestorationQueued) {
        if (transform(context.actor,0)) { state.phase = Phase::Normal; state.remaining_ticks = 0; }
    }
}
View view() { return state; }
bool is_active() { return state.phase == Phase::Active || state.phase == Phase::RestorationQueued; }
bool is_replacing() { return replacing; }
void reset() {
    if (!initialized) return;
    if (is_active() && safe(last_context) && live(last_context.actor)) {
        if (transform(last_context.actor,0)) { state.phase = Phase::Normal; state.remaining_ticks = 0; }
    }
    if (!is_active()) { state.charge = 0; state.ready = false; state.phase = Phase::Normal; }
}
void stop() {
    if (initialized) {
        reset();
        if (normal_overlay >= 0 && engine->module_loaded(29)) release_overlay(normal_overlay);
        normal_overlay = drive_overlay = -1;
        normal_appearance = {};
        initialized = false; state.available = false;
    }
    if (unload_hooked && engine && original_unload_party &&
        engine->replace("Ov022_UnloadParty", reinterpret_cast<void*>(original_unload_party), nullptr) == KH_OK) {
        unload_hooked = false;
        original_unload_party = nullptr;
    }
}
}
