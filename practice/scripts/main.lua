-- Loaded only by the launcher's separate practice profile. Native 0.1.5 ABI reads are guards only.
local MISSION, FILE = 3595, 20000
local HOME = { 0, 0.049, 0 }
local GROUPS = {
  { { 4, -2, -3, 3.5 }, { 4, 2, -3, 3.5 }, { 0, 0, -5, 4 } },
  { { 1, -2, -3, 4 }, { 1, 2, -3, 4 } },
  { { 41, 0, -4, 1.5 } },
  { { 4, 0, -3, 8 } },
}
local GROUP_NAMES = { "Ground encounter", "Aerial encounter", "Armored target", "Single ground target" }
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
  run = { phase = "waiting", ticks = 45, cycle = 1, failures = 0 }
end

local mode = mod.option{
  key = "targets", kind = "choice", label = "Target preset", default = 1,
  values = { "Mixed encounters", "Single ground target", "Aerial targets", "Armored target" },
  description = "Mixed follows Encounter flow through ground, aerial and armored groups. Changing the preset restarts this encounter.",
  on_change = function() reset_requested = true end,
}
local flow = mod.option{
  key = "flow", kind = "choice", label = "Encounter flow", default = 1,
  values = { "Advance on clear", "Repeat current group" },
  description = "Mixed preset only: advance after each clear, or repeat the current group for a focused drill. Changes apply at the next clear.",
}
local after_clear = mod.option{
  key = "after_clear", kind = "choice", label = "After clearing", default = 1,
  values = { "Start next encounter", "Wait for Restart" },
  description = "Start another encounter automatically, or leave the arena clear until Restart encounter. Switching back resumes the normal delay.",
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
  description = "Restore health and starting position, then replace the targets. Keep the selected preset.",
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
      local repeating = mode.value == 1 and flow.value == 2 and " Repeating." or ""
      mod.notice(GROUP_NAMES[group_index()] .. ". Targets remaining: " .. #enemies.list() .. "." .. repeating)
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
  heal()
  clear_encounter()
end

local function spawn()
  local ready = {}
  for _, entry in ipairs(enemies.kinds("all")) do ready[entry.kind] = entry.ready end
  local group = GROUPS[group_index()]
  for _, target in ipairs(group) do
    if not ready[target[1]] then return false end
  end
  local made = 0
  for _, target in ipairs(group) do
    local handles = enemies.make{
      kind = target[1], count = 1, at = { target[2], HOME[2], target[3] },
      level = 0, hp = target[4], strength = 0.6,
    }
    if handles ~= nil then made = made + #handles end
  end
  -- A partially filled group is played as it stands; retrying it would duplicate its live members.
  return made > 0
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
  if player.position() == nil then return end
  if reset_requested then reset_encounter() end
  if run.phase == "fighting" then
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
    run.phase = "waiting"
  end
  if #enemies.list() ~= 0 or not heal() then return end
  if spawn() then
    run.phase, run.failures = "fighting", 0
    if run.recovering then
      mod.notice("Practice recovered. " .. GROUP_NAMES[group_index()] .. " ready.")
      run.recovering = false
    end
  else
    run.failures, run.ticks = run.failures + 1, 30
    if run.failures >= 20 then
      run.phase = "failed"
      mod.notice("Practice targets could not load. Choose Restart encounter to try again.")
    end
  end
end)
