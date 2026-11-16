#pragma once
#include <kh/kh_engine.h>

// Explicitly selected native pose connections (sequence-update counts2/3/4/6).
// Native outgoing pose freezes during the fade; incoming clock stays native.
// This module never writes
// an animation clock, playback rate, contact record, skeleton or actor position.
namespace rxanim {
using Permitted = bool (*)(KhRef actor);
bool attach(const KhEngine* engine, Permitted permitted);
// Call immediately before the incoming native animation is applied, while the
// actor still carries the outgoing mode/frame. All frame arguments are FX12.
// Invalid/unlisted edges disarm any earlier request. First attacks are not links.
void transition(KhRef actor, int from_id, int to_id, int from_group, int to_group,
                int exit_frame_fx, int first_contact_fx);
// Reset before cancels, actor retirement, scene changes or Drive replacement.
void reset(KhRef actor = 0);
// Restores the previous replacement; a failed restore leaves a pass-through
// callback which can be retried by detach/the host's own plugin cleanup.
void detach();
}
