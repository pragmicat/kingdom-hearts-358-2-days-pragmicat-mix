#include "../src/animation_links.cpp"
#include <array>
#include <cstdint>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <functional>
#include <vector>

// Native fade body, verbatim logic from matching decomp Anim_StepCrossFade.c
// SHA25698b8ef232fbbbb60542f647544ad50990b154c6b569101e3ecd3e74a5bbd827.
// Compiled independently of the production wrapper: this tests the real ratio
// ramp/completion rule, including its early finalization. Pointer-to-int is the
// original 32-bit ABI; narrow through intptr_t explicitly for 64-bit compilers.
// Removal is a harmless mock in this host-only test and never dereferences it.
namespace native_fade {
int removals;
void NNS_G3dRenderObjRemoveAnmObj(int*,int) { ++removals; }
struct AnmTrack { int frame; int field_04; };
struct BlendState {
    unsigned short flags; short field_02; char pad_04[0xc-0x4];
    AnmTrack* outTrack; char pad_10[0xca-0x10]; short field_ca;
    char pad_cc[0xd4-0xcc]; AnmTrack* inTrack; int elapsed; int duration;
};
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4302 4311)
#endif
void Anim_StepCrossFade(BlendState* this_) {
    int diff = this_->elapsed - this_->duration;
    this_->elapsed = diff;
    if (diff < 0) { diff = 0; }
    short flag = this_->field_ca;
    AnmTrack* out = this_->outTrack;
    AnmTrack* in = this_->inTrack;
    int inv = 0x1000 - diff;
    if (flag >= 0) { out->field_04 = diff; in->field_04 = inv; }
    if (diff > this_->duration) { return; }
    NNS_G3dRenderObjRemoveAnmObj((int*)((char*)this_ + 0x20),static_cast<int>(reinterpret_cast<std::intptr_t>(out)));
    in->field_04 = 0x1000;
    this_->outTrack = in; this_->inTrack = 0;
    this_->field_02 = this_->field_ca; this_->field_ca = -1;
    this_->flags &= ~4;
}
#ifdef _MSC_VER
#pragma warning(pop)
#endif
}

namespace native_anchor {
using u16=unsigned short;
struct VecFx32 { int x,y,z; };
struct MtxFx33 { int m[3][3]; };
struct ActorNode { u16 nAnimFlags,nAngle; };
struct Actor { unsigned long long nFlags;ActorNode* pNode;int nAnchorScale,nSlotSpare,nSlotAnchor;VecFx32 vecAnchorDelta; };
short data_0203d210[]={0,-4096};
VecFx32 sampled;
int FX_Div(int a,int b) { return static_cast<int>(static_cast<long long>(a)*4096/b); }
void MTX_RotY33_(MtxFx33*,int,int) {}
void Ov022_GetSlotPos(Actor* a,int slot,VecFx32* out) { *out=slot==a->nSlotAnchor?sampled:VecFx32{}; }
void VEC_Subtract(VecFx32* a,VecFx32* b,VecFx32* out) { *out={a->x-b->x,a->y-b->y,a->z-b->z}; }
void ScaleVec3Fx12(int s,VecFx32* a,VecFx32* out) { *out={a->x*s/4096,a->y*s/4096,a->z*s/4096}; }
void MTX_MultVec33(VecFx32* a,MtxFx33*,VecFx32* out) { *out=*a; } // Test facing0 is identity.
constexpr auto FLAG_BIT29=1ull<<29,FLAG_BIT30=1ull<<30;
constexpr int FX32_ONE=0x1000,ANGLE_BIAS=0x8000,ANGLE_STEP=4,ANIM_HOLD=4;
// Matching Ov022_StepAnchorDelta body; mocks above isolate the actual native
// fade/first-sample flag logic from trigonometry and the engine's matrix cache.
void Ov022_StepAnchorDelta(Actor* pActor,VecFx32* pOut) {
    MtxFx33 mtx;VecFx32 vecAnchor,vecSpare;int nScale;u16 nAngle;int nTrig;
    nAngle=pActor->pNode->nAngle-ANGLE_BIAS;nScale=FX32_ONE;
    if(pActor->nAnchorScale!=FX32_ONE)nScale=FX_Div(FX32_ONE,pActor->nAnchorScale);
    nTrig=(nAngle>>ANGLE_STEP)*2;
    MTX_RotY33_(&mtx,-data_0203d210[nTrig],-data_0203d210[nTrig+1]);
    Ov022_GetSlotPos(pActor,pActor->nSlotAnchor,&vecAnchor);
    Ov022_GetSlotPos(pActor,pActor->nSlotSpare,&vecSpare);
    VEC_Subtract(&vecAnchor,&vecSpare,&vecAnchor);
    if(nScale!=FX32_ONE)ScaleVec3Fx12(nScale,&vecAnchor,&vecAnchor);
    if((pActor->nFlags&FLAG_BIT29)!=0||(pActor->nFlags&FLAG_BIT30)!=0)*pOut={0,0,0};
    else {VEC_Subtract(&vecAnchor,&pActor->vecAnchorDelta,pOut);MTX_MultVec33(pOut,&mtx,pOut);}
    if((pActor->pNode->nAnimFlags&ANIM_HOLD)!=0) {
        pOut->z=0;pOut->y=0;pOut->x=0;pActor->nFlags|=FLAG_BIT29;pActor->nFlags|=FLAG_BIT30;
    } else {
        if((pActor->nFlags&FLAG_BIT29)==0)pActor->nFlags&=~FLAG_BIT30;
        pActor->nFlags&=~FLAG_BIT29;
    }
    pActor->vecAnchorDelta=vecAnchor;
}
}

