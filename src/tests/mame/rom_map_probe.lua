-- MAME probe: the ROM regions as hex lines in the format of
-- `starforce --dump-rom`: "REGION:ADDR BYTE", region by region in the
-- port's order. Only the ROMs' bytes are dumped.
--
--   PROBE_OUT=out.txt mame starforc -autoboot_script rom_map_probe.lua ...

local out_path = os.getenv("PROBE_OUT") or "rom_map_probe.txt"

local regions = manager.machine.memory.regions
local dump = {
    { "maincpu", ":maincpu", 0x8000 },
    { "sub", ":sub", 0x2000 },
    { "fgtiles", ":fgtiles", 0x3000 },
    { "bgtiles1", ":bgtiles1", 0x6000 },
    { "bgtiles2", ":bgtiles2", 0x6000 },
    { "bgtiles3", ":bgtiles3", 0x3000 },
    { "sprites", ":sprites", 0xC000 },
    { "dac", ":dac", 0x20 },
}

local out = assert(io.open(out_path, "w"))
for _, d in ipairs(dump) do
    local r = regions[d[2]]
    for i = 0, d[3] - 1 do
        out:write(string.format("%s:%04X %02X\n", d[1], i, r:read_u8(i)))
    end
end
out:close()
manager.machine:exit()
