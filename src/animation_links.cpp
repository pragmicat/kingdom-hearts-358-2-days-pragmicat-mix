#include "animation_links.h"
#include "roxas_catalogue.h"
#include <cstdint>
#include <cstring>

namespace rxanim {
namespace {
constexpr int fx(int value) { return value * 4096; }
constexpr const char* kApply = "func_ov022_020a3c78";
using Apply = void (*)(KhRef actor, KhRef animation, int sub_index, int blend_frames);
const KhEngine* api;
Permitted permitted_actor;
Apply original;
bool installed, enabled;
struct Pending {
    KhRef actor = 0;
    int from_tag = 0, to_tag = 0, exit = 0, updates = 0, first_contact = 0;
    unsigned tick = 0;
} pending;

// Native N1 is already100% incoming; N2 lasts one update. These useful N3/N4/N6
// counts are sequence updates, not animation frames. N3/N4 complete at update2,
// N6 at update4. Longer profiles use ONLY B000/B001/B006/B007: their anchor stays
// stationary through the resulting startup baselines at <=1.6 frames/update.
// Traveling entries keep N2 (one mixed pose). Native anchor suppression prevents
// fade-weight rewind but would otherwise discard useful authored travel.
// No B011/B013 entry (contact at frame 0), launch/fall boundary, signature or
// exploratory gear route is smoothed. Exact poses still need human preview.
struct Edge { int from, to, group, updates; };
constexpr Edge edges[] = {
    {0,1060,0,3}, {1060,5070,0,6}, {5070,1,0,6}, {1,0,0,4},
    {0,1,0,6}, {1060,1,0,6},
    {0,13080,0,2}, {1060,13080,0,2}, {5070,13080,0,2}, {1,13080,0,2},
    {1,13090,0,2}, {13080,10120,0,2},
    {0,14021,0,2}, {1060,14021,0,2}, {5070,14021,0,2}, {1,14021,0,2},
    {0,6061,0,3}, {5070,6061,0,3}, {1,6061,0,3},
    {13090,0,0,4}, {13090,1060,0,3}, {13090,5070,0,6}, {13090,1,0,6},
    {10120,0,0,4}, {10120,1060,0,3}, {10120,5070,0,6}, {10120,1,0,6},
    {14021,0,0,4}, {14021,1060,0,3}, {14021,5070,0,6}, {14021,1,0,6},
    {6061,0,0,4}, {6061,5070,0,6}, {6061,1,0,6},
    {11,12,1,2}, {12,3,1,2},
    {3,4101,1,2}, {11,4101,1,2}, {12,4101,1,2},
    {3,13140,1,2}, {11,13140,1,2}, {12,13140,1,2},
    {3,13151,1,2}, {11,13151,1,2}, {12,13151,1,2},
    {3,11041,1,2}, {11,11041,1,2}, {12,11041,1,2},
    {3,5,1,2}, {11,5,1,2}, {12,5,1,2},
    {4101,3,1,2}, {4101,12,1,2}
};
constexpr int kMaxStepFx = 6554; // ceil(1.6 *4096), also checked against the actual actor step.
bool before_contact(int updates, int first_contact_fx) {
    const int last_partial_update = updates == 2 ? 0 : (updates == 3 || updates == 4) ? 1 : updates == 6 ? 3 : -1;
    return last_partial_update >= 0 && last_partial_update * kMaxStepFx < first_contact_fx;
}
bool step_supported(KhRef actor);

unsigned char* bytes(KhRef ref) {
    return api && ref ? static_cast<unsigned char*>(api->pointer(ref)) : nullptr;
}
template<class T> bool read(KhRef ref, unsigned offset, T& value) {
    if (auto* p = bytes(ref)) { std::memcpy(&value, p + offset, sizeof value); return true; }
    return false;
}
template<class T> T value(KhRef ref, unsigned offset = 0) {
    T result{}; read(ref, offset, result); return result;
}
bool step_supported(KhRef actor) {
    const int step = value<std::int16_t>(actor, 0x2aba);
    return step >= 0 && step <= kMaxStepFx;
}
bool valid(KhRef actor) {
    return enabled && api && api->module_loaded(22) && bytes(actor) &&
           value<int>(actor, 0xc) == 0 && permitted_actor && permitted_actor(actor);
}
const RxMoveTiming* timing(int id) {
    for (const auto& item : kRxMoveTimings) if (item.id == id) return &item;
    return nullptr;
}
int first_contact(int id) {
    for (const auto& item : kRxMoves) if (item.id == id) return item.first;
    return -1;
}
const Edge* edge(int from, int to, int group) {
    for (const auto& item : edges)
        if (item.from == from && item.to == to && item.group == group) return &item;
    return nullptr;
}
bool incoming_entry(KhRef actor, KhRef animation, int tag) {
    const KhRef table = actor + 0x910;
    const KhRef rows = value<KhRef>(table, 0x88);
    const int count = value<int>(table, 0x8c);
    if (!rows || !bytes(rows) || count < 1 || count > 16) return false;
    for (int i = 0; i < count; ++i) {
        const KhRef row = rows + static_cast<unsigned>(i) * 0x80;
        if (!bytes(row)) return false;
        if (value<unsigned char>(row, 3) == tag)
            return value<KhRef>(row, 0x38) + 4 == animation;
    }
    return false;
}
bool can_fade(KhRef actor, KhRef animation) {
    const KhRef node = value<KhRef>(actor, 0x20);
    if (!node || !bytes(node)) return false;
    const KhRef state = node + 4;
    // Native fade interruption promotes its pending animation. Keep that path
    // native rather than replacing a partially blended pose with another fade.
    if ((value<std::uint16_t>(state) & 4u) || value<std::int16_t>(state, 2) < 0)
        return false;
    const KhRef bound = value<KhRef>(state, 0xc);
    const KhRef candidates = value<KhRef>(animation, 0x10);
    if (!bound || !bytes(bound) || value<std::uint16_t>(animation) == 0 ||
        !candidates || !bytes(candidates)) return false;
    const KhRef target = value<KhRef>(candidates);
    return target && bytes(target) && target != bound;
}
void apply(KhRef actor, KhRef animation, int sub, int blend) {
    if (pending.actor == actor) {
        const Pending request = pending;
        pending = {}; // Consume before calling on: reentrant native calls cannot reuse it.
        if (valid(actor) && api->frame() == request.tick && sub == 0 && blend == 0 &&
            step_supported(actor) && before_contact(request.updates, request.first_contact) &&
            value<int>(actor, 0x6bc) == 30 + request.from_tag &&
            value<int>(actor, 0x7b0) == request.exit &&
            animation && bytes(animation) && incoming_entry(actor, animation, request.to_tag) &&
            can_fade(actor, animation)) blend = request.updates;
    }
    original(actor, animation, sub, blend);
}
}

bool attach(const KhEngine* engine, Permitted permitted) {
    if (installed || !engine || engine->size < sizeof(KhEngine) || !engine->pointer ||
        !engine->module_loaded || !engine->routine || !engine->replace || !engine->frame || !permitted)
        return false;
    int module = -1;
    const auto resolved = reinterpret_cast<Apply>(engine->routine(kApply, &module));
    if (!resolved || module != 22) return false;
    void* previous = nullptr;
    api = engine;
    permitted_actor = permitted;
    original = resolved;
    if (engine->replace(kApply, reinterpret_cast<void*>(&apply), &previous) != KH_OK) {
        original = nullptr; api = nullptr; permitted_actor = nullptr; return false;
    }
    // A successful replacement without a predecessor is not a usable native
    // chain. Restore the pre-resolved callback before reporting attach failure.
    if (!previous) {
        installed = true;
        enabled = false;
        detach(); // A failed restoration still leaves a callable pass-through.
        return false;
    }
    original = reinterpret_cast<Apply>(previous);
    pending = {};
    installed = enabled = true;
    return true;
}
void transition(KhRef actor, int from_id, int to_id, int from_group, int to_group,
                int exit_frame_fx, int first_contact_fx) {
    pending = {};
    if (!valid(actor) || from_group != to_group || (to_group != 0 && to_group != 1)) return;
    const auto* link = edge(from_id, to_id, to_group);
    const auto* from = timing(from_id);
    const auto* to = timing(to_id);
    const int native_first = first_contact(to_id), source_first = first_contact(from_id);
    if (!link || !from || !to || from->animation_tag == to->animation_tag || native_first < 0 ||
        !before_contact(link->updates, first_contact_fx) || !before_contact(link->updates, native_first) ||
        !step_supported(actor) ||
        first_contact_fx > native_first || source_first < 0 ||
        exit_frame_fx < source_first + fx(1) || exit_frame_fx >= fx(from->animation_frames) ||
        (from->state_end >= 0 && exit_frame_fx > from->state_end) ||
        value<int>(actor, 0x6bc) != 30 + from->animation_tag ||
        value<int>(actor, 0x7b0) != exit_frame_fx) return;
    pending = {actor, from->animation_tag, to->animation_tag, exit_frame_fx, link->updates, first_contact_fx, api->frame()};
}
void reset(KhRef actor) { if (!actor || pending.actor == actor) pending = {}; }
void detach() {
    pending = {};
    enabled = false;
    if (installed && api && original &&
        api->replace(kApply, reinterpret_cast<void*>(original), nullptr) != KH_OK) return;
    installed = false;
    original = nullptr;
    api = nullptr;
    permitted_actor = nullptr;
}
}