namespace test {
void check(bool yes, const char* message) { if (!yes) throw std::runtime_error(message); }
#define CHECK(x) ::test::check((x), #x)
constexpr KhRef actor = 0x1000, other = 0x5000, node = 0x9000;
constexpr KhRef rows = 0xa000, data = 0xb000, animation = data + 4;
constexpr KhRef candidates = 0xc000, bound = 0xc100, target = 0xc200;
constexpr const char* name = "func_ov022_020a3c78";
std::array<unsigned char, 0x10000> memory{};
KhEngine api{};
using Apply = void (*)(KhRef, KhRef, int, int);
std::map<std::string, void*> functions;
struct Call { KhRef actor, animation; int sub, blend; };
std::vector<Call> calls;
bool allowed, loaded, missing, replace_fail, null_previous, recurse;
bool fail_rollback;
unsigned tick, replacements;
int module_number;
void* pointer(KhRef ref) { return ref >= actor && ref < memory.size() ? memory.data() + ref : nullptr; }
template<class T> void put(KhRef ref, unsigned offset, T value) { std::memcpy(memory.data()+ref+offset, &value, sizeof value); }
template<class T> T get(KhRef ref, unsigned offset = 0) { T result{}; std::memcpy(&result, memory.data()+ref+offset, sizeof result); return result; }
bool permitted(KhRef ref) { return allowed && ref == actor; }
int module(int n) { return loaded && n == 22; }
unsigned frame() { return tick; }
void native(KhRef ref, KhRef anim, int sub, int blend) {
    calls.push_back({ref,anim,sub,blend});
    if (recurse) {
        recurse = false;
        reinterpret_cast<Apply>(functions.at(name))(ref,anim,sub,0);
    }
}
void* routine(const char* requested, int* out_module) {
    if (out_module) *out_module = module_number;
    return missing || !functions.count(requested) ? nullptr : functions.at(requested);
}
KhResult replace(const char* requested, void* callback, void** previous) {
    ++replacements;
    if (replace_fail || !functions.count(requested)) return KH_ERROR;
    if (previous) *previous = null_previous ? nullptr : functions.at(requested);
    functions[requested] = callback;
    if (fail_rollback && previous) replace_fail = true;
    return KH_OK;
}
void fixture() {
    replace_fail = false;
    rxanim::detach();
    memory.fill(0); calls.clear(); functions.clear();
    allowed=loaded=true; missing=replace_fail=null_previous=recurse=fail_rollback=false;
    tick=100; replacements=0; module_number=22;
    api={}; api.size=sizeof api; api.pointer=&pointer; api.module_loaded=&module;
    api.frame=&frame; api.routine=&routine; api.replace=&replace;
    functions[name]=reinterpret_cast<void*>(&native);
    put(actor,0xc,0); put(actor,0x20,node); put(actor,0x6bc,30); put(actor,0x7b0,12*4096);
    put(actor,0x2aba,static_cast<short>(6554));
    put(actor+0x910,0x88,rows); put(actor+0x910,0x8c,1);
    put(rows,3,static_cast<unsigned char>(6)); put(rows,0x38,data);
    put(animation,0,static_cast<unsigned short>(1)); put(animation,0x10,candidates);
    put(candidates,0,target); put(node+4,0xc,bound);
    // Sentinels catch an accidental gameplay clock/contact/position rewrite.
    put(target,0,12345); put(bound,0,67890); put(actor,0x498,4321);
    put(actor,0x7b4,9876); put(actor+0x910,0x474,333);
}
void attach() { CHECK(rxanim::attach(&api,&permitted)); }
void arm() { rxanim::transition(actor,0,1060,0,0,12*4096,3*4096); }
void run(KhRef ref=actor,KhRef anim=animation,int sub=0,int blend=0) {
    reinterpret_cast<Apply>(functions.at(name))(ref,anim,sub,blend);
}
void allowed_native_link_is_one_shot_and_clock_neutral() {
    fixture(); attach(); const auto before=memory;
    arm(); run(); CHECK(calls.size()==1); CHECK(calls[0].blend==3);
    CHECK(calls[0].actor==actor&&calls[0].animation==animation&&calls[0].sub==0);
    CHECK(memory==before); // Real wrapper is read-only, including every native frame/contact byte.
    run(); CHECK(calls.back().blend==0);
    // Longer-startup entry uses a visible N6 fade with an audited table tag.
    put(actor,0x6bc,36); put(actor,0x7b0,12*4096); put(rows,3,static_cast<unsigned char>(7));
    rxanim::transition(actor,1060,5070,0,0,12*4096,9*4096); run(); CHECK(calls.back().blend==6);
    CHECK(get<int>(target)==12345&&get<int>(bound)==67890);
}
void actor_and_lifetime_gates_do_not_leak() {
    fixture(); attach(); arm(); run(other); CHECK(calls.back().blend==0);
    run(); CHECK(calls.back().blend==3); // Another actor cannot steal the armed edge.
    for (int gate=0;gate<4;++gate) {
        arm();
        if(gate==0) allowed=false;
        if(gate==1) loaded=false;
        if(gate==2) put(actor,0xc,19);
        if(gate==3) ++tick;
        run(); CHECK(calls.back().blend==0);
        allowed=loaded=true; put(actor,0xc,0); run(); CHECK(calls.back().blend==0);
    }
}
void native_fades_self_objects_and_stock_blends_are_preserved() {
    fixture(); attach();
    put(candidates,0,bound); arm(); run(); CHECK(calls.back().blend==0);
    put(candidates,0,target); put(node+4,0,static_cast<unsigned short>(4));
    put(node+4,0xd4,target); arm(); run(); CHECK(calls.back().blend==0);
    CHECK(get<unsigned short>(node+4)==4&&get<KhRef>(node+4,0xd4)==target);
    put(node+4,0,static_cast<unsigned short>(0)); arm(); run(actor,animation,0,5);
    CHECK(calls.back().blend==5); run(); CHECK(calls.back().blend==0);
    put(node+4,2,static_cast<short>(-1)); arm(); run(); CHECK(calls.back().blend==0);
    put(node+4,2,static_cast<short>(0)); put(node+4,0xc,0u); arm(); run(); CHECK(calls.back().blend==0);
}
void edge_contact_and_exit_guards_reject_unreviewed_connections() {
    fixture(); attach();
    const auto rejected=[](int from,int to,int g0,int g1,int exit,int first) {
        rxanim::transition(actor,from,to,g0,g1,exit,first); run(); CHECK(calls.back().blend==0);
    };
    rejected(0,1060,0,1,12*4096,3*4096); // No smoothing across takeoff/landing.
    rejected(0,1060,0,0,12*4096,1*4096); // Too little contact-free startup.
    rejected(0,1060,0,0,12*4096,4*4096); // Caller cannot invent a later first contact.
    rejected(0,1060,0,0,6*4096,3*4096); // Source contact has not advanced one frame.
    put(actor,0x7b0,16*4096); rejected(0,1060,0,0,16*4096,3*4096); // Beyond native state end.
    put(actor,0x7b0,12*4096); rejected(0,5130,0,0,12*4096,0); // Frame-zero contacts never fade in.
    rejected(0,1000,0,0,12*4096,6*4096); // Exploratory gear routes are not allowlisted.
    rejected(-1,1060,0,0,12*4096,3*4096); // Opener.
    rejected(0,0,0,0,12*4096,6*4096); // Same clip.
    arm(); rejected(0,5130,0,0,12*4096,0); run(); CHECK(calls.back().blend==0); // Invalid arm clears old arm.
}
void pending_request_matches_live_mode_frame_and_incoming_entry() {
    fixture(); attach(); arm(); put(actor,0x7b0,12*4096+1); run(); CHECK(calls.back().blend==0);
    put(actor,0x7b0,12*4096); arm(); put(actor,0x6bc,0); run(); CHECK(calls.back().blend==0);
    put(actor,0x6bc,30); arm(); run(actor,animation,1); CHECK(calls.back().blend==0);
    arm(); run(actor,animation+0x40); CHECK(calls.back().blend==0); run(); CHECK(calls.back().blend==0);
    arm(); put(rows,3,static_cast<unsigned char>(7)); run(); CHECK(calls.back().blend==0);
    put(rows,3,static_cast<unsigned char>(6)); arm(); put(actor+0x910,0x8c,17); run(); CHECK(calls.back().blend==0);
    put(actor+0x910,0x8c,1); arm(); put(actor,0x20,0u); run(); CHECK(calls.back().blend==0);
}
void reset_detach_and_restore_failure_keep_a_valid_native_chain() {
    fixture(); attach(); arm(); rxanim::reset(other); run(); CHECK(calls.back().blend==3);
    arm(); rxanim::reset(actor); run(); CHECK(calls.back().blend==0);
    arm(); rxanim::reset(); run(); CHECK(calls.back().blend==0);
    arm(); replace_fail=true; rxanim::detach(); run(); CHECK(calls.back().blend==0);
    CHECK(!rxanim::attach(&api,&permitted)); // The disabled pass-through hook still owns its native chain.
    replace_fail=false; rxanim::detach(); CHECK(functions.at(name)==reinterpret_cast<void*>(&native));
    attach(); arm(); rxanim::detach(); CHECK(functions.at(name)==reinterpret_cast<void*>(&native));
    run(); CHECK(calls.back().blend==0);
}
void attachment_prerequisites_and_partial_install_rollback() {
    fixture(); KhEngine incomplete=api; incomplete.size=sizeof api-1;
    CHECK(!rxanim::attach(&incomplete,&permitted)); CHECK(replacements==0);
    incomplete=api; incomplete.pointer=nullptr; CHECK(!rxanim::attach(&incomplete,&permitted));
    incomplete=api; incomplete.frame=nullptr; CHECK(!rxanim::attach(&incomplete,&permitted));
    missing=true; CHECK(!rxanim::attach(&api,&permitted)); missing=false;
    module_number=30; CHECK(!rxanim::attach(&api,&permitted)); module_number=22;
    replace_fail=true; CHECK(!rxanim::attach(&api,&permitted));
    CHECK(functions.at(name)==reinterpret_cast<void*>(&native)); replace_fail=false;
    null_previous=true; CHECK(!rxanim::attach(&api,&permitted));
    CHECK(functions.at(name)==reinterpret_cast<void*>(&native)); null_previous=false;
    null_previous=fail_rollback=true;
    CHECK(!rxanim::attach(&api,&permitted)); run(); CHECK(calls.back().blend==0);
    replace_fail=false; null_previous=fail_rollback=false; rxanim::detach();
    CHECK(functions.at(name)==reinterpret_cast<void*>(&native));
    attach(); CHECK(!rxanim::attach(&api,&permitted)); arm(); run(); CHECK(calls.back().blend==3);
}
void reentrant_native_call_cannot_reuse_connection() {
    fixture(); attach(); arm(); recurse=true; run();
    CHECK(calls.size()==2&&calls[0].blend==3&&calls[1].blend==0);
}
void air_connections_use_audited_tag_and_contact_margin() {
    fixture(); attach(); put(actor,0x6bc,41); put(actor,0x7b0,9*4096);
    put(rows,3,static_cast<unsigned char>(12));
    rxanim::transition(actor,11,12,1,1,9*4096,9*4096); run(); CHECK(calls.back().blend==2);
    put(actor,0x6bc,33); put(actor,0x7b0,9*4096); put(rows,3,static_cast<unsigned char>(14));
    rxanim::transition(actor,3,13140,1,1,9*4096,3*4096); run(); CHECK(calls.back().blend==2);
    put(rows,3,static_cast<unsigned char>(11));
    rxanim::transition(actor,3,11,1,1,9*4096,0); run(); CHECK(calls.back().blend==0);
}
void native_ratio_ramp_and_every_contact_deadline() {
    const std::map<int,int> contact={{0,6},{1060,3},{5070,9},{1,12},{13080,9},{13090,18},
        {10120,9},{14021,12},{6061,3},{12,9},{3,6},{4101,9},{13140,3},{13151,12},{11041,6},{5,6}};
    for(const auto& edge:rxanim::edges) {
        const int n=edge.updates;
        // Exact FX_Div result: (4096/N), rounded to nearest Q12 integer.
        const int increment=(4096+n/2)/n;
        native_fade::AnmTrack outgoing{12*4096,4096-increment},incoming{0,increment};
        native_fade::BlendState fade{};
        fade.flags=4;fade.field_ca=0;fade.outTrack=&outgoing;fade.inTrack=&incoming;
        fade.elapsed=4096-increment;fade.duration=increment;
        CHECK(incoming.field_04>0&&incoming.field_04<4096); // N1 cannot sneak back as 'polish'.
        unsigned updates=0;
        while(fade.flags&4) {
            CHECK(incoming.frame<contact.at(edge.to)*4096);
            native_fade::Anim_StepCrossFade(&fade); // Real native fade math precedes frame advance.
            incoming.frame+=6554; // Conservative ceil(1.6frames/update), independent of ratio ramp.
            ++updates;
            if(fade.flags&4) {
                CHECK(outgoing.frame==12*4096); // Native outgoing pose freezes.
                CHECK(incoming.frame<contact.at(edge.to)*4096);
            }
            CHECK(updates<10);
        }
        CHECK(updates==static_cast<unsigned>(n==2?1:(n==3||n==4)?2:4));
        CHECK(incoming.field_04==4096&&fade.outTrack==&incoming&&fade.inTrack==nullptr);
    }
    // Source-confirmed sanity check: N1 has no mixed initial pose; N2 has one.
    CHECK(4096-(4096/1)==0); CHECK(4096-(4096/2)==2048);
}
void longer_profiles_only_use_audited_stationary_anchor_entries() {
    // Independently decoded NSBCA roxas_tg-minus-roxas_R stationary intervals.
    // Ceil(maxstep1.6) gives3.2002f for N3/N4 and6.4004f for N6;
    // each listed entry remains exactly0 throughout its wider bound below.
    const std::map<int,int> stationary={{0,5*4096},{1,7*4096},{6,20*4096},{7,7*4096}};
    for(const auto& edge:rxanim::edges) if(edge.updates>2) {
        const auto* destination=rxanim::timing(edge.to);
        CHECK(destination!=nullptr);
        const int completed=(edge.updates==6?4:2)*6554;
        CHECK(stationary.count(destination->animation_tag)==1);
        CHECK(completed<stationary.at(destination->animation_tag));
    }
}
void native_anchor_flags_suppress_fade_rewind_then_resume_real_motion() {
    native_anchor::ActorNode anchor_node{4,0x8000};
    native_anchor::Actor anchor_actor{0,&anchor_node,4096,0,1,{0,0,1500}};
    native_anchor::VecFx32 out{};
    for(int z:{1200,900,400,0}) {
        native_anchor::sampled={0,0,z};native_anchor::Ov022_StepAnchorDelta(&anchor_actor,&out);
        CHECK(out.x==0&&out.y==0&&out.z==0);
        CHECK((anchor_actor.nFlags&(native_anchor::FLAG_BIT29|native_anchor::FLAG_BIT30))!=0);
    }
    anchor_node.nAnimFlags=0;
    for(int z:{100,200}) {
        native_anchor::sampled={0,0,z};native_anchor::Ov022_StepAnchorDelta(&anchor_actor,&out);CHECK(out.z==0);
    }
    native_anchor::sampled={0,0,300};native_anchor::Ov022_StepAnchorDelta(&anchor_actor,&out);CHECK(out.z==100);
    // The test is sensitive: an unprotected decreasing anchor really emits a reverse delta.
    native_anchor::sampled={0,0,200};native_anchor::Ov022_StepAnchorDelta(&anchor_actor,&out);CHECK(out.z==-100);
    // N2 can finish before the next actor motion sample: transition flag29 only
    // baselines once, without adding an active-fade flag30 delay.
    anchor_actor.nFlags=native_anchor::FLAG_BIT29;
    native_anchor::sampled={0,0,400};native_anchor::Ov022_StepAnchorDelta(&anchor_actor,&out);CHECK(out.z==0);
    native_anchor::sampled={0,0,500};native_anchor::Ov022_StepAnchorDelta(&anchor_actor,&out);CHECK(out.z==100);
}
void faster_or_reversed_native_animation_steps_refuse_fades() {
    fixture();attach();put(actor,0x2aba,static_cast<short>(6555));arm();run();CHECK(calls.back().blend==0);
    put(actor,0x2aba,static_cast<short>(6554));arm();put(actor,0x2aba,static_cast<short>(6555));run();CHECK(calls.back().blend==0);
    put(actor,0x2aba,static_cast<short>(-1));arm();run();CHECK(calls.back().blend==0);
    put(actor,0x2aba,static_cast<short>(6554));arm();run();CHECK(calls.back().blend==3);
}
}

