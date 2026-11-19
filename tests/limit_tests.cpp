#include "../src/limit_finishers.cpp"
#include <array>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <functional>

namespace test {
void check(bool yes,const char* msg) { if(!yes)throw std::runtime_error(msg); }
#define CHECK(x) ::test::check((x),#x)
std::array<unsigned char,0x9000> memory{};
constexpr KhRef actor=0x1000,context=actor+0x2cb0;
KhEngine api{};
std::map<std::string,void*> funcs;
unsigned tick,replace_calls,native_calls,arm_calls,resource_calls,motion_calls;
unsigned exit_calls,effect_calls;
unsigned damage_calls;
unsigned part_bind_calls, acquire_calls;
int damage_factor;
int variant,mode,state,speed;
KhRef native_result;
bool allowed,loaded;
bool resource_failure,resource_async;
std::string absent,fail;
std::function<KhRef(KhRef,rxlimit::Finisher,int)> exit_action;
void* pointer(KhRef ref) { return ref>=actor&&ref<memory.size()?memory.data()+ref:nullptr; }
template<class T> void put(KhRef ref,unsigned offset,T value) {std::memcpy(memory.data()+ref+offset,&value,sizeof value);}
template<class T> T get(KhRef ref,unsigned offset=0) {T v;std::memcpy(&v,memory.data()+ref+offset,sizeof v);return v;}
bool permitted(KhRef ref) {return allowed&&ref==actor;}
int module(int n) {return loaded&&(n==22||n==30);}
unsigned frame() {return tick;}
void apply(KhRef ref,int value) {
    CHECK(ref==actor);
    if (value==46) reinterpret_cast<void(*)(KhRef)>(funcs.at("Ov030_BindPartRigs"))(context);
    mode=value;put(ref,0x6bc,value);
}
KhRef set_state(KhRef ref,int value) {CHECK(ref==actor);state=value;return 0x02000000u+static_cast<unsigned>(value)*4;}
void set_speed(KhRef ref,int value) {CHECK(ref==actor);speed=value;}
unsigned char resources(KhRef ref) {
    CHECK(ref==actor);++resource_calls;if(resource_failure)return 0;
    const auto acquire=reinterpret_cast<KhRef(*)(KhRef,int,int,KhRef)>(funcs.at("Ov022_AcquireGridSlot"));
    put(ref,0x262c,static_cast<unsigned char>(0xb));
    put(ref,0x2638,acquire(0x7000,0,0,actor+0x24));put(ref,0x263c,acquire(0x7100,0,1,context+0x234));
    put(actor+0x3c00,3,static_cast<unsigned char>(resource_async?1:3));
    put(actor+0x3c40,3,static_cast<unsigned char>(resource_async?1:3));return 0xb;
}
KhRef acquire(KhRef,int seat,int index,KhRef) {
    CHECK(seat==0 && index>=0 && index<2);++acquire_calls;
    const KhRef entry=actor+0x3c00+static_cast<unsigned>(index)*0x40;
    // Same fixed native grid address is reused even when a different resource
    // was loaded there. Stage3 alone cannot identify the original Limit file.
    put(entry,3,static_cast<unsigned char>(1));
    put(entry,0x28,0x4100u);put(entry,0x2c,index?0x5000u:0x7000u);
    put(entry,0x30,static_cast<std::uint16_t>(index?0x1d00:0x100));
    put(entry,0x32,static_cast<std::uint16_t>(index?0x4000:0x5000));
    put(entry,0x34,static_cast<std::uint16_t>(0));return entry;
}
void bind_parts(KhRef ctx) {
    CHECK(ctx==context);++part_bind_calls;
    const KhRef effect=get<KhRef>(actor,0x263c);
    const unsigned next=get<std::uint16_t>(effect,0x34)+8*600;
    // Native BuildResNodeSet appends eight600-byte sets; it never resets nUsed.
    // The first build fits16KB, while the second would overwrite adjacent RAM.
    CHECK(get<std::uint16_t>(effect,0x30)+next<=get<std::uint16_t>(effect,0x32));
    put(effect,0x34,static_cast<std::uint16_t>(next));
}
void motion(KhRef ctx,int) {CHECK(ctx==context);++motion_calls;put(actor,0x464,0x10000ull);put(actor,0x46c,0x10000ull);}
KhRef arm(KhRef ctx,int value) {
    CHECK(ctx==context);++arm_calls;variant=value;
    put(actor,0x7b0,value?57*4096:0);
    put(actor,0x6bc,value?48:47);
    return set_state(actor,value?35:34);
}
void damage(KhRef ref,int factor,KhRef out_damage,KhRef out_extra) {
    CHECK(ref==actor);++damage_calls;damage_factor=factor;put(out_damage,0,factor);put(out_extra,0,0);
}
KhRef native(KhRef ctx) {
    CHECK(ctx==context);++native_calls;CHECK((get<unsigned short>(actor,0x18)&1)==0);
    const int timeline=get<int>(actor,0x7b0);
    for(int at:{9,15,21,27,33,39,51,60,75})if(timeline==at*4096) {
        ++effect_calls;
        const auto scale=reinterpret_cast<void(*)(KhRef,int,KhRef,KhRef)>(funcs.at("Ov022_ScaleMoveDamage"));
        scale(actor,variant==1?0x12c0:0x13b0,0x8500,0x8504);
    }
    return native_result;
}
KhRef exit_handler(KhRef ref,rxlimit::Finisher kind,int timeline) {
    CHECK(ref==actor);++exit_calls;return exit_action?exit_action(ref,kind,timeline):0;
}
void* routine(const char* name,int*) {if(absent==name)return nullptr;return funcs.count(name)?funcs.at(name):nullptr;}
KhResult replace(const char* name,void* replacement,void** previous) {
    ++replace_calls;if(fail==name)return KH_ERROR;
    if(!funcs.count(name))return KH_ERROR;
    if(previous)*previous=funcs.at(name);funcs[name]=replacement;return KH_OK;
}
void reset() {
    rxlimit::detach();memory.fill(0);funcs.clear();tick=100;replace_calls=native_calls=arm_calls=resource_calls=motion_calls=0;
    variant=mode=state=speed=-1;native_result=0;allowed=loaded=true;resource_failure=resource_async=false;absent.clear();fail.clear();
    exit_calls=effect_calls=damage_calls=part_bind_calls=acquire_calls=0;damage_factor=0;exit_action={};
    api={};api.size=sizeof(api);api.pointer=&pointer;api.module_loaded=&module;api.frame=&frame;api.routine=&routine;api.replace=&replace;
    funcs={{"Ov030_ArmPlayerTarget",reinterpret_cast<void*>(&arm)},
           {"Ov030_ApplyModeChange",reinterpret_cast<void*>(&apply)},
           {"Ov022_ActorSetState",reinterpret_cast<void*>(&set_state)},
           {"Ov022_SetAnimSpeed",reinterpret_cast<void*>(&set_speed)},
           {"Ov030_BuildActorHandles",reinterpret_cast<void*>(&resources)},
           {"Ov030_BindPartRigs",reinterpret_cast<void*>(&bind_parts)},
           {"Ov022_AcquireGridSlot",reinterpret_cast<void*>(&acquire)},
           {"Ov030_UpdateMotionController",reinterpret_cast<void*>(&motion)},
           {"Ov022_ScaleMoveDamage",reinterpret_cast<void*>(&damage)},
           {"Ov030_UpdateController",reinterpret_cast<void*>(&native)},
           {"Ov030_ComputeAimAngle",reinterpret_cast<void*>(&native)}};
    put(actor,0xc,0);put(context,0xdb4,actor);put(actor,0x664,0x02001000u);put(actor,0x668,0x02002000u);
    put(actor,0x12,static_cast<unsigned short>(100));put(actor,0x18,static_cast<unsigned short>(0x801));put(actor,0x24,4u);
}
KhRef run(const char* name) {return reinterpret_cast<KhRef(*)(KhRef)>(funcs.at(name))(context);}
void attach() {CHECK(rxlimit::attach(&api,&permitted));}
void prerequisites_and_rollback() {
    reset();absent="Ov030_ArmPlayerTarget";CHECK(!rxlimit::attach(&api,&permitted));CHECK(replace_calls==0);
    reset();fail="Ov030_ComputeAimAngle";CHECK(!rxlimit::attach(&api,&permitted));
    CHECK(funcs.at("Ov030_UpdateController")==reinterpret_cast<void*>(&native));
    reset();fail="Ov022_ScaleMoveDamage";CHECK(!rxlimit::attach(&api,&permitted));
    CHECK(funcs.at("Ov030_UpdateController")==reinterpret_cast<void*>(&native));
    CHECK(funcs.at("Ov030_UpdateMotionController")==reinterpret_cast<void*>(&motion));
    reset();fail="Ov030_BindPartRigs";CHECK(!rxlimit::attach(&api,&permitted));
    CHECK(funcs.at("Ov022_ScaleMoveDamage")==reinterpret_cast<void*>(&damage));
    CHECK(funcs.at("Ov030_UpdateController")==reinterpret_cast<void*>(&native));
    reset();fail="Ov022_AcquireGridSlot";CHECK(!rxlimit::attach(&api,&permitted));
    CHECK(funcs.at("Ov030_BindPartRigs")==reinterpret_cast<void*>(&bind_parts));
    CHECK(funcs.at("Ov030_BuildActorHandles")==reinterpret_cast<void*>(&resources));
    reset();attach();put(context,0xdb4,0u);CHECK(rxlimit::begin(actor,rxlimit::Finisher::Rush)==0);CHECK(arm_calls==0);
    put(context,0xdb4,actor);put(actor,0xc,19);CHECK(rxlimit::begin(actor,rxlimit::Finisher::Rush)==0);
    put(actor,0xc,0);allowed=false;CHECK(rxlimit::begin(actor,rxlimit::Finisher::Rush)==0);CHECK(arm_calls==0);
}
void native_rush_is_bounded_and_health_independent() {
    reset();attach();
    CHECK(rxlimit::suppress_legacy_start(actor,23));CHECK(!rxlimit::suppress_legacy_start(actor,24));
    KhRef result=rxlimit::begin(actor,rxlimit::Finisher::Rush);
    CHECK(result==0x02000000u+34*4);CHECK(mode==46);CHECK(state==34);CHECK(variant==0);CHECK(speed==4096);
    CHECK(rxlimit::active(actor));CHECK(!rxlimit::suppress_legacy_start(actor,23));
    CHECK(rxlimit::begin(actor,rxlimit::Finisher::Rush)==0);CHECK(arm_calls==1);
    put(actor,0x7b0,9*4096);CHECK(run("Ov030_UpdateController")==0);CHECK(native_calls==1);
    CHECK(get<unsigned short>(actor,0x18)==0x801);CHECK(get<int>(context,8)==1);
    put(actor,0x7b0,15*4096);CHECK(run("Ov030_UpdateController")==0);CHECK(native_calls==2);
    put(actor,0x7b0,18*4096);CHECK(run("Ov030_UpdateController")==0x02000000u);
    CHECK(native_calls==2);CHECK(!rxlimit::active());CHECK(state==0);
    CHECK(get<unsigned short>(actor,0x12)==100);CHECK(rxlimit::suppress_legacy_start(actor,23));
    put(actor,0x12,static_cast<unsigned short>(1));CHECK(rxlimit::begin(actor,rxlimit::Finisher::Rush)!=0);
    CHECK(get<unsigned short>(actor,0x12)==1);rxlimit::forget(actor);
}
void burst_tail_and_native_exit_are_preserved() {
    reset();attach();CHECK(rxlimit::begin(actor,rxlimit::Finisher::LightBurst)==0x02000000u+35*4);
    CHECK(variant==1);CHECK(get<int>(actor,0x7b0)==57*4096);
    put(actor,0x7b0,60*4096);CHECK(run("Ov030_ComputeAimAngle")==0);
    put(actor,0x7b0,75*4096);CHECK(run("Ov030_ComputeAimAngle")==0);CHECK(native_calls==2);
    put(actor,0x24,0u);put(actor,0x7b0,81*4096);
    CHECK(run("Ov030_ComputeAimAngle")==0x02000000u+2*4);CHECK(native_calls==2);CHECK(state==2);
    CHECK(rxlimit::begin(actor,rxlimit::Finisher::Rush)!=0);native_result=0x0200abcd;
    CHECK(run("Ov030_UpdateController")==native_result);CHECK(!rxlimit::active());
}
void timeout_cancel_lifetime_and_unwrap() {
    reset();attach();CHECK(rxlimit::begin(actor,rxlimit::Finisher::Rush)!=0);tick+=121;
    CHECK(run("Ov030_UpdateController")!=0);CHECK(native_calls==0);CHECK(!rxlimit::active());
    CHECK(rxlimit::begin(actor,rxlimit::Finisher::Rush)!=0);CHECK(rxlimit::cancel(actor)!=0);CHECK(!rxlimit::active());
    CHECK(rxlimit::begin(actor,rxlimit::Finisher::Rush)!=0);rxlimit::forget(actor);CHECK(!rxlimit::active());
    rxlimit::detach();CHECK(funcs.at("Ov030_UpdateController")==reinterpret_cast<void*>(&native));
    CHECK(funcs.at("Ov030_ComputeAimAngle")==reinterpret_cast<void*>(&native));
}
void flurry_exposes_middle_without_runaway_loop() {
    reset();attach();CHECK(rxlimit::begin(actor,rxlimit::Finisher::Flurry)!=0);
    for(int frame:{9,15,21,27,33,39,51}) {put(actor,0x7b0,frame*4096);CHECK(run("Ov030_UpdateController")==0);}
    CHECK(native_calls==7);put(actor,0x7b0,57*4096);CHECK(run("Ov030_UpdateController")!=0);
    CHECK(native_calls==7);CHECK(!rxlimit::active());
}
void full_hp_lazily_loads_native_limit_handles_once() {
    reset();attach();CHECK(resource_calls==0);CHECK(get<KhRef>(actor,0x263c)==0);
    CHECK(rxlimit::begin(actor,rxlimit::Finisher::Rush)!=0);CHECK(resource_calls==1);CHECK(get<unsigned short>(actor,0x12)==100);
    rxlimit::forget();CHECK(rxlimit::begin(actor,rxlimit::Finisher::LightBurst)!=0);CHECK(resource_calls==1);rxlimit::forget();
    reset();attach();resource_failure=true;CHECK(rxlimit::begin(actor,rxlimit::Finisher::Rush)==0);
    CHECK(resource_calls==1);CHECK(arm_calls==0);CHECK(!rxlimit::active());
}
void asynchronous_resources_wait_and_reuse_native_grid_slots() {
    reset();attach();resource_async=true;
    CHECK(!rxlimit::prepare(actor));CHECK(resource_calls==1);CHECK(!rxlimit::prepare(actor));CHECK(resource_calls==1);
    CHECK(rxlimit::begin(actor,rxlimit::Finisher::Rush)==0);CHECK(arm_calls==0);CHECK(!rxlimit::active());
    put(actor+0x3c00,3,static_cast<unsigned char>(3));put(actor+0x3c40,3,static_cast<unsigned char>(3));
    CHECK(rxlimit::prepare(actor));CHECK(rxlimit::begin(actor,rxlimit::Finisher::Rush)!=0);CHECK(resource_calls==1);
    const auto build=reinterpret_cast<unsigned char(*)(KhRef)>(funcs.at("Ov030_BuildActorHandles"));
    CHECK(build(actor)==0xb);CHECK(resource_calls==1); // Stock low-HP preparation cannot reacquire occupied grids.
}
void repeated_signatures_reuse_the_eight_append_only_native_part_tables() {
    reset();attach();
    for(unsigned turn=0;turn<20;++turn) {
        const auto kind=turn%3==0?rxlimit::Finisher::Rush:turn%3==1?rxlimit::Finisher::Flurry:rxlimit::Finisher::LightBurst;
        CHECK(rxlimit::begin(actor,kind)!=0);CHECK(part_bind_calls==1);
        CHECK(get<std::uint16_t>(actor+0x3c40,0x34)==4800);
        if(turn%2==0) CHECK(rxlimit::cancel(actor)!=0);
        else rxlimit::forget(actor); // A live hit interruption must not discard the resource epoch.
        ++tick;
    }
    CHECK(resource_calls==1 && acquire_calls==2);
}
void foreign_grid_reloads_defer_until_ready_and_then_rebind_exactly_once() {
    reset();attach();CHECK(rxlimit::begin(actor,rxlimit::Finisher::Rush)!=0);CHECK(rxlimit::cancel(actor)!=0);
    const auto acquire_slot=reinterpret_cast<KhRef(*)(KhRef,int,int,KhRef)>(funcs.at("Ov022_AcquireGridSlot"));
    const KhRef old_grid=get<KhRef>(actor,0x2638);
    CHECK(acquire_slot(0x7200,0,0,actor+0x24)==old_grid);
    const auto calls=acquire_calls;
    CHECK(!rxlimit::prepare(actor));CHECK(acquire_calls==calls && resource_calls==1);
    CHECK(rxlimit::begin(actor,rxlimit::Finisher::Rush)==0);CHECK(acquire_calls==calls);
    put(old_grid,3,static_cast<unsigned char>(3));
    // Even a ready foreign grid cannot replace a resource while native emitter
    // animation objects are using its old archive-backed tables.
    put(context+0x234,0x12c,static_cast<unsigned char>(4));CHECK(!rxlimit::prepare(actor));CHECK(acquire_calls==calls);
    put(context+0x234,0x12c,static_cast<unsigned char>(0));
    CHECK(rxlimit::prepare(actor));CHECK(resource_calls==2 && acquire_calls==calls+2);
    CHECK(rxlimit::begin(actor,rxlimit::Finisher::Flurry)!=0);CHECK(part_bind_calls==2);
    CHECK(get<std::uint16_t>(actor+0x3c40,0x34)==4800);
    CHECK(rxlimit::cancel(actor)!=0);CHECK(rxlimit::begin(actor,rxlimit::Finisher::Rush)!=0);CHECK(part_bind_calls==2);
}
void lingering_effects_keep_native_updates_without_ordinary_input_lock() {
    reset();attach();CHECK(rxlimit::begin(actor,rxlimit::Finisher::Rush)!=0);
    const auto update=reinterpret_cast<void(*)(KhRef,int)>(funcs.at("Ov030_UpdateMotionController"));
    update(context,4096);CHECK(motion_calls==1);CHECK(get<unsigned long long>(actor,0x464)&0x10000ull);
    put(actor,0x7b0,18*4096);CHECK(run("Ov030_UpdateController")!=0);CHECK(!rxlimit::active());
    update(context,4096);CHECK(motion_calls==2);CHECK((get<unsigned long long>(actor,0x464)&0x10000ull)==0);
    put(actor,0x6bc,8);update(context,4096);CHECK(get<unsigned long long>(actor,0x464)&0x10000ull); // Hitstun is not globally unlocked.
    put(actor,0x6bc,30);update(context,4096);CHECK((get<unsigned long long>(actor,0x464)&0x10000ull)==0);
    rxlimit::forget(actor);update(context,4096);
    CHECK((get<unsigned long long>(actor,0x464)&0x10000ull)==0); // Existing effects survive a live interruption.
    const auto before_retirement=motion_calls;allowed=false;update(context,4096);
    CHECK(motion_calls==before_retirement); // True owner retirement must not update its old effects context.
}
void exit_windows_wait_for_native_impacts_and_do_not_repeat_damage() {
    reset();attach();rxlimit::set_exit_handler(&exit_handler);
    exit_action=[](KhRef ref,rxlimit::Finisher kind,int timeline) {
        CHECK(kind==rxlimit::Finisher::Rush&&timeline>=12*4096);
        CHECK(rxlimit::cancel(ref)!=0);return set_state(ref,8);
    };
    CHECK(rxlimit::begin(actor,rxlimit::Finisher::Rush)!=0);
    put(actor,0x7b0,12*4096);CHECK(run("Ov030_UpdateController")==0);CHECK(exit_calls==0); // no authored impact stepped yet
    put(actor,0x7b0,9*4096);CHECK(run("Ov030_UpdateController")==0);CHECK(effect_calls==1);
    put(actor,0x7b0,12*4096);CHECK(run("Ov030_UpdateController")==0x02000000u+8*4);
    CHECK(exit_calls==1&&state==8&&!rxlimit::active());CHECK(effect_calls==1&&native_calls==2);
    put(actor,0x7b0,15*4096);CHECK(run("Ov030_UpdateController")==0);CHECK(effect_calls==1&&native_calls==2);
}
void flurry_and_burst_have_distinct_finite_exit_windows() {
    reset();attach();rxlimit::set_exit_handler(&exit_handler);
    CHECK(rxlimit::begin(actor,rxlimit::Finisher::Flurry)!=0);
    put(actor,0x7b0,9*4096);CHECK(run("Ov030_UpdateController")==0);
    put(actor,0x7b0,15*4096);CHECK(run("Ov030_UpdateController")==0);CHECK(exit_calls==0&&effect_calls==2);
    put(actor,0x7b0,18*4096);CHECK(run("Ov030_UpdateController")==0);CHECK(exit_calls==1);
    put(actor,0x7b0,57*4096);CHECK(run("Ov030_UpdateController")!=0);CHECK(!rxlimit::active());
    rxlimit::set_exit_handler(&exit_handler);CHECK(rxlimit::begin(actor,rxlimit::Finisher::LightBurst)!=0);exit_calls=effect_calls=0;
    exit_action=[](KhRef ref,rxlimit::Finisher kind,int timeline) {
        CHECK(kind==rxlimit::Finisher::LightBurst&&timeline==78*4096);return rxlimit::cancel(ref);
    };
    put(actor,0x7b0,60*4096);CHECK(run("Ov030_ComputeAimAngle")==0);
    put(actor,0x7b0,75*4096);CHECK(run("Ov030_ComputeAimAngle")==0);CHECK(exit_calls==0&&effect_calls==2);
    put(actor,0x7b0,78*4096);CHECK(run("Ov030_ComputeAimAngle")!=0);
    CHECK(exit_calls==1&&effect_calls==2&&!rxlimit::active());
}
void buffered_ordinary_action_survives_exit_and_reentry_is_bounded() {
    reset();attach();rxlimit::set_exit_handler(&exit_handler);bool buffered=false;
    exit_action=[&](KhRef ref,rxlimit::Finisher,int) {
        CHECK(run("Ov030_UpdateController")==0); // callback reentry cannot advance effects
        CHECK(rxlimit::begin(ref,rxlimit::Finisher::Flurry)==0);
        buffered=true;return rxlimit::cancel(ref);
    };
    CHECK(rxlimit::begin(actor,rxlimit::Finisher::Rush)!=0);put(actor,0x7b0,9*4096);CHECK(run("Ov030_UpdateController")==0);
    put(actor,0x7b0,12*4096);CHECK(run("Ov030_UpdateController")!=0);CHECK(buffered&&effect_calls==1&&exit_calls==1);
    CHECK(rxlimit::begin(actor,rxlimit::Finisher::Rush)!=0);CHECK(buffered); // helper never clears caller's ordinary buffer
}
void suspension_freezes_callback_effects_and_independent_timeout() {
    reset();attach();rxlimit::set_exit_handler(&exit_handler);CHECK(rxlimit::begin(actor,rxlimit::Finisher::Rush)!=0);
    put(actor,0x7b0,9*4096);CHECK(run("Ov030_UpdateController")==0);put(actor,0x7b0,12*4096);
    rxlimit::set_suspended(true);allowed=false;tick+=1000;
    CHECK(run("Ov030_UpdateController")==0);CHECK(rxlimit::active()&&exit_calls==0&&effect_calls==1);
    const auto update=reinterpret_cast<void(*)(KhRef,int)>(funcs.at("Ov030_UpdateMotionController"));
    update(context,4096);CHECK(motion_calls==0);CHECK(rxlimit::begin(actor,rxlimit::Finisher::Flurry)==0);
    allowed=true;rxlimit::set_suspended(false);CHECK(run("Ov030_UpdateController")==0);CHECK(rxlimit::active()&&exit_calls==1);
    tick+=121;CHECK(run("Ov030_UpdateController")!=0);CHECK(!rxlimit::active());CHECK(exit_calls==1);
}
void retired_managed_callbacks_never_reach_native_or_exit_handler() {
    reset();attach();rxlimit::set_exit_handler(&exit_handler);CHECK(rxlimit::begin(actor,rxlimit::Finisher::Rush)!=0);
    put(actor,0x7b0,9*4096);CHECK(run("Ov030_UpdateController")==0);rxlimit::forget(actor);
    allowed=false;std::memset(memory.data()+actor,0xa5,0x8000-actor);
    CHECK(run("Ov030_UpdateController")==0);CHECK(native_calls==1&&effect_calls==1&&exit_calls==0);
    const auto update=reinterpret_cast<void(*)(KhRef,int)>(funcs.at("Ov030_UpdateMotionController"));update(context,4096);CHECK(motion_calls==0);
    CHECK(get<unsigned char>(actor)==0xa5);
}
void signature_never_enables_generic_limit_protection_or_changes_hp() {
    reset();attach();put(actor,0x22f8,0x04000200u);put(actor,0x22fc,3);
    put(actor,0x4b4,0x1234);const auto original_flags=get<unsigned long long>(actor);
    CHECK(rxlimit::begin(actor,rxlimit::Finisher::Rush)!=0);
    CHECK(get<unsigned long long>(actor)==original_flags);CHECK(get<unsigned short>(actor,0x12)==100);
    CHECK(get<unsigned>(actor,0x22f8)==0x04000200u&&get<int>(actor,0x22fc)==3&&get<int>(actor,0x4b4)==0x1234);
    put(actor,0x7b0,9*4096);CHECK(run("Ov030_UpdateController")==0);CHECK(speed==4096);
    CHECK(get<unsigned>(actor,0x22f8)==0x04000200u&&get<int>(actor,0x22fc)==3&&get<int>(actor,0x4b4)==0x1234);
}
void damage_budget_is_scoped_bounded_and_preserves_native_factors_by_default() {
    reset();attach();CHECK(rxlimit::begin(actor,rxlimit::Finisher::Rush)!=0);
    put(actor,0x7b0,9*4096);CHECK(run("Ov030_UpdateController")==0);CHECK(damage_calls==1&&damage_factor==0x13b0);
    const auto scale=reinterpret_cast<void(*)(KhRef,int,KhRef,KhRef)>(funcs.at("Ov022_ScaleMoveDamage"));
    rxlimit::set_damage_scale_fx(2048);
    scale(actor,0x13b0,0x8500,0x8504);CHECK(damage_factor==0x13b0); // outside managed controller
    put(actor,0x7b0,15*4096);CHECK(run("Ov030_UpdateController")==0);CHECK(damage_factor==0x9d8);
    rxlimit::set_damage_scale_fx(-1);put(actor,0x7b0,9*4096);CHECK(run("Ov030_UpdateController")==0);CHECK(damage_factor==0x4ec);
    rxlimit::set_damage_scale_fx(99999);put(actor,0x7b0,15*4096);CHECK(run("Ov030_UpdateController")==0);CHECK(damage_factor==0x13b0);
    CHECK(rxlimit::cancel(actor)!=0);CHECK(rxlimit::begin(actor,rxlimit::Finisher::LightBurst)!=0);
    rxlimit::set_damage_scale_fx(2048);put(actor,0x7b0,60*4096);CHECK(run("Ov030_ComputeAimAngle")==0);CHECK(damage_factor==0x960);
    scale(actor,0x400,0x8500,0x8504);CHECK(damage_factor==0x400); // ordinary factor passes unchanged
    CHECK(get<unsigned short>(actor,0x12)==100);
}
}
int main() {
    const std::pair<const char*,std::function<void()>> cases[]={
      {"Limit bridge requires complete native resources and rolls back hooks",test::prerequisites_and_rollback},
      {"regular Rush preserves native calls, caps slice and ignores HP threshold",test::native_rush_is_bounded_and_health_independent},
      {"LightBurst starts native tail and preserves native state reference",test::burst_tail_and_native_exit_are_preserved},
      {"frozen animation timeout, cancel, actor retirement and unwrap",test::timeout_cancel_lifetime_and_unwrap},
      {"bounded Flurry exposes the middle native action slice",test::flurry_exposes_middle_without_runaway_loop},
      {"full-HP native Limit resources load once, incomplete load refuses",test::full_hp_lazily_loads_native_limit_handles_once},
      {"asynchronous grid loads wait and preloaded slots are reused",test::asynchronous_resources_wait_and_reuse_native_grid_slots},
      {"repeated signatures reuse append-only emitter tables across recovery and live interruption",test::repeated_signatures_reuse_the_eight_append_only_native_part_tables},
      {"foreign grid replacement defers during loading and active emitters, then rebinds once",test::foreign_grid_reloads_defer_until_ready_and_then_rebind_exactly_once},
      {"native lingering effects do not lock ordinary actions or unlock hitstun",test::lingering_effects_keep_native_updates_without_ordinary_input_lock},
      {"Rush exits after native impact without duplicate future effects",test::exit_windows_wait_for_native_impacts_and_do_not_repeat_damage},
      {"Flurry and LightBurst preserve their distinct impact exit windows",test::flurry_and_burst_have_distinct_finite_exit_windows},
      {"ordinary input buffer survives and exit callback cannot recurse",test::buffered_ordinary_action_survives_exit_and_reentry_is_bounded},
      {"pause freezes native effects, exit callback and timeout",test::suspension_freezes_callback_effects_and_independent_timeout},
      {"retired managed callbacks cannot reach original or touch reused actor",test::retired_managed_callbacks_never_reach_native_or_exit_handler},
      {"signature bypass does not enable generic protection or change HP",test::signature_never_enables_generic_limit_protection_or_changes_hp},
      {"damage budget is bounded, scoped and native by default",test::damage_budget_is_scoped_bounded_and_preserves_native_factors_by_default}};
    unsigned failed=0;
    for(const auto& c:cases)try{c.second();std::cout<<"PASS "<<c.first<<'\n';}catch(const std::exception& ex){++failed;std::cout<<"FAIL "<<c.first<<": "<<ex.what()<<'\n';}
    std::cout<<(sizeof(cases)/sizeof(cases[0])-failed)<<" passed, "<<failed<<" failed\n";
    rxlimit::detach();return failed?1:0;
}
