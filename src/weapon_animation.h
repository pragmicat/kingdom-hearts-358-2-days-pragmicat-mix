#pragma once
#include <kh/kh_actors.h>
#include <kh/kh_engine.h>
#include <kh/kh_net.h>

namespace rxweapon {
using Permitted = bool (*)(KhRef actor);
using Log = void (*)(const char* message, void* context);
struct View {
    KhRef actor = 0;
    unsigned stock_tags = 0, supplementary_tags = 0;
    bool captured = false, complete = false;
};
// BuildActionTable precedes the native weapon-model setup. Bind the normal
// actor there; the load hook captures the original setup after it completes.
bool init(const KhEngine*, const KhActors*, const KhNet*, Permitted, Log = nullptr, void* = nullptr);
void bind(KhRef actor);
// Native weapon teardown calls this automatically before freeing the stock
// allocation. Explicit forget is idempotent and only accesses a live owner.
void forget(KhRef actor = 0);
void stop();
View view();
}
