-- MAME probe: RAM, the video RAM and registers, and the flip at chosen
-- frames, with scripted input. Only dumps; C does the decoding and comparing.
--
--   PROBE_OUT=<dir> PROBE_FRAMES="900 1500" PROBE_LAST=1800 \
--   PROBE_SCRIPT="600:coin 610: 700:start 710: ..." mame starforc -autoboot_script capture_probe.lua
--
-- PROBE_FIELDS="Cabinet=0" sets switches by MAME's names (the DIP switches).
--
-- Writes into PROBE_OUT, at each PROBE_FRAMES frame:
--   frame_<n>.txt   a line per part, its name then its bytes in hex: ram
--                   ($8000, 4 KB), fgvideo ($9000), fgcolor ($9400),
--                   sprites ($9800), palette ($9C00), regs ($9E00), bg3
--                   ($A000), bg2 ($A800), bg1 ($B000), radar ($B800),
--                   subram (the sub's $4000), flip (the last $D000 write,
--                   "--" = never)
--   and a MAME snapshot (snap/starforc/NNNN.png, in order)

local out_dir = os.getenv("PROBE_OUT") or "."
local last_frame = tonumber(os.getenv("PROBE_LAST") or "1800")

local snap_frames = {}
for n in string.gmatch(os.getenv("PROBE_FRAMES") or "", "%d+") do snap_frames[tonumber(n)] = true end

-- "frame:input,input frame:..." (tests/script.h's names); each entry holds
-- until the next one.
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

local set_fields = {}
for name, v in string.gmatch(os.getenv("PROBE_FIELDS") or "", "([%w ]+)=(%d+)") do
    if fields[name] then set_fields[#set_fields + 1] = { field = fields[name], value = tonumber(v) } end
end

local space = manager.machine.devices[":maincpu"].spaces["program"]
local subspace = manager.machine.devices[":sub"].spaces["program"]

local flip
-- Kept in a global: MAME removes a tap once its handle is garbage-collected.
_G.capture_tap = space:install_write_tap(0xD000, 0xD000, "capture_flip", function(offset, data, mask) flip = data end)

local function hex(sp, base, n)
    local row = {}
    for a = base, base + n - 1 do row[#row + 1] = string.format("%02X", sp:read_u8(a)) end
    return table.concat(row)
end

local PARTS = {
    { "ram", 0x8000, 0x1000 }, { "fgvideo", 0x9000, 0x400 }, { "fgcolor", 0x9400, 0x400 },
    { "sprites", 0x9800, 0x80 }, { "palette", 0x9C00, 0x200 }, { "regs", 0x9E00, 0x40 },
    { "bg3", 0xA000, 0x800 }, { "bg2", 0xA800, 0x800 }, { "bg1", 0xB000, 0x800 }, { "radar", 0xB800, 0x400 },
}

local function dump_frame(n)
    local f = assert(io.open(string.format("%s/frame_%d.txt", out_dir, n), "w"))
    for _, p in ipairs(PARTS) do f:write(p[1] .. " " .. hex(space, p[2], p[3]) .. "\n") end
    f:write("subram " .. hex(subspace, 0x4000, 0x400) .. "\n")
    f:write("flip " .. (flip and string.format("%02X", flip) or "--") .. "\n")
    f:close()
end

local frame, held = 0, {}
_G.capture_frames = emu.add_machine_frame_notifier(function()
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
    -- a DIP switch's setting is its field's value under its mask (Cabinet=0: cocktail)
    for _, f in ipairs(set_fields) do f.field.user_value = f.value end
    if snap_frames[frame] then
        dump_frame(frame)
        manager.machine.video:snapshot()
    end
    if frame >= last_frame then manager.machine:exit() end
end)
