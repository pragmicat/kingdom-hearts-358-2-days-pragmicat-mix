-- Loaded only by the launcher's separate practice profile. Native 0.1.5 ABI reads are guards only.
local MISSION, FILE = 3595, 20000
local HOME = { 0, 0.049, 0 }
local GROUND = { { 4, -2, -3, 3.5 }, { 4, 2, -3, 3.5 }, { 0, 0, -5, 4 } }
local AERIAL = { { 1, -2, -3, 4 }, { 1, 2, -3, 4 } }
local GROUPS = {
  GROUND, AERIAL,
  { { 41, 0, -4, 1.5 } },
  { { 4, 0, -3, 8 } },
  { GROUND[1], AERIAL[2], GROUND[3] },
}
local GROUP_NAMES = { "Ground encounter", "Aerial encounter", "Armored target", "Single ground target", "Ground and air" }
local ENDLESS_GROUPS = { 1, 2, 3, 5 }
local ENDLESS_PADS = { GROUND[1], GROUND[2], GROUND[3], GROUPS[3][1] }
local CONTROL_PAGES = {
  "1/4 ATTACKS: Attack=Light. X+direction=Technique; directions follow your target.",
  "2/4 FINISHERS: Hold Light, press Technique: Toward=Rush; Away=Burst; neutral/side=Flurry.",
  "3/4 AIR: Ground Away+X: launch, Jump as enemy rises. Air hit+Jump near airborne foe: Enemy Step.",
  "4/4 TOOLS: Defense=dodge/guard. Select=ready Drive/return. Magic=normal spells; Cure not refilled.",
}
local control_page = 1
local run, reset_requested

local function actor_kind()
  if not memory.loaded(22) then return nil end
  local base = memory.u32(0x020b2e7c)
  if base == nil or base == 0 then return nil end
  local entry = memory.u32(base + 4)
  if entry == nil or entry == 0 then return nil end
  local actor = memory.u32(entry + 0x20)
  if actor == nil or actor == 0 then return nil end
  return memory.s32(actor + 0xc)
end

local function belongs_here()
  if scene.id ~= 2 or mission.id ~= MISSION or memory.u16(0x0204c242) ~= FILE then return false end
  if memory.u8(0x0204c240) ~= 0 then return false end
  -- Session_IsActive: a present GameSession whose state differs from 1 is a network session.
  local session = memory.u32(0x0204c228)
  if session == nil or (session ~= 0 and memory.s32(session) ~= 1) then return false end
  local kind = actor_kind()
  return kind == nil or kind == 0 or kind == 19 -- normal Roxas, or his temporary Drive actor
end

local function release()
  if run == nil then return end
  if belongs_here() then enemies.remove(enemies.list()) end
  enemies.hold_mission(false)
  player.take_defeat(false)
  run, reset_requested = nil, false
end

local function arm()
  if run ~= nil or not belongs_here() then return end
  local held = enemies.hold_mission(true)
  if not held then return end
  if not player.take_defeat(true) then
    enemies.hold_mission(false)
    return
  end
  run = { phase = "waiting", ticks = 45, cycle = 1, failures = 0, restore_hp = true,
          wave = 1, cleared = 0, endless_spawned = 0, endless_targets = {}, refill_ticks = 30 }
end

