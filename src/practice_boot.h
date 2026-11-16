#pragma once
#include <kh/kh_actors.h>
#include <kh/kh_engine.h>
#include <kh/kh_mod.h>
#include <kh/kh_net.h>

namespace rxpractice {
// Only PRAGMICAT_PRACTICE=1 arms entry. Campaign starts install no extra hooks.
// The launcher supplies a separate runtime/profile and only the two project mods.
bool init(const KhHost*, const KhEngine*, const KhActors*, const KhNet*);
bool enabled();
// True only in this process's freshly initialized, local practice field.
bool active();
void stop();
}
