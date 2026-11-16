#include "limit_finishers.h"
#include <cstdint>
#include <cstring>

namespace rxlimit {
namespace {
constexpr unsigned kContext = 0x2cb0, kOwner = 0xdb4;
constexpr int fx(int frame) { return frame * 4096; }
// li.p: L000 is one frame; L001 is 81 frames. Native final variant starts at 57.
constexpr Profile profiles[] = {
    {Finisher::Rush, 0, 34, fx(0), fx(18), "b_ro_L001"},
    {Finisher::LightBurst, 1, 35, fx(57), fx(81), "b_ro_L001"},
    {Finisher::Flurry, 0, 34, fx(0), fx(57), "b_ro_L001"}
};
const KhEngine* api;
Permitted permitted_actor;
using Controller = KhRef (*)(KhRef context);
using Arm = KhRef (*)(KhRef context, int variant);
using Mode = void (*)(KhRef actor, int mode);
using State = KhRef (*)(KhRef actor, int state);
using Speed = void (*)(KhRef actor, int speed);
using BuildResources = unsigned char (*)(KhRef actor);
using MotionController = void (*)(KhRef context, int delta);
using ScaleDamage = void (*)(KhRef actor, int factor, KhRef out_damage, KhRef out_extra);
using BindParts = void (*)(KhRef context);
using AcquireGrid = KhRef (*)(KhRef path, int seat, int index, KhRef block);
Controller original_rush, original_burst;
Arm arm_target;
Mode apply_mode;
State set_state;
Speed set_speed;
BuildResources build_resources;
MotionController original_motion;
ScaleDamage original_damage;
BindParts original_bind_parts;
AcquireGrid original_acquire_grid;
KhRef owner;
KhRef lingering_owner;
const Profile* current;
bool installed;
unsigned start_tick;
ExitHandler exit_handler;
bool suspended, in_exit_handler, impact_observed;
unsigned suspended_tick, managed_generation;
KhRef retired_context;
KhRef damage_scope;
int damage_scale_fx = 4096;
std::uint64_t grid_epochs[2]{};
struct Resources {
    KhRef actor = 0, rig = 0, effect = 0;
    std::uint64_t rig_epoch = 0, effect_epoch = 0;
} resources;
struct PartBindings {
    KhRef actor = 0, effect = 0, archive = 0, info = 0;
    std::uint64_t epoch = 0;
    std::uint16_t size = 0, used = 0;
} part_bindings;

unsigned char* bytes(KhRef ref) { return api ? static_cast<unsigned char*>(api->pointer(ref)) : nullptr; }
template<class T> T get(KhRef ref, unsigned offset = 0) {
    T value{};
    if (auto* p = bytes(ref)) std::memcpy(&value, p + offset, sizeof value);
    return value;
}
template<class T> void put(KhRef ref, unsigned offset, T value) {
    if (auto* p = bytes(ref)) std::memcpy(p + offset, &value, sizeof value);
}
bool valid(KhRef actor) {
    // Permission performs the outer generation/lifetime test before any actor
    // bytes are read; a retired address may already belong to another actor.
    return api && actor && permitted_actor && permitted_actor(actor) &&
           api->module_loaded(22) && api->module_loaded(30) && bytes(actor) && get<int>(actor, 0xc) == 0;
}
bool resources_queued(KhRef actor) {
    return (get<unsigned char>(actor, 0x262c) & 0xbu) == 0xbu &&
           get<KhRef>(actor, 0x2638) && get<KhRef>(actor, 0x263c);
}
bool resources_owned(KhRef actor) {
    return resources.actor == actor && resources_queued(actor) &&
        resources.rig == get<KhRef>(actor, 0x2638) &&
        resources.effect == get<KhRef>(actor, 0x263c) &&
        bytes(resources.rig) && bytes(resources.effect) &&
        get<unsigned char>(resources.rig, 3) != 0 && get<unsigned char>(resources.effect, 3) != 0 &&
        resources.rig_epoch == grid_epochs[0] && resources.effect_epoch == grid_epochs[1];
}
bool resources_ready(KhRef actor) {
    if (!resources_owned(actor)) return false;
    const KhRef rig = get<KhRef>(actor, 0x2638), effect = get<KhRef>(actor, 0x263c);
    return bytes(rig) && bytes(effect) && get<unsigned char>(rig, 3) == 3 && get<unsigned char>(effect, 3) == 3;
}
bool can_reload(KhRef actor) {
    // AcquireGridSlot's native body dereferences its null candidate when a
    // requested cell is still loading. Do not call the two-cell builder until
    // both cells can be acquired. Existing emitters retain their old bindings.
    for (unsigned index = 0; index < 2; ++index) {
        const KhRef entry = get<KhRef>(actor, 0x2638u + index * 4);
        if (!entry) continue;
        if (!bytes(entry)) return false;
        const auto stage = get<unsigned char>(entry, 3);
        if (stage != 0 && stage != 3) return false;
    }
    for (unsigned index = 0; index < 8; ++index)
        if (get<unsigned char>(actor + kContext + 0x234 + index * 0x170, 0x12c) != 0) return false;
    return true;
}
unsigned char load_resources(KhRef actor) {
    // Stock's low-health run may ask again after this helper preloaded li.p. Reuse
    // its still-owned grid slots; reacquiring a loading slot would be invalid.
    if (!valid(actor)) return build_resources(actor);
    if (resources_owned(actor)) return get<unsigned char>(actor, 0x262c);
    if (!can_reload(actor)) return 0;
    const auto result = build_resources(actor);
    part_bindings = {};
    if (resources_queued(actor))
        resources = {actor, get<KhRef>(actor, 0x2638), get<KhRef>(actor, 0x263c), grid_epochs[0], grid_epochs[1]};
    else resources = {};
    return result;
}
KhRef acquire_grid(KhRef path, int seat, int index, KhRef block) {
    const KhRef entry = original_acquire_grid(path, seat, index, block);
    if (entry && seat == 0 && index >= 0 && index < 2) ++grid_epochs[index];
    return entry;
}
void bind_parts(KhRef context) {
    const KhRef actor = get<KhRef>(context, kOwner);
    if (actor && actor == owner && context == actor + kContext && current && valid(actor) && resources_ready(actor)) {
        const KhRef effect = get<KhRef>(actor, 0x263c);
        const PartBindings before{actor, effect, get<KhRef>(effect, 0x2c), get<KhRef>(effect, 0x28),
            grid_epochs[1], get<std::uint16_t>(effect, 0x30), get<std::uint16_t>(effect, 0x34)};
        if (part_bindings.actor == before.actor && part_bindings.effect == before.effect &&
            part_bindings.archive == before.archive && part_bindings.info == before.info &&
            part_bindings.epoch == before.epoch && part_bindings.size == before.size &&
            before.used >= part_bindings.used) return;
        // BuildResNodeSet appends behind the loaded archive and never resets
        // its 16-bit used count. The eight persistent emitter tables need one
        // build per loaded effect, rather than eight more per signature.
        original_bind_parts(context);
        part_bindings = before;
        part_bindings.used = get<std::uint16_t>(effect, 0x34);
        return;
    }
    original_bind_parts(context);
    if (part_bindings.actor == actor) part_bindings = {};
}
void motion(KhRef context, int delta) {
    if (context == retired_context && (!lingering_owner || !valid(lingering_owner))) return;
    const KhRef actor = get<KhRef>(context, kOwner);
    if ((actor == owner || actor == lingering_owner) && (suspended || !valid(actor))) return;
    original_motion(context, delta);
    if (actor && actor == lingering_owner && !active(actor) && valid(actor)) {
        const int mode = get<int>(actor, 0x6bc);
        const bool ordinary_mode = (mode >= 0 && mode <= 4) || (mode >= 16 && mode <= 18) ||
                                   (mode >= 30 && mode <= 45);
        if (ordinary_mode) {
            // Original motion controller reasserts Limit-engaged while beam effects
            // linger. Keep those native effects, but ordinary recovery has ended:
            // do not keep magic/items locked for their remaining visual lifetime.
            put<std::uint64_t>(actor, 0x464, get<std::uint64_t>(actor, 0x464) & ~0x10000ull);
            put<std::uint64_t>(actor, 0x46c, get<std::uint64_t>(actor, 0x46c) & ~0x10000ull);
        }
    }
}
void clear() { owner = 0; current = nullptr; start_tick = 0; impact_observed = false; }
void scale_damage(KhRef actor, int factor, KhRef out_damage, KhRef out_extra) {
    // These are the two exact factors in Ov030_EmitTimelineEffect. The scoped
    // original controller is the sole caller being budgeted; everything else,
    // including other players, ordinary attacks and lingering shots, passes on.
    if (actor && actor == owner && actor == damage_scope && current &&
        (factor == 0x13b0 || factor == 0x12c0))
        factor = static_cast<int>((static_cast<std::int64_t>(factor) * damage_scale_fx + 2048) >> 12);
    original_damage(actor, factor, out_damage, out_extra);
}
bool timeline_impact(int timeline) {
    // Ov030_EmitTimelineEffect's exact native event times. This is bookkeeping,
    // not an emitter: only a call through the original controller earns it.
    switch (timeline) {
    case fx(9): case fx(15): case fx(21): case fx(27): case fx(33):
    case fx(39): case fx(51): case fx(60): case fx(75): return true;
    default: return false;
    }
}
int exit_frame(Finisher kind) {
    return fx(kind == Finisher::LightBurst ? 78 : kind == Finisher::Flurry ? 18 : 12);
}
KhRef finish(KhRef actor) {
    // Match Ov030_UpdateController's ordinary exit: stop drift, mark motion released,
    // enter ground idle or normal fall. Leave the native projectile/effect pool alone.
    put<int>(actor, 0x498, 0); put<int>(actor, 0x49c, 0); put<int>(actor, 0x4a0, 0);
    put<int>(actor, 0x698, 0); put<int>(actor, 0x69c, 0); put<int>(actor, 0x6a0, 0);
    put<std::uint64_t>(actor, 0, get<std::uint64_t>(actor) | 4ull);
    put<std::uint64_t>(actor, 0x464, get<std::uint64_t>(actor, 0x464) & ~0x10000ull);
    put<std::uint64_t>(actor, 0x46c, get<std::uint64_t>(actor, 0x46c) & ~0x10000ull);
    const bool ground = (get<unsigned>(actor, 0x24) & 4u) != 0;
    lingering_owner = actor;
    clear();
    const KhRef result = set_state(actor, ground ? 0 : 2);
    if (ground) apply_mode(actor, 0);
    return result;
}
KhRef controller(KhRef context, Controller original) {
    if (context == retired_context) return 0;
    const KhRef context_actor = get<KhRef>(context, kOwner);
    // A state routine captured before cancel/finish may be called one final time.
    // Already-spawned native effects keep updating in motion(), but no new
    // character timeline effects may be emitted by that retired managed action.
    if (!owner) return lingering_owner && context_actor == lingering_owner ? 0 : original(context);
    if (context_actor != owner) return original(context);
    if (suspended || in_exit_handler) return 0;
    const KhRef actor = owner;
    if (!valid(actor)) { retired_context = context; clear(); lingering_owner = 0; return 0; }
    const int timeline = get<int>(actor, 0x7b0);
    // A second independent clock prevents an external mod freezing the animation forever.
    const bool timed_out = api->frame && static_cast<unsigned>(api->frame() - start_tick) > 120;
    if (!current || timeline >= current->end_frame_fx || timed_out) return finish(actor);
    if (exit_handler && impact_observed && timeline >= exit_frame(current->kind)) {
        const auto kind = current->kind;
        const unsigned generation = managed_generation;
        in_exit_handler = true;
        const KhRef result = exit_handler(actor, kind, timeline);
        in_exit_handler = false;
        if (result) {
            // The handler has already performed cancel and/or selected a native
            // defense. Do not finish again and overwrite that selected state.
            if (owner == actor && managed_generation == generation) { lingering_owner = actor; clear(); }
            return result;
        }
        if (!owner || owner != actor || !current || managed_generation != generation) return 0;
        if (!valid(actor)) { retired_context = context; clear(); lingering_owner = 0; return 0; }
    }
    // Native Event Horizon's latch normally asks whether another held attack repeats.
    // Keep this one bounded action alive until its authored slice ends, never a held loop.
    if (current->kind != Finisher::LightBurst) put<int>(context, 8, 1);
    const auto buttons = get<std::uint16_t>(actor, 0x18);
    put<std::uint16_t>(actor, 0x18, static_cast<std::uint16_t>(buttons & ~1u));
    // Limit effects test exact source timeline values; preserve original native quantization.
    set_speed(actor, 4096);
    const KhRef previous_scope = damage_scope;
    damage_scope = actor;
    const KhRef result = original(context); // Original effects + native damage/contact handling.
    damage_scope = previous_scope;
    if (!valid(actor)) { retired_context = context; clear(); lingering_owner = 0; return result; }
    put<std::uint16_t>(actor, 0x18, buttons);
    if (result) { lingering_owner = actor; clear(); return result; }
    if (!owner || !current) return 0;
    // Burst exits follow its final75-frame impact. Rush/Flurry can leave once
    // an actual authored event has been passed through the original controller.
    if (timeline_impact(timeline) &&
        (current->kind != Finisher::LightBurst || timeline >= fx(75))) impact_observed = true;
    if (get<int>(actor, 0x7b0) >= current->end_frame_fx) return finish(actor);
    return 0;
}
KhRef rush(KhRef context) { return controller(context, original_rush); }
KhRef burst(KhRef context) { return controller(context, original_burst); }
template<class T> T resolve(const char* name) {
    return reinterpret_cast<T>(api->routine(name, nullptr));
}
}

const Profile& profile(Finisher kind) { return profiles[kind == Finisher::LightBurst ? 1 : kind == Finisher::Flurry ? 2 : 0]; }
bool attach(const KhEngine* engine, Permitted permitted) {
    if (installed || !engine || engine->size < sizeof(KhEngine) || !engine->pointer || !engine->routine ||
        !engine->module_loaded || !engine->replace || !engine->frame || !permitted) return false;
    api = engine;
    permitted_actor = permitted;
    arm_target = resolve<Arm>("Ov030_ArmPlayerTarget");
    apply_mode = resolve<Mode>("Ov030_ApplyModeChange");
    set_state = resolve<State>("Ov022_ActorSetState");
    set_speed = resolve<Speed>("Ov022_SetAnimSpeed");
    build_resources = resolve<BuildResources>("Ov030_BuildActorHandles");
    original_damage = resolve<ScaleDamage>("Ov022_ScaleMoveDamage");
    original_bind_parts = resolve<BindParts>("Ov030_BindPartRigs");
    original_acquire_grid = resolve<AcquireGrid>("Ov022_AcquireGridSlot");
    if (!arm_target || !apply_mode || !set_state || !set_speed || !build_resources || !original_damage ||
        !original_bind_parts || !original_acquire_grid) { api = nullptr; return false; }
    void* previous = nullptr;
    if (api->replace("Ov030_UpdateController", reinterpret_cast<void*>(&rush), &previous) != KH_OK) {
        api = nullptr; return false;
    }
    original_rush = reinterpret_cast<Controller>(previous);
    previous = nullptr;
    if (api->replace("Ov030_ComputeAimAngle", reinterpret_cast<void*>(&burst), &previous) != KH_OK) {
        api->replace("Ov030_UpdateController", reinterpret_cast<void*>(original_rush), nullptr);
        api = nullptr; return false;
    }
    original_burst = reinterpret_cast<Controller>(previous);
    previous = nullptr;
    if (api->replace("Ov030_BuildActorHandles", reinterpret_cast<void*>(&load_resources), &previous) != KH_OK) {
        api->replace("Ov030_ComputeAimAngle", reinterpret_cast<void*>(original_burst), nullptr);
        api->replace("Ov030_UpdateController", reinterpret_cast<void*>(original_rush), nullptr);
        api = nullptr; return false;
    }
    build_resources = reinterpret_cast<BuildResources>(previous);
    previous = nullptr;
    if (api->replace("Ov030_UpdateMotionController", reinterpret_cast<void*>(&motion), &previous) != KH_OK) {
        api->replace("Ov030_BuildActorHandles", reinterpret_cast<void*>(build_resources), nullptr);
        api->replace("Ov030_ComputeAimAngle", reinterpret_cast<void*>(original_burst), nullptr);
        api->replace("Ov030_UpdateController", reinterpret_cast<void*>(original_rush), nullptr);
        api = nullptr; return false;
    }
    original_motion = reinterpret_cast<MotionController>(previous);
    previous = nullptr;
    if (api->replace("Ov022_ScaleMoveDamage", reinterpret_cast<void*>(&scale_damage), &previous) != KH_OK) {
        api->replace("Ov030_UpdateMotionController", reinterpret_cast<void*>(original_motion), nullptr);
        api->replace("Ov030_BuildActorHandles", reinterpret_cast<void*>(build_resources), nullptr);
        api->replace("Ov030_ComputeAimAngle", reinterpret_cast<void*>(original_burst), nullptr);
        api->replace("Ov030_UpdateController", reinterpret_cast<void*>(original_rush), nullptr);
        api = nullptr; return false;
    }
    original_damage = reinterpret_cast<ScaleDamage>(previous);
    previous = nullptr;
    if (api->replace("Ov030_BindPartRigs", reinterpret_cast<void*>(&bind_parts), &previous) != KH_OK) {
        api->replace("Ov022_ScaleMoveDamage", reinterpret_cast<void*>(original_damage), nullptr);
        api->replace("Ov030_UpdateMotionController", reinterpret_cast<void*>(original_motion), nullptr);
        api->replace("Ov030_BuildActorHandles", reinterpret_cast<void*>(build_resources), nullptr);
        api->replace("Ov030_ComputeAimAngle", reinterpret_cast<void*>(original_burst), nullptr);
        api->replace("Ov030_UpdateController", reinterpret_cast<void*>(original_rush), nullptr);
        api = nullptr; return false;
    }
    original_bind_parts = reinterpret_cast<BindParts>(previous);
    previous = nullptr;
    if (api->replace("Ov022_AcquireGridSlot", reinterpret_cast<void*>(&acquire_grid), &previous) != KH_OK) {
        api->replace("Ov030_BindPartRigs", reinterpret_cast<void*>(original_bind_parts), nullptr);
        api->replace("Ov022_ScaleMoveDamage", reinterpret_cast<void*>(original_damage), nullptr);
        api->replace("Ov030_UpdateMotionController", reinterpret_cast<void*>(original_motion), nullptr);
        api->replace("Ov030_BuildActorHandles", reinterpret_cast<void*>(build_resources), nullptr);
        api->replace("Ov030_ComputeAimAngle", reinterpret_cast<void*>(original_burst), nullptr);
        api->replace("Ov030_UpdateController", reinterpret_cast<void*>(original_rush), nullptr);
        api = nullptr; return false;
    }
    original_acquire_grid = reinterpret_cast<AcquireGrid>(previous);
    resources = {}; part_bindings = {}; grid_epochs[0] = grid_epochs[1] = 0;
    installed = true;
    return true;
}
bool prepare(KhRef actor) {
    if (!installed || suspended || !valid(actor) || get<KhRef>(actor + kContext, kOwner) != actor) return false;
    if (!resources_owned(actor)) load_resources(actor);
    return resources_ready(actor);
}
KhRef begin(KhRef actor, Finisher kind) {
    if (!installed || suspended || owner || in_exit_handler || !valid(actor) ||
        get<KhRef>(actor + kContext, kOwner) != actor) return 0;
    // Native ro's resources and owner must already have been built; never create a partial rig.
    if (!get<KhRef>(actor, 0x664) || !get<KhRef>(actor, 0x668)) return 0;
    // Original grid loading is asynchronous. A first request may queue loading and
    // return 0; the caller retains its regular cm attack until a later ready request.
    if (!prepare(actor)) return 0;
    lingering_owner = 0;
    owner = actor;
    current = &profile(kind);
    retired_context = 0;
    ++managed_generation;
    impact_observed = false;
    start_tick = api->frame ? api->frame() : 0;
    put<int>(actor + kContext, 4, 0);
    put<int>(actor + kContext, 8, 1);
    // Native intro setup's character part only: bind the already-loaded li rig. This avoids
    // generic state23's low-health run flag, camera mode6, and 27-frame charge introduction.
    apply_mode(actor, 46);
    const KhRef result = arm_target(actor + kContext, current->native_variant);
    if (!result) { clear(); return 0; }
    set_speed(actor, 4096);
    return result;
}
bool active(KhRef actor) { return owner != 0 && (!actor || actor == owner); }
void set_exit_handler(ExitHandler handler) { exit_handler = handler; }
void set_suspended(bool pause) {
    if (suspended == pause) return;
    const unsigned tick = api && api->frame ? api->frame() : 0;
    if (pause) suspended_tick = tick;
    else if (owner) start_tick += static_cast<unsigned>(tick - suspended_tick);
    suspended = pause;
}
void set_damage_scale_fx(int scale) { damage_scale_fx = scale < 1024 ? 1024 : scale > 4096 ? 4096 : scale; }
KhRef cancel(KhRef actor) {
    if (!active(actor)) return 0;
    if (!valid(actor)) { clear(); return 0; }
    return finish(actor);
}
void forget(KhRef actor) {
    if (actor && owner != actor && lingering_owner != actor) return;
    const KhRef previous = owner ? owner : lingering_owner;
    const bool alive = previous && valid(previous);
    if (previous) retired_context = previous + kContext;
    clear();
    // Interruption of a still-live actor retires only its timeline controller;
    // its existing native emitters must finish rather than freeze in place.
    // True actor retirement fails permitted_actor and never reaches old memory.
    lingering_owner = alive ? previous : 0;
}
void detach() {
    clear(); lingering_owner = 0; retired_context = 0;
    if (installed && api) {
        api->replace("Ov022_AcquireGridSlot", reinterpret_cast<void*>(original_acquire_grid), nullptr);
        api->replace("Ov030_BindPartRigs", reinterpret_cast<void*>(original_bind_parts), nullptr);
        api->replace("Ov022_ScaleMoveDamage", reinterpret_cast<void*>(original_damage), nullptr);
        api->replace("Ov030_UpdateMotionController", reinterpret_cast<void*>(original_motion), nullptr);
        api->replace("Ov030_BuildActorHandles", reinterpret_cast<void*>(build_resources), nullptr);
        api->replace("Ov030_ComputeAimAngle", reinterpret_cast<void*>(original_burst), nullptr);
        api->replace("Ov030_UpdateController", reinterpret_cast<void*>(original_rush), nullptr);
    }
    installed = false; api = nullptr; permitted_actor = nullptr;
    exit_handler = nullptr; suspended = false; in_exit_handler = false; suspended_tick = 0;
    damage_scope = 0; damage_scale_fx = 4096;
    resources = {}; part_bindings = {}; grid_epochs[0] = grid_epochs[1] = 0;
}
bool suppress_legacy_start(KhRef actor, int state) { return state == 23 && valid(actor) && !active(actor); }
}