local mode = mod.option{
  key = "targets", kind = "choice", label = "Target preset", default = 1,
  values = { "Mixed encounters", "Single ground target", "Aerial targets", "Armored target", "Endless Heartless" },
  description = "Endless cycles ground, aerial, armored, then combined ground-and-air waves. At most three enemies stay active. Changing the preset restarts this encounter.",
  on_change = function() reset_requested = true end,
}
local durability = mod.option{
  key = "durability", kind = "choice", label = "Target durability", default = 1,
  values = { "Standard", "Extended combos (3x HP)" },
  description = "Extended combos gives targets three times this preset's usual HP, with the same attack strength and enemy count. Applies to the next encounter; Restart applies it now.",
}
local recovery = mod.option{
  key = "hp_recovery", kind = "choice", label = "HP recovery", default = 1,
  values = { "Between encounters", "On restart only" },
  description = "Restore health between encounters/endless waves, or carry damage for defense drills. Restart, next-group reset and defeat recovery always restore health; endless replacements do not heal.",
}
local flow = mod.option{
  key = "flow", kind = "choice", label = "Encounter flow", default = 1,
  values = { "Advance on clear", "Repeat current group" },
  description = "Mixed preset only: advance after each clear, or repeat the current group for a focused drill. Changes apply at the next clear.",
}
local after_clear = mod.option{
  key = "after_clear", kind = "choice", label = "After clearing", default = 1,
  values = { "Start next encounter", "Wait for Restart" },
  description = "Start another encounter automatically, or wait for Restart after a clear. Endless Heartless always continues; use another preset for paused drills.",
}
mod.option{
  key = "next_group", kind = "button", label = "Next mixed group",
  description = "Mixed preset only: queue the next group, or start the group already planned after a clear. Health and position reset when play resumes.",
  on_change = function()
    if not belongs_here() or run == nil then
      mod.notice("Combat Practice is preparing or inactive. Wait for the arena to load.")
    elseif mode.value ~= 1 then
      mod.notice("Next mixed group: choose the Mixed encounters target preset first.")
    else
      -- A clear may already have advanced the planned group. Starting it must
      -- not skip another group; repeat mode still allows an explicit skip.
      if run.phase ~= "cleared" or not run.advanced_on_clear then
        run.cycle = run.cycle % 3 + 1
      end
      run.advanced_on_clear = false
      reset_requested = true
    end
  end,
}
mod.option{
  key = "restart", kind = "button", label = "Restart encounter",
  description = "Restore health and starting position, then replace the targets. Endless Heartless starts a fresh run at wave 1; other presets stay selected.",
  on_change = function() reset_requested = true end,
}
mod.option{
  key = "controls", kind = "button", label = "Combat controls",
  description = "Attack=Light; X=Technique; hold Attack then X=finisher; Jump=air moves; Defense=dodge/guard; Select=Drive. Use your game mapping. Press again for the next help page.",
  on_change = function()
    mod.notice(CONTROL_PAGES[control_page])
    control_page = control_page % #CONTROL_PAGES + 1
  end,
}

