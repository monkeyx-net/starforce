-- MAME probe: an instruction trace of one CPU over chosen frames of a
-- scripted run (needs -debug -debugger none). Only dumps; test_emu compares.
--
--   PROBE_OUT=<dir> TRACE_FROM=100 TRACE_TO=200 TRACE_CPU=sub \
--   PROBE_SCRIPT="300:coin ..." mame starforc -debug -debugger none -autoboot_script trace_probe.lua
--
-- TRACE_CPU is maincpu (the default) or sub: one a run, as MAME's tracelog
-- writes to the debugger's visible CPU's trace. Writes <dir>/<cpu>.tr, a
-- line per instruction: "<total cycles> <registers> <PC>: <disassembly>",
-- the registers AF BC DE HL SP IX IY. TRACE_BEAM=1 adds the beam's line and
-- pixel first. TRACE_FROM 0 starts at power-on. Traces hold disassembled
-- ROM code: they stay in the build folder, never commit them. Inputs as in
-- capture_probe.lua.

local out_dir = os.getenv("PROBE_OUT") or "."
local from = tonumber(os.getenv("TRACE_FROM") or "100")
local to = tonumber(os.getenv("TRACE_TO") or "200")
local cpu = os.getenv("TRACE_CPU") or "maincpu"
local LOG = '{tracelog "%d %04X %04X %04X %04X %04X %04X %04X ",totalcycles,af,bc,de,hl,sp,ix,iy}'
if os.getenv("TRACE_BEAM") == "1" then
    LOG = '{tracelog "%d %d %d %04X %04X %04X %04X %04X %04X %04X ",beamy,beamx,totalcycles,af,bc,de,hl,sp,ix,iy}'
end

local script = {}
for f, what in string.gmatch(os.getenv("PROBE_SCRIPT") or "", "(%d+):([%w,]*)") do
    table.insert(script, { frame = tonumber(f), inputs = what })
end
local fields = {}
for _, port in pairs(manager.machine.ioport.ports) do
    for name, field in pairs(port.fields) do fields[name] = field end
end
local NAMES = { coin = "Coin 1", coin2 = "Coin 2", start = "1 Player Start", start2 = "2 Players Start",
                left = "P1 Left", right = "P1 Right", up = "P1 Up", down = "P1 Down", fire = "P1 Button 1",
                left2 = "P2 Left", right2 = "P2 Right", up2 = "P2 Up", down2 = "P2 Down", fire2 = "P2 Button 1" }

local dbg = manager.machine.debugger
local frame, held = 0, {}
local function start_trace()
    dbg.visible_cpu = manager.machine.devices[":" .. cpu]
    dbg:command('trace ' .. out_dir .. '/' .. cpu .. '.tr,' .. cpu .. ',noloop,' .. LOG)
end
local function stop_trace()
    dbg:command("trace off," .. cpu)
end
if from == 0 then start_trace() end
_G.trace_frames = emu.add_machine_frame_notifier(function()
    frame = frame + 1
    for _, s in ipairs(script) do
        if s.frame == frame then
            held = {}
            for what in string.gmatch(s.inputs, "%w+") do held[what] = true end
        end
    end
    for what, name in pairs(NAMES) do
        if fields[name] then fields[name]:set_value(held[what] and 1 or 0) end
    end
    if frame == from then
        start_trace()
    elseif frame == to then
        stop_trace()
    elseif frame == to + 2 then
        manager.machine:exit()
    end
end)
