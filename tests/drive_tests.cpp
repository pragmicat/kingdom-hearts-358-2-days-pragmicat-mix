// Execute the actual adapter against an independent bounded native-host model.
// No game launch, task runner edits, or mocked implementation of the adapter.
#include "../src/drive_form.cpp"
#include <map>
#include <set>
#include <string>
#include <vector>
#include <stdexcept>
#include <iostream>

namespace testdrive {
void check(bool value, const char* expression) { if (!value) throw std::runtime_error(expression); }
#define REQUIRE(x) ::testdrive::check((x), #x)
std::vector<unsigned char> memory(16 * 1024 * 1024);
std::map<KhRef,unsigned> allocations;
std::map<std::string,KhRef> globals;
std::map<std::string,void*> functions;
std::map<int,KhRef> descriptors;
std::map<KhRef,int> descriptor_overlay;
std::map<KhRef,std::vector<KhRef>> task_parts;
std::map<KhRef,int> render_groups;
std::set<int> loaded;
std::vector<int> released;
std::vector<std::string> logs;
KhRef cursor, current_heap, root, actor_ref, original_actor, loader_ref, party_ref, metadata_ref;
KhRef slots_ref, groups_ref, scene_ref, mode_ref;
KhHost host_api{};
KhEngine engine_api{};
KhActors actors_api{};
KhNet net_api{};
unsigned acquire_calls, destroy_calls, construct_calls, before_calls, after_calls;
unsigned group_calls;
bool veto, room, fail_dual, fail_normal, reset_failures_after_one, fail_scratch;
bool null_previous, refuse_install, refuse_restore;
unsigned passthrough_calls;
unsigned scratch_calls;
int last_after_kind;
bool last_after_rollback;
int language_value;
struct ConstructorArgs { int overlay; std::array<int,8> words; };
std::vector<ConstructorArgs> constructor_args;

void* pointer(KhRef ref) {
    auto found=allocations.upper_bound(ref);
    if(found==allocations.begin())return nullptr;
    --found;
    return ref>=found->first && ref<found->first+found->second ? memory.data()+ref : nullptr;
}
KhRef alloc(unsigned bytes) {
    const KhRef out=cursor;
    cursor=(cursor+bytes+15)&~15u;
    REQUIRE(cursor<memory.size());
    allocations[out]=bytes;
    std::memset(memory.data()+out,0,bytes);
    return out;
}
KhRef native_scratch_alloc(unsigned bytes) {
    ++scratch_calls;
    if(fail_scratch)return 0;
    const KhRef result=alloc(bytes);
    std::memset(pointer(result),0xcd,bytes); // native allocation is not zero-filled
    return result;
}
void free_mem(KhRef ref) { REQUIRE(allocations.erase(ref)==1); }
template<class T> void put(KhRef ref,unsigned offset,T value) {
    REQUIRE(pointer(ref+offset));
    std::memcpy(pointer(ref+offset),&value,sizeof value);
}
template<class T> T get(KhRef ref,unsigned offset=0) {
    REQUIRE(pointer(ref+offset)); T value{};
    std::memcpy(&value,pointer(ref+offset),sizeof value); return value;
}
void remember(KhRef task,KhRef part) { task_parts[task].push_back(part); }

KhRef make_task(int overlay, bool initial=false) {
    KhRef task=alloc(0x2c);
    const int kind=overlay==30?0:19;
    KhRef actor=alloc(overlay==30?14952:12168), node=alloc(0x200);
    remember(task,actor);remember(task,node);
    put(task,0x20,actor);put(task,0x1c,current_heap);
    put(actor,0xc,kind);put(actor,0x20,node);
    put<std::int16_t>(actor,0x66,-1); // class construction alone has no room membership
    put<unsigned char>(actor,0x694,1);
    put<unsigned>(actor,0x24,4);
    put<std::uint16_t>(actor,0x16,100);
    // The native constructor binds these from the deck, not copied old pointers.
    put<unsigned char>(actor,0x1070,1);put<unsigned char>(actor,0x118c,1);
    put<KhRef>(actor,0x4ec,0x1234); // persistent shared enemy-manager player body
    put<std::uint64_t>(actor,0,1ull<<36);
    put<unsigned char>(actor,0x918,static_cast<unsigned char>(kind==19?0:13));
    put<unsigned char>(actor,0x919,static_cast<unsigned char>(kind==19?1:9));
    put<unsigned char>(actor,0x928,static_cast<unsigned char>(kind==19?0:3));
    if(initial) {
        put<std::int16_t>(actor,0x66,2);render_groups[actor]=2;
        put<std::uint64_t>(actor,0,(1ull<<36) | 0x6000000000ull);
        put<int>(actor,0x4b4,8192);
        put<unsigned char>(actor,0x2bb0,2);
        put<std::uint16_t>(actor,0x12,37);
        put<std::uint16_t>(node,0x80,0xabcd);
        put<int>(actor,0x48c,12345);put<int>(actor,0x490,23456);put<int>(actor,0x494,34567);
        put<int>(actor,0x2770,7);put<int>(actor,0x2774,123);
        put<int>(actor,0x2c04,456);put<int>(actor,0x2c0c,78);
        put<int>(actor,0x2bf0,19);put<int>(actor,0x2bf8,23);
        put<std::int16_t>(actor,0x91a,44);put<std::int16_t>(actor,0x91c,-3);
        put<std::int16_t>(actor,0x91e,12);put<int>(actor,0x92c,8192);
        put<unsigned char>(actor,0x4f0,1);put<unsigned char>(actor,0x4f1,8);
        put<unsigned char>(actor,0x4f2,2);
    }
    return task;
}
KhRef instantiate_class(KhRef desc,KhRef args) {
    ++construct_calls;
    const int overlay=descriptor_overlay.at(desc);
    REQUIRE(loaded.count(overlay));
    REQUIRE(get<int>(args)==0);
    ConstructorArgs observed{overlay,{}};
    for(unsigned word=0;word<8;++word)observed.words[word]=get<int>(args,4*word);
    constructor_args.push_back(observed);
    // Real normal CreateActor reads BOTH archive selectors. The config occupies
    // all32 bytes; the remaining vector scratch starts clean on every attempt.
    REQUIRE(observed.words[6]==0 || observed.words[6]==1);
    REQUIRE(observed.words[7]==0 || observed.words[7]==1);
    for(unsigned tail=32;tail<48;++tail)REQUIRE(get<unsigned char>(args,tail)==0);
    const bool failure=overlay==30 ? fail_normal : fail_dual;
    if(failure) {
        if(reset_failures_after_one) { fail_normal=false;fail_dual=false; }
        return 0;
    }
    const KhRef task=make_task(overlay);
    // Verify adapter restores live inventory instead of accepting refilled constructor data.
    std::memset(pointer(party_ref),0xee,4*0x104);
    std::memset(pointer(metadata_ref),0xdd,4*0x44);
    return task;
}
KhRef destroy(KhRef task) {
    ++destroy_calls;
    REQUIRE(get<KhRef>(root,4)==task); // original destructor can resolve its own seat
    render_groups.erase(get<KhRef>(task,0x20));
    for(auto part:task_parts.at(task))free_mem(part);
    task_parts.erase(task);free_mem(task);return 0;
}
KhRef current_actor() {
    const KhRef task=get<KhRef>(root,4);
    return task && pointer(task) ? get<KhRef>(task,0x20) : 0;
}
KhActor sdk_player(int seat) {
    if(seat==0) return current_actor();
    return seat<4?get<KhRef>(root,4+12*seat):0;
}
KhRef sdk_ref(KhActor handle) { return static_cast<KhRef>(handle); }
int local_seat() { return 0; }
int native_party_count() { return get<unsigned char>(root,0x34); }
int native_language() { return language_value; }
int on_game_thread() { return 1; }
int in_room() { return room?1:0; }
KhRef heap_current() { return current_heap; }
KhRef heap_change(KhRef heap) { auto old=current_heap;current_heap=heap;return old; }
int acquire(int,int kind) {
    ++acquire_calls;REQUIRE(kind==19);
    for(int slot=3;slot>=0;--slot) {
        if(get<int>(slots_ref,4*slot)==-1) {
            const int overlay=get<int>(groups_ref,16*kind+4*slot);
            if(overlay==-1)continue;
            put(slots_ref,4*slot,overlay);loaded.insert(overlay);
            put<KhRef>(loader_ref,0,0xabcdef01);return overlay;
        }
    }
    throw std::runtime_error("adapter called unsafe overlay allocator without capacity");
}
void release(int overlay) {
    REQUIRE(overlay>=0);REQUIRE(loaded.count(overlay));released.push_back(overlay);loaded.erase(overlay);
    for(int slot=0;slot<4;++slot)if(get<int>(slots_ref,4*slot)==overlay)put<int>(slots_ref,4*slot,-1);
}
void no_int(int) {}
void hide_group(int seat) {
    REQUIRE(seat==0);const auto actor=current_actor();REQUIRE(actor);
    put<std::int16_t>(actor,0x66,-1); // native hide also removes the old room assignment
    render_groups.erase(actor);
}
void submit_group(int seat,int group) {
    REQUIRE(seat==0);const auto actor=current_actor();REQUIRE(actor);
    REQUIRE(group>=0 && group<4); // this fixture models four live room slots
    put<std::int16_t>(actor,0x66,static_cast<std::int16_t>(group));
    render_groups[actor]=group;++group_calls;
}
void native_candidate_scan(KhRef actor) {
    // The native path casts GetSlotTableByte(GetEntryField66(seat)) to u16.
    // A constructor-only -1 group becomes list index65535 before dereference.
    const int group=get<std::int16_t>(actor,0x66);
    const int slot=group>=0 && group<4 ? group : -1;
    const auto list_index=static_cast<std::uint16_t>(slot);
    REQUIRE(list_index<4);
    REQUIRE(render_groups.count(actor) && render_groups.at(actor)==group);
}
void visibility(KhRef actor,int visible) { put<unsigned char>(actor,0x694,visible?1:0); }
void hp_set(KhRef actor,int hp) { put<std::uint16_t>(actor,0x12,static_cast<std::uint16_t>(hp)); }
void position(int seat,KhRef xyz) {
    REQUIRE(seat==0);auto actor=current_actor();REQUIRE(actor);
    native_candidate_scan(actor); // room submission precedes native placement
    std::memcpy(pointer(actor+0x48c),pointer(xyz),12);
    // Match native placement's resetting of status/DoT; adapter must restore after it.
    std::memset(pointer(actor+0x2770),0,0x18);std::memset(pointer(actor+0x2c00),0,0x20);
}
void target_resolve(KhRef actor) {
    native_candidate_scan(actor); // actor must already belong to the live room
    REQUIRE(get<unsigned char>(actor,0x4f0)==1);
    put<KhRef>(actor,0x4fc,0xf00d1234); // fresh resolver output, not a retired actor pointer
}
void original_unload() {
    // Reserved normal must already have been released while ov029 is resident.
    REQUIRE(!loaded.count(30));release(get<int>(root,8));loaded.erase(29);
}
void host_log(KhPlugin*,KhLogLevel,const char* text) { logs.emplace_back(text); }
KhRef datum(const char* name) { auto f=globals.find(name);return f==globals.end()?0:f->second; }
void* routine(const char* name,int*) { auto f=functions.find(name);return f==functions.end()?nullptr:f->second; }
int module_loaded(int module) { return loaded.count(module)?1:0; }
KhResult replace(const char* name,void* fn,void** old) {
    if(std::string(name)!="Ov022_UnloadParty")return KH_ERROR;
    const bool installing=fn==reinterpret_cast<void*>(&rxdrive::party_unload);
    if(installing?refuse_install:refuse_restore)return KH_ERROR;
    if(old)*old=null_previous?nullptr:functions[name];functions[name]=fn;return KH_OK;
}
void passthrough_unload() {++passthrough_calls;}
bool before(KhRef old,int,void*) { REQUIRE(old==current_actor());++before_calls;return !veto; }
void after(KhRef old,KhRef fresh,int kind,bool rollback,void*) {
    REQUIRE(!pointer(old));REQUIRE(fresh==current_actor());
    ++after_calls;last_after_kind=kind;last_after_rollback=rollback;
}
rxdrive::FrameContext context(unsigned tick=100,bool safe=true) {
    return {current_actor(),tick,true,false,false,true,safe};
}
void setup(bool initialize=true) {
    // Each case stops the previous adapter before resetting mock memory.
    rxdrive::stop();
    allocations.clear();globals.clear();functions.clear();descriptors.clear();descriptor_overlay.clear();
    task_parts.clear();render_groups.clear();loaded.clear();released.clear();logs.clear();cursor=0x1000;current_heap=0x55550000;
    constructor_args.clear();language_value=0x10;
    acquire_calls=destroy_calls=construct_calls=before_calls=after_calls=group_calls=0;
    veto=room=fail_dual=fail_normal=reset_failures_after_one=fail_scratch=false;scratch_calls=0;
    null_previous=refuse_install=refuse_restore=false;passthrough_calls=0;
    loader_ref=alloc(48);root=alloc(0xe4);party_ref=alloc(4*0x104);metadata_ref=alloc(4*0x44);
    scene_ref=alloc(16);mode_ref=alloc(4);slots_ref=alloc(16);groups_ref=alloc(20*16);
    put<unsigned char>(root,0x34,1);
    put(scene_ref,8,2);put(loader_ref,4,root);put<KhRef>(loader_ref,0,0x33334444);
    for(unsigned x=0;x<4*0x104;++x) static_cast<unsigned char*>(pointer(party_ref))[x]=static_cast<unsigned char>(x%251);
    put<unsigned char>(party_ref,0,0);put<unsigned char>(party_ref,1,0);
    put<unsigned char>(party_ref,3,0);put<unsigned char>(party_ref,4,23);
    for(int slot=0;slot<4;++slot)put<int>(slots_ref,4*slot,-1);
    for(int kind=0;kind<20;++kind)for(int slot=0;slot<4;++slot)put<int>(groups_ref,16*kind+4*slot,-1);
    const int dual_overlays[]={49,68,87,104};
    for(int slot=0;slot<4;++slot)put<int>(groups_ref,19*16+4*slot,dual_overlays[slot]);
    for(int overlay:{30,49,68,87,104}) {
        const KhRef desc=alloc(20);put<unsigned short>(desc,0,10);put<unsigned short>(desc,2,6);
        put<KhRef>(desc,4,0xaaaa);put<KhRef>(desc,8,0xbbbb);
        put<unsigned>(desc,0xc,overlay==30?14952:12168);
        descriptors[overlay]=desc;descriptor_overlay[desc]=overlay;
        globals[rxdrive::descriptor_name(overlay)]=desc;
    }
    loaded={22,29,30};put<int>(slots_ref,0,30);
    auto task=make_task(30,true);put(root,4,task);put(root,8,30);
    original_actor=current_actor();
    // Ally task identity is deliberately opaque; adapter must not destroy or replace it.
    put<KhRef>(root,16,0x71717171);put<int>(root,20,40);
    globals["data_ov022_020b2e78"]=loader_ref;globals["gPartyMembers"]=party_ref;
    globals["data_0204c3d8"]=metadata_ref;globals["gSceneCtl"]=scene_ref;
    globals["data_0204c240"]=mode_ref;globals["data_ov029_020b3200"]=slots_ref;
    globals["data_ov029_020b30b0"]=groups_ref;
    functions={{"Heap_GetCurrent",reinterpret_cast<void*>(&heap_current)},
        {"Heap_SetCurrent",reinterpret_cast<void*>(&heap_change)},
        {"VeneerTo_Obj_Destroy",reinterpret_cast<void*>(&destroy)},
        {"InstantiateClass",reinterpret_cast<void*>(&instantiate_class)},
        {"Ov029_AcquireOverlaySlot",reinterpret_cast<void*>(&acquire)},
        {"Ov029_ReleaseOverlaySlot",reinterpret_cast<void*>(&release)},
        {"func_ov022_020882f8",reinterpret_cast<void*>(&native_party_count)},
        {"LoadGlobalU16At0",reinterpret_cast<void*>(&native_language)},
        {"Ov022_CleanupEntry",reinterpret_cast<void*>(&no_int)},
        {"func_ov022_02087298",reinterpret_cast<void*>(&hide_group)},
        {"func_ov022_02088428",reinterpret_cast<void*>(&submit_group)},
        {"SNDi_ProcessEntry",reinterpret_cast<void*>(&no_int)},
        {"Ov022_ResolveTargetPick",reinterpret_cast<void*>(&target_resolve)},
        {"func_ov022_020a0678",reinterpret_cast<void*>(&visibility)},
        {"Ov022_ActorSetHp",reinterpret_cast<void*>(&hp_set)},
        {"func_ov022_02088218",reinterpret_cast<void*>(&position)},
        {"NNSi_FndAllocFromDefaultExpHeap",reinterpret_cast<void*>(&native_scratch_alloc)},
        {"NNSi_FndFreeFromDefaultHeap",reinterpret_cast<void*>(&free_mem)},
        {"Ov022_UnloadParty",reinterpret_cast<void*>(&original_unload)}};
    host_api={};host_api.size=sizeof(host_api);host_api.abi_major=KH_ABI_MAJOR;host_api.port_version="0.1.5";host_api.log=&host_log;
    engine_api={};engine_api.size=sizeof(engine_api);engine_api.pointer=&pointer;engine_api.datum=&datum;
    engine_api.routine=&routine;engine_api.replace=&replace;engine_api.module_loaded=&module_loaded;
    engine_api.on_game_thread=&on_game_thread;
    actors_api={};actors_api.size=sizeof(actors_api);actors_api.player=&sdk_player;
    actors_api.ref=&sdk_ref;actors_api.local_seat=&local_seat;
    net_api={};net_api.size=sizeof(net_api);net_api.in_room=&in_room;
    if(initialize) {
        REQUIRE(rxdrive::init(&host_api,&engine_api,&actors_api,&net_api,{&before,&after,nullptr,nullptr}));
        rxdrive::frame(context());
    }
}
void test_hook_preflight_and_refusal_preserve_native_callback() {
    for(bool missing:{false,true}) {
        setup(false);
        if(missing)functions.erase("Ov022_UnloadParty");else refuse_install=true;
        REQUIRE(!rxdrive::init(&host_api,&engine_api,&actors_api,&net_api));
        REQUIRE(!rxdrive::view().available && !destroy_calls && !acquire_calls);
        if(missing)REQUIRE(!functions.count("Ov022_UnloadParty"));
        else REQUIRE(functions.at("Ov022_UnloadParty")==reinterpret_cast<void*>(&original_unload));
        refuse_install=false;rxdrive::stop();
    }
}
void test_incomplete_hook_restores_or_remains_callable_until_retry() {
    for(bool retry:{false,true}) {
        setup(false);functions["Ov022_UnloadParty"]=reinterpret_cast<void*>(&passthrough_unload);
        null_previous=true;refuse_restore=retry;
        REQUIRE(!rxdrive::init(&host_api,&engine_api,&actors_api,&net_api));
        REQUIRE(!rxdrive::view().available && !rxdrive::request_activation());
        if(!retry)REQUIRE(functions.at("Ov022_UnloadParty")==reinterpret_cast<void*>(&passthrough_unload));
        reinterpret_cast<void(*)()>(functions.at("Ov022_UnloadParty"))();
        REQUIRE(passthrough_calls==1 && !destroy_calls && !acquire_calls);
        null_previous=refuse_restore=false;rxdrive::stop();
        REQUIRE(functions.at("Ov022_UnloadParty")==reinterpret_cast<void*>(&passthrough_unload));
        REQUIRE(rxdrive::init(&host_api,&engine_api,&actors_api,&net_api));
        rxdrive::stop();
    }
}
void charge() {
    for(unsigned serial=0;serial<25;++serial)
        rxdrive::on_hit(current_actor(),0x11111111,serial,100);
    REQUIRE(rxdrive::view().ready);
}
void test_charge() {
    setup();REQUIRE(!rxdrive::request_activation());
    for(unsigned i=0;i<10;++i)rxdrive::on_hit(current_actor(),77,1,100);
    REQUIRE(rxdrive::view().charge==40);
    for(unsigned target=78;target<90;++target)rxdrive::on_hit(current_actor(),target,1,100);
    REQUIRE(rxdrive::view().charge==120);
    rxdrive::on_hit(current_actor()+4,99,2,100);REQUIRE(rxdrive::view().charge==120);
    room=true;rxdrive::on_hit(current_actor(),99,2,100);REQUIRE(rxdrive::view().charge==120);
}
void test_native_roundtrip() {
    setup();charge();
    std::array<unsigned char,4*0x104> inventory{};std::memcpy(inventory.data(),pointer(party_ref),inventory.size());
    REQUIRE(rxdrive::request_activation());rxdrive::frame(context(101,false));
    REQUIRE(destroy_calls==0);REQUIRE(rxdrive::view().phase==rxdrive::Phase::ActivationQueued);
    rxdrive::frame(context(102));
    REQUIRE(rxdrive::is_active());REQUIRE(get<int>(current_actor(),0xc)==19);
    REQUIRE(!pointer(original_actor));REQUIRE(construct_calls==1);REQUIRE(destroy_calls==1);
    REQUIRE(loaded.count(30));REQUIRE(loaded.count(104));
    REQUIRE(get<std::uint16_t>(current_actor(),0x12)==37);
    REQUIRE(get<int>(current_actor(),0x48c)==12345);REQUIRE(get<int>(current_actor(),0x2774)==123);
    REQUIRE(get<int>(current_actor(),0x2c04)==456);REQUIRE(get<int>(current_actor(),0x2bf8)==23);
    REQUIRE(get<std::uint16_t>(get<KhRef>(current_actor(),0x20),0x80)==0xabcd);
    REQUIRE(get<KhRef>(current_actor(),0x4fc)==0xf00d1234);
    REQUIRE(get<std::int16_t>(current_actor(),0x91a)==44);
    REQUIRE(get<std::int16_t>(current_actor(),0x91c)==-3);
    REQUIRE(get<std::int16_t>(current_actor(),0x91e)==12);
    REQUIRE(get<int>(current_actor(),0x92c)==8192);
    REQUIRE((get<std::uint64_t>(current_actor()) & 0x6000000000ull)==0x6000000000ull);
    REQUIRE(get<int>(current_actor(),0x4b4)==8192);
    REQUIRE(get<unsigned char>(current_actor(),0x2bb0)==2);
    REQUIRE(get<unsigned char>(current_actor(),0x918)==0); // real dual animation family
    REQUIRE(get<unsigned char>(current_actor(),0x919)==1); // real dual resource selection
    REQUIRE(get<unsigned char>(current_actor(),0x928)==0); // real dual graph
    REQUIRE(std::memcmp(inventory.data(),pointer(party_ref),inventory.size())==0);
    REQUIRE(get<KhRef>(root,16)==0x71717171);REQUIRE(last_after_kind==19);REQUIRE(!last_after_rollback);
    // Consume magic and HP while driving; restore must preserve CURRENT values.
    put<unsigned char>(party_ref,0x9c,3);put<std::uint16_t>(current_actor(),0x12,22);
    REQUIRE(rxdrive::request_activation());rxdrive::frame(context(103));
    REQUIRE(!rxdrive::is_active());REQUIRE(get<int>(current_actor(),0xc)==0);
    REQUIRE(get<std::uint16_t>(current_actor(),0x12)==22);REQUIRE(get<unsigned char>(party_ref,0x9c)==3);
    REQUIRE(!loaded.count(104));REQUIRE(loaded.count(30));REQUIRE(destroy_calls==2);
}
void test_pause_expiry() {
    setup();charge();REQUIRE(rxdrive::request_activation());rxdrive::frame(context(101));
    auto paused=context(1001);paused.paused=true;rxdrive::frame(paused);
    REQUIRE(rxdrive::view().remaining_ticks==900);
    auto cinema=context(2001);cinema.cinematic=true;rxdrive::frame(cinema);
    REQUIRE(rxdrive::view().remaining_ticks==900);
    rxdrive::frame(context(2002));REQUIRE(rxdrive::view().remaining_ticks==900);
    rxdrive::frame(context(2902,false));REQUIRE(rxdrive::view().phase==rxdrive::Phase::RestorationQueued);
    REQUIRE(get<int>(current_actor(),0xc)==19);
    rxdrive::frame(context(2903));REQUIRE(get<int>(current_actor(),0xc)==0);
}
void test_resume_does_not_charge_a_suspended_frame_gap() {
    for (bool cinematic : {false,true}) {
        setup();charge();REQUIRE(rxdrive::request_activation());rxdrive::frame(context(101));
        auto suspended=context(110);suspended.paused=!cinematic;suspended.cinematic=cinematic;
        rxdrive::frame(suspended);const auto remaining=rxdrive::view().remaining_ticks;
        rxdrive::frame(context(2000));
        REQUIRE(rxdrive::view().phase==rxdrive::Phase::Active);
        REQUIRE(rxdrive::view().remaining_ticks==remaining);
        auto combat=context(2010,false);combat.controllable=false;rxdrive::frame(combat);
        REQUIRE(rxdrive::view().remaining_ticks==remaining-10); // hit recovery still spends time
        rxdrive::frame(combat);REQUIRE(rxdrive::view().remaining_ticks==remaining-10); // same frame once
    }
}
void test_room_membership_roundtrip() {
    setup();charge();REQUIRE(rxdrive::request_activation());rxdrive::frame(context(101));
    REQUIRE(rxdrive::is_active());REQUIRE(group_calls==1);
    REQUIRE(get<std::int16_t>(current_actor(),0x66)==2);native_candidate_scan(current_actor());
    // A room transition while driving is current state; returning must not use
    // the room from the earlier activation snapshot.
    submit_group(0,1);
    REQUIRE(rxdrive::request_activation());rxdrive::frame(context(102));
    REQUIRE(!rxdrive::is_active());REQUIRE(group_calls==3);
    REQUIRE(get<std::int16_t>(current_actor(),0x66)==1);native_candidate_scan(current_actor());
    REQUIRE(render_groups.size()==1);

    setup();charge();fail_dual=true;REQUIRE(rxdrive::request_activation());rxdrive::frame(context(101));
    REQUIRE(last_after_rollback && group_calls==1);
    REQUIRE(get<std::int16_t>(current_actor(),0x66)==2);native_candidate_scan(current_actor());
}
void test_unassigned_room_refused() {
    setup();charge();put<std::int16_t>(current_actor(),0x66,-1);
    REQUIRE(rxdrive::request_activation());rxdrive::frame(context(101));
    REQUIRE(destroy_calls==0 && construct_calls==0 && group_calls==0);
    REQUIRE(rxdrive::view().charge==1000 && !rxdrive::is_active());
}
void test_veto_and_overlay_full() {
    setup();charge();veto=true;REQUIRE(rxdrive::request_activation());rxdrive::frame(context(101));
    REQUIRE(!destroy_calls);REQUIRE(rxdrive::view().charge==1000);REQUIRE(acquire_calls==1);
    REQUIRE(before_calls==1);REQUIRE(!loaded.count(104));
    setup();charge();for(int i=1;i<4;++i)put<int>(slots_ref,4*i,40+i);
    REQUIRE(rxdrive::request_activation());rxdrive::frame(context(102));
    REQUIRE(!destroy_calls);REQUIRE(!acquire_calls);REQUIRE(rxdrive::view().charge==1000);
}
void test_preflight_before_detach() {
    setup();charge();const KhRef original=current_actor();
    for(int i=1;i<4;++i)put<int>(slots_ref,4*i,40+i);
    REQUIRE(rxdrive::request_activation());rxdrive::frame(context(101));
    REQUIRE(!before_calls);REQUIRE(!scratch_calls);REQUIRE(!destroy_calls);
    REQUIRE(current_actor()==original);REQUIRE(rxdrive::view().charge==1000);

    setup();charge();put<unsigned short>(descriptors.at(104),0,0);
    REQUIRE(rxdrive::request_activation());rxdrive::frame(context(101));
    REQUIRE(acquire_calls==1);REQUIRE(!before_calls);REQUIRE(!scratch_calls);
    REQUIRE(!destroy_calls);REQUIRE(!loaded.count(104));REQUIRE(loaded.count(30));
    REQUIRE(get<KhRef>(loader_ref)==0x33334444);REQUIRE(rxdrive::view().charge==1000);

    setup();charge();fail_scratch=true;const KhRef saved_heap=current_heap;
    REQUIRE(rxdrive::request_activation());rxdrive::frame(context(101));
    REQUIRE(acquire_calls==1);REQUIRE(scratch_calls==1);REQUIRE(!before_calls);
    REQUIRE(!destroy_calls);REQUIRE(!loaded.count(104));REQUIRE(loaded.count(30));
    REQUIRE(current_heap==saved_heap);REQUIRE(rxdrive::view().charge==1000);

    setup();charge();veto=true;const auto saved_allocations=allocations.size();
    const KhRef saved_actor=current_actor(), veto_heap=current_heap;
    REQUIRE(rxdrive::request_activation());rxdrive::frame(context(101));
    REQUIRE(before_calls==1);REQUIRE(scratch_calls==1);REQUIRE(!destroy_calls);
    REQUIRE(allocations.size()==saved_allocations);REQUIRE(current_heap==veto_heap);
    REQUIRE(current_actor()==saved_actor);REQUIRE(loaded.count(30));REQUIRE(!loaded.count(104));
    REQUIRE(released.size()==1 && released[0]==104);REQUIRE(rxdrive::view().charge==1000);
}
void test_rollback() {
    setup();charge();fail_dual=true;REQUIRE(rxdrive::request_activation());rxdrive::frame(context(101));
    REQUIRE(!rxdrive::is_active());REQUIRE(get<int>(current_actor(),0xc)==0);
    REQUIRE(last_after_rollback);REQUIRE(rxdrive::view().charge==1000);REQUIRE(!loaded.count(104));
    REQUIRE(get<std::uint16_t>(current_actor(),0x12)==37);
    fail_dual=false;REQUIRE(rxdrive::request_activation());rxdrive::frame(context(102));
    REQUIRE(rxdrive::is_active());fail_normal=true;
    REQUIRE(rxdrive::request_activation());rxdrive::frame(context(103));
    REQUIRE(rxdrive::is_active());REQUIRE(get<int>(current_actor(),0xc)==19);REQUIRE(last_after_rollback);
    fail_normal=false;rxdrive::frame(context(104));REQUIRE(!rxdrive::is_active());
}
void test_party_unload() {
    setup();charge();REQUIRE(rxdrive::request_activation());rxdrive::frame(context(101));
    reinterpret_cast<void(*)()>(functions.at("Ov022_UnloadParty"))();
    REQUIRE(!rxdrive::is_active());REQUIRE(!loaded.count(30));REQUIRE(!loaded.count(104));
    REQUIRE(released.size()>=2);REQUIRE(released[released.size()-2]==30);
}
void test_stale_and_scripted() {
    setup();charge();REQUIRE(rxdrive::request_activation());
    auto wrong=context(101);wrong.actor+=4;rxdrive::frame(wrong);REQUIRE(!destroy_calls);
    REQUIRE(!rxdrive::view().ready);
    rxdrive::frame(context(102));charge();
    put<unsigned char>(mode_ref,0,4);REQUIRE(rxdrive::request_activation());rxdrive::frame(context(103));
    REQUIRE(!destroy_calls);REQUIRE(!rxdrive::view().ready);
}
void test_owned_magic_quiescence() {
    setup();charge();
    const KhRef actor=current_actor(), task=get<KhRef>(root,4);
    put<unsigned>(actor,0x2288+8,0x7fff);
    std::array<KhRef,15> channels{};
    std::array<KhRef,15> parts{};
    for(unsigned kind=0;kind<15;++kind) {
        const bool direct=kind==5 || kind>=11;
        channels[kind]=alloc(direct?0x119:0x174);remember(task,channels[kind]);
        put<unsigned char>(channels[kind],0,static_cast<unsigned char>(kind));
        put<KhRef>(actor,0x2288+0x18+4*kind,channels[kind]);
        if(!direct) {
            // Idle equipped magic has a nonzero kind at118; Blizzard variants
            // even initialize nSeq2=4. Neither means a live projectile.
            put<unsigned char>(channels[kind],0x118,static_cast<unsigned char>(kind));
            put<unsigned char>(channels[kind],2,4);
            put<unsigned char>(channels[kind],1,5);
            parts[kind]=alloc(5*0x150);remember(task,parts[kind]);
            put<KhRef>(channels[kind],0x168,parts[kind]);
        }
    }
    REQUIRE(!rxdrive::ready_for_safe_point(actor)); // no transition requested
    REQUIRE(rxdrive::request_activation());
    put<unsigned char>(parts[6],4*0x150+0x14c,2); // flying final part
    REQUIRE(!rxdrive::ready_for_safe_point(actor));
    rxdrive::frame(context(101));REQUIRE(!destroy_calls);
    REQUIRE(rxdrive::view().phase==rxdrive::Phase::ActivationQueued);
    put<unsigned char>(parts[6],4*0x150+0x14c,0);
    put<int>(channels[0],0x170,1); // pending native cast tracks
    REQUIRE(!rxdrive::ready_for_safe_point(actor));
    rxdrive::frame(context(102));REQUIRE(!destroy_calls);
    put<int>(channels[0],0x170,0);
    put<unsigned char>(channels[14],0x118,1); // direct reaction still active
    REQUIRE(!rxdrive::ready_for_safe_point(actor));
    rxdrive::frame(context(103));REQUIRE(!destroy_calls);
    put<unsigned char>(channels[14],0x118,0);
    std::vector<unsigned char> before_readiness(14952);
    std::memcpy(before_readiness.data(),pointer(actor),before_readiness.size());
    REQUIRE(rxdrive::ready_for_safe_point(actor)); // idle-equipped magic is allowed
    REQUIRE(!rxdrive::ready_for_safe_point(actor+4)); // stale caller reference
    REQUIRE(std::memcmp(before_readiness.data(),pointer(actor),before_readiness.size())==0);
    rxdrive::frame(context(104));REQUIRE(rxdrive::is_active());
    REQUIRE(destroy_calls==1); // all15 equipped but idle channels permit Drive
    // Timer still drains through a non-controllable hit reaction.
    auto stunned=context(105,false);stunned.controllable=false;rxdrive::frame(stunned);
    REQUIRE(rxdrive::view().remaining_ticks==899);
}
void test_cancel_queued_activation() {
    setup();charge();const KhRef actor=current_actor();
    std::vector<unsigned char> unchanged(14952);
    std::memcpy(unchanged.data(),pointer(actor),unchanged.size());
    REQUIRE(rxdrive::request_activation());REQUIRE(rxdrive::ready_for_safe_point(actor));
    REQUIRE(rxdrive::request_activation());
    REQUIRE(rxdrive::view().phase==rxdrive::Phase::Normal);
    REQUIRE(rxdrive::view().charge==1000 && rxdrive::view().ready);
    REQUIRE(!rxdrive::ready_for_safe_point(actor));
    REQUIRE(!destroy_calls && !acquire_calls && !before_calls);
    REQUIRE(std::memcmp(unchanged.data(),pointer(actor),unchanged.size())==0);
    REQUIRE(rxdrive::request_activation());
    room=true;REQUIRE(!rxdrive::ready_for_safe_point(actor));room=false;
    rxdrive::frame(context(101));REQUIRE(rxdrive::is_active());
    REQUIRE(rxdrive::request_activation());
    REQUIRE(rxdrive::view().phase==rxdrive::Phase::RestorationQueued);
    REQUIRE(rxdrive::ready_for_safe_point(current_actor()));
    REQUIRE(!rxdrive::request_activation()); // restoration cannot be canceled
    REQUIRE(rxdrive::view().phase==rxdrive::Phase::RestorationQueued);
    rxdrive::frame(context(102));REQUIRE(!rxdrive::is_active());
}
void test_full_constructor_appearance_args() {
    struct Variant { unsigned count;int language;unsigned special;int alternate;int group; };
    const Variant variants[]={{1,0x10,0,1,0},{3,0x10,0,0,0},{1,0x2a,0,0,0},
                              {1,0x10,0x14,1,1},{1,0x10,0x15,1,1}};
    for(const auto& variant:variants) {
        setup();put<unsigned char>(root,0x34,static_cast<unsigned char>(variant.count));
        language_value=variant.language;
        put<unsigned char>(party_ref,3,static_cast<unsigned char>(variant.special));
        charge();REQUIRE(rxdrive::request_activation());rxdrive::frame(context(101));
        REQUIRE(rxdrive::is_active());REQUIRE(constructor_args.size()==1);
        REQUIRE(constructor_args.back().words[6]==variant.alternate);
        REQUIRE(constructor_args.back().words[7]==variant.group);
        // The original normal selectors are retained, rather than reconstructed
        // from dual defaults or changed globals when returning from Drive.
        put<unsigned char>(root,0x34,4);language_value=0x2a;
        REQUIRE(rxdrive::request_activation());rxdrive::frame(context(102));
        REQUIRE(!rxdrive::is_active());REQUIRE(constructor_args.size()==2);
        REQUIRE(constructor_args.back().overlay==30);
        REQUIRE(constructor_args.back().words[6]==variant.alternate);
        REQUIRE(constructor_args.back().words[7]==variant.group);
        REQUIRE(get<unsigned char>(party_ref,3)==variant.special);
    }
}
void test_practice_meter() {
    setup(); const KhRef actor=current_actor();
    std::vector<unsigned char> unchanged(14952);
    std::memcpy(unchanged.data(),pointer(actor),unchanged.size());
    REQUIRE(rxdrive::view().charge==0);
    REQUIRE(!rxdrive::prime_practice_meter(actor+4));
    room=true; REQUIRE(!rxdrive::prime_practice_meter(actor)); room=false;
    auto paused=context(101); paused.paused=true; rxdrive::frame(paused);
    REQUIRE(!rxdrive::prime_practice_meter(actor));
    auto cinematic=context(102); cinematic.cinematic=true; rxdrive::frame(cinematic);
    REQUIRE(!rxdrive::prime_practice_meter(actor));
    rxdrive::frame(context(103));
    REQUIRE(rxdrive::prime_practice_meter(actor));
    REQUIRE(rxdrive::view().charge==1000 && rxdrive::view().ready);
    REQUIRE(std::memcmp(unchanged.data(),pointer(actor),unchanged.size())==0);
    REQUIRE(!destroy_calls && !acquire_calls);
    REQUIRE(rxdrive::request_activation());
    REQUIRE(!rxdrive::prime_practice_meter(actor));
    rxdrive::frame(context(104)); REQUIRE(rxdrive::is_active());
    const unsigned remaining=rxdrive::view().remaining_ticks;
    REQUIRE(!rxdrive::prime_practice_meter(current_actor()));
    REQUIRE(rxdrive::view().remaining_ticks==remaining && rxdrive::view().charge==0);
    rxdrive::stop(); REQUIRE(!rxdrive::prime_practice_meter(actor));
}
}
int main() {
    using namespace testdrive;
    struct Case{const char*name;void(*test)();};
    const Case cases[]={{"charge-dedup-cap-and-coop",test_charge},{"native-model-class-roundtrip-preserves-state",test_native_roundtrip},
        {"hook-preflight-and-refusal-preserve-native-callback",test_hook_preflight_and_refusal_preserve_native_callback},
        {"incomplete-hook-rollback-and-callable-retry",test_incomplete_hook_restores_or_remains_callable_until_retry},
        {"pause-cinematic-timer-and-safe-expiry",test_pause_expiry},{"veto-overlay-capacity-fails-before-destroy",test_veto_and_overlay_full},
        {"resume-excludes-paused-and-cinematic-frame-gaps",test_resume_does_not_charge_a_suspended_frame_gap},
        {"activation-and-restoration-rollback",test_rollback},{"reserved-overlay-release-before-party-unload",test_party_unload},
        {"stale-actor-and-mission-mode-refused",test_stale_and_scripted},
        {"equipped-idle-magic-allowed-live-projectiles-deferred",test_owned_magic_quiescence},
        {"full-eight-word-dirty-heap-appearance-roundtrip",test_full_constructor_appearance_args},
        {"resource-preflight-before-binding-detach-and-veto-cleanup",test_preflight_before_detach},
        {"queued-activation-cancel-preserves-charge-without-actor-writes",test_cancel_queued_activation},
        {"native-room-membership-before-placement-targeting-and-current-room-rollback",test_room_membership_roundtrip},
        {"unassigned-room-fails-before-native-destruction",test_unassigned_room_refused},
        {"practice-meter-only-primes-valid-normal-actor-without-form-or-stat-writes",test_practice_meter}};
    unsigned passed=0;
    for(const auto& c:cases)try{c.test();++passed;std::cout<<"PASS "<<c.name<<'\n';}
        catch(const std::exception&e){std::cout<<"FAIL "<<c.name<<": "<<e.what()<<'\n';}
    std::cout<<passed<<"/"<<std::size(cases)<<" drive tests passed\n";
    return passed==std::size(cases)?0:1;
}
