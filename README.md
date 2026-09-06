# xmp-gamemusic 1.0.9

Native **32-bit** XMPlay input plugin for chip / console music.
Display name **Game Music**. DLL `xmp-gamemusic.dll`.

This replaces FIX94 / mudlord `xmp-gme` and the Winamp **NEZplug** /
**NotSo Fatso** plugins with **one** native plugin. It is not a wrapper
of `in_nez.dll` or `in_notsofatso.dll`.

Intended home: `Elrinth/xmplay_gamemusic_plugin`.

Classic XMPlay is **32-bit only**. This DLL is PE32 i386.
VERSIONINFO FILEVERSION is **1.0.9.0**; `PLUGIN_XMPVER` is **1000900**.

## Install

Copy `xmp-gamemusic.dll` next to `xmplay.exe` (or into XMPlay's plugin
folder) and restart XMPlay.

**Delete or disable `xmp-gme.dll`, `in_nez.dll`, `in_notsofatso.dll`,
`in_gsf.dll`, and `in_usf.dll` to avoid fights.** Keep **`xmp-pokey`**
for **`.sap`**. This plugin never claims SAP.

ZXTune also claims GBS / NSF / KSS / AY. We still claim those — disable
overlapping plugins yourself. We also claim **GSF / MINIGSF** and
**USF / MINIUSF**. We do not claim other xSF (PSF / SSF / DSF / 2SF).

Config is written to `xmp-gamemusic.ini` next to the DLL.

## Formats claimed (CheckFile + GetFormat)

AY, GBS, GBR, GYM, HES, KSS, NSF, NSFE, NEZ, NSZ, NSD, RSN, SGC, SPC,
VGM, VGZ, CPC, GSF, MINIGSF, USF, MINIUSF.

Also opens **zip** / **7z** / **gz** when they hold those formats, and
reads a **sibling `.m3u`** inside the archive (foobar GME method: we open
the archive ourselves). VGZ is gzip VGM. RSN is a RAR SPC pack — XMPlay
`xmp-rar` may unwrap it; we still claim `.rsn`.

XMPlay `xmp-7z` unpacks before input plugins but does **not** attach the
sibling `.m3u`. If you want 7z+m3u+gbs, let this plugin see the `.7z`.

## Formats never claimed

- **`.sap`** — keep `xmp-pokey` (ASAP). GME SAP is compiled out.
- TFMX / hip, Furnace / DMF, OpenMPT, SNDH / sc68, Future Composer
- Other xSF: PSF / SSF / DSF / 2SF (GSF and USF **are** claimed)
- N64 / PS1 except USF

## Default engines (override in Config)