local function group_index()
  if mode.value == 2 then return 4 end
  if mode.value == 3 then return 2 end
  if mode.value == 4 then return 3 end
  if mode.value == 5 then return ENDLESS_GROUPS[(run.wave - 1) % #ENDLESS_GROUPS + 1] end
  return run.cycle
end
local function cleared_notice()
  mod.notice("Clear. Next: " .. GROUP_NAMES[group_index()] .. ". Choose Restart encounter when ready.")
end

mod.option{
  key = "status", kind = "button", label = "Encounter status",
  description = "Show the current encounter, remaining targets or reset progress without interrupting practice.",
  on_change = function()
    if not belongs_here() then
      mod.notice("Combat Practice is inactive. Open Combat Practice from the launcher.")
    elseif run == nil or actor_kind() == nil or not memory.loaded(2) or not memory.loaded(107) then
      mod.notice("Combat Practice is preparing. Wait for Roxas and the arena to load.")
    elseif reset_requested then
      mod.notice("Encounter reset queued. Practice will restart when play resumes.")
    elseif run.phase == "fighting" then
      if mode.value == 5 then
        local targets = run.hp_scale == 3 and " 3x HP." or ""
        local healing = recovery.value == 2 and " No wave heal." or ""
        mod.notice("Endless wave " .. run.wave .. ". Active: " .. #enemies.list() ..
                   ". Cleared: " .. run.cleared .. ". " .. GROUP_NAMES[group_index()] .. "." .. targets .. healing)
        return
      end
      local repeating = mode.value == 1 and flow.value == 2 and " Repeating." or ""
      local targets = run.hp_scale == 3 and " 3x preset HP." or ""
      local healing = recovery.value == 2 and " HP: restart only." or ""
      mod.notice(GROUP_NAMES[group_index()] .. ". Targets remaining: " .. #enemies.list() .. "." .. repeating .. targets .. healing)
    elseif run.phase == "failed" then
      mod.notice("Practice needs a retry. Choose Restart encounter to try again.")
    elseif run.phase == "cleared" then
      cleared_notice()
    elseif run.phase == "clearing" then
      mod.notice("Resetting practice. Waiting for the current targets to clear before replacing them.")
    elseif run.phase == "reset" then
      mod.notice("Resetting practice. Restoring health and starting position before the next encounter.")
    else
      mod.notice("Next: " .. GROUP_NAMES[group_index()] .. ". Waiting for the arena to be ready.")
    end
  end,
}

local function heal()
  local maximum = player.max_hp
  return maximum ~= nil and maximum > 0 and player.set_hp(maximum)
end

local function clear_encounter()
  local targets = enemies.list()
  if #targets > 0 then enemies.remove(targets) end
  -- Native removal may be temporarily refused. Verify the live list before
  -- recentering or spawning; a reset must never duplicate surviving targets.
  if #enemies.list() == 0 then
    run.phase, run.ticks, run.failures = "reset", 90, 0
    return
  end
  run.failures, run.ticks = run.failures + 1, 30
  if run.failures >= 20 then
    run.phase = "failed"
    mod.notice("Practice targets could not reset. Choose Restart encounter to try again.")
  end
end

local function reset_encounter()
  run.recovering = run.recovering or run.phase == "failed"
  run.phase, run.ticks, run.failures = "clearing", 0, 0
  reset_requested = false
  run.restore_hp = true
  if mode.value == 5 then
    run.cycle, run.wave, run.cleared = 1, 1, 0
  end
  run.endless_spawned, run.endless_targets = 0, {}
  run.refill_ticks, run.endless_failures = 30, 0
  heal()
  clear_encounter()
end

local function endless_pad(preferred, nearby)
  local function clearance(pad)
    local nearest = math.huge
    for _, actor in ipairs(nearby) do
      local dx, dz = pad[2] - actor.x, pad[3] - actor.z
      nearest = math.min(nearest, dx * dx + dz * dz)
    end
    return nearest
  end
  local best, distance = preferred, clearance(preferred)
  -- Keep the authored lane whenever there are two floor units of space.
  if distance >= 4 then return best end
  for _, pad in ipairs(ENDLESS_PADS) do
    local space = clearance(pad)
    if space > distance then best, distance = pad, space end
  end
  return best
end

local function make_target(target, hp_scale, nearby)
  local pad = nearby and endless_pad(target, nearby) or target
  local handles = enemies.make{
    kind = target[1], count = 1, at = { pad[2], HOME[2], pad[3] },
    level = 0, hp = target[4] * hp_scale, strength = 0.6,
  }
  if nearby and handles and #handles > 0 then
    -- The other arrivals in this batch have yet to move from their pads.
    nearby[#nearby + 1] = { x = pad[2], z = pad[3] }
  end
  return handles
end

local function spawn(x, z)
  local ready = {}
  for _, entry in ipairs(enemies.kinds("all")) do ready[entry.kind] = entry.ready end
  local group = GROUPS[group_index()]
  local hp_scale = durability.value == 2 and 3 or 1
  for _, target in ipairs(group) do
    if not ready[target[1]] then return false end
  end
  local made = 0
  local nearby = mode.value == 5 and { { x = x, z = z } } or nil
  for slot, target in ipairs(group) do
    local handles = make_target(target, hp_scale, nearby)
    if handles ~= nil then made = made + #handles end
    if mode.value == 5 and handles ~= nil then
      for _, handle in ipairs(handles) do run.endless_targets[handle] = slot end
    end
  end
  if made > 0 then
    run.hp_scale = hp_scale
    if mode.value == 5 then
      run.endless_spawned = made
      run.refill_slot = 1
    end
  end
  -- A partially filled group is played as it stands; retrying it would duplicate its live members.
  return made > 0
end

local function step_endless(x, z)
  local targets, live, occupied = enemies.list(), {}, {}
  for _, handle in ipairs(targets) do live[handle] = true end
  for handle, slot in pairs(run.endless_targets) do
    if live[handle] then occupied[slot] = true
    else
      run.endless_targets[handle] = nil
      run.cleared = run.cleared + 1
    end
  end
  local group = GROUPS[group_index()]
  -- Each wave plays two sets of its native group. Individual losses refill
  -- while the other enemies remain alive; no survivor is removed or duplicated.
  if run.endless_spawned >= #group * 2 and #targets == 0 then
    local finished = run.wave
    run.wave = run.wave + 1
    run.cycle = run.cycle % 3 + 1 -- preserve the Mixed drill selected when leaving Endless
    mod.notice("Wave " .. finished .. " clear! Wave " .. run.wave .. ": " .. GROUP_NAMES[group_index()] .. ".")
    run.phase, run.ticks = "waiting", 30
    run.endless_spawned, run.endless_targets = 0, {}
    run.refill_ticks, run.endless_failures = 30, 0
    return
  end
  if run.refill_ticks > 0 then run.refill_ticks = run.refill_ticks - 1; return end
  if #targets >= #group or run.endless_spawned >= #group * 2 then return end
  local ready = {}
  for _, entry in ipairs(enemies.kinds("all")) do ready[entry.kind] = entry.ready end
  local made = false
  for offset = 0, #group - 1 do
    local slot = (run.refill_slot + offset - 1) % #group + 1
    local target = group[slot]
    if not occupied[slot] and ready[target[1]] then
      -- Rotate after a refusal so an available slot still gets a turn.
      run.refill_slot = slot % #group + 1
      local nearby = { { x = x, z = z } }
      for _, handle in ipairs(targets) do
        local actor = enemies.info(handle)
        if actor then nearby[#nearby + 1] = actor end
      end
      local handles = make_target(target, run.hp_scale, nearby)
      if handles ~= nil and #handles > 0 then
        for _, handle in ipairs(handles) do run.endless_targets[handle] = slot end
        run.endless_spawned = run.endless_spawned + #handles
        made = true
      end
      break -- at most one replacement attempt per interval
    end
  end
  run.refill_ticks = 30
  if made then run.endless_failures = 0
  else
    run.endless_failures = (run.endless_failures or 0) + 1
    if run.endless_failures >= 20 then
      run.phase = "failed"
      mod.notice("Endless Heartless needs a retry. Choose Restart encounter to start a fresh run.")
    end
  end
end

mod.on("mission_start", function()
  release()
  arm()
end)
mod.on("scene", function(e)
  if e.to == 2 then arm() else release() end
end)
mod.on("mission_end", release)
mod.on("defeat", function()
  if run ~= nil and belongs_here() then reset_requested = true end
end)

mod.on("frame", function(e)
  if not belongs_here() then release(); return end
  if run == nil then arm() end
  if run == nil or e.paused or actor_kind() == nil then return end
  if not memory.loaded(2) or not memory.loaded(107) then return end
  local x, _, z = player.position()
  if x == nil then return end
  if reset_requested then reset_encounter() end
  if run.phase == "fighting" then
    if mode.value == 5 then step_endless(x, z); return end
    if #enemies.list() == 0 then
      run.advanced_on_clear = mode.value == 1 and flow.value ~= 2
      if run.advanced_on_clear then run.cycle = run.cycle % 3 + 1 end
      if after_clear.value == 2 then
        run.phase, run.ticks = "cleared", 0
        cleared_notice()
      else
        run.phase, run.ticks = "waiting", 90
      end
    end
    return
  end
  if run.phase == "failed" then return end
  if run.phase == "cleared" then
    if after_clear.value ~= 2 then run.phase, run.ticks = "waiting", 90 end
    return
  end
  if run.ticks > 0 then run.ticks = run.ticks - 1; return end
  if run.phase == "clearing" then clear_encounter(); return end
  if run.phase == "reset" then
    if not heal() or not player.set_position(HOME[1], HOME[2], HOME[3]) then return end
    x, z = HOME[1], HOME[3]
    run.phase = "waiting"
  end
  if #enemies.list() ~= 0 then return end
  if (recovery.value ~= 2 or run.restore_hp) and not heal() then return end
  if spawn(x, z) then
    run.phase, run.failures = "fighting", 0
    run.restore_hp = false
    if mode.value == 5 and run.wave == 1 then
      local prefix = run.recovering and "Practice recovered. " or ""
      mod.notice(prefix .. "Wave 1: " .. GROUP_NAMES[group_index()] .. ".")
    elseif run.recovering then
      mod.notice("Practice recovered. " .. GROUP_NAMES[group_index()] .. " ready.")
    end
    run.recovering = false
  else
    run.failures, run.ticks = run.failures + 1, 30
    if run.failures >= 20 then
      run.phase = "failed"
      mod.notice("Practice targets could not load. Choose Restart encounter to try again.")
    end
  end
end)