int main() {
    const std::pair<const char*,std::function<void()>> cases[] = {
        {"allowed links are one-shot and preserve every gameplay byte",test::allowed_native_link_is_one_shot_and_clock_neutral},
        {"actor, dual-wield, module, permission and frame lifetime gates",test::actor_and_lifetime_gates_do_not_leak},
        {"self-object, active-fade and preexisting native blends stay native",test::native_fades_self_objects_and_stock_blends_are_preserved},
        {"unreviewed edges, frame-zero contacts and unsafe exits refuse smoothing",test::edge_contact_and_exit_guards_reject_unreviewed_connections},
        {"one-shot request matches exact live source and incoming animation entry",test::pending_request_matches_live_mode_frame_and_incoming_entry},
        {"reset, detach and failed restore retain a callable native chain",test::reset_detach_and_restore_failure_keep_a_valid_native_chain},
        {"attachment checks interfaces and rolls back incomplete installation",test::attachment_prerequisites_and_partial_install_rollback},
        {"reentrant native animation calls cannot consume an edge twice",test::reentrant_native_call_cannot_reuse_connection},
        {"air links retain native tag and first-contact margins",test::air_connections_use_audited_tag_and_contact_margin},
        {"compiled native fade math meets every edge deadline at max1.6speed",test::native_ratio_ramp_and_every_contact_deadline},
        {"unsupported actual native frame steps refuse smoothing",test::faster_or_reversed_native_animation_steps_refuse_fades},
        {"longer profiles respect actual stationary-anchor startup intervals",test::longer_profiles_only_use_audited_stationary_anchor_entries},
        {"native anchor fade flags block rewind and preserve later real motion",test::native_anchor_flags_suppress_fade_rewind_then_resume_real_motion}
    };
    unsigned failed=0;
    for(const auto& item:cases) try { item.second(); std::cout<<"PASS "<<item.first<<'\n'; }
    catch(const std::exception& error) { ++failed; std::cout<<"FAIL "<<item.first<<": "<<error.what()<<'\n'; }
    rxanim::detach();
    std::cout<<(sizeof(cases)/sizeof(cases[0])-failed)<<" passed, "<<failed<<" failed\n";
    return failed?1:0;
}