| Format | Default | Notes |
|--------|---------|--------|
| GBS, GBR | **NEZplug++** | GME GBS is known-thin. We default away from it. |
| KSS | **NEZplug++** | Official GME KSS is AY/SCC/SMS PSG only. OPLL/YM2413/Y8950 KSS is empty or wrong on GME. Never the KSS default. NEZ++ already has YM2413/Y8950/SCC. libkss exists; **kss-drivers is a more restrictive license** and is not vendored. NEZ `kss_conv` is not public domain and is not shipped. |
| AY, HES, CPC, SGC, NSD | **NEZplug++** | SGC/GYM/SPC/VGM stay in this plugin (not leftover xmp-gme). |
| NSF / NSFE / NSF2 | **NSFPlay** | Full NSF2: IRQ (on by default), non-returning INIT, suppress PLAY, mixe, RATE, regn/Dendy, N163 multiplex, all expansions. 0cc/Dn multi-chip + NSF2 only reliably play here. |
| NSF1 / NSFe alternate | **NotSo Fatso** | No NSF2. Channel mute/volume, expansions, silence detect, NTSC/PAL, filters. |
| NSF last resort | **GME** | 2017 xmp-gme missed VRC. |
| SPC, VGM, VGZ, GYM, RSN | **GME only** | Current libgme 0.6.6, Nuked YM2612 (not MAME). |
| GSF / MINIGSF | **VIOGSF** | kode54 VBA-M **library** (https://github.com/kode54/viogsf). Same idea as foo_input_gsf: a real emulator lib + **psflib**, not a wrap of the 2004 `in_gsf.dll` binary. Sibling `.gsflib` in the same folder or the same zip/7z (Josh W / GSF Central). PSF tags: title, artist, length, fade. Length tags already include **2 loops**; Config `gsf_loops` scales that. Fade tag **0** = natural ending, no extra fade. Interpolation on (Config off switch). **Highly Advanced** 0.11 C core is an optional fallback (CaitSith2 / Zoopd; source, not the Winamp DLL). foobar `foo_input_gsf` 3.x uses libmgba (`mgba` `gsfplayer` branch) — that tree is a full mGBA emulator and is not vendored here. **Not GNOME libgsf.** |
| USF / MINIUSF | **lazyusf2** | kode54 Mupen64plus-based ([lazyusf2](https://gitlab.com/kode54/lazyusf2) + same [psflib](https://gitlab.com/kode54/psflib) as GSF). Sibling `.usflib` in the same folder or the same zip/7z — **not** a playlist / CheckFile item (same pattern as `.gsflib`). PSF tags: title, artist, length, fade. Length tag is used as-is (sets often already include **2** loops). Fade tag **0** = natural ending. HLE RSP gfx is always on so Display Lists fire DP interrupts (Mario Kart 64 re-rip, Yoshi's Story). HLE audio default on (Systems tab). Interpreter (i386 dynarec not required). Decode on the process thread; seek is reset + fast-forward. **Not** a wrap of 64th Note / `in_usf.dll`. Format: [HCS](https://hcs64.com/usf/) / Adam Gashlin; core: kode54. |

If the chosen/default engine fails CheckFile/init, the next engine that
claims that format is tried (soft fallback). Config can still **force**
an engine. Forcing GME on FM KSS will sound wrong — that is GME, not us.

Forced stereo widen / reverb from old `xmp-gme` are **not** defaults.
Stereo width default is **0**.

## 1.0.9

- **Trust short M3U SFX times:** durations in **[250 ms, 15 s]** from NEZ/nsfe2m3u apply as tagged play TIME on Open / GetFileInfo / set_track (e.g. Zelda II Treasure `0:00:02.5` → ~2500, Flute `0:00:03` → ~3000). Music M3U times (≥15 s, including Title `0:02:09` → 129 s) still apply.
- Only reject absurd M3U crumbs **&lt;250 ms** (or parse failure) — not all shorts / not a 2.5 s or 55 s floor for tagged M3U.
- **Live silence-end for unlisted SFX:** deferred side-player silence-end **&lt;15 s** may `SetLength` / shrink the 10-minute placeholder on first play (`live_ok`). Long one-loop measure (≥55 s) remains **cache-only** until the next Open / set_track (no mid-play shrink of Title ~68 s).
- Sidecar M3U: if playlist paths do not match a renamed NSF basename, fall back to applying all entries (single-file playlist).
- Keep H:MM:SS parser from 1.0.8.
- Version **1.0.9**.

## 1.0.8

- **NEZ / nsfe2m3u M3U times:** `gc_parse_mmss` parses **H:MM:SS[.frac]** correctly (`0:02:09` → **129 s**, not ~2 s). Also `M:SS`, plain seconds, and fractional `0:00:02.5`.
- Sidecar / archive M3U durations below **2.5 s** are never applied as play TIME (failed-parse / EXTINF garbage); titles still merge.
- `GetFileInfo` / `SetLength` never advertise absurd shorts (&lt;2.5 s) — use the **10-minute** placeholder instead.
- `GC_DEFAULT_PLAY_MS` / untagged fallback remain **600000** (10 min) everywhere.
- Host check: Zelda II real M3U track 1 → **~129000** ms; synthetic `0:02` / `#EXTINF:2` still does not become a 2 s XMPlay length.
- Version **1.0.8**.

## 1.0.7

- **Playback survival first:** while an untagged NSF/GBS/KSS/… track plays on the 10-minute placeholder, deferred measure **never** calls `SetLength`, never shrinks `length_ms`/`cap_frames`, and never silence-cuts the live stream. Measure writes the **length cache only**; the next Open / GetFileInfo / set_track applies it.
- NSF family: live silence EOF is **always off**. NSFPlay `IsStopped()` is re-armed or silenced — Process ends only at `cap_frames`.
- GetFileInfo: cached confident length if present, else **600000** (never a short sync false-measure).
- Host check: Zelda II track 1 → open **600000**, process ≥**90 s** without EOF; cache may hold ~**68000** while the current play stays at 600000.
- Version **1.0.7**.

## 1.0.6

- **Confident lengths only:** commit a measured TIME only for one-loop/end **≥ ~55 s**, or short SFX silence-ends (**&lt; 15 s** after ≥800 ms hush). Phrase repeats in the 15–55 s band are discarded (keep the 10-minute default).
- Rejected early NSFPlay detects no longer abort measure or re-enable live silence-cut on the 10-min placeholder (root cause of instant EOF while TIME still read 600000).
- Never `SetLength` / `cap_frames` to a value that would end within ~500 ms of the current play position.
- Length cache **v3** (ignores v2 and older); only confident lengths are stored.
- Version **1.0.6**.

## 1.0.5

- Untagged NSF/GBS/… default TIME is **10 minutes**; Open/GetFileInfo no longer block on one-loop measure.
- One-loop length is measured **while the track plays** (NSFPlay side detector + PCM fallback); `SetLength` updates when ready; result is cached.
- NSFPlay playback clears detection state (`playtime_unknown`, `time_in_ms`, `loop_num`, AUTO_DETECT/STOP) so `IsStopped` cannot end a stream at ~2s while TIME is still long.
- `nsf_render` re-arms if NSFPlay fades early before our `cap_frames`.
- Version **1.0.5**.

## 1.0.4

- NSF measure: use NSFPlay stock `DETECT_TIME`/`DETECT_INT` (30s/5s) and reject absurdly short APU-write "loops" (Zelda II title/temple/flute no longer get ~2–29 s false TIME).
- PCM signature loop minimum raised; tiny measured lengths fall back so `GetFileInfo` never advertises empty-looking subsongs (playlist skip 3→7).
- Length cache key bumped to **v2** (ignores stale 1.0.x entries).
- Quieter defaults: `loudness_db` **+2** (was +6), NSF trim **0 dB** (was +2). Soft limiter unchanged.
- Version **1.0.4**.

## 1.0.3

- NSF / chip: do not silence-cut a **measured** one-loop TIME on mid-song rests (Zelda II and similar were ending after ~1–2 s of hush).
- `GetFileInfo` returns `XMPIN_INFO_NOSUBTAGS`; `SetPosition` accepts signed/relative subsong steps so Shift+Left reaches the previous track.


## Lengths and tracks

NSF / GBS / KSS / AY / HES are multi-song (`GetSubSongs`,
`XMPIN_POS_SUBSONG`).

- Use M3U or NSFe / NSF2 duration when present (NEZ-style M3U: titles,
  `mm:ss`, optional loop/fade, 1-based NSF index, zip paths)
- **Untagged** NSF (and GBS / KSS / AY / HES when they also lack M3U
  times): default TIME is **10 minutes** so Open stays instant. When
  **Measure untagged song lengths** is on (default), a one-loop /
  song-end scan runs **during playback** (NSFPlay APU detector, then
  PCM). Only **confident** results commit (loop ≥55s, or SFX silence-end <15s).
  When it finishes, the length is written to the length cache only
  (applied on the next Open / GetFileInfo / set_track — never a mid-play
  `SetLength` / cap shrink). Length is intro + **one loop**
  + fade, or last audible + tail for one-shots.
- The scan is capped at **Max untagged / scan cap** (default **180**
  seconds). If the cap is hit with no loop and no song-end, TIME is
  that many seconds + fade. The same value is used when Measure is
  off. This is a fallback, not the intended length for looping
  music.
- **Never** use the silence-detect window (default 1200 ms) as TIME.
  Live silence EOF is **always off** for NSF/NSFE/NEZ. For other chip
  formats it stays off while on the 10-minute placeholder.
- **Never** ship library dummy TIME (GME 2:30 / 150000, NEZ 5:00 /
  300000, or a dummy 3:00 placeholder). Process EOF is the advertised
  current-track length. `GetSubSongs` total is the sum of measured
  (or tagged / fallback) tracks.
- Lengths are cached in `xmp-gamemusic-lengths.ini` next to the DLL
  (path + size + mtime). Library `GetFileInfo` is synchronous on one
  thread — a cache hit returns immediately with no emulate. A miss
  with Measure on scans on that thread (XMPlay waits), then writes
  the cache. Measure off returns the configured default seconds.
- **Never** drop the last track

## Volume

- Loudness default **+2 dB** (was +6), soft limiter, optional auto-normalize
- Per-format trim (KSS/MSX especially)
- Per-engine gain (Fatso vs NSFPlay)
- Channel mute / per-channel level
- No forced stereo widening or reverb

## Config and File Info

**Decoders → Config** and **Configure…** (Options resource 1000) open
the full Fatso-style property sheet. The Options pane itself is one
short line plus that button so it fits XMPlay’s ~300px decoder list.
**File Info** is the Info tab of the same sheet. Settings persist in
`xmp-gamemusic.ini` next to the DLL. Mute / volume / pan apply while
a track is playing.

NSFe / M3U / PSF lengths win when present. Untagged chip tunes use a
**measured one-loop** TIME when Measure is on. Silence detect only
auto-advances **after** audible audio, and never overwrites a tagged
TIME.

- **General** — rate 8–96 kHz (default **48000**), stereo/mono, stereo
  width default **0**, filter None/Hipass/Lopass/Prepass + Hz (greyed
  when the current engine has no filter), loops, fade, loudness,
  auto-normalize, soft limiter, “Use M3U / embedded tag length”,
  **Measure untagged song lengths** (default **on**, CPU-heavy),
  **Max untagged / scan cap** seconds (default **180**),
  per-format engine picker (Auto + list)
- **NSF** — all NotSo Fatso checkboxes (DMC/N106/FDS pop reducers,
  Ignore $4011, Reset duty on $4003/7, Ignore BRK / illegal opcodes,
  Don’t wait for PLAY return, Reset 6502 regs, Ignore NSF version,
  Force $4017 None/$00/$80, invert under Hz, silence ms + “unless
  track length specified”, NTSC/PAL), plus NSFPlay IRQ, N163
  multiplex, region (auto / NTSC / PAL / Dendy), and the same
  Measure / max-untagged controls as General
- **NES mixer** — 2A03 + FDS only. Mute + vol 0–255 + pan −128…128.
  Defaults: Sq1 pan **−19**, Sq2 **+19**, Tri vol **124**, others 255
- **VRC6 / MMC5 / N163 / VRC7 / FME-07** — same mixer rows per chip.
  N163 page includes NSFPlay multiplex. Config keeps every expansion
  page so defaults can be set; File Info hides chips this NSF does
  not use
- **Game Boy** — Pulse 1/2, Wave, Noise + Timer+VBlank / Use INT +
  optional high-pass (~90 Hz)
- **MSX / KSS** — PSG (AY3), SCC, YM2413, Y8950 always listed (GME
  KSS has no FM — NEZ++ is Auto). Master KSS trim
- **HES / PCE** — 6 PSG + ADPCM
- **AY / CPC / SGC** — AY or SN76489 channels
- **SPC** — 8 voice mute + interpolation (off / linear / cubic). No
  default reverb
- **VGM / GYM** — gain + mute if GME exposes voices. No chip-FM editor
- **GSF** — interpolation (off recommended — Highly Advanced can
  crash with it on), loops (default 2). File Info shows **gsflib**
  path when `_lib` is set
- **USF** — HLE audio (default on), loops/fade. File Info shows
  **usflib**. No fake voice grid
- **File Info** — title/artist/ripper/copyright, engine, track x/y,
  length source, NSF load/init/play + banks + chip bits, GSF/USF lib
  path. Config / Options / plugin File Info are the same property
  sheet; Config always lists every tab so defaults can be set

## Build

32-bit Windows DLL, `i686-w64-mingw32`. Crash-safe `DllMain` (only
`DisableThreadLibraryCalls`). Static libgcc / libstdc++. Imports are
`KERNEL32`, `USER32`, `COMCTL32`, `msvcrt`.

```
make          # host tests + dist/xmp-gamemusic.dll
make dll
make test
make pack     # xmp-gamemusic-1.0.9.zip = dll + README.md
```

Emulation cores are built at `-O2`.

## License

The combined plugin is **GPLv2+** because NotSo Fatso is GPL-2+.
Game_Music_Emu is LGPL-2.1-or-later and is compatible when statically
linked this way. YM2612 uses **Nuked OPN2** (LGPL), not MAME YM2612
(GPL), so GME stays LGPL-clean beside Fatso.

NSFPlay (Brad Smith / Brezza; permissive / unnamed) — Brad's notice is
kept. NEZplug++ cores used here are public domain (Mamiya / RuRuRu /
OffGao) after dropping `kss_conv`. miniz is Unlicense. LZMA SDK 7z
decoder is public domain (Igor Pavlov). psflib is MIT (kode54).
VIOGSF / VBA-M (default GSF library) and lazyusf2 are GPLv2. Highly
Advanced 0.11 (CaitSith2 and Zoopd; Audacious/playgsf ports GPLv2) is
an optional GSF fallback. USF: HCS / Adam Gashlin (64th Note format);
kode54 (lazyusf2).

See `third_party/README.md` and `LICENSE`.
