"""Execute the shipped practice controller in Lua 5.4 against bounded native-API mocks."""
from pathlib import Path
from lupa.lua54 import LuaRuntime

ROOT = Path(__file__).resolve().parents[1]
runtime = LuaRuntime(unpack_returned_tuples=True)
runtime.globals().SOURCE = (ROOT / 'practice/scripts/main.lua').read_text(encoding='utf-8')
runtime.execute(r'''
local function boot()
  local c = { live = {}, made = {}, options = {}, reads = {}, holds = false, held_defeat = false,
              hp = 100, kind = 0, ready = true, actor = true, loaded = {}, notices = 0, heals = 0, messages = {} }
  local handlers, next_id = {}, 0
  local addresses = { [0x0204c242] = 20000, [0x0204c240] = 0, [0x0204c228] = 0,
                      [0x020b2e7c] = 0x20000000, [0x20000004] = 0x20001000,
                      [0x20001020] = 0x20010000 }
  local function read(a)
    c.reads[a] = true
    if a == 0x2001000c then return c.kind end
    if a == 0x020b2e7c and not c.actor then return 0 end
    return addresses[a]
  end
  local env = setmetatable({ scene = {id=2}, mission = {id=3595},
    memory = {u8=read,u16=read,u32=read,s32=read,loaded=function(id) return c.loaded[id] ~= false end},
    mod = { on=function(name,f) handlers[name]=f end,
            option=function(o) o.value=o.default; c.options[o.key]=o; return o end,
            notice=function(message) c.notices=c.notices+1; c.last_notice=message; c.messages[#c.messages+1]=message end },
    player = { max_hp=100, position=function() if c.actor then return 0,.049,0 end end,
      set_hp=function(hp) if c.no_gauge then return nil,'not now' end; c.hp=hp; c.heals=c.heals+1; return true end,
      set_position=function(x,y,z) assert(x==0 and y==.049 and z==0); c.recentred=true; return not c.no_place end,
      take_defeat=function(on) c.held_defeat=on; return true end },
    enemies = { hold_mission=function(on) c.holds=on; return true end,
      kinds=function() local t={}; for _,id in ipairs({0,1,4,41}) do t[#t+1]={kind=id,ready=c.ready} end; return t end,
      list=function() local t={}; for i,v in ipairs(c.live) do t[i]=v end; return t end,
      remove=function(ids) for _,id in ipairs(ids) do for i=#c.live,1,-1 do if c.live[i]==id then table.remove(c.live,i) end end end; return true end,
      make=function(spec)
        c.made[#c.made+1]=spec
        if c.reject == spec.kind then return nil,'no room' end
        assert(spec.count==1 and spec.hp>0 and spec.strength>0 and spec.at[2]==.049)
        next_id=next_id+1; c.live[#c.live+1]=next_id; assert(#c.live<=3); return {next_id}
      end },
  }, {__index=_G})
  assert(load(SOURCE,'practice','t',env))()
  function c.fire(name,e) if handlers[name] then handlers[name](e or {}) end end
  function c.step(n,paused) for _=1,n do c.fire('frame',{paused=paused}) end end
  function c.clear() c.live={} end
  function c.reset() c.options.restart.on_change() end
  function c.choose(value) c.options.targets.value=value; c.options.targets.on_change(value) end
  c.env,c.addresses=env,addresses
  return c
end
local groups=0
local function test(name,f) f(); groups=groups+1; print('PASS '..name) end
test('wrong mission and payload never take control',function()
  for _,which in ipairs({'mission','file'}) do
    local c=boot(); if which=='mission' then c.env.mission.id=1 else c.addresses[0x0204c242]=2 end
    c.fire('mission_start'); c.step(200); assert(not c.holds and not c.held_defeat and #c.made==0 and c.heals==0)
  end
end)
test('network session and unsupported character are refused',function()
  local c=boot(); c.addresses[0x0204c228]=0x20020000; c.addresses[0x20020000]=3
  c.step(200); assert(not c.holds and #c.made==0)
  c=boot(); c.kind=1; c.step(200); assert(not c.holds and #c.made==0)
end)
test('loading waits for a real actor and battle resources',function()
  local c=boot(); c.actor=false; c.loaded[107]=false; c.fire('mission_start'); c.step(200)
  assert(c.holds and c.held_defeat and #c.made==0)
  c.actor=true; c.loaded[107]=true; c.step(60); assert(#c.live==3)
end)
test('pause never advances a pending encounter',function()
  local c=boot(); c.fire('mission_start'); c.step(500,true); assert(#c.made==0)
  c.step(60); assert(#c.live==3)
end)
test('ground encounter uses bounded real native enemy classes',function()
  local c=boot(); c.fire('scene',{to=2}); c.step(60)
  assert(c.holds and c.held_defeat and #c.live==3 and c.made[1].kind==4 and c.made[3].kind==0)
end)
test('cleared encounters cycle ground then air then armor',function()
  local c=boot(); c.step(60); c.clear(); c.step(110)
  assert(#c.live==2 and c.made[#c.made].kind==1)
  c.clear(); c.step(110); assert(#c.live==1 and c.made[#c.made].kind==41)
  c.clear(); c.step(110); assert(#c.live==3 and c.made[#c.made].kind==0)
end)
test('single target option resets and retains one durable target',function()
  local c=boot(); c.step(60); c.choose(2); c.step(110)
  assert(#c.live==1 and c.made[#c.made].kind==4 and c.made[#c.made].hp==8 and c.recentred)
  c.clear(); c.step(110); assert(#c.live==1 and c.made[#c.made].kind==4)
end)
test('manual reset removes the old encounter before spawning again',function()
  local c=boot(); c.step(60); local before=#c.made; c.hp=10; c.reset(); c.step(1)
  assert(#c.live==0 and c.hp==100 and #c.made==before)
  c.step(100); assert(#c.live==3 and c.recentred)
end)
test('held lethal hit recovers health and restarts without game over',function()
  local c=boot(); c.step(60); assert(c.held_defeat); c.hp=1; c.fire('defeat'); c.step(1)
  assert(c.hp==100 and #c.live==0 and c.held_defeat)
  c.step(100); assert(#c.live==3)
end)
test('temporary Drive actor keeps practice active',function()
  local c=boot(); c.step(60); c.kind=19; c.clear(); c.step(110)
  assert(c.holds and c.held_defeat and #c.live==2)
end)
test('partial enemy creation is not duplicated',function()
  local c=boot(); c.reject=0; c.step(300); assert(#c.live==2 and #c.made==3)
end)
test('resource refusal retries finitely and emits one useful error',function()
  local c=boot(); c.ready=false; c.step(1100); assert(#c.live==0 and c.notices==1)
  c.step(1100); assert(c.notices==1)
  c.ready=true; c.reset(); c.step(110); assert(#c.live==3)
end)
test('health gauge and reset placement wait for native readiness',function()
  local c=boot(); c.no_gauge=true; c.step(200); assert(#c.made==0)
  c.no_gauge=false; c.step(1); assert(#c.live==3)
  c.no_place=true; c.reset(); c.step(120); assert(#c.live==0)
  c.no_place=false; c.step(1); assert(#c.live==3)
end)
test('leaving the field cancels pending respawns and releases only practice holds',function()
  local c=boot(); c.step(60); c.reset(); c.fire('mission_end'); c.env.scene.id=19; c.step(300)
  assert(not c.holds and not c.held_defeat and #c.live==0 and #c.made==3)
end)
test('scope change never removes actors belonging to another field or session',function()
  for _,reason in ipairs({'mission','file','network','actor'}) do
    local c=boot(); c.step(60); c.live={999}
    if reason=='mission' then c.env.mission.id=1
    elseif reason=='file' then c.addresses[0x0204c242]=2
    elseif reason=='network' then c.addresses[0x0204c228]=0x20020000; c.addresses[0x20020000]=3
    else c.kind=1 end
    c.step(200)
    assert(not c.holds and not c.held_defeat and #c.live==1 and c.live[1]==999 and #c.made==3)
  end
end)
test('reset retries transient removal before replacing any targets',function()
  for _,partial in ipairs({false,true}) do
    local c=boot(); c.step(60); local remove=c.env.enemies.remove; local attempts=0
    c.env.enemies.remove=function(ids)
      attempts=attempts+1
      if attempts==1 then
        if partial then remove({ids[1]}) end
        return nil,'temporarily busy'
      end
      return remove(ids)
    end
    c.reset(); c.step(1)
    assert(attempts==1 and #c.live==(partial and 2 or 3) and #c.made==3 and not c.recentred)
    c.step(200)
    assert(attempts==2 and #c.live==3 and #c.made==6 and c.live[1]>3)
    assert(c.recentred and c.hp==100 and c.notices==0)
  end
end)
test('persistent reset refusal stops once and manual retry can recover',function()
  local c=boot(); c.step(60); local remove=c.env.enemies.remove; local attempts=0
  c.env.enemies.remove=function() attempts=attempts+1; return nil,'temporarily busy' end
  c.reset(); c.step(2000)
  assert(attempts>1 and attempts<=20 and c.notices==1 and #c.made==3 and #c.live==3)
  assert(c.last_notice:find('Restart encounter',1,true))
  local stopped=attempts; c.step(2000); assert(attempts==stopped and c.notices==1)
  c.env.enemies.remove=remove; c.reset(); c.step(200)
  assert(#c.live==3 and #c.made==6 and c.recentred and c.notices==2 and c.last_notice:find('recovered',1,true))
end)
test('pending reset retries respect pause readiness and field ownership',function()
  for _,gate in ipairs({'pause','actor','resources','position','scope'}) do
    local c=boot(); c.step(60); local remove=c.env.enemies.remove; local attempts=0
    local position=c.env.player.position
    c.env.enemies.remove=function() attempts=attempts+1; return nil,'temporarily busy' end
    c.reset(); c.step(1); assert(attempts==1)
    if gate=='actor' then c.actor=false
    elseif gate=='resources' then c.loaded[107]=false
    elseif gate=='position' then c.env.player.position=function() return nil end
    elseif gate=='scope' then c.env.mission.id=1 end
    c.step(300,gate=='pause'); assert(attempts==1 and #c.made==3 and c.notices==0)
    if gate=='scope' then
      assert(not c.holds and not c.held_defeat and #c.live==3)
    else
      c.actor=true; c.loaded[107]=true; c.env.player.position=position; c.env.enemies.remove=remove
      c.step(200); assert(#c.live==3 and #c.made==6 and c.recentred)
    end
  end
end)
test('combat controls are on demand complete and leave the arena unchanged',function()
  local c=boot(); c.step(60)
  assert(c.options.controls and c.options.controls.kind=='button')
  for _,text in ipairs({'Attack','X','Jump','Defense','Select','game mapping'}) do
    assert(c.options.controls.description:find(text,1,true),text)
  end
  assert(c.notices==0)
  local before_made,before_heals=#c.made,c.heals
  local pages={}
  for i=1,4 do
    c.options.controls.on_change()
    assert(c.notices==i and #c.live==3 and #c.made==before_made and c.heals==before_heals)
    assert(#c.last_notice<=100 and not c.last_notice:find('[\r\n]'))
    assert(not c.last_notice:find('[^ -~]')) -- short ASCII lines fit native timed notices
    pages[#pages+1]=c.last_notice
  end
  local guide=table.concat(pages,'\n')
  for _,text in ipairs({'Light','Technique','target','Rush','Burst','Flurry','Jump','Enemy Step','Defense','Select','Magic'}) do
    assert(guide:find(text,1,true),text)
  end
  assert(guide:find('rises',1,true) and guide:find('airborne',1,true))
  c.options.controls.on_change(); assert(c.last_notice==pages[1])
  c.step(200); assert(c.notices==5) -- no automatic help or frame spam
end)
test('encounter status reports current phase and preset without changing play',function()
  local c=boot(); assert(c.options.status and c.options.status.kind=='button')
  c.options.status.on_change(); assert(c.last_notice:find('preparing',1,true))
  c.step(60); local made,heals=#c.made,c.heals
  c.options.status.on_change()
  assert(c.last_notice:find('Ground',1,true) and c.last_notice:find('3',1,true))
  assert(#c.made==made and c.heals==heals and #c.live==3)
  c.choose(3); c.options.status.on_change(); assert(c.last_notice:find('queued',1,true))
  c.step(110); c.options.status.on_change(); assert(c.last_notice:find('Aerial',1,true))
  c.env.mission.id=1; c.options.status.on_change(); assert(c.last_notice:find('inactive',1,true))
  c.step(1); assert(not c.holds and not c.held_defeat and #c.live==2)
end)
test('failure recovery is announced once after targets are genuinely ready',function()
  local c=boot(); c.ready=false; c.step(1100); assert(c.notices==1)
  c.options.status.on_change(); assert(c.last_notice:find('Restart encounter',1,true))
  c.reset(); c.step(200); assert(#c.live==0 and c.notices==2)
  c.ready=true; c.step(200)
  assert(#c.live==3 and c.notices==3 and c.last_notice:find('recovered',1,true))
  c.step(400); assert(c.notices==3)
  c.clear(); c.step(110); assert(#c.live==2 and c.notices==3)
end)
test('mixed encounter flow can repeat drills then resume advancing',function()
  local c=boot(); assert(c.options.flow.value==1)
  c.options.flow.value=2; c.step(60); c.clear(); c.step(110)
  assert(#c.live==3 and c.made[#c.made].kind==0)
  c.options.status.on_change(); assert(c.last_notice:find('Repeating',1,true))
  c.options.flow.value=1; local count=#c.made; c.step(1)
  assert(#c.live==3 and #c.made==count) -- changing flow does not interrupt combat
  c.clear(); c.step(110); assert(#c.live==2 and c.made[#c.made].kind==1)
  c.options.flow.value=2; c.clear(); c.step(110)
  assert(#c.live==2 and c.made[#c.made].kind==1)
end)
test('next mixed group queues one safe reset and works in repeat mode',function()
  local c=boot(); c.step(60); c.options.flow.value=2; c.hp=10
  c.options.next_group.on_change(); c.options.status.on_change()
  assert(c.last_notice:find('queued',1,true) and #c.live==3 and #c.made==3)
  c.step(300,true); assert(#c.live==3 and #c.made==3 and c.hp==10)
  c.step(110); assert(#c.live==2 and #c.made==5 and c.hp==100 and c.recentred)
  c.options.next_group.on_change(); c.step(110)
  assert(#c.live==1 and #c.made==6 and c.made[6].kind==41)
  c.options.next_group.on_change(); c.step(110)
  assert(#c.live==3 and #c.made==9 and c.made[9].kind==0)
end)
test('mixed flow actions preserve fixed presets and out of scope actors',function()
  for _,preset in ipairs({2,3,4}) do
    local c=boot(); c.choose(preset); c.step(110); c.options.flow.value=2
    local count=#c.made; c.options.next_group.on_change(); c.step(1)
    assert(#c.made==count and c.last_notice:find('Mixed encounters',1,true))
    c.clear(); c.step(110)
    assert(c.made[#c.made].kind==({4,1,41})[preset-1])
  end
  local c=boot(); c.options.next_group.on_change(); assert(#c.made==0 and c.notices==1)
  c.step(60); c.env.mission.id=1; c.live={999}; c.options.next_group.on_change()
  c.step(110); assert(#c.live==1 and c.live[1]==999 and #c.made==3 and not c.holds)
end)
test('optional clear arena waits without healing and restarts the planned drill',function()
  for _,repeat_group in ipairs({false,true}) do
    local c=boot(); assert(c.options.after_clear.value==1)
    c.options.after_clear.value=2; c.options.flow.value=repeat_group and 2 or 1
    c.step(60); c.hp=37; local heals=c.heals
    c.clear(); c.step(1000)
    assert(#c.live==0 and #c.made==3 and c.hp==37 and c.heals==heals and not c.recentred)
    assert(c.notices==1 and c.holds and c.held_defeat)
    c.options.status.on_change()
    assert(c.last_notice:find('Restart encounter',1,true) and #c.last_notice<=100)
    assert(c.last_notice:find(repeat_group and 'Ground' or 'Aerial',1,true))
    c.reset(); c.step(110)
    assert(#c.live==(repeat_group and 3 or 2) and c.hp==100 and c.recentred)
    assert(c.made[#c.made].kind==(repeat_group and 0 or 1))
  end
end)
test('clear arena next action and automatic resume preserve the next group and delay',function()
  for _,repeat_group in ipairs({false,true}) do
    local c=boot(); c.options.after_clear.value=2; c.options.flow.value=repeat_group and 2 or 1
    c.step(60); c.clear(); c.step(1)
    c.options.next_group.on_change(); c.step(300,true)
    assert(#c.live==0 and #c.made==3 and not c.recentred)
    c.step(110); assert(#c.live==2 and #c.made==5 and c.made[5].kind==1)
  end
  local c=boot(); c.options.after_clear.value=2; c.step(60); c.clear(); c.step(300)
  c.options.after_clear.value=1; c.step(300,true); assert(#c.made==3)
  c.step(1); c.step(90); assert(#c.live==0 and #c.made==3)
  c.step(1); assert(#c.live==2 and c.made[#c.made].kind==1)
end)
test('clear arena preserves fixed drills and releases ownership on a scope change',function()
  for _,preset in ipairs({2,3,4}) do
    local c=boot(); c.options.after_clear.value=2; c.choose(preset); c.step(110)
    local count=#c.made; c.clear(); c.step(500); assert(#c.made==count and #c.live==0)
    c.reset(); c.step(110)
    assert(c.made[#c.made].kind==({4,1,41})[preset-1] and c.recentred)
  end
  local c=boot(); c.options.after_clear.value=2; c.step(60); c.clear(); c.step(1)
  c.env.mission.id=1; c.live={999}; c.step(300)
  assert(not c.holds and not c.held_defeat and #c.made==3 and c.live[1]==999)
end)
test('manual drill clear announces the planned group once after play resumes',function()
  for preset=1,4 do
    local c=boot(); c.options.after_clear.value=2; c.choose(preset); c.step(110)
    assert(c.notices==0)
    c.hp=37; local heals=c.heals; c.clear(); c.step(300,true)
    assert(c.notices==0)
    c.step(1)
    assert(c.notices==1 and c.hp==37 and c.heals==heals and #c.live==0)
    assert(c.last_notice:find('Clear.',1,true) and c.last_notice:find('Restart encounter',1,true))
    assert(c.last_notice:find(({'Aerial encounter','Single ground target','Aerial encounter','Armored target'})[preset],1,true))
    assert(#c.last_notice<=100 and not c.last_notice:find('[^ -~]'))
    local notice=c.last_notice; c.step(1000); assert(c.notices==1 and #c.live==0)
    c.options.status.on_change(); assert(c.notices==2 and c.last_notice==notice)
  end
end)
test('automatic drills and manual resets do not emit completion cues',function()
  local c=boot(); c.step(60)
  for _=1,4 do c.clear(); c.step(110) end
  assert(c.notices==0)
  c.options.after_clear.value=2; c.reset(); c.step(110); assert(c.notices==0)
  c.clear(); c.step(1); assert(c.notices==1)
  c.reset(); c.step(110); assert(c.notices==1)
  c.clear(); c.step(1); assert(c.notices==2)
  c.env.mission.id=1; c.step(300); assert(c.notices==2 and not c.holds)
end)
test('extended combo targets apply on the next spawn without interrupting a drill',function()
  for preset=1,4 do
    local c=boot(); assert(c.options.durability.value==1)
    c.choose(preset); c.step(110)
    local count,live,heals=#c.made,#c.live,c.heals
    local standard_hp=c.made[count].hp
    c.options.durability.value=2; c.step(30)
    assert(#c.made==count and #c.live==live and c.heals==heals)
    c.options.status.on_change(); assert(not c.last_notice:find('3x preset HP',1,true))
    c.reset(); c.step(110)
    assert(#c.live==live and c.made[#c.made].hp==standard_hp*3)
    for n=count+1,#c.made do assert(c.made[n].strength==.6 and c.made[n].count==1 and c.made[n].level==0) end
    c.options.status.on_change(); assert(c.last_notice:find('3x preset HP',1,true) and #c.last_notice<=100)
    c.options.durability.value=1; c.reset(); c.step(110)
    assert(c.made[#c.made].hp==standard_hp)
  end
end)
test('restart-only HP recovery carries damage across groups and repeat drills',function()
  for repeat_group=1,2 do
    local c=boot(); assert(c.options.hp_recovery.value==1)
    c.options.hp_recovery.value=2; c.options.flow.value=repeat_group; c.hp=5; c.step(60)
    assert(c.hp==100) -- initial practice entry still starts ready
    c.hp=37; local heals=c.heals; c.clear(); c.step(110)
    assert(c.hp==37 and c.heals==heals and #c.live==(repeat_group==2 and 3 or 2))
    c.options.status.on_change(); assert(c.last_notice:find('HP: restart only',1,true))
    c.hp=23; c.clear(); c.step(110); assert(c.hp==23 and c.heals==heals)
    c.reset(); c.step(110); assert(c.hp==100 and c.recentred)
    c.hp=12; c.options.next_group.on_change(); c.step(110); assert(c.hp==100)
    c.hp=1; c.fire('defeat'); c.step(110); assert(c.hp==100 and c.held_defeat)
  end
end)
test('HP policy changes defer to the next encounter and keep default recovery',function()
  local c=boot(); c.step(60); c.hp=31; local heals=c.heals
  c.options.hp_recovery.value=2; c.step(30); assert(c.hp==31 and c.heals==heals and #c.live==3)
  c.options.hp_recovery.value=1; c.step(1); assert(c.hp==31)
  c.clear(); c.step(110); assert(c.hp==100 and c.heals>heals)
  c.options.hp_recovery.value=2; c.options.after_clear.value=2
  c.hp=29; heals=c.heals; c.clear(); c.step(500)
  assert(c.hp==29 and c.heals==heals and #c.live==0)
  c.options.after_clear.value=1; c.step(110); assert(c.hp==29 and #c.live==1)
end)
test('extended defense drills remain bounded during pause and leave foreign actors alone',function()
  local c=boot(); c.options.durability.value=2; c.options.hp_recovery.value=2; c.options.flow.value=2
  c.step(60); c.hp=41; local count,heals=#c.made,c.heals
  c.options.status.on_change()
  assert(c.last_notice:find('Repeating',1,true) and c.last_notice:find('3x preset HP',1,true))
  assert(c.last_notice:find('HP: restart only',1,true) and #c.last_notice<=100 and not c.last_notice:find('[^ -~]'))
  c.clear(); c.step(500,true); assert(#c.made==count and c.hp==41 and c.heals==heals)
  c.step(110); assert(#c.live==3 and c.hp==41 and c.made[#c.made].hp==12)
  c.env.mission.id=1; c.live={999}; c.reset(); c.step(300)
  assert(c.live[1]==999 and c.hp==41 and c.heals==heals and not c.holds)
end)
print(('%d/%d practice Lua groups passed'):format(groups,groups))
''')
