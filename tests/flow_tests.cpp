// Real controller and Limit bridge, independent original graph/record fixtures.
// Companion modules have separate native-host suites; controller wiring uses observable stubs.
#include "../src/combat.cpp"
#include "flow_module_stubs.h"
// Production compiles this helper separately. Suppress only the artificial
// same-translation-unit global-shadow warning while retaining /W4 elsewhere.
#pragma warning(push)
#pragma warning(disable:4459)
#include "../src/limit_finishers.cpp"
#pragma warning(pop)
#include "generated_fixture.h"
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <functional>
#include <iostream>

namespace test {
void check(bool yes, const char* message) { if (!yes) throw std::runtime_error(message); }
#define CHECK(x) ::test::check((x), #x)
std::vector<unsigned char> memory(16*1024*1024);
std::map<KhRef,unsigned> live;
std::map<KhRef,KhRef> allocation_heaps;
std::map<KhRef,std::vector<KhRef>> parts;
std::map<KhRef,std::array<unsigned char,kTableSize>> snapshots;
std::map<std::string,void*> routines;
std::vector<std::string> logs;
std::vector<KhRef> torn;
unsigned cursor, pad_frame, pad_pressed, pad_held, hidden, replace_calls, find_calls, speed_calls;
unsigned pair_calls,node_clear_calls,state_calls,native_limit_calls;
unsigned resolve_calls;
KhRef current_heap,active_bank_resource;
int roll_permission,guard_permission,steer_result,observed_move_speed,observed_turn_speed,observed_state;
std::function<void()> during_step;
int blocked, room, seat, build_calls, next_calls, walk_calls, translate_calls, step_calls;
int observed_command, observed_phase, step_result;
bool loaded;
bool sdk_live;
int input_heading, target_valid;
bool target_handle_live, resolve_wrong_generation;
KhRef sdk_raw_ref;
KhRef actor, original_table, scene, cinematic;
std::string missing, missing_routine, fail_replace;
int short_interface;
KhHost api{};
KhEngine e{};
KhActors a{};
KhInput i{};
KhNet n{};
KhEvents ev{};

void* ptr(KhRef ref) {
    auto it=live.upper_bound(ref);
    if(it==live.begin())return nullptr;
    --it;
    return ref>=it->first&&ref<it->first+it->second ? memory.data()+ref:nullptr;
}
KhRef alloc(unsigned size) {
    KhRef ref=cursor;
    cursor=(cursor+size+15)&~15u;
    CHECK(cursor<memory.size());
    live.emplace(ref,size);
    allocation_heaps[ref]=current_heap;
    std::memset(memory.data()+ref,0,size);
    return ref;
}
void free_mem(KhRef ref) {CHECK(allocation_heaps.at(ref)==current_heap);CHECK(allocation_heaps.erase(ref)==1);CHECK(live.erase(ref)==1);}
template<class T> void put(KhRef ref,unsigned off,T value) {
    CHECK(ptr(ref)!=nullptr);
    std::memcpy(static_cast<unsigned char*>(ptr(ref))+off,&value,sizeof value);
}
template<class T> T get(KhRef ref,unsigned off=0) {
    CHECK(ptr(ref)!=nullptr);
    T value;
    std::memcpy(&value,static_cast<unsigned char*>(ptr(ref))+off,sizeof value);
    return value;
}
KhRef part(KhRef owner,unsigned size) { KhRef ref=alloc(size);parts[owner].push_back(ref);return ref; }
const FixtureRecord& record(int kind,int id) {
    for(const auto& r:fixture_records) if(r.kind==kind&&r.id==id)return r;
    throw std::runtime_error("fixture record missing");
}
void build_original(KhRef table,KhRef who,int kind,int row) {
    ++build_calls;
    CHECK(who==actor);CHECK(kind==get<int>(actor,0xc));CHECK(ptr(table)!=nullptr);
    CHECK(parts.count(table)==0);
    std::memset(ptr(table),0,kTableSize);
    for(unsigned x=8;x<0x28;++x)put(table,x,static_cast<unsigned char>(row+x));
    // Mock the original loader using independent graph records from the game data.
    for(int group=0;group<2;++group)for(int pool=0;pool<3;++pool) {
        std::vector<const FixtureNode*> nodes;
        // r2 wp rows0 and1 both select graph0 in the independently parsed wp.b.
        // The unused graph1 must not be invented by the native-loader mock.
        const int fixture_row = kind==19 && row==1 ? 0 : row;
        for(const auto& node:fixture_nodes)if(node.kind==kind&&node.row==fixture_row&&node.group==group&&node.pool==pool)nodes.push_back(&node);
        if(nodes.empty())continue;
        KhRef base=part(table,static_cast<unsigned>(nodes.size()*0x18));
        put(table,0x28+0x30*group+0x18+4*pool,base);
        put(table,0x28+0x30*group+0x24+4*pool,static_cast<unsigned>(nodes.size()));
        for(unsigned x=0;x<nodes.size();++x) {
            const auto& r=record(kind,nodes[x]->id);
            KhRef rr=part(table,0x80);
            put(rr,0,static_cast<std::uint16_t>(r.id));
            put(rr,2,static_cast<unsigned char>(r.words[1]));
            put(rr,3,static_cast<unsigned char>(r.words[2]));
            for(unsigned w=3;w<14;++w)put(rr,4+(w-3)*4,r.words[w]);
            put(rr,0x30,static_cast<std::int16_t>(r.words[14]));
            if(r.words[14]>0) {
                KhRef channels=part(table,static_cast<unsigned>(r.words[14])*0x48);
                put(rr,0x34,channels);
                for(const auto& c:fixture_contacts)if(c.kind==kind&&c.id==r.id)put(channels+0x48u*c.index,8,c.end);
            }
            put(base+0x18*x,0,nodes[x]->id);
            put(base+0x18*x,0x14,rr);
        }
    }
    // Mark bank-owned animation row resources at the real resource-pointer offset.
    put(table,0x1a8+0x28,part(table,32));
    std::array<unsigned char,kTableSize> copy{};
    std::memcpy(copy.data(),ptr(table),kTableSize);
    snapshots[table]=copy;
}
void tear_original(KhRef table) {
    CHECK(ptr(table)!=nullptr);
    CHECK(parts.count(table)==1);
    CHECK(table!=active_bank_resource);
    if(table==original_table)CHECK(std::memcmp(ptr(table),snapshots.at(table).data(),kTableSize)==0);
    for(KhRef p:parts.at(table))free_mem(p);
    parts.erase(table);
    torn.push_back(table);
}
void next_original(KhRef) {++next_calls;}
void walk_original(KhRef,int,int) {++walk_calls;}
void translate_original(KhRef who) {++translate_calls;observed_command=get<unsigned char>(who,0x4d6);}
int step_original(KhRef who) {++step_calls;observed_phase=get<int>(who+kTableOffset,0x47c);if(during_step)during_step();return step_result;}
void reset_original(KhRef rr) { CHECK(ptr(rr)!=nullptr);for(const auto& owner:parts)for(KhRef p:owner.second)if(p==rr)active_bank_resource=owner.first; }
void speed_original(KhRef who,int value) {CHECK(who==actor);CHECK(value>=4096&&value<=6554);put(who,0x458,value);++speed_calls;}
void animation_original(KhRef who,int) {CHECK(who==actor);active_bank_resource=0;}
KhRef get_heap() {return current_heap;}
KhRef set_heap(KhRef heap) {KhRef old=current_heap;current_heap=heap;return old;}
int steer_original(KhRef who,int) {CHECK(who==actor);observed_move_speed=get<int>(who,0x4b8);observed_turn_speed=get<int>(who,0x4b0);put(who,0x458,4096);return steer_result;}
int state_original(KhRef who,int state) {CHECK(who==actor);++state_calls;observed_state=state;return 0x02000000+state*4;}
int jump_original(KhRef who,int signal) {
    CHECK(who==actor&&signal==0);put(who,0x698,0);put(who,0x69c,0x630);put(who,0x6a0,0);
    put(who,0x464,get<std::uint64_t>(who,0x464)|0x20ull);
    return change_state(who,1);
}
void pair_original(KhRef who) {CHECK(who==actor);++pair_calls;}
void node_clear_original(KhRef table) {CHECK(table==original_table);++node_clear_calls;}
int roll_original(KhRef block) {CHECK(block==actor+0x118c);return roll_permission;}
int guard_original(KhRef block) {CHECK(block==actor+0x1070);return guard_permission;}
void apply_original(KhRef who,int value) {CHECK(who==actor);put(who,0x6bc,value);}
KhRef arm_original(KhRef ctx,int variant) {
    CHECK(ctx==actor+0x2cb0);put(actor,0x7b0,variant?57*4096:0);
    return static_cast<KhRef>(reinterpret_cast<int(*)(KhRef,int)>(routines.at("Ov022_ActorSetState"))(actor,variant?35:34));
}
KhRef limit_original(KhRef ctx) {CHECK(ctx==actor+0x2cb0);++native_limit_calls;return 0;}
unsigned char limit_resources_original(KhRef who) {
    CHECK(who==actor);put(who,0x262c,static_cast<unsigned char>(0xb));
    put(who,0x2638,actor+0x3c00);put(who,0x263c,actor+0x3c40);
    put(actor+0x3c00,3,static_cast<unsigned char>(3));put(actor+0x3c40,3,static_cast<unsigned char>(3));return 0xb;
}
void limit_motion_original(KhRef,int) {}
void limit_damage_original(KhRef,int,KhRef,KhRef) {}
void limit_bind_parts_original(KhRef) {}
KhRef limit_acquire_grid_original(KhRef,int,int index,KhRef) { return actor+0x3c00+static_cast<unsigned>(index)*0x40; }
int heading_original(KhRef who) {CHECK(who==actor);return input_heading;}
int target_valid_original(KhRef who) {CHECK(who==actor);return target_valid;}
KhRef target_position_original(KhRef who) {CHECK(who==actor);return actor+0x3d00;}
void resolve_target_original(KhRef who) {
    CHECK(who==actor);++resolve_calls;
    if(get<unsigned char>(who,0x4f0)==1&&get<unsigned char>(who,0x4f1)==7) {
        put(who,0x4fc,actor+(resolve_wrong_generation?0x3900u:0x3800u));
        put(who,0x500,actor+0x3a00);
    } else std::memset(static_cast<unsigned char*>(ptr(who))+0x4f0,0,0x18);
}
int blocked_original() {return blocked;}
KhRef reference(const void* pointer) {return static_cast<KhRef>(static_cast<const unsigned char*>(pointer)-memory.data());}
int module(int value) {return (value==22||value==30)&&loaded;}
unsigned frame() {return pad_frame;}
int thread() {return 1;}
int in_room() {return room;}
int local_seat() {return seat;}
KhActor sdk_player(int seat_number) {return sdk_live&&seat_number==0?42:0;}
KhRef sdk_ref(KhActor handle) {return sdk_live&&handle==42?sdk_raw_ref:handle==9001&&target_handle_live?actor+0x3800:0;}
KhActor sdk_of(KhRef ref) {return target_handle_live&&ref==actor+0x3800?9001:ref==sdk_raw_ref&&sdk_live?42:0;}
int sdk_kind(KhActor handle) {return handle==9001&&target_handle_live?KH_ACTOR_ENEMY:handle==42&&sdk_live?KH_ACTOR_PLAYER:KH_ACTOR_NONE;}
KhResult sdk_position(KhActor handle,float at[3]) {if(handle!=42||!sdk_live)return KH_ERROR;at[0]=at[1]=at[2]=0;return KH_OK;}
unsigned held() {return pad_pressed|pad_held;}
unsigned pressed() {return pad_pressed;}
unsigned released() {return 0;}
void hide(KhPlugin*,unsigned mask) {hidden=mask;}
void log(KhPlugin*,KhLogLevel,const char* message) {logs.emplace_back(message);}
void* routine(const char* name,int*) {
    if(missing_routine==name)return nullptr;
    auto it=routines.find(name);return it==routines.end()?nullptr:it->second;
}
KhRef datum(const char* name) {return std::string(name)=="gSceneCtl"?scene:std::string(name)=="data_0204be04"?cinematic:std::string(name)=="data_0204c240"?cinematic+8:0;}
KhResult replace(const char* name,void* replacement,void** previous) {
    ++replace_calls;if(fail_replace==name)return KH_ERROR;
    auto it=routines.find(name);if(it==routines.end())return KH_ERROR;
    if(previous)*previous=it->second;it->second=replacement;return KH_OK;
}
KhSubscription subscribe(KhPlugin*,const char*,KhEventFunction,void*) {static unsigned count=1;return count++;}
const void* find(KhPlugin*,const char* name,unsigned) {
    ++find_calls;if(missing==name)return nullptr;
    if(std::string(name)==KH_ENGINE)return &e;
    if(std::string(name)==KH_ACTORS)return &a;
    if(std::string(name)==KH_INPUT)return &i;
    if(std::string(name)==KH_NET)return &n;
    if(std::string(name)==KH_EVENTS)return &ev;
    return nullptr;
}
void reset() {
    rxlimit::detach();
    flow_stub::reset();
    // A new mock process/library instance per case, including the plugin's static state.
    player=Player{};host=nullptr;engine=nullptr;actors=nullptr;input=nullptr;net=nullptr;
    paused=false;menu_depth=0;building=false;stopping=false;buffer_ticks=12;speed_fx=5120;style=0;last_input_frame=~0u;
    repertoire_debug=false;defensive_cancels=true;move_fx=4710;turn_fx=5325;
    drive_enabled=drive_meter=true;signature_damage_fx=4096;drive_settings={1000,25,2,900};
    live.clear();allocation_heaps.clear();parts.clear();snapshots.clear();torn.clear();logs.clear();routines.clear();cursor=0x1000;
    loaded=true;blocked=room=seat=0;pad_frame=100;pad_pressed=pad_held=hidden=replace_calls=find_calls=speed_calls=0;
    sdk_live=true;
    input_heading=-1;target_valid=0;
    target_handle_live=true;resolve_wrong_generation=false;resolve_calls=0;
    build_calls=next_calls=walk_calls=translate_calls=step_calls=0;observed_command=observed_phase=-1;step_result=0;
    pair_calls=node_clear_calls=state_calls=native_limit_calls=0;current_heap=0x1111;active_bank_resource=0;
    roll_permission=guard_permission=1;steer_result=0;observed_move_speed=observed_turn_speed=observed_state=-1;during_step={};
    missing.clear();missing_routine.clear();fail_replace.clear();short_interface=0;
    actor=alloc(0x4000);original_table=actor+kTableOffset;scene=alloc(32);cinematic=alloc(16);
    sdk_raw_ref=actor;
    put(actor,0xc,0);put(actor,8,static_cast<unsigned char>(0));put(actor,9,static_cast<unsigned char>(0));
    put(actor,0x12,static_cast<std::uint16_t>(100));put(actor,0x4d6,static_cast<unsigned char>(7));put(scene,8,2);
    put(actor,0x694,static_cast<unsigned char>(1));put(actor,0x458,4096);put(actor,0x664,0x02001000u);put(actor,0x668,0x02002000u);
    put(actor+0x2cb0,0xdb4,actor);
    e={sizeof(KhEngine),&ptr,&reference,&routine,&datum,&module,&frame,&thread,&replace};
    a={};a.size=sizeof(KhActors);a.local_seat=&local_seat;a.player=&sdk_player;a.ref=&sdk_ref;a.position=&sdk_position;a.of=&sdk_of;a.kind=&sdk_kind;
    i={sizeof(KhInput),&held,&pressed,&released,&hide,&frame};
    n={};n.size=sizeof(KhNet);n.in_room=&in_room;
    ev={};ev.size=sizeof(KhEvents);ev.subscribe=&subscribe;
    api={sizeof(KhHost),KH_ABI_MAJOR,KH_ABI_MINOR,reinterpret_cast<KhPlugin*>(1),"0.1.5",&log,&find};
    routines={
      {"NNSi_FndAllocFromDefaultExpHeap",reinterpret_cast<void*>(&alloc)},
      {"NNSi_FndFreeFromDefaultHeap",reinterpret_cast<void*>(&free_mem)},
      {"Ov022_ResetChannelGrid",reinterpret_cast<void*>(&reset_original)},
      {"Ov022_SetAnimSpeed",reinterpret_cast<void*>(&speed_original)},
      {"Ov022_SetAnimState",reinterpret_cast<void*>(&animation_original)},
      {"Heap_GetCurrent",reinterpret_cast<void*>(&get_heap)},
      {"Heap_SetCurrent",reinterpret_cast<void*>(&set_heap)},
      {"Ov022_SteerIdleByAim",reinterpret_cast<void*>(&steer_original)},
      {"Ov022_ActorSetState",reinterpret_cast<void*>(&state_original)},
      {"Ov022_ResetTimersAndMaybeSignal",reinterpret_cast<void*>(&jump_original)},
      {"func_ov022_0209c700",reinterpret_cast<void*>(&pair_original)},
      {"func_ov022_020b09c4",reinterpret_cast<void*>(&node_clear_original)},
      {"Ov022_IsBit0Set_2",reinterpret_cast<void*>(&roll_original)},
      {"Ov022_IsBit0Set",reinterpret_cast<void*>(&guard_original)},
      {"Ov030_ArmPlayerTarget",reinterpret_cast<void*>(&arm_original)},
      {"Ov030_BuildActorHandles",reinterpret_cast<void*>(&limit_resources_original)},
      {"Ov030_UpdateMotionController",reinterpret_cast<void*>(&limit_motion_original)},
      {"Ov022_ScaleMoveDamage",reinterpret_cast<void*>(&limit_damage_original)},
      {"Ov030_ApplyModeChange",reinterpret_cast<void*>(&apply_original)},
      {"Ov030_BindPartRigs",reinterpret_cast<void*>(&limit_bind_parts_original)},
      {"Ov022_AcquireGridSlot",reinterpret_cast<void*>(&limit_acquire_grid_original)},
      {"Ov030_UpdateController",reinterpret_cast<void*>(&limit_original)},
      {"Ov030_ComputeAimAngle",reinterpret_cast<void*>(&limit_original)},
      {"Ov022_IsInputAllowedForActiveSlot",reinterpret_cast<void*>(&blocked_original)},
      {"Ov022_HeadingFromHeldDpad",reinterpret_cast<void*>(&heading_original)},
      {"Ov022_ValidateTargetRef",reinterpret_cast<void*>(&target_valid_original)},
      {"func_ov022_020ad0c0",reinterpret_cast<void*>(&target_position_original)},
      {"Ov022_ResolveTargetPick",reinterpret_cast<void*>(&resolve_target_original)},
      {"Ov022_BuildActionTable",reinterpret_cast<void*>(&build_original)},
      {"Ov022_TearDownActionTable",reinterpret_cast<void*>(&tear_original)},
      {"Ov022_StartGroupWalk",reinterpret_cast<void*>(&walk_original)},
      {"func_ov022_020b09e0",reinterpret_cast<void*>(&next_original)},
      {"func_ov022_020b0a54",reinterpret_cast<void*>(&next_original)},
      {"Ov022_TranslateCommandToAction",reinterpret_cast<void*>(&translate_original)},
      {"Ov022_StepComboState",reinterpret_cast<void*>(&step_original)}};
}
void launch() {CHECK(kh_plugin_query()->start(&api)==KH_OK);}
void build_player() {build(original_table,actor,0,0);CHECK(player.actor==actor);}
void close() {kh_plugin_query()->stop();}

void version_gate() {
    for(const char* version:{"0.1.4","0.1.6","garbage"}) {
        reset();api.port_version=version;
        CHECK(kh_plugin_query()->start(&api)==KH_UNSUPPORTED);CHECK(replace_calls==0);
    }
    reset();api.abi_major=99;CHECK(kh_plugin_query()->start(&api)==KH_UNSUPPORTED);CHECK(find_calls==0);
    reset();api.size=0;CHECK(kh_plugin_query()->start(&api)==KH_UNSUPPORTED);CHECK(find_calls==0);
    reset();launch();CHECK(replace_calls>=9);close();
}
void missing_interfaces() {
    for(const char* name:{KH_ENGINE,KH_ACTORS,KH_INPUT,KH_NET,KH_EVENTS}) {
        reset();missing=name;CHECK(kh_plugin_query()->start(&api)==KH_UNSUPPORTED);CHECK(replace_calls==0);
    }
    for(int v=0;v<5;++v) {
        reset();
        if(v==0)e.size=0;if(v==1)a.size=0;if(v==2)i.size=0;if(v==3)n.size=0;if(v==4)ev.size=0;
        CHECK(kh_plugin_query()->start(&api)==KH_UNSUPPORTED);CHECK(replace_calls==0);
    }
    reset();missing_routine="Ov022_SetAnimSpeed";CHECK(kh_plugin_query()->start(&api)==KH_UNSUPPORTED);CHECK(replace_calls==0);
    reset();i.held=nullptr;CHECK(kh_plugin_query()->start(&api)==KH_UNSUPPORTED);CHECK(replace_calls==0);
    reset();a.position=nullptr;CHECK(kh_plugin_query()->start(&api)==KH_UNSUPPORTED);CHECK(replace_calls==0);
    for(const char* name:{"Ov022_HeadingFromHeldDpad","Ov022_ValidateTargetRef","func_ov022_020ad0c0"}) {
        reset();missing_routine=name;CHECK(kh_plugin_query()->start(&api)==KH_UNSUPPORTED);CHECK(replace_calls==0);
    }
}
void actual_graph_maps_and_lifetime() {
    reset();launch();build_player();CHECK(player.banks.size()==18);CHECK(build_calls==19);
    std::set<int> ids;
    for(const auto& bank:player.banks) {
        CHECK(ptr(bank.table)!=nullptr);
        for(const auto& move:bank.moves) {CHECK(ptr(move.node)!=nullptr);CHECK(ptr(move.record)!=nullptr);ids.insert(move.id);}
    }
    CHECK(ids.size()==94);CHECK(ids.count(10040)==0);CHECK(ids.count(8100)==1);
    CHECK(select_move(original_table,0,false));
    const auto& bank=player.banks[player.active_bank];
    CHECK(get<KhRef>(original_table,0x1a8+0x28)==get<KhRef>(bank.table,0x1a8+0x28));
    CHECK(ptr(get<KhRef>(original_table,0x470))!=nullptr);
    CHECK(parts.count(original_table)==1);
    teardown(original_table);
    CHECK(player.actor==0);CHECK(parts.empty());CHECK(torn.size()==19);CHECK(torn.back()==original_table);
    CHECK(ptr(original_table)!=nullptr);CHECK(live.size()==3);close();
}
void input_priority_expiry_and_context() {
    reset();launch();build_player();pad_pressed=KH_BUTTON_A|KH_BUTTON_X;sample_input(actor);
    CHECK(player.queued==static_cast<int>(rxroutes::Action::Flurry));CHECK(hidden==(KH_BUTTON_X|KH_BUTTON_SELECT));CHECK(queue_live());
    pad_frame=112;CHECK(queue_live());pad_frame=113;CHECK(!queue_live());
    pad_frame=114;pad_pressed=KH_BUTTON_A;sample_input(actor);CHECK(player.queued==1);
    pad_pressed=KH_BUTTON_X;sample_input(actor);CHECK(player.queued==1); // same sampled pad frame
    KhEvent menu{};menu_open(&menu,nullptr);CHECK(!queue_live());CHECK(hidden==0);
    pad_frame=115;sample_input(actor);CHECK(player.queued==0);CHECK(hidden==0);
    menu_close(&menu,nullptr);room=1;pad_frame=116;sample_input(actor);CHECK(player.queued==0);CHECK(hidden==0);
    room=0;put(actor,0x12,static_cast<std::uint16_t>(0));pad_frame=117;sample_input(actor);CHECK(player.queued==0);
    close();CHECK(parts.size()==1);tear_original(original_table);CHECK(live.size()==3);
}
void initial_technique_and_command_guard() {
    reset();launch();build_player();pad_pressed=KH_BUTTON_X;
    translate(actor);CHECK(observed_command==9);CHECK(player.queued==2);
    change_state(actor,14);CHECK(player.queued==2);
    walk(original_table,0,0);CHECK(player.queued==0);CHECK(player.active_bank>=0);CHECK(player.depth==1);
    CHECK(player.active_move==14021);CHECK(player.role==rxroutes::Role::Sweep);
    ++pad_frame;pad_pressed=KH_BUTTON_X;put(actor,0x4d6,static_cast<unsigned char>(3));translate(actor);CHECK(observed_command==3);
    close();tear_original(original_table);
}
void authored_window_and_finisher_boundary() {
    reset();launch();build_player();CHECK(select_move(original_table,0,false));
    KhRef node=get<KhRef>(original_table,0x470),rr=get<KhRef>(node,0x14);
    put(rr,0x10,10*4096);put(rr,0x14,20*4096);
    pad_pressed=KH_BUTTON_A;put(actor,0x7b0,10*4096-1);step(actor);CHECK(observed_phase==0);
    ++pad_frame;put(actor,0x7b0,10*4096);step(actor);CHECK(observed_phase==1);
    put(original_table,0x47c,0);++pad_frame;put(actor,0x7b0,20*4096);step(actor);CHECK(observed_phase==0);
    put(rr,0x14,-4096);++pad_frame;put(actor,0x7b0,20*4096);step(actor);CHECK(observed_phase==1);
    player.active_finisher=true;++pad_frame;put(actor,0x7b0,15*4096);step(actor);CHECK(observed_phase==0);
    CHECK(get<std::uint16_t>(actor,0x1c)==0);CHECK(speed_calls>0);
    close();tear_original(original_table);
}
void stop_restores_ownership_and_missing_overlay() {
    reset();launch();build_player();CHECK(select_move(original_table,1,true));
    const KhRef owning_heap=current_heap;current_heap=0x2222;
    close();CHECK(player.actor==0);CHECK(parts.size()==1);CHECK(torn.size()==18);
    CHECK(current_heap==0x2222);current_heap=owning_heap;
    CHECK(std::memcmp(ptr(original_table),snapshots.at(original_table).data(),kTableSize)==0);
    tear_original(original_table);CHECK(live.size()==3);
    reset();launch();build_player();loaded=false;close();CHECK(player.actor==0);CHECK(hidden==0);CHECK(torn.empty());
}
void native_dual_stays_in_its_own_overlay() {
    // An unowned native story/multiplayer dual form is never adopted by this mod.
    reset();launch();put(actor,0xc,19);build(original_table,actor,19,0);
    CHECK(player.actor==0);CHECK(player.banks.empty());CHECK(build_calls==1);
    teardown(original_table);CHECK(parts.empty());close();
    reset();launch();flow_stub::drive.phase=rxdrive::Phase::Active;put(actor,0xc,19);build(original_table,actor,19,1);
    CHECK(player.kind==19);CHECK(player.banks.size()==1);CHECK(build_calls==2);
    std::set<int> ids;
    for(const auto& bank:player.banks)for(const auto& move:bank.moves)ids.insert(move.id);
    CHECK(ids.size()==10);CHECK(ids.count(1030)==1);CHECK(ids.count(2080)==0);
    CHECK(select_move(original_table,0,false));CHECK(player.active_bank>=0);
    CHECK(select_move(original_table,1,false));CHECK(player.active_bank>=0);
    CHECK(select_move(original_table,1,true));CHECK(player.active_bank>=0);
    teardown(original_table);CHECK(torn.size()==2);CHECK(parts.empty());close();
}
void visible_actor_gate_and_native_passthrough() {
    reset();launch();build_player();CHECK(controllable(actor));
    put(actor,0x694,static_cast<unsigned char>(0));CHECK(!controllable(actor));
    pad_pressed=KH_BUTTON_X;translate(actor);CHECK(observed_command==7);CHECK(player.queued==0);CHECK(hidden==0);
    put(actor,0x694,static_cast<unsigned char>(9));CHECK(!controllable(actor));
    put(actor,0x694,static_cast<unsigned char>(1));CHECK(controllable(actor));
    close();tear_original(original_table);
}
void movement_boost_is_temporary_and_respects_state_change() {
    reset();launch();build_player();put(actor,0x4b8,1000);put(actor,0x4b0,2000);
    put(actor,0x464,1ull<<19);
    CHECK(steer(actor,0)==0);CHECK(observed_move_speed==1000*move_fx/4096);CHECK(observed_turn_speed==2000*turn_fx/4096);
    CHECK(get<int>(actor,0x4b8)==1000);CHECK(get<int>(actor,0x4b0)==2000);
    CHECK(steer(actor,0)==0);CHECK(observed_move_speed==1000*move_fx/4096);CHECK(observed_turn_speed==2000*turn_fx/4096);
    const unsigned before=speed_calls;steer_result=0x02001234;
    CHECK(steer(actor,0)==steer_result);CHECK(speed_calls==before);CHECK(get<int>(actor,0x4b8)==1000);
    steer_result=0;put(actor,0x694,static_cast<unsigned char>(0));steer(actor,0);CHECK(observed_move_speed==1000);
    close();tear_original(original_table);
}
void defense_preserves_contacts_and_requires_native_abilities() {
    reset();launch();build_player();CHECK(select_move(original_table,0,false));
    KhRef rr=get<KhRef>(get<KhRef>(original_table,0x470),0x14);
    CHECK(get<std::int16_t>(rr,0x30)==1);CHECK(get<int>(get<KhRef>(rr,0x34),8)==12*4096);
    put(actor,0x24,4u);put(actor,0x2abc,0);player.defense_queued=true;player.defense_until=pad_frame+12;
    put(actor,0x7b0,13*4096-1);CHECK(post_contact_defense(actor,rr)==0);CHECK(state_calls==0);
    put(actor,0x7b0,13*4096);CHECK(post_contact_defense(actor,rr)==0x02000000+8*4);
    CHECK(observed_state==8);CHECK(pair_calls==1);CHECK(node_clear_calls==1);CHECK(!player.defense_queued);
    player.defense_queued=true;player.defense_until=pad_frame-1;CHECK(post_contact_defense(actor,rr)==0);
    player.defense_until=pad_frame+12;roll_permission=guard_permission=0;CHECK(post_contact_defense(actor,rr)==0);
    guard_permission=1;put(actor,0x24,0u);CHECK(post_contact_defense(actor,rr)==0);
    put(actor,0x24,4u);put(actor,0x2abc,-1);CHECK(post_contact_defense(actor,rr)==0x02000000+11*4);CHECK(observed_state==11);
    player.defense_queued=true;defensive_cancels=false;CHECK(post_contact_defense(actor,rr)==0);
    defensive_cancels=true;put(actor,0,1ull<<4);CHECK(post_contact_defense(actor,rr)==0);
    put(actor,0,0ull);put(rr,2,static_cast<unsigned char>(1));CHECK(post_contact_defense(actor,rr)==0);
    close();tear_original(original_table);
}
void finisher_waits_for_native_transition_return_and_keeps_state_ref() {
    reset();launch();build_player();CHECK(select_move(original_table,0,false));
    put(actor,0x24,4u);player.depth=99;player.techniques=0;input_heading=0;pad_pressed=KH_BUTTON_A|KH_BUTTON_X;
    during_step=[] {
        CHECK(!rxlimit::active());next_a(original_table);
        CHECK(player.pending_finisher!=0);CHECK(!rxlimit::active());
    };
    const int result=step(actor);during_step={};
    CHECK(result==0x02000000+34*4);CHECK(rxlimit::active(actor));CHECK(observed_state==34);
    CHECK(player.pending_finisher==0);CHECK(player.queued==0);
    rxlimit::forget(actor);CHECK(change_state(actor,23)==0);CHECK(change_state(actor,2)==0x02000000+2*4);
    // A native transition to some other state wins over a queued regular finisher.
    player.depth=3;player.techniques=0;player.queued=3;player.queue_until=pad_frame+12;pad_pressed=0;step_result=0x02005678;
    during_step=[] {next_a(original_table);};CHECK(step(actor)==step_result);CHECK(!rxlimit::active());during_step={};
    close();tear_original(original_table);
}
void forced_native_states_cancel_a_signature_waiting_for_transition_cleanup() {
    for (auto action : {rxroutes::Action::Rush,rxroutes::Action::Burst,rxroutes::Action::Flurry})
    for (int forced : {13,16,20,31}) {
        reset();launch();build_player();CHECK(select_move(original_table,0,false));
        player.queued=static_cast<int>(action);player.queue_until=pad_frame+12;pad_pressed=0;
        during_step=[forced] {
            next_a(original_table);CHECK(player.pending_finisher!=0);
            change_state(actor,forced);
            CHECK(player.pending_finisher==0);
        };
        CHECK(step(actor)==0);CHECK(!rxlimit::active(actor));
        during_step={};++pad_frame;CHECK(step(actor)==0);CHECK(!rxlimit::active(actor));
        close();tear_original(original_table);
    }
}
void ordinary_native_melee_states_preserve_a_signature_waiting_for_cleanup() {
    for (int native : {14,15,22}) {
        reset();launch();build_player();CHECK(select_move(original_table,0,false));
        player.queued=static_cast<int>(rxroutes::Action::Rush);player.queue_until=pad_frame+12;pad_pressed=0;
        during_step=[native] {
            next_a(original_table);CHECK(player.pending_finisher!=0);
            change_state(actor,native);CHECK(player.pending_finisher!=0);
        };
        CHECK(step(actor)==0x02000000+34*4);CHECK(rxlimit::active(actor));
        during_step={};close();tear_original(original_table);
    }
}
void default_routes_cover_all_stock_attack_clips() {
    reset();launch();build_player();std::set<int> tags;
    for(int group=0;group<2;++group) {
        player.light_cursor[group]=0;player.active_move=-1;
        for(unsigned position=0;position<8;++position) {
            player.queued=1;player.queue_until=pad_frame+12;
            CHECK(select_move(original_table,group,false));CHECK(!player.active_finisher);
            const KhRef rr=get<KhRef>(get<KhRef>(original_table,0x470),0x14);
            tags.insert(get<unsigned char>(rr,3));
        }
        for(auto direction:{rxroutes::Direction::Neutral,rxroutes::Direction::Toward,rxroutes::Direction::Away,rxroutes::Direction::Left,rxroutes::Direction::Right}) {
            player.active_move=-1;player.direction=direction;
            for(unsigned branch=0;branch<2;++branch) {
                player.queued=2;player.queue_until=pad_frame+12;CHECK(select_move(original_table,group,true));
                const KhRef rr=get<KhRef>(get<KhRef>(original_table,0x470),0x14);tags.insert(get<unsigned char>(rr,3));
            }
        }
    }
    CHECK(tags.size()==16);for(int tag=0;tag<16;++tag)CHECK(tags.count(tag)==1);
    close();tear_original(original_table);
}
void stale_sdk_actor_is_not_controlled_or_dereferenced_on_stop() {
    reset();launch();build_player();CHECK(controllable(actor));CHECK(select_move(original_table,0,false));
    sdk_raw_ref=actor+0x100;CHECK(!controllable(actor));CHECK(!finisher_permitted(actor));
    const auto before=torn.size();sdk_live=false;close();CHECK(torn.size()==before);CHECK(player.actor==0);CHECK(hidden==0);
}
KhRef active_record() {return get<KhRef>(get<KhRef>(original_table,0x470),0x14);}
void offer(rxroutes::Action action,rxroutes::Direction direction=rxroutes::Direction::Neutral) {
    player.queued=static_cast<int>(action);player.queue_until=pad_frame+buffer_ticks;player.direction=direction;
}
void semantic_directions_and_signature_chords_are_stable() {
    reset();launch();build_player();
    const std::pair<rxroutes::Direction,int> ground[]={
      {rxroutes::Direction::Neutral,14021},{rxroutes::Direction::Toward,1},
      {rxroutes::Direction::Away,13080},{rxroutes::Direction::Left,6061},{rxroutes::Direction::Right,5130}};
    for(const auto& role:ground) {
        player.active_move=-1;offer(rxroutes::Action::Technique,role.first);CHECK(select_move(original_table,0,true));CHECK(player.active_move==role.second);
        CHECK(flow_stub::motion.attack_id==role.second);CHECK(flow_stub::rules.attack_serial==flow_stub::motion.attack_serial);
        if(role.first==rxroutes::Direction::Away)CHECK(flow_stub::reaction==rxrules::Reaction::Launcher);
    }
    const std::pair<rxroutes::Direction,int> air[]={
      {rxroutes::Direction::Neutral,13140},{rxroutes::Direction::Toward,4101},
      {rxroutes::Direction::Away,13151},{rxroutes::Direction::Left,11041},{rxroutes::Direction::Right,5}};
    for(const auto& role:air) {offer(rxroutes::Action::Technique,role.first);CHECK(select_move(original_table,1,true));CHECK(player.active_move==role.second);}
    // Relative to a real native target (+X), rather than a menu's screen axis.
    target_valid=1;put(actor+0x3d00,0,5*4096);put(actor+0x3d00,8,0);input_heading=49152;
    ++pad_frame;pad_pressed=KH_BUTTON_X;sample_input(actor);CHECK(player.direction==rxroutes::Direction::Toward);
    put(actor+0x3d00,0,0);put(actor+0x3d00,8,-5*4096);CHECK(player.direction==rxroutes::Direction::Toward);
    CHECK(select_move(original_table,0,true));CHECK(player.role==rxroutes::Role::Advance);
    target_valid=0;put(actor,0x488,0);
    for(const auto& pair:std::array<std::pair<int,rxroutes::Action>,3>{{{0,rxroutes::Action::Rush},{32768,rxroutes::Action::Burst},{-1,rxroutes::Action::Flurry}}}) {
        ++pad_frame;input_heading=pair.first;pad_pressed=KH_BUTTON_X;pad_held=KH_BUTTON_A;
        sample_input(actor);CHECK(player.queued==static_cast<int>(pair.second));
    }
    close();tear_original(original_table);
}
void equipped_stats_survive_every_borrowed_bank() {
    reset();launch();build_player();std::array<unsigned char,0x20> equipped{};
    for(unsigned x=0;x<equipped.size();++x) {equipped[x]=static_cast<unsigned char>(0xf0u-x);put(original_table,8+x,equipped[x]);}
    for(int group=0;group<2;++group)for(auto direction:{rxroutes::Direction::Neutral,rxroutes::Direction::Toward,rxroutes::Direction::Away,rxroutes::Direction::Left,rxroutes::Direction::Right}) {
        offer(rxroutes::Action::Technique,direction);CHECK(select_move(original_table,group,true));
        CHECK(std::memcmp(static_cast<unsigned char*>(ptr(original_table))+8,equipped.data(),equipped.size())==0);
        const auto& bank=player.banks[player.active_bank];
        CHECK(get<KhRef>(original_table,0x1a8+0x28)==get<KhRef>(bank.table,0x1a8+0x28));
    }
    close();tear_original(original_table);
}
void light_after_held_technique_does_not_request_an_accidental_signature() {
    for(int group:{0,1})for(int heading:{0,32768,-1}) {
        reset();launch();build_player();CHECK(select_move(original_table,group,false));
        const KhRef rr=active_record();put(rr,0x10,10*4096);put(rr,0x14,20*4096);
        put(actor,0x7b0,10*4096);input_heading=heading;
        pad_held=KH_BUTTON_X;pad_pressed=KH_BUTTON_A;
        CHECK(step(actor)==0);CHECK(player.committed==static_cast<int>(rxroutes::Action::Light));
        pad_pressed=0;next_a(original_table);
        CHECK(player.active_move==(group?11:1060));
        CHECK(player.pending_finisher==0&&!rxlimit::active(actor));
        close();tear_original(original_table);
    }
}
void light_pressure_has_no_automatic_fifth_hit_or_signature() {
    reset();launch();build_player();constexpr int expected[]={0,1060,5070,1};
    for(unsigned x=0;x<20;++x) {
        offer(rxroutes::Action::Light);if(x==0)walk(original_table,0,0);else next_a(original_table);
        CHECK(player.active_move==expected[x%4]);CHECK(!player.active_finisher);CHECK(player.pending_finisher==0);
        CHECK(player.depth==x+1);
    }
    CHECK(!rxlimit::active());close();tear_original(original_table);
}
void confirmed_hit_cancels_earlier_than_a_whiff_and_respects_role_commitment() {
    reset();launch();build_player();CHECK(select_move(original_table,0,false));const KhRef rr=active_record();
    put(actor,0x7b0,7*4096);CHECK(!cancel_open(actor,rr));
    put(actor,0x7b0,13*4096-1);CHECK(!cancel_open(actor,rr));put(actor,0x7b0,13*4096);CHECK(cancel_open(actor,rr));
    put(actor,0x7b0,6*4096);flow_stub::confirm(actor,player.active_move,pad_frame);
    CHECK(player.hit_animation_frame==6*4096);CHECK(flow_stub::drive_hits==1);CHECK(flow_stub::rules_hits==1);
    put(actor,0x7b0,7*4096-1);CHECK(!cancel_open(actor,rr));put(actor,0x7b0,7*4096);CHECK(cancel_open(actor,rr));
    player.role=rxroutes::Role::Flurry;put(actor,0x7b0,9*4096-1);CHECK(!cancel_open(actor,rr));put(actor,0x7b0,9*4096);CHECK(cancel_open(actor,rr));
    flow_stub::motion.attack_id=-9;put(actor,0x7b0,9*4096);CHECK(!cancel_open(actor,rr));
    put(actor,0x7b0,15*4096);CHECK(cancel_open(actor,rr));
    close();tear_original(original_table);
}
void native_chase_enemy_step_and_magic_share_safe_exit_gates() {
    reset();launch();build_player();offer(rxroutes::Action::Technique,rxroutes::Direction::Away);CHECK(select_move(original_table,0,true));
    put(actor,0x7b0,6*4096);flow_stub::confirm(actor,player.active_move,pad_frame);
    player.jump_queued=true;player.jump_until=pad_frame+12;flow_stub::native_motion_return=0x0200abcd;
    CHECK(step(actor)==0);CHECK(flow_stub::motion_steps==0); // clean-hit delay still closed
    ++pad_frame;put(actor,0x7b0,7*4096);CHECK(step(actor)==static_cast<int>(0x0200abcdu));
    CHECK(pair_calls==1&&node_clear_calls==1);CHECK(!player.jump_queued);CHECK(player.resume_route);
    offer(rxroutes::Action::Light);CHECK(select_move(original_table,1,false));put(actor,0x7b0,6*4096);flow_stub::confirm(actor,player.active_move,pad_frame);
    player.jump_queued=true;player.jump_until=pad_frame+12;++pad_frame;put(actor,0x7b0,7*4096);
    CHECK(step(actor)==static_cast<int>(0x0200abcdu));CHECK(flow_stub::step_requests>0);
    CHECK(select_move(original_table,0,false));flow_stub::native_magic_return=0x0200beef;
    put(actor,0x7b0,0);CHECK(step(actor)==0);CHECK(!flow_stub::magic_contacts_finished);
    put(actor,0x7b0,100*4096);CHECK(step(actor)==static_cast<int>(0x0200beefu));CHECK(flow_stub::magic_contacts_finished);
    close();tear_original(original_table);
}
void route_resume_is_brief_consumable_and_forced_or_menu_state_clears_it() {
    reset();launch();build_player();offer(rxroutes::Action::Light);walk(original_table,0,0);
    offer(rxroutes::Action::Light);next_a(original_table);CHECK(player.active_move==1060);
    combo_exit(actor,true);const unsigned expiry=player.resume_until;CHECK(expiry>pad_frame);
    pad_frame=expiry-1;offer(rxroutes::Action::Light);walk(original_table,0,0);
    CHECK(player.active_move==5070);CHECK(player.depth==3);CHECK(!player.resume_route);
    offer(rxroutes::Action::Light);walk(original_table,0,0);CHECK(player.active_move==0);CHECK(player.depth==1);
    combo_exit(actor,true);pad_frame=player.resume_until+1;offer(rxroutes::Action::Light);walk(original_table,0,0);CHECK(player.active_move==0);
    combo_exit(actor,true);player.jump_queued=player.defense_queued=true;change_state(actor,20);
    CHECK(!player.resume_route&&!player.jump_queued&&!player.defense_queued);CHECK(flow_stub::motion_last_state==20);
    combo_exit(actor,true);offer(rxroutes::Action::Technique);player.jump_queued=player.defense_queued=true;
    KhEvent menu{};menu_open(&menu,nullptr);CHECK(!queue_live()&&!player.resume_route&&!player.jump_queued&&!player.defense_queued);
    menu_open(&menu,nullptr);menu_close(&menu,nullptr);CHECK(paused);menu_close(&menu,nullptr);CHECK(!paused);
    close();tear_original(original_table);
}
void drive_expiry_suppresses_new_attacks_and_frame_replacement_is_gated() {
    reset();launch();build_player();CHECK(select_move(original_table,0,false));
    flow_stub::drive.phase=rxdrive::Phase::RestorationQueued;pad_pressed=KH_BUTTON_A|KH_BUTTON_X|KH_BUTTON_B;
    put(actor,0x18,static_cast<unsigned short>(3));translate(actor);
    CHECK(!queue_live()&&!player.jump_queued);CHECK((hidden&KH_BUTTON_A)!=0);CHECK((get<unsigned short>(actor,0x18)&1)==0);
    CHECK(observed_command==7);const auto selected=flow_stub::motion_starts;
    put(actor,0x7b0,100*4096);step(actor);CHECK(flow_stub::motion_starts==selected);
    KhEvent event{};put(actor,0x6bc,1);frame_event(&event,nullptr);CHECK(flow_stub::drive_frame.safe_to_replace);
    flow_stub::motion.chasing=true;frame_event(&event,nullptr);CHECK(!flow_stub::drive_frame.safe_to_replace);
    flow_stub::motion.chasing=false;event.b=1;frame_event(&event,nullptr);CHECK(flow_stub::drive_frame.paused&&!flow_stub::drive_frame.safe_to_replace);
    CHECK(!flow_stub::hud_visible);
    event.b=0;put(cinematic,8,static_cast<unsigned char>(4));frame_event(&event,nullptr);
    CHECK(!flow_stub::drive_frame.story&&!flow_stub::hud_visible);
    put(cinematic,8,static_cast<unsigned char>(0));frame_event(&event,nullptr);
    CHECK(flow_stub::drive_frame.story&&flow_stub::hud_visible);
    close();tear_original(original_table);
}
void hit_pipeline_and_form_callbacks_are_wired_once() {
    reset();launch();build_player();CHECK(flow_stub::primary_hit!=nullptr);CHECK(flow_stub::secondary_hit==&rxrules::on_confirmed_melee_hit);
    CHECK(flow_stub::packet_policy==&rxrules::before_native_hit);flow_stub::packet_policy(actor,0,0,flow_stub::policy_ctx);CHECK(flow_stub::policy_calls==1);
    CHECK(flow_stub::drive_callbacks.before_replace!=nullptr&&flow_stub::drive_callbacks.after_replace!=nullptr);
    CHECK(select_move(original_table,0,false));CHECK(flow_stub::rules_starts==flow_stub::motion_starts);
    CHECK(flow_stub::anim_links==1);translate(actor);CHECK(flow_stub::rules_refreshes==1);
    flow_stub::motion.chasing=true;CHECK(!flow_stub::drive_callbacks.before_replace(actor,19,nullptr));
    flow_stub::motion.chasing=false;CHECK(flow_stub::drive_callbacks.before_replace(actor,19,nullptr));CHECK(flow_stub::motion_resets>0);
    close();tear_original(original_table);
}
void accepted_intent_survives_buffer_expiry_and_unaccepted_input_does_not_start_a_move() {
    reset();launch();build_player();CHECK(select_move(original_table,0,false));const KhRef rr=active_record();
    put(rr,0x10,10*4096);put(rr,0x14,20*4096);put(actor,0x7b0,10*4096);
    input_heading=32768;pad_pressed=KH_BUTTON_X;CHECK(step(actor)==0);
    CHECK(player.committed==static_cast<int>(rxroutes::Action::Technique));
    CHECK(player.committed_direction==rxroutes::Direction::Away);
    pad_frame+=13;pad_pressed=pad_held=0;input_heading=0;sample_input(actor);
    CHECK(!queue_live());CHECK(requested_action()==static_cast<int>(rxroutes::Action::Technique));
    next_a(original_table);CHECK(player.active_move==13080);CHECK(player.committed==0);
    const auto selected=flow_stub::motion_starts;const int native_next=next_calls;
    offer(rxroutes::Action::Technique,rxroutes::Direction::Right);pad_frame=player.queue_until+1;
    next_a(original_table);CHECK(flow_stub::motion_starts==selected);CHECK(next_calls==native_next+1);
    player.committed=2;change_state(actor,20);CHECK(player.committed==0);
    player.committed=2;KhEvent menu{};menu_open(&menu,nullptr);CHECK(player.committed==0);menu_close(&menu,nullptr);
    close();tear_original(original_table);
}
void forced_control_loss_discards_old_intentions(int gate) {
    // Every entry point must invalidate the accepted command, even when the
    // native actor stops calling the other two callbacks during a script.
    for(int entry=0;entry<3;++entry) {
        reset();launch();build_player();CHECK(select_move(original_table,0,false));
        const KhRef rr=active_record();put(rr,0x10,10*4096);put(rr,0x14,20*4096);
        put(actor,0x7b0,10*4096);pad_pressed=KH_BUTTON_X;CHECK(step(actor)==0);
        CHECK(player.committed==static_cast<int>(rxroutes::Action::Technique));
        player.queued_target.handle=player.committed_target.handle=9001;
        player.jump_queued=player.defense_queued=player.resume_route=player.blend_pending=true;
        player.pending_finisher=3;
        pad_pressed=pad_held=0;++pad_frame;
        if(gate==0)put(cinematic,0,static_cast<unsigned char>(1));
        else if(gate==1)blocked=1;
        else put(actor,0x694,static_cast<unsigned char>(0));
        if(entry==0) {KhEvent event{};frame_event(&event,nullptr);}
        else if(entry==1)translate(actor);
        else CHECK(step(actor)==0);
        CHECK(player.queued==0&&player.committed==0);
        CHECK(player.queued_target.handle==0&&player.committed_target.handle==0);
        CHECK(!player.jump_queued&&!player.defense_queued&&!player.resume_route&&!player.blend_pending);
        CHECK(player.pending_finisher==0);CHECK(hidden==0);
        CHECK(get<int>(original_table,0x47c)==0&&get<std::uint16_t>(actor,0x1c)==0);
        put(cinematic,0,static_cast<unsigned char>(0));blocked=0;
        put(actor,0x694,static_cast<unsigned char>(1));pad_frame+=buffer_ticks+20;
        sample_input(actor);CHECK(requested_action()==0);
        const auto moves=flow_stub::motion_starts;const int native_next=next_calls;
        next_a(original_table);CHECK(flow_stub::motion_starts==moves&&next_calls==native_next+1);
        close();tear_original(original_table);
    }
}
void ordinary_pause_preserves_an_accepted_command_but_menu_open_cancels_it() {
    reset();launch();build_player();CHECK(select_move(original_table,0,false));
    const KhRef rr=active_record();put(rr,0x10,10*4096);put(rr,0x14,20*4096);
    put(actor,0x7b0,10*4096);pad_pressed=KH_BUTTON_X;CHECK(step(actor)==0);
    const int accepted=player.committed;CHECK(accepted==static_cast<int>(rxroutes::Action::Technique));
    KhEvent event{};event.b=1;blocked=1;pad_pressed=pad_held=0;++pad_frame;frame_event(&event,nullptr);
    translate(actor);CHECK(step(actor)==0);sample_input(actor);CHECK(player.committed==accepted);
    pad_frame+=30;frame_event(&event,nullptr);CHECK(player.committed==accepted);
    event.b=0;blocked=0;frame_event(&event,nullptr);CHECK(requested_action()==accepted);
    menu_open(&event,nullptr);CHECK(requested_action()==0);
    CHECK(get<int>(original_table,0x47c)==0&&get<std::uint16_t>(actor,0x1c)==0);
    menu_close(&event,nullptr);
    close();tear_original(original_table);
}
void signature_input_lock_keeps_its_deliberate_defense_request() {
    reset();launch();build_player();CHECK(select_move(original_table,0,false));
    CHECK(rxlimit::begin(actor,rxlimit::Finisher::Rush));CHECK(rxlimit::active(actor));
    blocked=1;pad_pressed=KH_BUTTON_Y;++pad_frame;KhEvent event{};frame_event(&event,nullptr);
    CHECK(player.defense_queued);CHECK(rxlimit::active(actor));
    close();tear_original(original_table);
}
void prepare_buffered_defense(bool signature) {
    reset();launch();build_player();CHECK(select_move(original_table,0,false));
    put(actor,0x24,4u);put(actor,0x7b0,6*4096);
    flow_stub::confirm(actor,player.active_move,pad_frame);
    if(signature) CHECK(rxlimit::begin(actor,rxlimit::Finisher::Rush));
}
int consume_buffered_defense(bool signature) {
    put(actor,0x7b0,7*4096);pad_pressed=pad_held=0;++pad_frame;
    return signature?static_cast<int>(signature_exit(actor,rxlimit::Finisher::Rush,12*4096)):step(actor);
}
void defense_preserves_a_newer_attack_and_its_intended_target() {
    for(bool signature:{false,true})for(bool guard:{false,true}) {
        prepare_buffered_defense(signature);
        if(guard) roll_permission=0;
        pad_pressed=KH_BUTTON_Y;sample_input(actor);CHECK(player.defense_queued);
        ++pad_frame;pad_pressed=KH_BUTTON_X;input_heading=32768;sample_input(actor);
        CHECK(player.queued==2);player.queued_target.handle=9001;
        CHECK(consume_buffered_defense(signature)!=0);CHECK(observed_state==(guard?11:8));
        CHECK(!player.defense_queued&&player.queued==2&&player.queued_target.handle==9001);
        CHECK(player.direction==rxroutes::Direction::Away&&player.resume_route);
        const auto exits=state_calls;const auto attacks=flow_stub::motion_starts;
        translate(actor);CHECK(state_calls==exits&&flow_stub::motion_starts==attacks);
        walk(original_table,0,0);CHECK(player.active_move==13080&&player.queued==0);
        CHECK(flow_stub::motion_starts==attacks+1);
        close();tear_original(original_table);
    }
}
void defense_retires_same_or_older_attack_and_jump_intentions() {
    for(bool signature:{false,true})for(bool same_frame:{false,true})for(bool guard:{false,true}) {
        prepare_buffered_defense(signature);
        if(guard) roll_permission=0;
        pad_pressed=KH_BUTTON_A|KH_BUTTON_B|(same_frame?KH_BUTTON_Y:0u);sample_input(actor);
        if(!same_frame) {++pad_frame;pad_pressed=KH_BUTTON_Y;sample_input(actor);}
        player.committed=2;player.committed_target.handle=9001;
        CHECK(consume_buffered_defense(signature)!=0);
        CHECK(!player.jump_queued&&!player.defense_queued&&!player.queued&&!player.committed);
        CHECK(!player.queued_target.handle&&!player.committed_target.handle);
        close();tear_original(original_table);
    }
}
void defense_preserves_a_newer_jump_tap_without_repeating_defense() {
    for(bool signature:{false,true})for(bool guard:{false,true}) {
        prepare_buffered_defense(signature);
        if(guard) roll_permission=0;
        pad_pressed=KH_BUTTON_Y;sample_input(actor);
        ++pad_frame;pad_pressed=KH_BUTTON_B;sample_input(actor);
        const unsigned deadline=player.jump_until,intent=player.jump_intent;
        CHECK(consume_buffered_defense(signature)!=0);
        CHECK(player.jump_queued&&!player.defense_queued);
        CHECK(player.jump_until==deadline&&player.jump_intent==intent);
        const auto exits=state_calls;sample_input(actor);CHECK(state_calls==exits&&!player.defense_queued);
        close();tear_original(original_table);
    }
}
void unavailable_defense_keeps_buffered_followups() {
    for(bool signature:{false,true}) {
        prepare_buffered_defense(signature);roll_permission=guard_permission=0;
        const auto before=state_calls;
        pad_pressed=KH_BUTTON_Y|KH_BUTTON_A|KH_BUTTON_B;sample_input(actor);
        put(actor,0x7b0,7*4096);pad_pressed=pad_held=0;++pad_frame;
        // Inspect defense refusal before the ordinary Jump fallback gets its
        // own chance to consume this independent input.
        CHECK((signature?static_cast<int>(signature_exit(actor,rxlimit::Finisher::Rush,12*4096)):
            post_contact_defense(actor,active_record()))==0);
        CHECK(player.defense_queued&&player.jump_queued&&player.queued==1);
        CHECK(state_calls==before);
        close();tear_original(original_table);
    }
}
void accepted_defense_retires_previously_accepted_motion() {
    for(bool chase:{false,true})for(bool guard:{false,true}) {
        prepare_buffered_defense(false);
        if(guard) roll_permission=0;
        flow_stub::motion.chase_queued=chase;flow_stub::motion.enemy_step_queued=!chase;
        pad_pressed=KH_BUTTON_Y|KH_BUTTON_B;sample_input(actor);
        CHECK(consume_buffered_defense(false)!=0);
        CHECK(!flow_stub::motion.chase_queued&&!flow_stub::motion.enemy_step_queued);
        CHECK(flow_stub::motion_last_state==(guard?11:8)&&!player.jump_queued);
        const auto requests=flow_stub::chase_requests+flow_stub::step_requests;
        const auto movement=flow_stub::motion_steps;
        ++pad_frame;flow_stub::native_motion_return=0x0200abcd;CHECK(step(actor)==0);
        CHECK(flow_stub::motion_steps==movement&&flow_stub::chase_requests+flow_stub::step_requests==requests);
        close();tear_original(original_table);
    }
}
void refused_native_defense_preserves_pending_inputs_and_motion() {
    prepare_buffered_defense(false);
    pad_pressed=KH_BUTTON_Y|KH_BUTTON_A|KH_BUTTON_B;sample_input(actor);
    flow_stub::motion.chase_queued=true;
    const auto previous=set_state;set_state=[](KhRef,int) {return 0;};
    put(actor,0x7b0,7*4096);
    const auto changes=flow_stub::motion_state_changes;
    CHECK(post_contact_defense(actor,active_record())==0);set_state=previous;
    CHECK(player.defense_queued&&player.jump_queued&&player.queued==1);
    CHECK(flow_stub::motion.chase_queued&&flow_stub::motion_state_changes==changes);
    CHECK(pair_calls==0&&node_clear_calls==0);
    close();tear_original(original_table);
}
void suppressed_signature_discards_old_intentions() {
    for(auto kind : {rxlimit::Finisher::Rush,rxlimit::Finisher::LightBurst,rxlimit::Finisher::Flurry})
    for(int entry=0;entry<4;++entry) {
        reset();launch();build_player();CHECK(select_move(original_table,0,false));
        CHECK(rxlimit::begin(actor,kind));CHECK(rxlimit::active(actor));
        player.queued=player.committed=2;
        player.queued_target.handle=player.committed_target.handle=9001;
        player.jump_queued=player.defense_queued=player.resume_route=player.blend_pending=true;
        player.pending_finisher=3;
        put(original_table,0x47c,1);put(actor,0x1c,static_cast<std::uint16_t>(1));
        blocked=1;put(actor,0x694,static_cast<unsigned char>(9));++pad_frame;
        if(entry==0) {KhEvent event{};frame_event(&event,nullptr);}
        else if(entry==1)sample_input(actor);
        else if(entry==2)translate(actor);
        else step(actor);
        CHECK(player.queued==0&&player.committed==0);
        CHECK(!player.queued_target.handle&&!player.committed_target.handle);
        CHECK(!player.jump_queued&&!player.defense_queued&&!player.resume_route&&!player.blend_pending);
        CHECK(player.pending_finisher==0&&hidden==0);
        CHECK(get<int>(original_table,0x47c)==0&&get<std::uint16_t>(actor,0x1c)==0);
        close();tear_original(original_table);
    }
}
void suppressed_signature_refuses_taps_then_accepts_fresh_visible_input() {
    for(auto kind : {rxlimit::Finisher::Rush,rxlimit::Finisher::LightBurst,rxlimit::Finisher::Flurry}) {
        reset();launch();build_player();CHECK(select_move(original_table,0,false));
        CHECK(rxlimit::begin(actor,kind));blocked=1;
        put(actor,0x694,static_cast<unsigned char>(9));++pad_frame;
        pad_pressed=KH_BUTTON_A|KH_BUTTON_X|KH_BUTTON_B|KH_BUTTON_Y|KH_BUTTON_SELECT;
        pad_held=KH_BUTTON_A|KH_BUTTON_X;KhEvent event{};frame_event(&event,nullptr);
        CHECK(requested_action()==0&&!player.jump_queued&&!player.defense_queued&&hidden==0);
        CHECK(flow_stub::drive_requests==0);
        put(actor,0x694,static_cast<unsigned char>(1));++pad_frame;pad_pressed=pad_held=0;
        frame_event(&event,nullptr);CHECK(requested_action()==0&&!player.jump_queued&&!player.defense_queued);
        ++pad_frame;pad_pressed=KH_BUTTON_Y;frame_event(&event,nullptr);
        CHECK(player.defense_queued&&rxlimit::active(actor));
        close();tear_original(original_table);
    }
}
void frame_only_attack_tap_reaches_the_next_native_window(unsigned buttons) {
    reset();launch();build_player();CHECK(select_move(original_table,0,false));
    const KhRef rr=active_record();put(rr,0x10,10*4096);put(rr,0x14,20*4096);
    put(actor,0x7b0,9*4096);pad_pressed=buttons;KhEvent event{};
    // No actor translate/step callback runs on this input frame.
    frame_event(&event,nullptr);
    const int expected=(buttons&KH_BUTTON_X)?((buttons&KH_BUTTON_A)?5:2):1;
    CHECK(requested_action()==expected);CHECK(player.committed==0);
    const auto moves=flow_stub::motion_starts;
    ++pad_frame;pad_pressed=pad_held=0;put(actor,0x7b0,10*4096);
    during_step=[] {next_a(original_table);};const int result=step(actor);during_step={};
    CHECK(flow_stub::motion_starts==moves+1);CHECK(requested_action()==0);
    if(expected==5) {CHECK(result!=0);CHECK(rxlimit::active(actor));}
    else {
        CHECK(result==0);CHECK(!rxlimit::active(actor));
        CHECK(player.active_move==(expected==2?14021:1060));
        ++pad_frame;CHECK(step(actor)==0);CHECK(flow_stub::motion_starts==moves+1);
    }
    frame_event(&event,nullptr);CHECK(requested_action()==0);
    close();tear_original(original_table);
}
void frame_only_mobility_tap_reaches_one_eligible_exit(unsigned button) {
    reset();launch();build_player();CHECK(select_move(original_table,0,false));
    put(actor,0x24,4u);put(actor,0x2abc,0);put(actor,0x7b0,6*4096);
    flow_stub::confirm(actor,player.active_move,pad_frame);
    pad_pressed=button;KhEvent event{};frame_event(&event,nullptr);
    CHECK(button==KH_BUTTON_B?player.jump_queued:player.defense_queued);
    CHECK(state_calls==0);++pad_frame;pad_pressed=pad_held=0;put(actor,0x7b0,7*4096);
    CHECK(step(actor)==0x02000000+(button==KH_BUTTON_B?1:8)*4);
    CHECK(state_calls==1);CHECK(!player.jump_queued&&!player.defense_queued);
    ++pad_frame;frame_event(&event,nullptr);CHECK(step(actor)==0);CHECK(state_calls==1);
    close();tear_original(original_table);
}
void frame_sampling_deduplicates_and_keeps_expiry_and_control_gates() {
    reset();launch();build_player();input_heading=32768;pad_pressed=KH_BUTTON_X;
    KhEvent event{};frame_event(&event,nullptr);
    CHECK(player.queued==2&&player.direction==rxroutes::Direction::Away);
    const unsigned until=player.queue_until;
    input_heading=0;pad_pressed=KH_BUTTON_A;frame_event(&event,nullptr);translate(actor);
    CHECK(player.queued==2&&player.direction==rxroutes::Direction::Away&&player.queue_until==until);
    pad_frame=until+1;pad_pressed=pad_held=0;frame_event(&event,nullptr);
    CHECK(player.queued==0&&requested_action()==0);
    ++pad_frame;event.b=1;pad_pressed=KH_BUTTON_X;frame_event(&event,nullptr);CHECK(player.queued==0);
    ++pad_frame;event.b=0;put(cinematic,0,static_cast<unsigned char>(1));frame_event(&event,nullptr);
    CHECK(player.queued==0&&hidden==0);
    ++pad_frame;pad_pressed=0;put(cinematic,0,static_cast<unsigned char>(0));frame_event(&event,nullptr);
    CHECK(requested_action()==0);
    close();tear_original(original_table);
}
void accepted_motion_queue_can_finish_after_raw_jump_expiry() {
    for(bool chase:{false,true}) {
        reset();launch();build_player();CHECK(select_move(original_table,chase?0:1,false));
        put(actor,0x7b0,6*4096);flow_stub::confirm(actor,player.active_move,pad_frame);
        flow_stub::motion.launcher=chase;flow_stub::motion.launcher_confirmed=chase;
        flow_stub::motion.chase_queued=chase;flow_stub::motion.enemy_step_queued=!chase;
        flow_stub::request_allowed=false;flow_stub::native_motion_return=0x0200abcd;
        player.jump_queued=true;player.jump_until=pad_frame-1;
        ++pad_frame;CHECK(step(actor)==0);CHECK(flow_stub::motion_steps==0); // native exit is not open yet
        ++pad_frame;put(actor,0x7b0,7*4096);
        CHECK(step(actor)==0x0200abcd);CHECK(flow_stub::motion_steps==1);
        CHECK(flow_stub::chase_requests==0&&flow_stub::step_requests==0);
        CHECK(!player.jump_queued&&player.resume_route);
        CHECK(!flow_stub::motion.chase_queued&&!flow_stub::motion.enemy_step_queued);
        flow_stub::native_motion_return=0;++pad_frame;
        CHECK(step(actor)==0);CHECK(flow_stub::motion_steps==1);
        close();tear_original(original_table);
    }
}
void replaced_motion_transition_keeps_new_action_intentions() {
    reset();launch();build_player();CHECK(select_move(original_table,1,false));
    put(actor,0x7b0,6*4096);flow_stub::confirm(actor,player.active_move,pad_frame);
    flow_stub::motion.enemy_step_queued=true;flow_stub::native_motion_return=0x0200abcd;
    const auto resets=flow_stub::anim_resets;
    ++pad_frame;put(actor,0x7b0,7*4096); // advance past the hit's animation frame
    flow_stub::during_motion_step=[] {
        ++flow_stub::motion.attack_serial;
        player.committed=2;player.jump_queued=player.defense_queued=true;
        player.resume_route=false;
    };
    CHECK(step(actor)==0x0200abcd);
    CHECK(player.committed==2&&player.jump_queued&&player.defense_queued&&!player.resume_route);
    CHECK(flow_stub::anim_resets==resets);
    close();tear_original(original_table);
}
void retired_motion_queue_cannot_reaccept_an_old_long_jump_buffer() {
    for(bool chase:{false,true}) {
        reset();launch();build_player();CHECK(select_move(original_table,chase?0:1,false));
        put(actor,0x7b0,6*4096);flow_stub::confirm(actor,player.active_move,pad_frame);
        flow_stub::motion.launcher=chase;flow_stub::motion.launcher_confirmed=chase;
        flow_stub::motion.chase_queued=chase;flow_stub::motion.enemy_step_queued=!chase;
        // Motion independently rejects an expired/invalid accepted queue while
        // the original longer raw Jump buffer and hit are still fresh.
        flow_stub::motion_queue_rejected=true;
        player.jump_queued=true;player.jump_until=pad_frame+24;
        ++pad_frame;put(actor,0x7b0,7*4096);CHECK(step(actor)==0);
        CHECK(!player.jump_queued);CHECK(flow_stub::motion_steps==1);CHECK(state_calls==0);
        ++pad_frame;CHECK(step(actor)==0);
        CHECK(flow_stub::motion_steps==1&&flow_stub::chase_requests==0&&flow_stub::step_requests==0);
        // A fresh deliberate press remains eligible; rejection retires one tap,
        // rather than disabling subsequent pursuit or Enemy Step input.
        ++pad_frame;pad_pressed=KH_BUTTON_B;flow_stub::motion_queue_rejected=false;
        flow_stub::native_motion_return=0x0200abcd;CHECK(step(actor)==0x0200abcd);
        CHECK(flow_stub::chase_requests+flow_stub::step_requests==1);
        close();tear_original(original_table);
    }
}
void retired_motion_queue_preserves_a_newer_frame_only_jump_tap() {
    reset();launch();build_player();CHECK(select_move(original_table,1,false));
    put(actor,0x7b0,6*4096);flow_stub::confirm(actor,player.active_move,pad_frame);
    flow_stub::motion.enemy_step_queued=true;flow_stub::motion_queue_rejected=true;
    player.jump_queued=true;player.jump_until=pad_frame+24;
    // A second tap arrives through the global callback before the old accepted
    // motion is retired on the next actor update.
    ++pad_frame;pad_pressed=KH_BUTTON_B;KhEvent event{};frame_event(&event,nullptr);
    ++pad_frame;pad_pressed=0;put(actor,0x7b0,7*4096);CHECK(step(actor)==0);
    CHECK(player.jump_queued);CHECK(flow_stub::motion_steps==1&&flow_stub::step_requests==0);
    ++pad_frame;flow_stub::motion_queue_rejected=false;flow_stub::native_motion_return=0x0200abcd;
    CHECK(step(actor)==0x0200abcd);CHECK(flow_stub::step_requests==1&&!player.jump_queued);
    close();tear_original(original_table);
}
void newly_accepted_motion_rejection_retires_its_original_jump_tap() {
    for(bool chase:{false,true}) {
        reset();launch();build_player();CHECK(select_move(original_table,chase?0:1,false));
        put(actor,0x7b0,6*4096);flow_stub::confirm(actor,player.active_move,pad_frame);
        flow_stub::motion.launcher=chase;flow_stub::motion.launcher_confirmed=chase;
        CHECK(!flow_stub::motion.chase_queued&&!flow_stub::motion.enemy_step_queued);
        flow_stub::motion_queue_rejected=true;
        ++pad_frame;pad_pressed=KH_BUTTON_B;put(actor,0x7b0,7*4096);CHECK(step(actor)==0);
        CHECK(flow_stub::chase_requests+flow_stub::step_requests==1&&flow_stub::motion_steps==1);
        CHECK(!flow_stub::motion.chase_queued&&!flow_stub::motion.enemy_step_queued);
        CHECK(!player.jump_queued);
        ++pad_frame;pad_pressed=0;CHECK(step(actor)==0);
        CHECK(flow_stub::chase_requests+flow_stub::step_requests==1&&flow_stub::motion_steps==1);
        close();tear_original(original_table);
    }
}
void newly_accepted_chase_waits_without_retiring_or_reaccepting_its_tap() {
    reset();launch();build_player();offer(rxroutes::Action::Technique,rxroutes::Direction::Away);
    CHECK(select_move(original_table,0,true));put(actor,0x7b0,6*4096);
    flow_stub::confirm(actor,player.active_move,pad_frame);
    ++pad_frame;pad_pressed=KH_BUTTON_B;put(actor,0x7b0,7*4096);CHECK(step(actor)==0);
    CHECK(flow_stub::motion.chase_queued&&player.jump_queued);
    CHECK(flow_stub::chase_requests==1&&flow_stub::motion_steps==1);
    ++pad_frame;pad_pressed=0;CHECK(step(actor)==0);
    CHECK(flow_stub::motion.chase_queued&&player.jump_queued);
    CHECK(flow_stub::chase_requests==1&&flow_stub::motion_steps==2);
    ++pad_frame;flow_stub::motion.launcher_confirmed=true;flow_stub::native_motion_return=0x0200abcd;
    CHECK(step(actor)==0x0200abcd);CHECK(!player.jump_queued&&!flow_stub::motion.chase_queued);
    CHECK(flow_stub::chase_requests==1&&flow_stub::motion_steps==3);
    close();tear_original(original_table);
}
void a_second_defensive_movement_cannot_bank_the_old_combo_route() {
    reset();launch();build_player();offer(rxroutes::Action::Light);walk(original_table,0,0);
    offer(rxroutes::Action::Light);next_a(original_table);CHECK(player.depth==2);
    change_state(actor,8);combo_exit(actor,true);CHECK(player.resume_route);
    // The real native state setter does not write its state number into2770;
    // that byte is the independent partner/charge kind (StartCharge.c).
    CHECK(get<signed char>(actor,0x2770)==0);change_state(actor,11);CHECK(!player.resume_route);
    offer(rxroutes::Action::Light);walk(original_table,0,0);CHECK(player.depth==1&&player.active_move==0);
    close();tear_original(original_table);
}
void committed_target_is_resolved_fresh_and_stale_generations_are_not_restored() {
    auto queue_target = [] {
        put(actor,0x4f0,static_cast<unsigned char>(1));put(actor,0x4f1,static_cast<unsigned char>(7));
        put(actor,0x4f2,static_cast<unsigned char>(2));put(actor,0x4fc,actor+0x3800);
        put(actor,0x500,0xdeadbeefu);target_valid=1;pad_pressed=KH_BUTTON_X;input_heading=-1;
        sample_input(actor);CHECK(player.queued_target.handle==9001);
    };
    reset();launch();build_player();CHECK(select_move(original_table,0,false));const KhRef rr=active_record();
    put(rr,0x10,10*4096);put(rr,0x14,20*4096);put(actor,0x7b0,10*4096);
    queue_target();step(actor);CHECK(player.committed_target.handle==9001);
    pad_frame+=13;pad_pressed=0;sample_input(actor);
    put(actor,0x4fc,actor+0x3900);put(actor,0x500,0xf00dbeefu); // native auto-target changed meanwhile
    next_a(original_table);CHECK(resolve_calls==1);CHECK(get<KhRef>(actor,0x4fc)==actor+0x3800);
    CHECK(get<KhRef>(actor,0x500)==actor+0x3a00);CHECK(player.committed_target.handle==0);
    ++pad_frame;queue_target();target_handle_live=false;
    put(actor,0x4f0,static_cast<unsigned char>(0));CHECK(select_move(original_table,0,true));CHECK(resolve_calls==1);
    target_handle_live=true;++pad_frame;queue_target();resolve_wrong_generation=true;
    CHECK(select_move(original_table,0,true));CHECK(resolve_calls==2);
    for(unsigned x=0;x<0x18;++x)CHECK(get<unsigned char>(actor,0x4f0+x)==0);
    close();tear_original(original_table);
}
void accepted_signature_survives_an_ending_and_raw_buffer_expiry() {
    reset();launch();build_player();offer(rxroutes::Action::Technique,rxroutes::Direction::Away);
    CHECK(select_move(original_table,1,true));CHECK(player.active_finisher);
    const KhRef rr=active_record();put(rr,0x10,10*4096);put(rr,0x14,30*4096);
    put(actor,0x7b0,10*4096);pad_pressed=pad_held=KH_BUTTON_A|KH_BUTTON_X;
    step(actor);CHECK(player.committed==static_cast<int>(rxroutes::Action::Flurry));
    pad_frame+=13;pad_pressed=pad_held=0;put(actor,0x7b0,15*4096);
    step(actor);CHECK(!queue_live());CHECK(observed_phase==1);CHECK(player.committed==5);
    during_step=[] { next_a(original_table); };
    CHECK(step(actor)==0x02000000+34*4);during_step={};CHECK(rxlimit::active(actor));
    close();tear_original(original_table);
}
void retired_buffered_target_clears_its_native_cache_without_stopping_the_chain() {
    for(bool committed:{false,true}) {
        reset();launch();build_player();CHECK(select_move(original_table,0,false));
        put(actor,0x4f0,static_cast<unsigned char>(1));put(actor,0x4f1,static_cast<unsigned char>(7));
        put(actor,0x4f2,static_cast<unsigned char>(2));put(actor,0x4fc,actor+0x3800);
        put(actor,0x500,0xdeadbeefu);pad_pressed=KH_BUTTON_A;sample_input(actor);
        CHECK(player.queued_target.handle==9001);
        if(committed) {
            const KhRef rr=active_record();put(rr,0x10,10*4096);put(rr,0x14,20*4096);
            put(actor,0x7b0,10*4096);CHECK(step(actor)==0);
            CHECK(player.committed_target.handle==9001);
        }
        // The SDK generation retires, but the native target and collision-part
        // cache can still contain its old address until the next resolver pass.
        target_handle_live=false;const auto attacks=flow_stub::motion_starts;
        CHECK(select_move(original_table,0,false));CHECK(player.active_move==1060);
        CHECK(flow_stub::motion_starts==attacks+1&&resolve_calls==0);
        for(unsigned x=0;x<0x18;++x)CHECK(get<unsigned char>(actor,0x4f0+x)==0);
        close();tear_original(original_table);
    }
    reset();launch();build_player();CHECK(select_move(original_table,0,false));
    put(actor,0x4f0,static_cast<unsigned char>(1));put(actor,0x4fc,actor+0x3800);
    put(actor,0x500,actor+0x3a00);offer(rxroutes::Action::Light); // no captured target
    CHECK(select_move(original_table,0,false));CHECK(resolve_calls==0);
    CHECK(get<unsigned char>(actor,0x4f0)==1&&get<KhRef>(actor,0x500)==actor+0x3a00);
    close();tear_original(original_table);
}
void buffered_defense_cannot_become_a_native_heavy_branch() {
    reset();launch();build_player();CHECK(select_move(original_table,0,false));
    put(actor,0x24,4u);put(actor,0x7b0,1*4096);
    put(actor,0x18,static_cast<std::uint16_t>(0x800));pad_pressed=KH_BUTTON_Y;
    during_step=[] { CHECK((get<std::uint16_t>(actor,0x18)&0x800)==0); };
    CHECK(step(actor)==0);during_step={};CHECK(player.defense_queued);CHECK(state_calls==0);
    close();tear_original(original_table);
}
void grounded_jump_exit_uses_native_launch_and_preserves_momentum() {
    reset();launch();build_player();CHECK(select_move(original_table,0,false));
    put(actor,0x24,4u);put(actor,0x7b0,6*4096);flow_stub::confirm(actor,player.active_move,pad_frame);
    player.jump_queued=true;player.jump_until=pad_frame+12;player.committed=1;
    ++pad_frame;put(actor,0x7b0,7*4096);put(actor,0x698,300);put(actor,0x6a0,-700);
    CHECK(step(actor)==0x02000000+1*4);CHECK(observed_state==1);
    CHECK(get<int>(actor,0x69c)==0x630);CHECK(get<int>(actor,0x698)==300&&get<int>(actor,0x6a0)==-700);
    CHECK(player.resume_route&&!player.jump_queued&&player.committed==0);
    CHECK(pair_calls==1&&node_clear_calls==1);
    close();tear_original(original_table);
}
void resistant_launcher_does_not_swallow_the_ordinary_jump() {
    reset();launch();build_player();offer(rxroutes::Action::Technique,rxroutes::Direction::Away);
    CHECK(select_move(original_table,0,true));put(actor,0x24,4u);put(actor,0x7b0,9*4096);
    flow_stub::confirm(actor,player.active_move,pad_frame);player.jump_queued=true;player.jump_until=pad_frame+12;
    put(actor,0x7b0,10*4096);++pad_frame;CHECK(step(actor)==0); // short rise observation grace
    pad_frame+=4;put(actor,0x7b0,13*4096);
    CHECK(step(actor)==0x02000000+1*4);CHECK(!player.jump_queued);CHECK(observed_state==1);
    close();tear_original(original_table);
}
void pending_form_keeps_melee_until_spells_finish_then_uses_a_real_exit() {
    reset();launch();build_player();CHECK(select_move(original_table,0,false));
    flow_stub::drive.phase=rxdrive::Phase::ActivationQueued;flow_stub::resources_settled=false;
    pad_pressed=KH_BUTTON_A;sample_input(actor);CHECK(queue_live()); // no forced idle for an old projectile
    ++pad_frame;pad_pressed=KH_BUTTON_A;put(actor,0x4d6,static_cast<unsigned char>(8));
    put(actor,0x18,static_cast<std::uint16_t>(1));translate(actor);
    CHECK(observed_command==7);CHECK(!queue_live()); // no new spell to extend the wait
    put(actor,0x24,4u);put(actor,0x7b0,13*4096);pad_pressed=0;++pad_frame;
    CHECK(step(actor)==0);CHECK(state_calls==0);
    flow_stub::resources_settled=true;++pad_frame;
    CHECK(step(actor)==0x02000000);CHECK(observed_state==0);
    CHECK(!player.resume_route&&!player.jump_queued&&pair_calls==1&&node_clear_calls==1);
    close();tear_original(original_table);
}
void pending_form_can_leave_a_signature_at_its_authored_exit() {
    reset();launch();build_player();CHECK(select_move(original_table,0,false));put(actor,0x24,4u);
    CHECK(rxlimit::begin(actor,rxlimit::Finisher::Rush));CHECK(rxlimit::active(actor));
    flow_stub::drive.phase=rxdrive::Phase::ActivationQueued;flow_stub::resources_settled=true;
    CHECK(signature_exit(actor,rxlimit::Finisher::Rush,12*4096)==0x02000000);
    CHECK(!rxlimit::active(actor));CHECK(!player.resume_route);CHECK(node_clear_calls==1);
    close();tear_original(original_table);
}
}
namespace test {
void practice_gauge_is_scoped_and_once_per_scene() {
    reset();launch();build_player();
    KhEvent event{};
    frame_event(&event,nullptr); CHECK(flow_stub::practice_primes==0);
    flow_stub::practice_active=true;
    event.b=1;frame_event(&event,nullptr);CHECK(flow_stub::practice_primes==0);
    event.b=0;drive_enabled=false;frame_event(&event,nullptr);CHECK(flow_stub::practice_primes==0);
    drive_enabled=true;frame_event(&event,nullptr);CHECK(flow_stub::practice_primes==1);
    frame_event(&event,nullptr);CHECK(flow_stub::practice_primes==1);
    scene_event(&event,nullptr);frame_event(&event,nullptr);CHECK(flow_stub::practice_primes==2);
    close();tear_original(original_table);
}
void initial_signature_keeps_its_accepted_intent_until_the_native_window() {
    reset();launch();build_player();
    offer(rxroutes::Action::Flurry,rxroutes::Direction::Neutral);
    player.queued_target.handle=9001;
    player.queued_target.identity[0]=1;
    change_state(actor,14);
    CHECK(requested_action()==static_cast<int>(rxroutes::Action::Flurry));
    walk(original_table,0,0);
    CHECK(player.active_move==0);
    CHECK(player.committed==static_cast<int>(rxroutes::Action::Flurry));
    CHECK(player.committed_target.handle==9001&&player.committed_target.identity[0]==1);
    pad_frame+=buffer_ticks+20;pad_pressed=pad_held=0;
    sample_input(actor);
    CHECK(!queue_live());
    CHECK(requested_action()==static_cast<int>(rxroutes::Action::Flurry));
    const auto record=active_record();
    put(actor,0x7b0,get<int>(record,0x10));
    during_step=[] {CHECK(get<int>(original_table,0x47c)==1);next_a(original_table);};
    CHECK(step(actor)!=0);
    during_step={};
    CHECK(rxlimit::active(actor));
    CHECK(requested_action()==0);
    close();tear_original(original_table);
}
}
int main() {
    const std::pair<const char*,std::function<void()>> cases[]={
      {"supported version and host ABI gate",test::version_gate},
      {"missing and undersized interfaces refuse safely",test::missing_interfaces},
      {"18 original banks / 94 IDs and ownership lifetime",test::actual_graph_maps_and_lifetime},
      {"input priority, expiry, pause, room and defeated gates",test::input_priority_expiry_and_context},
      {"initial technique and original command validation",test::initial_technique_and_command_guard},
      {"authored half-open window and finisher boundary",test::authored_window_and_finisher_boundary},
      {"stop restores ownership, absent overlay is not touched",test::stop_restores_ownership_and_missing_overlay},
      {"only managed Drive Roxas uses the native dual graph",test::native_dual_stays_in_its_own_overlay},
      {"visible/enabled actor polarity and invisible native passthrough",test::visible_actor_gate_and_native_passthrough},
      {"temporary movement boost never compounds or overrides a state change",test::movement_boost_is_temporary_and_respects_state_change},
      {"defense waits for contacts, expiry, ground and native abilities",test::defense_preserves_contacts_and_requires_native_abilities},
      {"regular finisher follows original transition and preserves state reference",test::finisher_waits_for_native_transition_return_and_keeps_state_ref},
      {"forced native states cancel a signature waiting for cleanup",test::forced_native_states_cancel_a_signature_waiting_for_transition_cleanup},
      {"ordinary melee states preserve a signature waiting for cleanup",test::ordinary_native_melee_states_preserve_a_signature_waiting_for_cleanup},
      {"default normal routes reach all sixteen stock attack clips",test::default_routes_cover_all_stock_attack_clips},
      {"stale SDK actor is refused and stop leaves retired actor memory alone",test::stale_sdk_actor_is_not_controlled_or_dereferenced_on_stop},
      {"target-relative directions and deliberately chosen signature chords",test::semantic_directions_and_signature_chords_are_stable},
      {"Light after held Technique continues ground/air chains without accidental signatures",test::light_after_held_technique_does_not_request_an_accidental_signature},
      {"all borrowed moves retain equipped live weapon stats",test::equipped_stats_survive_every_borrowed_bank},
      {"Light pressure continues past five hits without forced signature",test::light_pressure_has_no_automatic_fifth_hit_or_signature},
      {"hit-confirm defense opens earlier than whiff recovery by authored role",test::confirmed_hit_cancels_earlier_than_a_whiff_and_respects_role_commitment},
      {"launcher chase, Enemy Step and magic respect safe native exit gates",test::native_chase_enemy_step_and_magic_share_safe_exit_gates},
      {"one route resume expires, consumes and clears on forced/menu states",test::route_resume_is_brief_consumable_and_forced_or_menu_state_clears_it},
      {"Drive expiry blocks input and frame replacement obeys safe state",test::drive_expiry_suppresses_new_attacks_and_frame_replacement_is_gated},
      {"native packet policy, hit listeners and form callbacks are connected",test::hit_pipeline_and_form_callbacks_are_wired_once},
      {"committed native-window intent survives expiry; unaccepted input expires",test::accepted_intent_survives_buffer_expiry_and_unaccepted_input_does_not_start_a_move},
      {"cinematic control loss discards accepted and queued combat intentions",[]{test::forced_control_loss_discards_old_intentions(0);}},
      {"blocked input discards accepted and queued combat intentions",[]{test::forced_control_loss_discards_old_intentions(1);}},
      {"hidden actor discards accepted and queued combat intentions",[]{test::forced_control_loss_discards_old_intentions(2);}},
      {"ordinary pause preserves accepted input while explicit menus cancel it",test::ordinary_pause_preserves_an_accepted_command_but_menu_open_cancels_it},
      {"native signature input lock still accepts deliberate defense",test::signature_input_lock_keeps_its_deliberate_defense_request},
      {"defense preserves newer attack, target and route until one native followup",test::defense_preserves_a_newer_attack_and_its_intended_target},
      {"defense retires same or older attacks and jumps",test::defense_retires_same_or_older_attack_and_jump_intentions},
      {"defense preserves a newer Jump without repeating defense",test::defense_preserves_a_newer_jump_tap_without_repeating_defense},
      {"unavailable defense retains its buffered followups",test::unavailable_defense_keeps_buffered_followups},
      {"accepted defense retires previously accepted aerial motion",test::accepted_defense_retires_previously_accepted_motion},
      {"refused native defense preserves pending inputs and motion",test::refused_native_defense_preserves_pending_inputs_and_motion},
      {"suppressed signatures discard old intentions at every input entry",test::suppressed_signature_discards_old_intentions},
      {"suppressed signatures refuse taps and accept fresh visible defense",test::suppressed_signature_refuses_taps_then_accepts_fresh_visible_input},
      {"frame-only Light tap reaches one native follow-up",[]{test::frame_only_attack_tap_reaches_the_next_native_window(KH_BUTTON_A);}},
      {"frame-only Technique tap reaches one native follow-up",[]{test::frame_only_attack_tap_reaches_the_next_native_window(KH_BUTTON_X);}},
      {"frame-only signature chord reaches one native finisher",[]{test::frame_only_attack_tap_reaches_the_next_native_window(KH_BUTTON_A|KH_BUTTON_X);}},
      {"frame-only Jump tap reaches one eligible native exit",[]{test::frame_only_mobility_tap_reaches_one_eligible_exit(KH_BUTTON_B);}},
      {"frame-only defense tap reaches one eligible native exit",[]{test::frame_only_mobility_tap_reaches_one_eligible_exit(KH_BUTTON_Y);}},
      {"frame sampling keeps dedupe, expiry, pause and cinematic gates",test::frame_sampling_deduplicates_and_keeps_expiry_and_control_gates},
      {"accepted motion queue survives raw Jump expiry and consumes once",test::accepted_motion_queue_can_finish_after_raw_jump_expiry},
      {"replaced motion transition preserves new action intentions",test::replaced_motion_transition_keeps_new_action_intentions},
      {"retired motion queue cannot renew an old long Jump buffer",test::retired_motion_queue_cannot_reaccept_an_old_long_jump_buffer},
      {"retiring accepted motion preserves a newer frame-only Jump tap",test::retired_motion_queue_preserves_a_newer_frame_only_jump_tap},
      {"immediately rejected new motion cannot reuse its original Jump tap",test::newly_accepted_motion_rejection_retires_its_original_jump_tap},
      {"new chase acceptance preserves a waiting queue without renewed requests",test::newly_accepted_chase_waits_without_retiring_or_reaccepting_its_tap},
      {"a second defensive movement clears the saved attack route",test::a_second_defensive_movement_cannot_bank_the_old_combo_route},
      {"committed target resolves fresh and refuses retired/reused generations",test::committed_target_is_resolved_fresh_and_stale_generations_are_not_restored},
      {"retired buffered targets clear native caches while combos and uncaptured targeting continue",test::retired_buffered_target_clears_its_native_cache_without_stopping_the_chain},
      {"accepted signature remains committed across ending recovery and raw expiry",test::accepted_signature_survives_an_ending_and_raw_buffer_expiry},
      {"buffered defense cannot fall through into a native heavy branch",test::buffered_defense_cannot_become_a_native_heavy_branch},
      {"grounded jump exit keeps native launch and horizontal momentum",test::grounded_jump_exit_uses_native_launch_and_preserves_momentum},
      {"a resistant launcher preserves the ordinary jump exit",test::resistant_launcher_does_not_swallow_the_ordinary_jump},
      {"queued form preserves melee while old spells finish, then exits safely",test::pending_form_keeps_melee_until_spells_finish_then_uses_a_real_exit},
      {"queued form uses the signature's authored exit",test::pending_form_can_leave_a_signature_at_its_authored_exit},
      {"practice primes Drive once per scene without changing campaign or paused input",test::practice_gauge_is_scoped_and_once_per_scene},
      {"initial signature chord stays committed until its authored native transition",test::initial_signature_keeps_its_accepted_intent_until_the_native_window}};
    unsigned failed=0;
    for(const auto& item:cases)try {std::cout<<"RUN "<<item.first<<std::endl;item.second();std::cout<<"PASS "<<item.first<<std::endl;}
      catch(const std::exception& ex) {++failed;std::cout<<"FAIL "<<item.first<<": "<<ex.what()<<'\n';}
    std::cout<<(sizeof(cases)/sizeof(cases[0])-failed)<<" passed, "<<failed<<" failed\n";
    return failed?1:0;
}
