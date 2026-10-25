-- RE helper (not a test): MAME's disassembly of the two Z80s' programs,
-- for reading the game's code. Writes text files only, to DASM_OUT
-- (default build/re); never commit them, they hold the ROMs' code.
--
--   DASM_OUT=build/re mame starforc -debug -debugger none -video none -sound none \
--     -seconds_to_run 30 -autoboot_script src/tests/mame/dasm_probe.lua
--
-- Writes main.asm ($0000-$7FFF) and sub.asm ($0000-$1FFF).
-- DASM_EXTRA="main:1a55:40,sub:0100:80" also disassembles from those starts
-- (where data ahead misaligns the listing), to <cpu>_<start>.asm.

local out = os.getenv("DASM_OUT") or "build/re"
local dbg = manager.machine.debugger
local jobs = {
    { "main.asm", "0", "8000", "maincpu" },
    { "sub.asm", "0", "2000", "sub" },
}
local cpus = { main = "maincpu", sub = "sub" }
for cpu, a, n in string.gmatch(os.getenv("DASM_EXTRA") or "", "(%w+):(%x+):(%x+)") do
    jobs[#jobs + 1] = { cpu .. "_" .. a .. ".asm", a, n, cpus[cpu] or cpu }
end
local frame = 0
_G.dasm_frames = emu.add_machine_frame_notifier(function()
    frame = frame + 1
    local j = jobs[frame - 4]
    if j then
        dbg:command("dasm " .. out .. "/" .. j[1] .. "," .. j[2] .. "," .. j[3] .. ",1," .. j[4])
    elseif frame == 5 + #jobs + 30 then
        manager.machine:exit()
    end
end)
