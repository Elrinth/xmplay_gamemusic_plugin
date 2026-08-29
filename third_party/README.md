# Vendored engines

None of these are Winamp `in_*.dll` wrappers. Cores only.

## NEZplug++ 0.9.4.8 + 3 + 24.10

Source: OffGao http://offgao.net/program/nezplug++.html (`nezplug++_s.zip`).
Original NEZplug is public domain (Mamiya). OffGao / RuRuRu improvements
remain attributed here.

`jprjr/libnezplug` was 404 at vendor time; the OffGao NSFSDK-style
`NEZNew` / `NEZLoad` / `NEZRender` API is used instead.

## Game_Music_Emu (libgme 0.6.6)

https://github.com/libgme/game-music-emu — LGPL-2.1-or-later.
SAP emulator is **not** compiled (`USE_GME_SAP` off). Stereo-depth /
Effects_Buffer echo is compiled out (`GME_DISABLE_STEREO_DEPTH`).
YM2612 uses Nuked OPN2 (LGPL).

## NSFPlay

https://github.com/bbbradsmith/nsfplay — permissive (Brad Smith / Brezza).
`xgm` + `vcm` library style (same sources as `contrib/`).

## NotSo Fatso

https://github.com/wmjordan/NotSoFatso — GPLv2 (Disch).
`NSF_Core` / `NSF_File` / `NSF_6502` only. No `in_notsofatso.dll`.

## miniz

https://github.com/richgel999/miniz — zip + deflate (gzip payload).
Unlicense / public domain.

## LZMA SDK 7z decoder

https://github.com/ip7z/7zip — public domain (Igor Pavlov). Memory 7z
extract so sibling `.m3u` inside a `.7z` is visible (XMPlay `xmp-7z`
does not attach it).

## psflib

https://gitlab.com/kode54/psflib — MIT (Christopher Snowhill / kode54).
PSF chain + `_lib` resolution. Inflate uses miniz (zlib-compatible).

## VIOGSF (VBA-M, default GSF)

https://github.com/kode54/viogsf — GPLv2. Reentrant VBA-M **library**
for GSF / MINIGSF. Same approach as foobar `foo_input_gsf`: emulator
lib + psflib, **not** a wrap of the 2004 `in_gsf.dll` binary.
Not GNOME libgsf (unrelated OLE/structured-storage library).

## Highly Advanced 0.11 (optional GSF fallback)

Official player: https://caitsith2.com/gsf/ (CaitSith2 and Zoopd).
Linux port playgsf: https://projects.raphnet.net/#playgsf
Audacious port (GPLv2): https://github.com/unmacaque/audacious-plugin-highlyadvanced

Vendored VBA/ARM C core (`-DC_CORE`) plus libresample interpolation.
`CPULoadRomMem` loads a ROM assembled by our psflib + `gc_vfs` — we do
**not** ship or wrap `in_gsf.dll`. `Util.cpp` / disk GSF loaders are
not used. GBA.cpp is compiled at `-O1` (gcc `-O3` can infinite-loop
from inlining). Non-reentrant; VIOGSF is used if HA is already open.

## lazyusf2

https://gitlab.com/kode54/lazyusf2 — GPLv2 (kode54 / Mupen64plus).
USF / MINIUSF. Interpreter (no new_dynarec). HLE RSP gfx enabled in
the core (`hle.hle_gfx = 1`) so Display Lists fire DP interrupts.
HLE audio default on (`usf_set_hle_audio`). Same psflib as GSF.
Sibling `.usflib` is a library, not a CheckFile format.
Not a wrap of 64th Note / `in_usf.dll`. Format: HCS / Adam Gashlin
(https://hcs64.com/usf/).

## Not vendored

- NEZ `kss_conv` (not public domain)
- libkss **kss-drivers** (more restrictive license)
- GME MAME YM2612 (would GPL the GME side)
- libgme SAP (`USE_GME_SAP` off)
- GNOME **libgsf** (unrelated; not a GSF player)
- Full **libmgba** / kode54 `mgba` `gsfplayer` branch (foo_input_gsf 3.x).
  That tree is a complete mGBA emulator. VIOGSF is the GSF library we
  ship as the default.
