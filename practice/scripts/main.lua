-- Loaded only by the launcher's separate practice profile. Native 0.1.5 ABI reads are guards only.
local MISSION, FILE = 3595, 20000
local HOME = { 0, 0.049, 0 }
local GROUPS = {
  { { 4, -2, -3, 3.5 }, { 4, 2, -3, 3.5 }, { 0, 0, -5, 4 } },
  { { 1, -2, -3, 4 }, { 1, 2, -3, 4 } },
  { { 41, 0, -4, 1.5 } },
  { { 4, 0, -3, 8 } },
}
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
  key = "targets", kind = "choice", label = "Practice targets", default = 1,
  values = { "Mixed encounters", "Single ground target", "Aerial targets", "Armored target" },
  description = "Mixed encounters cycle through ground, aerial and armored enemies.",
  on_change = function() reset_requested = true end,
}
mod.option{
  key = "restart", kind = "button", label = "Restart encounter",
  description = "Restore health, return to the start and replace the current practice enemies.",
  on_change = function() reset_requested = true end,
}

local function group_index()
  if mode.value == 2 then return 4 end
  if mode.value == 3 then return 2 end
  if mode.value == 4 then return 3 end
  return run.cycle
end

local function heal()
  local maximum = player.max_hp
  return maximum ~= nil and maximum > 0 and player.set_hp(maximum)
end

local function reset_encounter()
  enemies.remove(enemies.list())
  run.phase, run.ticks, run.failures = "reset", 90, 0
  reset_requested = false
  heal()
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
      run.cycle = run.cycle % 3 + 1
      run.phase, run.ticks = "waiting", 90
    end
    return
  end
  if run.phase == "failed" then return end
  if run.ticks > 0 then run.ticks = run.ticks - 1; return end
  if run.phase == "reset" then
    if not heal() or not player.set_position(HOME[1], HOME[2], HOME[3]) then return end
    run.phase = "waiting"
  end
  if #enemies.list() ~= 0 or not heal() then return end
  if spawn() then
    run.phase, run.failures = "fighting", 0
  else
    run.failures, run.ticks = run.failures + 1, 30
    if run.failures >= 20 then
      run.phase = "failed"
      mod.notice("Practice targets could not load. Reopen Practice from the launcher.")
    end
  end
end)
