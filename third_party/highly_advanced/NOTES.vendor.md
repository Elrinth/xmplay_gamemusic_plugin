# Vendored Highly Advanced subset

Source: Audacious port of Highly Advanced 0.11
https://github.com/unmacaque/audacious-plugin-highlyadvanced

Original GSF player by CaitSith2 and Zoopd:
https://caitsith2.com/gsf/

This tree is the VBA/ARM C core (`-DC_CORE`) plus libresample.
It is not `in_gsf.dll` and not GNOME libgsf.

Local additions:
- `CPULoadRomMem` in `VBA/GBA.cpp` / `VBA/GBA.h` — load a ROM image
  already assembled by psflib (minigsf + sibling gsflib).
- `ha_myROM` rename so the built-in BIOS image does not collide with
  VIOGSF's `myROM` when both cores are linked.
- `VBA/unzip.h` stub — HA save-state gz types only; no zip I/O.

`Util.cpp`, `gsf.cpp`, `plugin.c`, and disk `_lib` loaders are not
compiled. GBA.cpp is built at `-O1`.
