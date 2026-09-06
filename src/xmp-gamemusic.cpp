/*
 * xmp-gamemusic — native XMPlay input plugin for chip / game-console music.
 *
 * Display name: Game Music. DLL: xmp-gamemusic.dll
 * DllMain only DisableThreadLibraryCalls — no CRT surprises, no decoder ctor.
 * Does not wrap in_nez.dll / in_notsofatso.dll / xmp-gme.dll.
 */
#if defined(__GNUC__)
#define XMPIN_GetInterface XMPIN_GetInterface_Declared
#endif
#include "xmpin.h"
#if defined(__GNUC__)
#undef XMPIN_GetInterface
#endif

#include "gamechip.h"
#include "player.h"
#include "probe.h"
#include "m3u.h"
#include "config.h"
#include "archive.h"
#include "lencache.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commctrl.h>
#include <prsht.h>
#endif

#define INFO_WRITE_MAX 32766
#define IDD_CONFIG 101
#define IDC_NSF_ENG 1001
#define IDC_GBS_ENG 1002
#define IDC_KSS_ENG 1003
#define IDC_AY_ENG  1004
#define IDC_HES_ENG 1005
#define IDC_CPC_ENG 1006
#define IDC_SGC_ENG 1007
#define IDC_RATE    1010
#define IDC_LOOPS   1011
#define IDC_FADE    1012
#define IDC_LOUD    1013
#define IDC_AUTONORM 1014
#define IDC_WIDTH   1015
#define IDC_TRIMKSS 1016
#define IDC_FATSO_PAL 1017
#define IDC_FATSO_SIL 1018
#define IDC_GSF_INTERP 1019
#define IDC_LIMITER 1020
#define IDC_TRIMGBS 1021
#define IDC_TRIMAY  1022
#define IDC_TRIMNSF 1023
#define IDC_FATSO_HPF 1024
#define IDC_FATSO_LPF 1025
#define IDC_NSF_IRQ 1026
#define IDC_NSF_N163 1027
#define IDC_NSF_REGION 1028
#define IDC_GSF_LOOPS 1029
#define IDC_GSF_ENG 1034
#define IDC_USF_HLE 1035
#define IDC_MUTE0   1100
#define IDD_OPTIONS 1000
#define IDC_OPEN_SHEET 1090
#define IDC_OPEN_INFO 1091
#define IDD_TAB_GENERAL 200
#define IDD_TAB_NSF 201
#define IDD_TAB_MIX 202
#define IDD_TAB_SYS 203
#define IDD_TAB_INFO 204
#define IDD_TAB_EXP 205
#define IDD_TAB_GB 206
#define IDD_TAB_KSS 207
#define IDD_TAB_HES 208
#define IDD_TAB_AY 209
#define IDD_TAB_SPC 210
#define IDD_TAB_VGM 211
#define IDD_TAB_GSF 212
#define IDD_TAB_USF 213
#define IDD_TAB_VRC6 214
#define IDD_TAB_MMC5 215
#define IDD_TAB_N163 216
#define IDD_TAB_VRC7 217
#define IDD_TAB_5B 218
#define IDC_FILTER 1030
#define IDC_MONO 1031
#define IDC_USELEN 1032
#define IDC_FILTER_HZ 1033
#define IDC_DMC_POP 1040
#define IDC_N106_POP 1041
#define IDC_FDS_POP 1042
#define IDC_IGN4011 1043
#define IDC_DUTY 1044
#define IDC_IGNBRK 1045
#define IDC_IGNILL 1046
#define IDC_NOWAIT 1047
#define IDC_RSTREG 1048
#define IDC_IGNVER 1049
#define IDC_F4017 1050
#define IDC_NOSILT 1052
#define IDC_PREPASS 1053
#define IDC_INVERT_HZ 1054
#define IDC_GBS_INT 1060
#define IDC_SPC_INT 1061
#define IDC_GBS_HPF 1062
#define IDC_TRIMVGM 1036
#define IDC_INFOBOX 1070
#define IDC_MEASURE 1071
#define IDC_UNTAGMAX 1072
#define IDC_MEASURE_NSF 1073
#define IDC_UNTAGMAX_NSF 1074

static XMPFUNC_IN *xmpfin;
static XMPFUNC_MISC *xmpfmisc;
static XMPFUNC_FILE *xmpffile;

static gc_player *g_play;
static gc_config g_cfg;
static char g_name_hint[512];
static int g_cfg_ready;
static gc_info g_last_info;
static char g_last_engine[64];
static int g_last_ok;

#ifdef _WIN32
static HINSTANCE g_hinst;
#endif

static void bounded_copy(char *dst, size_t cap, const char *src)
{
	size_t n;
	if (!dst || cap == 0)
		return;
	if (!src) {
		dst[0] = '\0';
		return;
	}
	n = strlen(src);
	if (n >= cap)
		n = cap - 1;
	memcpy(dst, src, n);
	dst[n] = '\0';
}

static void sanitize_line(char *s)
{
	if (!s)
		return;
	for (; *s; ++s) {
		if (*s == '\t' || *s == '\r' || *s == '\n')
			*s = ' ';
	}
}

static void write_kv(char **cursor, char *end, const char *name, const char *value)
{
	size_t nl, vl, need;
	if (!cursor || !*cursor || !end || !name || !value || !value[0])
		return;
	nl = strlen(name);
	vl = strlen(value);
	need = nl + 1 + vl + 1;
	if (*cursor + need >= end)
		return;
	memcpy(*cursor, name, nl);
	*cursor += nl;
	**cursor = '\t';
	*cursor += 1;
	memcpy(*cursor, value, vl);
	*cursor += vl;
	**cursor = '\r';
	*cursor += 1;
	**cursor = '\0';
}

static void *xmp_alloc(DWORD n)
{
	if (!xmpfmisc || !xmpfmisc->Alloc || n == 0)
		return NULL;
	return xmpfmisc->Alloc(n);
}

static void remember_hint(const char *filename)
{
	size_t n;
	if (!filename || !filename[0])
		return;
	n = strlen(filename);
	if (n >= sizeof g_name_hint)
		n = sizeof g_name_hint - 1;
	memcpy(g_name_hint, filename, n);
	g_name_hint[n] = '\0';
}

static void ensure_cfg(void)
{
	char dir[GC_MAX_PATH];
	if (g_cfg_ready)
		return;
	gc_config_defaults(&g_cfg);
	dir[0] = '\0';
#ifdef _WIN32
	if (g_hinst) {
		char path[MAX_PATH];
		DWORD n = GetModuleFileNameA(g_hinst, path, MAX_PATH);
		if (n && n < MAX_PATH) {
			char *slash = strrchr(path, '\\');
			if (!slash)
				slash = strrchr(path, '/');
			if (slash) {
				*slash = '\0';
				bounded_copy(dir, sizeof dir, path);
			}
		}
	}
#endif
	gc_config_set_ini_path(&g_cfg, dir[0] ? dir : NULL);
	gc_config_load(&g_cfg);
	g_cfg_ready = 1;
}

static int slurp_xmpfile(XMPFILE file, unsigned char **out, size_t *out_len)
{
	DWORD type, sz, got, pos;
	unsigned char *buf;
	if (out) *out = NULL;
	if (out_len) *out_len = 0;
	if (!file || !out || !out_len || !xmpffile || !xmpffile->Read)
		return 0;
	type = xmpffile->GetType(file);
	if (type == XMPFILE_TYPE_MEMORY) {
		const void *mem;
		if (!xmpffile->GetMemory || !xmpffile->GetSize)
			return 0;
		mem = xmpffile->GetMemory(file);
		sz = xmpffile->GetSize(file);
		if (!mem || sz < 4 || (size_t)sz > GC_MAX_MODULE)
			return 0;
		buf = (unsigned char *)malloc(sz);
		if (!buf)
			return 0;
		memcpy(buf, mem, sz);
		*out = buf;
		*out_len = sz;
		return 1;
	}
	sz = xmpffile->GetSize ? xmpffile->GetSize(file) : 0;
	pos = xmpffile->Tell ? xmpffile->Tell(file) : 0;
	if (xmpffile->Seek)
		xmpffile->Seek(file, 0);
	if (sz > 0) {
		if (sz < 4 || (size_t)sz > GC_MAX_MODULE) {
			if (xmpffile->Seek) xmpffile->Seek(file, pos);
			return 0;
		}
		buf = (unsigned char *)malloc(sz);
		if (!buf) {
			if (xmpffile->Seek) xmpffile->Seek(file, pos);
			return 0;
		}
		got = xmpffile->Read(file, buf, sz);
		if (xmpffile->Seek) xmpffile->Seek(file, pos);
		if (got < 4) {
			free(buf);
			return 0;
		}
		*out = buf;
		*out_len = got;
		return 1;
	}
	{
		size_t cap = 64 * 1024, total = 0;
		buf = (unsigned char *)malloc(cap);
		if (!buf)
			return 0;
		for (;;) {
			DWORD chunk;
			if (total == cap) {
				size_t ncap = cap * 2;
				unsigned char *nb;
				if (ncap > GC_MAX_MODULE)
					ncap = GC_MAX_MODULE;
				if (ncap <= cap) {
					free(buf);
					return 0;
				}
				nb = (unsigned char *)realloc(buf, ncap);
				if (!nb) {
					free(buf);
					return 0;
				}
				buf = nb;
				cap = ncap;
			}
			chunk = xmpffile->Read(file, buf + total, (DWORD)(cap - total));
			if (chunk == 0)
				break;
			total += chunk;
			if (total >= GC_MAX_MODULE)
				break;
		}
		if (xmpffile->Seek)
			xmpffile->Seek(file, pos);
		if (total < 4) {
			free(buf);
			return 0;
		}
		*out = buf;
		*out_len = total;
		return 1;
	}
}

static XMPFILE open_if_needed(const char *filename, XMPFILE file, int *opened)
{
	*opened = 0;
	if (file)
		return file;
	if (!filename || !xmpffile || !xmpffile->Open)
		return NULL;
	file = xmpffile->Open(filename);
	if (file)
		*opened = 1;
	return file;
}

static void close_if_opened(XMPFILE file, int opened)
{
	if (opened && file && xmpffile && xmpffile->Close)
		xmpffile->Close(file);
}

static void sidecar_m3u_path(const char *path, char *out, size_t cap)
{
	char stem[GC_MAX_PATH];
	char *dot;
	bounded_copy(stem, sizeof stem, path ? path : "");
	dot = strrchr(stem, '.');
	if (dot && dot != stem)
		*dot = '\0';
	snprintf(out, cap, "%s.m3u", stem);
}

static int load_sidecar_m3u(const char *filename, char **text, size_t *len)
{
	char path[GC_MAX_PATH];
	XMPFILE f;
	unsigned char *buf = NULL;
	size_t n = 0;
	*text = NULL;
	*len = 0;
	if (!filename || !xmpffile || !xmpffile->Open)
		return 0;
	sidecar_m3u_path(filename, path, sizeof path);
	f = xmpffile->Open(path);
	if (!f)
		return 0;
	if (!slurp_xmpfile(f, &buf, &n) || !buf) {
		if (xmpffile->Close)
			xmpffile->Close(f);
		return 0;
	}
	if (xmpffile->Close)
		xmpffile->Close(f);
	*text = (char *)buf;
	*len = n;
	return 1;
}

static void unload_playback(void)
{
	if (g_play) {
		gc_player_close(g_play);
		g_play = NULL;
	}
}

static void append_tag(char **p, char *end, const char *key, const char *val)
{
	size_t kl, vl;
	if (!p || !*p || !end || !key || !val || !val[0])
		return;
	kl = strlen(key);
	vl = strlen(val);
	if (*p + kl + 1 + vl + 1 + 1 >= end)
		return;
	memcpy(*p, key, kl);
	*p += kl;
	**p = '\0';
	*p += 1;
	memcpy(*p, val, vl);
	*p += vl;
	**p = '\0';
	*p += 1;
}

static char *finish_tags(char *stack, char *p, size_t stack_sz)
{
	char *end = stack + stack_sz;
	char *out;
	size_t n;
	if (p + 1 < end)
		*p++ = '\0';
	n = (size_t)(p - stack);
	out = (char *)xmp_alloc((DWORD)n);
	if (!out)
		return NULL;
	memcpy(out, stack, n);
	return out;
}

static char *build_tags(const gc_info *inf, int track0)
{
	char stack[8192];
	char *p = stack;
	char *end = stack + sizeof stack;
	const char *title;
	if (!xmpfmisc || !inf)
		return NULL;
	title = (track0 >= 0 && track0 < inf->track_count && inf->tracks[track0].title[0])
	            ? inf->tracks[track0].title
	            : inf->game;
	append_tag(&p, end, "filetype", gc_format_name(inf->format));
	append_tag(&p, end, "title", title);
	append_tag(&p, end, "artist", inf->artist);
	append_tag(&p, end, "album", inf->game);
	append_tag(&p, end, "comment", inf->comment);
	return finish_tags(stack, p, sizeof stack);
}

static void set_length_now(int play_ms)
{
	float sec;
	if (!xmpfin || !xmpfin->SetLength || play_ms <= 0)
		return;
	/* Never advertise absurd crumbs to XMPlay (<250ms). Short SFX M3U
	   (2.5s / 3s / 6s) must pass through. */
	if (play_ms < GC_M3U_MIN_MS)
		play_ms = GC_DEFAULT_PLAY_MS;
	sec = (float)play_ms / 1000.0f;
	if (sec > 0.0f && sec < 86400.0f)
		xmpfin->SetLength(sec, TRUE);
}

static int archive_holds_music(const unsigned char *data, size_t len, const char *filename)
{
	gc_format fmt = gc_probe(data, len, filename);
	gc_blob music, m3u;
	unsigned char *raw = NULL;
	size_t raw_len = 0;
	int ok = 0;
	if (fmt == GC_FMT_GZIP || fmt == GC_FMT_VGZ) {
		if (gc_inflate_gzip(data, len, &raw, &raw_len) && raw) {
			gc_format inner = gc_probe(raw, raw_len, filename);
			ok = gc_format_claimed(inner) && inner != GC_FMT_ZIP && inner != GC_FMT_SEVENZ;
			free(raw);
		}
		return ok;
	}
	if (fmt == GC_FMT_ZIP || fmt == GC_FMT_SEVENZ ||
	    fmt == GC_FMT_NSZ || fmt == GC_FMT_NEZ) {
		memset(&music, 0, sizeof music);
		memset(&m3u, 0, sizeof m3u);
		ok = gc_archive_extract_music(data, len, &music, &m3u);
		free(music.data);
		free(m3u.data);
		return ok;
	}
	return gc_format_claimed(fmt);
}

static void append_line(char *buf, size_t cap, const char *line)
{
	size_t n, add;
	if (!buf || !line || cap == 0)
		return;
	n = strlen(buf);
	add = strlen(line);
	if (n + add + 3 >= cap)
		return;
	memcpy(buf + n, line, add);
	n += add;
	buf[n++] = '\r';
	buf[n++] = '\n';
	buf[n] = '\0';
}

static void remember_info(const gc_info *inf, const char *eng)
{
	if (!inf)
		return;
	g_last_info = *inf;
	bounded_copy(g_last_engine, sizeof g_last_engine, eng ? eng : "");
	g_last_ok = 1;
}

static void format_file_info(char *buf, size_t cap)
{
	gc_info inf;
	char line[320];
	int t, n, ms, i, voices;
	const char *eng = "";
	if (!buf || cap == 0)
		return;
	buf[0] = '\0';
	memset(&inf, 0, sizeof inf);
	if (g_play) {
		gc_player_info(g_play, &inf);
		t = gc_player_current_track(g_play) + 1;
		n = gc_player_track_count(g_play);
		ms = gc_player_length_ms(g_play);
		eng = gc_player_engine_name(g_play);
	} else if (g_last_ok) {
		inf = g_last_info;
		t = inf.start_track + 1;
		n = inf.track_count;
		ms = inf.tracks[inf.start_track].duration_ms;
		eng = g_last_engine;
	} else {
		snprintf(buf, cap,
		         "No file loaded.\r\n"
		         "Decoders → Config (or Options → Configure…) opens every tab.\r\n"
		         "File Info hides pages that do not apply to the current file.");
		return;
	}
	snprintf(line, sizeof line, "Title / game: %s", inf.game);
	append_line(buf, cap, line);
	if (inf.tracks[t > 0 && t <= n ? t - 1 : 0].title[0]) {
		snprintf(line, sizeof line, "Track title: %s",
		         inf.tracks[t > 0 && t <= n ? t - 1 : 0].title);
		append_line(buf, cap, line);
	}
	if (inf.artist[0]) {
		snprintf(line, sizeof line, "Artist: %s", inf.artist);
		append_line(buf, cap, line);
	}
	if (inf.ripper[0]) {
		snprintf(line, sizeof line, "Ripper: %s", inf.ripper);
		append_line(buf, cap, line);
	}
	if (inf.copyright[0]) {
		snprintf(line, sizeof line, "Copyright: %s", inf.copyright);
		append_line(buf, cap, line);
	}
	if (inf.comment[0] && (!inf.ripper[0] || strcmp(inf.comment, inf.ripper) != 0)) {
		snprintf(line, sizeof line, "Comment: %s", inf.comment);
		append_line(buf, cap, line);
	}
	snprintf(line, sizeof line, "Format: %s", gc_format_name(inf.format));
	append_line(buf, cap, line);
	snprintf(line, sizeof line, "Engine: %s", eng[0] ? eng : "(unknown)");
	append_line(buf, cap, line);
	if (inf.system[0]) {
		snprintf(line, sizeof line, "System: %s", inf.system);
		append_line(buf, cap, line);
	}
	snprintf(line, sizeof line, "Track: %d / %d", t > 0 ? t : 1, n > 0 ? n : 1);
	append_line(buf, cap, line);
	if (ms > 0)
		snprintf(line, sizeof line, "Length: %d:%02d (%d ms)", ms / 60000, (ms / 1000) % 60, ms);
	else
		snprintf(line, sizeof line, "Length: unknown (no dummy TIME)");
	append_line(buf, cap, line);
	snprintf(line, sizeof line, "Length source: %s",
	         inf.length_src[0] ? inf.length_src : "unknown");
	append_line(buf, cap, line);
	if (inf.lib_path[0]) {
		snprintf(line, sizeof line, "%s: %s",
		         (inf.format == GC_FMT_USF || inf.format == GC_FMT_MINIUSF)
		             ? "usflib" : "gsflib",
		         inf.lib_path);
		append_line(buf, cap, line);
	} else if (inf.lib_name[0]) {
		snprintf(line, sizeof line, "Library (_lib): %s", inf.lib_name);
		append_line(buf, cap, line);
	}
	if (inf.nsf_load || inf.nsf_init || inf.nsf_play) {
		snprintf(line, sizeof line, "NSF load $%04X  init $%04X  play $%04X",
		         inf.nsf_load & 0xFFFF, inf.nsf_init & 0xFFFF, inf.nsf_play & 0xFFFF);
		append_line(buf, cap, line);
		if (inf.nsf_has_bank) {
			snprintf(line, sizeof line,
			         "NSF banks $%02X $%02X $%02X $%02X $%02X $%02X $%02X $%02X",
			         inf.nsf_bank[0], inf.nsf_bank[1], inf.nsf_bank[2], inf.nsf_bank[3],
			         inf.nsf_bank[4], inf.nsf_bank[5], inf.nsf_bank[6], inf.nsf_bank[7]);
			append_line(buf, cap, line);
		}
		{
			char chips[80];
			chips[0] = '\0';
			if (inf.nsf_exp & GC_NSF_VRC6) strcat(chips, " VRC6");
			if (inf.nsf_exp & GC_NSF_VRC7) strcat(chips, " VRC7");
			if (inf.nsf_exp & GC_NSF_FDS) strcat(chips, " FDS");
			if (inf.nsf_exp & GC_NSF_MMC5) strcat(chips, " MMC5");
			if (inf.nsf_exp & GC_NSF_N163) strcat(chips, " N163");
			if (inf.nsf_exp & GC_NSF_5B) strcat(chips, " 5B");
			if (inf.nsf_ver)
				snprintf(line, sizeof line,
				         "NSF version %d  chips $%02X%s  NSF2 $%02X",
				         inf.nsf_ver, inf.nsf_exp & 0xFF,
				         chips[0] ? chips : "", inf.nsf2_bits & 0xFF);
			else
				snprintf(line, sizeof line, "NSF chips $%02X%s",
				         inf.nsf_exp & 0xFF, chips[0] ? chips : "");
			append_line(buf, cap, line);
		}
	}
	if (inf.extra[0])
		append_line(buf, cap, inf.extra);
	voices = g_play ? gc_player_voices(g_play) : 0;
	if (voices > 0) {
		append_line(buf, cap, "Voices:");
		for (i = 0; i < voices && i < 32; ++i) {
			const char *vn = gc_player_voice_name(g_play, i);
			snprintf(line, sizeof line, "  %2d %s%s", i + 1, vn ? vn : "",
			         g_cfg.mute[i] ? "  (muted)" : "");
			append_line(buf, cap, line);
		}
	}
	snprintf(line, sizeof line, "Player: %s %s", PLUGIN_NAME, PLUGIN_VERSION);
	append_line(buf, cap, line);
}

static void WINAPI gc_About(HWND win)
{
	char buf[2200];
	snprintf(buf, sizeof buf,
	         PLUGIN_NAME " " PLUGIN_VERSION "\r\n"
	         "Native XMPlay input plugin for chip / console music.\r\n"
	         "Replaces xmp-gme, in_nez, and in_notsofatso.\r\n\r\n"
	         "Engines:\r\n"
	         "  NEZplug++ 0.9.4.8 + 3 + 24.10 (OffGao) — GBS/GBR, KSS, AY, HES, SGC\r\n"
	         "  NSFPlay (Brad Smith / Brezza) — default NSF/NSFE/NSF2\r\n"
	         "  NotSo Fatso (Disch) — alternate NSF\r\n"
	         "  Game_Music_Emu (blargg / libgme) — SPC, VGM/VGZ, GYM, RSN, SGC\r\n"
	         "    GME KSS is PSG/SCC only; FM KSS stays on NEZ++.\r\n\r\n"
	         "  VIOGSF (kode54 / VBA-M) — default GSF / MINIGSF + sibling .gsflib\r\n"
	         "  Highly Advanced 0.11 C core — optional GSF fallback (not in_gsf.dll)\r\n"
	         "  lazyusf2 (kode54) — USF / MINIUSF + sibling .usflib\r\n"
	         "    HLE RSP gfx (DP interrupt). Not 64th Note / in_usf.dll.\r\n"
	         "    USF format: HCS / Adam Gashlin.\r\n\r\n"
	         "Does NOT claim .sap (keep xmp-pokey). No TFMX, Furnace,\r\n"
	         "OpenMPT, SNDH, or other xSF (PSF/SSF/DSF/2SF). ZXTune also claims\r\n"
	         "GBS/NSF/KSS/AY.\r\n"
	         "No forced stereo widen or reverb. Zip/7z + sibling .m3u in-process.\r\n"
	         "Delete or disable xmp-gme.dll / in_nez.dll / in_notsofatso.dll.\r\n\r\n"
	         "Config / File Info: one property sheet (General, NSF, NES mixer,\r\n"
	         "VRC6/MMC5/N163/VRC7/FME-07, Game Boy, MSX/KSS, HES/PCE,\r\n"
	         "AY/CPC/SGC, SPC, VGM, GSF, USF, File Info). Decoders → Config\r\n"
	         "shows every tab. Plugin File Info / Options → File Info hide\r\n"
	         "pages that do not apply to this file. Mixer is mute+vol+pan.\r\n"
	         "Options pane: Configure… (full sheet). Resource 1000 is tiny.\r\n\r\n"
	         "32-bit XMPlay only (PE32 i386). License: GPLv2+.");
#ifdef _WIN32
	MessageBoxA(win, buf, PLUGIN_NAME, MB_OK | MB_ICONINFORMATION);
#else
	(void)win;
	(void)buf;
#endif
}

#ifdef _WIN32
static gc_config g_cfg_edit;
static int g_sheet_fileinfo;
static int g_sheet_restrict;
static int g_ui_ready;

static void combo_eng(HWND cb, gc_engine cur, int nsf)
{
	const char *items_nsf[] = { "Auto", "NSFPlay", "NotSo Fatso", "GME", "NEZplug++", NULL };
	const char *items_gen[] = { "Auto", "NEZplug++", "GME", NULL };
	const char **it = nsf ? items_nsf : items_gen;
	int i, sel = 0;
	if (!cb)
		return;
	SendMessageA(cb, CB_RESETCONTENT, 0, 0);
	for (i = 0; it[i]; ++i) {
		SendMessageA(cb, CB_ADDSTRING, 0, (LPARAM)it[i]);
		if (nsf) {
			gc_engine e = (i == 0) ? GC_ENG_AUTO : (i == 1) ? GC_ENG_NSFPLAY :
			               (i == 2) ? GC_ENG_FATSO : (i == 3) ? GC_ENG_GME : GC_ENG_NEZ;
			if (e == cur)
				sel = i;
		} else {
			gc_engine e = (i == 0) ? GC_ENG_AUTO : (i == 1) ? GC_ENG_NEZ : GC_ENG_GME;
			if (e == cur)
				sel = i;
		}
	}
	SendMessageA(cb, CB_SETCURSEL, (WPARAM)sel, 0);
}

static void combo_gsf(HWND cb, gc_engine cur)
{
	const char *items[] = { "Auto", "VIOGSF", "Highly Advanced", NULL };
	gc_engine map[] = { GC_ENG_AUTO, GC_ENG_GSF, GC_ENG_HA };
	int i, sel = 0;
	if (!cb)
		return;
	SendMessageA(cb, CB_RESETCONTENT, 0, 0);
	for (i = 0; items[i]; ++i) {
		SendMessageA(cb, CB_ADDSTRING, 0, (LPARAM)items[i]);
		if (map[i] == cur)
			sel = i;
	}
	SendMessageA(cb, CB_SETCURSEL, (WPARAM)sel, 0);
}

static gc_engine combo_get_gsf(HWND cb)
{
	int i = cb ? (int)SendMessageA(cb, CB_GETCURSEL, 0, 0) : 0;
	if (i == 1) return GC_ENG_GSF;
	if (i == 2) return GC_ENG_HA;
	return GC_ENG_AUTO;
}

static gc_engine combo_get_nsf(HWND cb)
{
	int i = cb ? (int)SendMessageA(cb, CB_GETCURSEL, 0, 0) : 0;
	if (i == 1) return GC_ENG_NSFPLAY;
	if (i == 2) return GC_ENG_FATSO;
	if (i == 3) return GC_ENG_GME;
	if (i == 4) return GC_ENG_NEZ;
	return GC_ENG_AUTO;
}

static gc_engine combo_get_gen(HWND cb)
{
	int i = cb ? (int)SendMessageA(cb, CB_GETCURSEL, 0, 0) : 0;
	if (i == 1) return GC_ENG_NEZ;
	if (i == 2) return GC_ENG_GME;
	return GC_ENG_AUTO;
}

static void set_int_item(HWND h, int id, int v)
{
	char buf[32];
	snprintf(buf, sizeof buf, "%d", v);
	SetDlgItemTextA(h, id, buf);
}

static void set_float_item(HWND h, int id, float v)
{
	char buf[32];
	snprintf(buf, sizeof buf, "%.1f", (double)v);
	SetDlgItemTextA(h, id, buf);
}

static int get_int_item(HWND h, int id, int def)
{
	char buf[32];
	if (!GetDlgItem(h, id) || !GetDlgItemTextA(h, id, buf, sizeof buf))
		return def;
	return atoi(buf);
}

static float get_float_item(HWND h, int id, float def)
{
	char buf[32];
	if (!GetDlgItem(h, id) || !GetDlgItemTextA(h, id, buf, sizeof buf))
		return def;
	return (float)atof(buf);
}

static void enable_id(HWND h, int id, int on)
{
	HWND c = GetDlgItem(h, id);
	if (c)
		EnableWindow(c, on ? TRUE : FALSE);
}

static void enable_mixer_range(HWND h, int lo, int hi, int on)
{
	int i;
	for (i = lo; i <= hi && i < GC_MAX_CHANNELS; ++i) {
		enable_id(h, IDC_MUTE0 + i, on);
		enable_id(h, 1300 + i, on);
		enable_id(h, 1400 + i, on);
	}
}

static int playing_nsf_exp(void)
{
	if (g_play) {
		gc_info inf;
		gc_player_info(g_play, &inf);
		return inf.nsf_exp;
	}
	if (g_last_ok)
		return g_last_info.nsf_exp;
	return 0;
}

static void fill_mixer_range(HWND h, const gc_config *c, int lo, int hi)
{
	int i;
	for (i = lo; i <= hi && i < GC_MAX_CHANNELS; ++i) {
		if (!GetDlgItem(h, IDC_MUTE0 + i))
			continue;
		CheckDlgButton(h, IDC_MUTE0 + i, c->mute[i] ? BST_CHECKED : BST_UNCHECKED);
		set_int_item(h, 1300 + i, c->chan_vol[i]);
		set_int_item(h, 1400 + i, c->chan_pan[i]);
	}
}

static void read_mixer_range(HWND h, gc_config *c, int lo, int hi)
{
	int i;
	for (i = lo; i <= hi && i < GC_MAX_CHANNELS; ++i) {
		if (!GetDlgItem(h, IDC_MUTE0 + i))
			continue;
		c->mute[i] = IsDlgButtonChecked(h, IDC_MUTE0 + i) ? 1 : 0;
		c->chan_vol[i] = get_int_item(h, 1300 + i, c->chan_vol[i]);
		if (c->chan_vol[i] < 0) c->chan_vol[i] = 0;
		if (c->chan_vol[i] > 255) c->chan_vol[i] = 255;
		c->chan_pan[i] = get_int_item(h, 1400 + i, c->chan_pan[i]);
		if (c->chan_pan[i] < -128) c->chan_pan[i] = -128;
		if (c->chan_pan[i] > 128) c->chan_pan[i] = 128;
		c->chan_level[i] = (float)c->chan_vol[i] / 255.0f;
	}
}

static void apply_live(void)
{
	g_cfg = g_cfg_edit;
	if (g_play)
		gc_player_apply_mute(g_play, &g_cfg);
}

static void commit_cfg(void)
{
	g_cfg = g_cfg_edit;
	if (g_cfg.rate < 8000 || g_cfg.rate > 96000)
		g_cfg.rate = GC_DEFAULT_RATE;
	gc_config_save(&g_cfg);
	if (g_play)
		gc_player_apply_mute(g_play, &g_cfg);
}

static gc_engine playing_engine(void)
{
	return g_play ? gc_player_engine(g_play) : GC_ENG_AUTO;
}

static gc_format playing_format(void)
{
	if (g_play)
		return gc_player_format(g_play);
	if (g_last_ok)
		return g_last_info.format;
	return GC_FMT_UNKNOWN;
}

static gc_engine sheet_engine(void)
{
	if (g_play)
		return gc_player_engine(g_play);
	return GC_ENG_AUTO;
}

static int fmt_is_nsf(gc_format f)
{
	return f == GC_FMT_NSF || f == GC_FMT_NSFE || f == GC_FMT_NEZ || f == GC_FMT_NSZ;
}

static void fill_general(HWND h, const gc_config *c)
{
	const char *filt[] = { "None", "Hipass", "Lopass", "Prepass" };
	HWND cb = GetDlgItem(h, IDC_FILTER);
	int i, sel, has_filter;
	combo_eng(GetDlgItem(h, IDC_NSF_ENG), c->engine_nsf, 1);
	combo_eng(GetDlgItem(h, IDC_GBS_ENG), c->engine_gbs, 0);
	combo_eng(GetDlgItem(h, IDC_KSS_ENG), c->engine_kss, 0);
	combo_eng(GetDlgItem(h, IDC_AY_ENG), c->engine_ay, 0);
	combo_eng(GetDlgItem(h, IDC_HES_ENG), c->engine_hes, 0);
	combo_eng(GetDlgItem(h, IDC_CPC_ENG), c->engine_cpc, 0);
	combo_eng(GetDlgItem(h, IDC_SGC_ENG), c->engine_sgc, 0);
	combo_gsf(GetDlgItem(h, IDC_GSF_ENG), c->engine_gsf);
	set_int_item(h, IDC_RATE, c->rate);
	set_int_item(h, IDC_LOOPS, c->loop_count);
	set_int_item(h, IDC_FADE, c->fade_ms);
	set_float_item(h, IDC_LOUD, c->loudness_db);
	set_float_item(h, IDC_WIDTH, c->stereo_width);
	set_int_item(h, IDC_FILTER_HZ, c->filter_hz);
	CheckDlgButton(h, IDC_AUTONORM, c->auto_normalize ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(h, IDC_LIMITER, c->soft_limiter ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(h, IDC_MONO, c->output_mono ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(h, IDC_USELEN, c->use_tag_length ? BST_CHECKED : BST_UNCHECKED);
	if (GetDlgItem(h, IDC_MEASURE))
		CheckDlgButton(h, IDC_MEASURE, c->measure_untagged ? BST_CHECKED : BST_UNCHECKED);
	if (GetDlgItem(h, IDC_UNTAGMAX))
		set_int_item(h, IDC_UNTAGMAX, c->untagged_max_sec);
	if (cb) {
		SendMessageA(cb, CB_RESETCONTENT, 0, 0);
		for (i = 0; i < 4; ++i)
			SendMessageA(cb, CB_ADDSTRING, 0, (LPARAM)filt[i]);
		sel = c->filter_mode;
		if (sel < 0 || sel > 3)
			sel = 0;
		SendMessageA(cb, CB_SETCURSEL, (WPARAM)sel, 0);
	}
	has_filter = !g_sheet_restrict || sheet_engine() == GC_ENG_FATSO ||
	             sheet_engine() == GC_ENG_AUTO;
	enable_id(h, IDC_FILTER, has_filter);
	enable_id(h, IDC_FILTER_HZ, has_filter && c->filter_mode != 0);
	if (g_sheet_restrict) {
		gc_format f = playing_format();
		enable_id(h, IDC_NSF_ENG, fmt_is_nsf(f));
		enable_id(h, IDC_GBS_ENG, f == GC_FMT_GBS || f == GC_FMT_GBR);
		enable_id(h, IDC_KSS_ENG, f == GC_FMT_KSS);
		enable_id(h, IDC_AY_ENG, f == GC_FMT_AY);
		enable_id(h, IDC_HES_ENG, f == GC_FMT_HES);
		enable_id(h, IDC_CPC_ENG, f == GC_FMT_CPC);
		enable_id(h, IDC_SGC_ENG, f == GC_FMT_SGC);
		enable_id(h, IDC_GSF_ENG, f == GC_FMT_GSF || f == GC_FMT_MINIGSF);
	}
}

static void read_general(HWND h, gc_config *c)
{
	c->rate = get_int_item(h, IDC_RATE, c->rate);
	if (c->rate < 8000 || c->rate > 96000)
		c->rate = GC_DEFAULT_RATE;
	c->loop_count = get_int_item(h, IDC_LOOPS, c->loop_count);
	c->fade_ms = get_int_item(h, IDC_FADE, c->fade_ms);
	c->loudness_db = get_float_item(h, IDC_LOUD, c->loudness_db);
	c->stereo_width = get_float_item(h, IDC_WIDTH, c->stereo_width);
	c->auto_normalize = IsDlgButtonChecked(h, IDC_AUTONORM) ? 1 : 0;
	c->soft_limiter = IsDlgButtonChecked(h, IDC_LIMITER) ? 1 : 0;
	c->output_mono = IsDlgButtonChecked(h, IDC_MONO) ? 1 : 0;
	c->use_tag_length = IsDlgButtonChecked(h, IDC_USELEN) ? 1 : 0;
	if (GetDlgItem(h, IDC_MEASURE))
		c->measure_untagged = IsDlgButtonChecked(h, IDC_MEASURE) ? 1 : 0;
	if (GetDlgItem(h, IDC_UNTAGMAX)) {
		c->untagged_max_sec = get_int_item(h, IDC_UNTAGMAX, c->untagged_max_sec);
		if (c->untagged_max_sec < 10)
			c->untagged_max_sec = 10;
		if (c->untagged_max_sec > 600)
			c->untagged_max_sec = 600;
	}
	c->filter_mode = (int)SendMessageA(GetDlgItem(h, IDC_FILTER), CB_GETCURSEL, 0, 0);
	if (c->filter_mode < 0)
		c->filter_mode = 0;
	c->filter_hz = get_int_item(h, IDC_FILTER_HZ, c->filter_hz);
	c->engine_nsf = combo_get_nsf(GetDlgItem(h, IDC_NSF_ENG));
	c->engine_gbs = combo_get_gen(GetDlgItem(h, IDC_GBS_ENG));
	c->engine_kss = combo_get_gen(GetDlgItem(h, IDC_KSS_ENG));
	c->engine_ay = combo_get_gen(GetDlgItem(h, IDC_AY_ENG));
	c->engine_hes = combo_get_gen(GetDlgItem(h, IDC_HES_ENG));
	c->engine_cpc = combo_get_gen(GetDlgItem(h, IDC_CPC_ENG));
	c->engine_sgc = combo_get_gen(GetDlgItem(h, IDC_SGC_ENG));
	if (GetDlgItem(h, IDC_GSF_ENG))
		c->engine_gsf = combo_get_gsf(GetDlgItem(h, IDC_GSF_ENG));
	if (c->filter_mode == 1) {
		c->fatso_highpass = 1;
		c->fatso_hpf_hz = c->filter_hz;
	} else if (c->filter_mode == 2) {
		c->fatso_lowpass = 1;
		c->fatso_lpf_hz = c->filter_hz;
	} else if (c->filter_mode == 3) {
		c->fatso_prepass = 1;
		c->fatso_pre_hz = c->filter_hz;
	}
}

static void fill_nsf(HWND h, const gc_config *c)
{
	HWND cb;
	const char *f4017[] = { "None", "$00", "$80" };
	const char *regn[] = { "Auto", "Prefer NTSC", "Prefer PAL", "Prefer Dendy",
	                       "Force NTSC", "Force PAL", "Force Dendy" };
	int i, sel, fatso_on, nsfplay_on;
	CheckDlgButton(h, IDC_DMC_POP, c->fatso_dmc_pop ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(h, IDC_N106_POP, c->fatso_n106_pop ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(h, IDC_FDS_POP, c->fatso_fds_pop ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(h, IDC_IGN4011, c->fatso_ignore_4011 ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(h, IDC_DUTY, c->fatso_reset_duty ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(h, IDC_IGNBRK, c->fatso_ignore_brk ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(h, IDC_IGNILL, c->fatso_ignore_illegal ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(h, IDC_NOWAIT, c->fatso_no_wait_play ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(h, IDC_RSTREG, c->fatso_reset_regs ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(h, IDC_IGNVER, c->fatso_ignore_version ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(h, IDC_FATSO_PAL, c->fatso_pal ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(h, IDC_NOSILT, c->fatso_no_silence_if_time ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(h, IDC_FATSO_HPF, c->fatso_highpass ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(h, IDC_FATSO_LPF, c->fatso_lowpass ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(h, IDC_PREPASS, c->fatso_prepass ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(h, IDC_NSF_IRQ, c->nsfplay_irq ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(h, IDC_NSF_N163, c->nsfplay_n163_mux ? BST_CHECKED : BST_UNCHECKED);
	if (GetDlgItem(h, IDC_MEASURE_NSF))
		CheckDlgButton(h, IDC_MEASURE_NSF, c->measure_untagged ? BST_CHECKED : BST_UNCHECKED);
	if (GetDlgItem(h, IDC_UNTAGMAX_NSF))
		set_int_item(h, IDC_UNTAGMAX_NSF, c->untagged_max_sec);
	set_int_item(h, IDC_FATSO_SIL, c->fatso_silence_ms);
	set_int_item(h, IDC_INVERT_HZ, c->fatso_invert_hz);
	cb = GetDlgItem(h, IDC_F4017);
	if (cb) {
		SendMessageA(cb, CB_RESETCONTENT, 0, 0);
		for (i = 0; i < 3; ++i)
			SendMessageA(cb, CB_ADDSTRING, 0, (LPARAM)f4017[i]);
		sel = c->fatso_force_4017;
		if (sel < 0 || sel > 2)
			sel = 0;
		SendMessageA(cb, CB_SETCURSEL, (WPARAM)sel, 0);
	}
	cb = GetDlgItem(h, IDC_NSF_REGION);
	if (cb) {
		SendMessageA(cb, CB_RESETCONTENT, 0, 0);
		for (i = 0; i < 7; ++i)
			SendMessageA(cb, CB_ADDSTRING, 0, (LPARAM)regn[i]);
		sel = c->nsfplay_region;
		if (sel < 0 || sel > 6)
			sel = 0;
		SendMessageA(cb, CB_SETCURSEL, (WPARAM)sel, 0);
	}
	fatso_on = !g_sheet_restrict || playing_engine() == GC_ENG_FATSO ||
	           (playing_engine() == GC_ENG_AUTO && fmt_is_nsf(playing_format()));
	nsfplay_on = !g_sheet_restrict || playing_engine() == GC_ENG_NSFPLAY ||
	             (playing_engine() == GC_ENG_AUTO && fmt_is_nsf(playing_format()));
	{
		static const int fatso_ids[] = {
			IDC_DMC_POP, IDC_N106_POP, IDC_FDS_POP, IDC_IGN4011, IDC_DUTY,
			IDC_IGNBRK, IDC_IGNILL, IDC_NOWAIT, IDC_RSTREG, IDC_IGNVER,
			IDC_F4017, IDC_FATSO_PAL, IDC_FATSO_SIL, IDC_NOSILT,
			IDC_FATSO_HPF, IDC_FATSO_LPF, IDC_PREPASS, IDC_INVERT_HZ
		};
		for (i = 0; i < (int)(sizeof fatso_ids / sizeof fatso_ids[0]); ++i)
			enable_id(h, fatso_ids[i], fatso_on);
	}
	enable_id(h, IDC_NSF_IRQ, nsfplay_on);
	enable_id(h, IDC_NSF_N163, nsfplay_on);
	enable_id(h, IDC_NSF_REGION, nsfplay_on);
}

static void read_nsf(HWND h, gc_config *c)
{
	c->fatso_dmc_pop = IsDlgButtonChecked(h, IDC_DMC_POP) ? 1 : 0;
	c->fatso_n106_pop = IsDlgButtonChecked(h, IDC_N106_POP) ? 1 : 0;
	c->fatso_fds_pop = IsDlgButtonChecked(h, IDC_FDS_POP) ? 1 : 0;
	c->fatso_ignore_4011 = IsDlgButtonChecked(h, IDC_IGN4011) ? 1 : 0;
	c->fatso_reset_duty = IsDlgButtonChecked(h, IDC_DUTY) ? 1 : 0;
	c->fatso_ignore_brk = IsDlgButtonChecked(h, IDC_IGNBRK) ? 1 : 0;
	c->fatso_ignore_illegal = IsDlgButtonChecked(h, IDC_IGNILL) ? 1 : 0;
	c->fatso_no_wait_play = IsDlgButtonChecked(h, IDC_NOWAIT) ? 1 : 0;
	c->fatso_reset_regs = IsDlgButtonChecked(h, IDC_RSTREG) ? 1 : 0;
	c->fatso_ignore_version = IsDlgButtonChecked(h, IDC_IGNVER) ? 1 : 0;
	c->fatso_pal = IsDlgButtonChecked(h, IDC_FATSO_PAL) ? 1 : 0;
	c->fatso_no_silence_if_time = IsDlgButtonChecked(h, IDC_NOSILT) ? 1 : 0;
	c->fatso_highpass = IsDlgButtonChecked(h, IDC_FATSO_HPF) ? 1 : 0;
	c->fatso_lowpass = IsDlgButtonChecked(h, IDC_FATSO_LPF) ? 1 : 0;
	c->fatso_prepass = IsDlgButtonChecked(h, IDC_PREPASS) ? 1 : 0;
	c->nsfplay_irq = IsDlgButtonChecked(h, IDC_NSF_IRQ) ? 1 : 0;
	c->nsfplay_n163_mux = IsDlgButtonChecked(h, IDC_NSF_N163) ? 1 : 0;
	c->fatso_silence_ms = get_int_item(h, IDC_FATSO_SIL, c->fatso_silence_ms);
	c->fatso_invert_hz = get_int_item(h, IDC_INVERT_HZ, c->fatso_invert_hz);
	c->fatso_force_4017 = (int)SendMessageA(GetDlgItem(h, IDC_F4017), CB_GETCURSEL, 0, 0);
	if (c->fatso_force_4017 < 0)
		c->fatso_force_4017 = 0;
	c->nsfplay_region = (int)SendMessageA(GetDlgItem(h, IDC_NSF_REGION), CB_GETCURSEL, 0, 0);
	if (c->nsfplay_region < 0)
		c->nsfplay_region = 0;
	if (GetDlgItem(h, IDC_MEASURE_NSF))
		c->measure_untagged = IsDlgButtonChecked(h, IDC_MEASURE_NSF) ? 1 : 0;
	if (GetDlgItem(h, IDC_UNTAGMAX_NSF)) {
		c->untagged_max_sec = get_int_item(h, IDC_UNTAGMAX_NSF, c->untagged_max_sec);
		if (c->untagged_max_sec < 10)
			c->untagged_max_sec = 10;
		if (c->untagged_max_sec > 600)
			c->untagged_max_sec = 600;
	}
}

static void fill_mix(HWND h, const gc_config *c, int lo, int hi)
{
	fill_mixer_range(h, c, lo, hi);
	if (GetDlgItem(h, IDC_TRIMNSF))
		set_float_item(h, IDC_TRIMNSF, c->trim_db[GC_FMT_NSF]);
	if (g_sheet_restrict && fmt_is_nsf(playing_format()) && lo <= 5 && hi >= 5)
		enable_mixer_range(h, 5, 5, (playing_nsf_exp() & GC_NSF_FDS) != 0);
}

static void read_mix(HWND h, gc_config *c, int lo, int hi)
{
	read_mixer_range(h, c, lo, hi);
	if (GetDlgItem(h, IDC_TRIMNSF)) {
		c->trim_db[GC_FMT_NSF] = get_float_item(h, IDC_TRIMNSF, c->trim_db[GC_FMT_NSF]);
		c->trim_db[GC_FMT_NSFE] = c->trim_db[GC_FMT_NSF];
		c->trim_db[GC_FMT_NEZ] = c->trim_db[GC_FMT_NSF];
	}
}

static void fill_sys(HWND h, const gc_config *c)
{
	const char *spc[] = { "Off", "Linear", "Cubic" };
	HWND cb = GetDlgItem(h, IDC_SPC_INT);
	int i, sel, kss, gbs, spc_on, gsf_on, usf_on;
	CheckDlgButton(h, IDC_GBS_INT, c->gbs_use_int ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(h, IDC_GSF_INTERP, c->gsf_interpolation ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(h, IDC_USF_HLE, c->usf_hle_audio ? BST_CHECKED : BST_UNCHECKED);
	set_int_item(h, IDC_GSF_LOOPS, c->gsf_loops);
	combo_gsf(GetDlgItem(h, IDC_GSF_ENG), c->engine_gsf);
	if (cb) {
		SendMessageA(cb, CB_RESETCONTENT, 0, 0);
		for (i = 0; i < 3; ++i)
			SendMessageA(cb, CB_ADDSTRING, 0, (LPARAM)spc[i]);
		sel = c->spc_interp;
		if (sel < 0 || sel > 2)
			sel = 1;
		SendMessageA(cb, CB_SETCURSEL, (WPARAM)sel, 0);
	}
	fill_mixer_range(h, c, 0, 3);
	kss = !g_sheet_restrict || playing_format() == GC_FMT_KSS;
	gbs = !g_sheet_restrict || playing_format() == GC_FMT_GBS ||
	      playing_format() == GC_FMT_GBR;
	spc_on = !g_sheet_restrict || playing_format() == GC_FMT_SPC;
	gsf_on = !g_sheet_restrict || playing_format() == GC_FMT_GSF ||
	         playing_format() == GC_FMT_MINIGSF;
	usf_on = !g_sheet_restrict || playing_format() == GC_FMT_USF ||
	         playing_format() == GC_FMT_MINIUSF;
	enable_id(h, IDC_GBS_INT, gbs);
	enable_id(h, IDC_SPC_INT, spc_on);
	enable_id(h, IDC_GSF_INTERP, gsf_on);
	enable_id(h, IDC_GSF_LOOPS, gsf_on);
	enable_id(h, IDC_GSF_ENG, gsf_on);
	enable_id(h, IDC_USF_HLE, usf_on);
	for (i = 0; i < 4; ++i) {
		enable_id(h, IDC_MUTE0 + i, kss);
		enable_id(h, 1300 + i, kss);
		enable_id(h, 1400 + i, kss);
	}
}

static void read_sys(HWND h, gc_config *c)
{
	c->gbs_use_int = IsDlgButtonChecked(h, IDC_GBS_INT) ? 1 : 0;
	c->gsf_interpolation = IsDlgButtonChecked(h, IDC_GSF_INTERP) ? 1 : 0;
	c->usf_hle_audio = IsDlgButtonChecked(h, IDC_USF_HLE) ? 1 : 0;
	c->gsf_loops = get_int_item(h, IDC_GSF_LOOPS, c->gsf_loops);
	if (GetDlgItem(h, IDC_GSF_ENG))
		c->engine_gsf = combo_get_gsf(GetDlgItem(h, IDC_GSF_ENG));
	if (c->gsf_loops < 1)
		c->gsf_loops = 2;
	c->spc_interp = (int)SendMessageA(GetDlgItem(h, IDC_SPC_INT), CB_GETCURSEL, 0, 0);
	if (c->spc_interp < 0)
		c->spc_interp = 1;
	read_mixer_range(h, c, 0, 3);
}

static INT_PTR page_notify(HWND h, LPARAM lParam, void (*readfn)(HWND, gc_config *))
{
	NMHDR *nm = (NMHDR *)lParam;
	if (!nm)
		return FALSE;
	if (nm->code == PSN_APPLY) {
		if (readfn)
			readfn(h, &g_cfg_edit);
		commit_cfg();
		SetWindowLongPtr(h, DWLP_MSGRESULT, PSNRET_NOERROR);
		return TRUE;
	}
	if (nm->code == PSN_KILLACTIVE) {
		if (readfn)
			readfn(h, &g_cfg_edit);
		SetWindowLongPtr(h, DWLP_MSGRESULT, FALSE);
		return TRUE;
	}
	return FALSE;
}

static INT_PTR CALLBACK TabGeneralProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
	if (m == WM_INITDIALOG) {
		g_ui_ready = 0;
		fill_general(h, &g_cfg_edit);
		g_ui_ready = 1;
		return TRUE;
	}
	if (m == WM_NOTIFY)
		return page_notify(h, l, read_general);
	if (m == WM_COMMAND && g_ui_ready && HIWORD(w) == CBN_SELCHANGE &&
	    LOWORD(w) == IDC_FILTER) {
		int mode = (int)SendMessageA(GetDlgItem(h, IDC_FILTER), CB_GETCURSEL, 0, 0);
		enable_id(h, IDC_FILTER_HZ, mode > 0);
	}
	return FALSE;
}

static INT_PTR CALLBACK TabNsfProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
	(void)w;
	if (m == WM_INITDIALOG) {
		fill_nsf(h, &g_cfg_edit);
		return TRUE;
	}
	if (m == WM_NOTIFY)
		return page_notify(h, l, read_nsf);
	return FALSE;
}

static INT_PTR CALLBACK TabMixProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
	if (m == WM_INITDIALOG) {
		g_ui_ready = 0;
		fill_mix(h, &g_cfg_edit, 0, 5);
		g_ui_ready = 1;
		return TRUE;
	}
	if (m == WM_NOTIFY)
		return page_notify(h, l, [](HWND hw, gc_config *c) { read_mix(hw, c, 0, 5); });
	if (m == WM_COMMAND && g_ui_ready &&
	    (HIWORD(w) == BN_CLICKED || HIWORD(w) == EN_CHANGE)) {
		read_mix(h, &g_cfg_edit, 0, 5);
		apply_live();
	}
	return FALSE;
}

static void fill_n163(HWND h, const gc_config *c)
{
	CheckDlgButton(h, IDC_NSF_N163, c->nsfplay_n163_mux ? BST_CHECKED : BST_UNCHECKED);
	fill_mixer_range(h, c, 12, 19);
	if (g_sheet_restrict && fmt_is_nsf(playing_format()))
		enable_id(h, IDC_NSF_N163, playing_engine() == GC_ENG_NSFPLAY ||
		                           playing_engine() == GC_ENG_AUTO);
}

static void read_n163(HWND h, gc_config *c)
{
	if (GetDlgItem(h, IDC_NSF_N163))
		c->nsfplay_n163_mux = IsDlgButtonChecked(h, IDC_NSF_N163) ? 1 : 0;
	read_mixer_range(h, c, 12, 19);
}

static INT_PTR CALLBACK TabSysProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
	if (m == WM_INITDIALOG) {
		g_ui_ready = 0;
		fill_sys(h, &g_cfg_edit);
		g_ui_ready = 1;
		return TRUE;
	}
	if (m == WM_NOTIFY)
		return page_notify(h, l, read_sys);
	if (m == WM_COMMAND && g_ui_ready &&
	    (HIWORD(w) == BN_CLICKED || HIWORD(w) == EN_CHANGE)) {
		read_sys(h, &g_cfg_edit);
		apply_live();
	}
	return FALSE;
}

static void fill_gb(HWND h, const gc_config *c)
{
	CheckDlgButton(h, IDC_GBS_INT, c->gbs_use_int ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(h, IDC_GBS_HPF, c->gbs_highpass ? BST_CHECKED : BST_UNCHECKED);
	fill_mixer_range(h, c, 0, 3);
	if (GetDlgItem(h, IDC_TRIMGBS))
		set_float_item(h, IDC_TRIMGBS, c->trim_db[GC_FMT_GBS]);
}

static void read_gb(HWND h, gc_config *c)
{
	c->gbs_use_int = IsDlgButtonChecked(h, IDC_GBS_INT) ? 1 : 0;
	if (GetDlgItem(h, IDC_GBS_HPF))
		c->gbs_highpass = IsDlgButtonChecked(h, IDC_GBS_HPF) ? 1 : 0;
	read_mixer_range(h, c, 0, 3);
	if (GetDlgItem(h, IDC_TRIMGBS)) {
		c->trim_db[GC_FMT_GBS] = get_float_item(h, IDC_TRIMGBS, c->trim_db[GC_FMT_GBS]);
		c->trim_db[GC_FMT_GBR] = c->trim_db[GC_FMT_GBS];
	}
}

static void fill_kss(HWND h, const gc_config *c)
{
	fill_mixer_range(h, c, 0, 3);
	if (GetDlgItem(h, IDC_TRIMKSS))
		set_float_item(h, IDC_TRIMKSS, c->trim_db[GC_FMT_KSS]);
}

static void read_kss(HWND h, gc_config *c)
{
	read_mixer_range(h, c, 0, 3);
	if (GetDlgItem(h, IDC_TRIMKSS))
		c->trim_db[GC_FMT_KSS] = get_float_item(h, IDC_TRIMKSS, c->trim_db[GC_FMT_KSS]);
}

static void fill_hes(HWND h, const gc_config *c)
{
	fill_mixer_range(h, c, 0, 6);
	if (GetDlgItem(h, IDC_TRIMAY))
		set_float_item(h, IDC_TRIMAY, c->trim_db[GC_FMT_HES]);
}

static void read_hes(HWND h, gc_config *c)
{
	read_mixer_range(h, c, 0, 6);
	if (GetDlgItem(h, IDC_TRIMAY))
		c->trim_db[GC_FMT_HES] = get_float_item(h, IDC_TRIMAY, c->trim_db[GC_FMT_HES]);
}

static void fill_ay(HWND h, const gc_config *c)
{
	fill_mixer_range(h, c, 0, 3);
	if (GetDlgItem(h, IDC_TRIMAY))
		set_float_item(h, IDC_TRIMAY, c->trim_db[GC_FMT_AY]);
}

static void read_ay(HWND h, gc_config *c)
{
	read_mixer_range(h, c, 0, 3);
	if (GetDlgItem(h, IDC_TRIMAY)) {
		c->trim_db[GC_FMT_AY] = get_float_item(h, IDC_TRIMAY, c->trim_db[GC_FMT_AY]);
		c->trim_db[GC_FMT_CPC] = c->trim_db[GC_FMT_AY];
		c->trim_db[GC_FMT_SGC] = c->trim_db[GC_FMT_AY];
	}
}

static void fill_spc(HWND h, const gc_config *c)
{
	const char *spc[] = { "Off", "Linear", "Cubic" };
	HWND cb = GetDlgItem(h, IDC_SPC_INT);
	int i, sel;
	if (cb) {
		SendMessageA(cb, CB_RESETCONTENT, 0, 0);
		for (i = 0; i < 3; ++i)
			SendMessageA(cb, CB_ADDSTRING, 0, (LPARAM)spc[i]);
		sel = c->spc_interp;
		if (sel < 0 || sel > 2)
			sel = 1;
		SendMessageA(cb, CB_SETCURSEL, (WPARAM)sel, 0);
	}
	fill_mixer_range(h, c, 0, 7);
}

static void read_spc(HWND h, gc_config *c)
{
	if (GetDlgItem(h, IDC_SPC_INT)) {
		c->spc_interp = (int)SendMessageA(GetDlgItem(h, IDC_SPC_INT), CB_GETCURSEL, 0, 0);
		if (c->spc_interp < 0)
			c->spc_interp = 1;
	}
	read_mixer_range(h, c, 0, 7);
}

static void fill_gsf_page(HWND h, const gc_config *c)
{
	CheckDlgButton(h, IDC_GSF_INTERP, c->gsf_interpolation ? BST_CHECKED : BST_UNCHECKED);
	set_int_item(h, IDC_GSF_LOOPS, c->gsf_loops);
	combo_gsf(GetDlgItem(h, IDC_GSF_ENG), c->engine_gsf);
}

static void read_gsf_page(HWND h, gc_config *c)
{
	if (GetDlgItem(h, IDC_GSF_INTERP))
		c->gsf_interpolation = IsDlgButtonChecked(h, IDC_GSF_INTERP) ? 1 : 0;
	if (GetDlgItem(h, IDC_GSF_LOOPS)) {
		c->gsf_loops = get_int_item(h, IDC_GSF_LOOPS, c->gsf_loops);
		if (c->gsf_loops < 1)
			c->gsf_loops = 2;
	}
	if (GetDlgItem(h, IDC_GSF_ENG))
		c->engine_gsf = combo_get_gsf(GetDlgItem(h, IDC_GSF_ENG));
}

static void fill_vgm(HWND h, const gc_config *c)
{
	int i;
	if (GetDlgItem(h, IDC_TRIMVGM))
		set_float_item(h, IDC_TRIMVGM, c->trim_db[GC_FMT_VGM]);
	for (i = 0; i < 8; ++i) {
		if (!GetDlgItem(h, IDC_MUTE0 + i))
			continue;
		CheckDlgButton(h, IDC_MUTE0 + i, c->mute[i] ? BST_CHECKED : BST_UNCHECKED);
	}
}

static void read_vgm(HWND h, gc_config *c)
{
	int i;
	if (GetDlgItem(h, IDC_TRIMVGM)) {
		c->trim_db[GC_FMT_VGM] = get_float_item(h, IDC_TRIMVGM, c->trim_db[GC_FMT_VGM]);
		c->trim_db[GC_FMT_VGZ] = c->trim_db[GC_FMT_VGM];
		c->trim_db[GC_FMT_GYM] = c->trim_db[GC_FMT_VGM];
	}
	for (i = 0; i < 8; ++i) {
		if (!GetDlgItem(h, IDC_MUTE0 + i))
			continue;
		c->mute[i] = IsDlgButtonChecked(h, IDC_MUTE0 + i) ? 1 : 0;
	}
}

static void fill_usf_page(HWND h, const gc_config *c)
{
	CheckDlgButton(h, IDC_USF_HLE, c->usf_hle_audio ? BST_CHECKED : BST_UNCHECKED);
	if (GetDlgItem(h, IDC_LOOPS))
		set_int_item(h, IDC_LOOPS, c->loop_count);
	if (GetDlgItem(h, IDC_FADE))
		set_int_item(h, IDC_FADE, c->fade_ms);
}

static void read_usf_page(HWND h, gc_config *c)
{
	if (GetDlgItem(h, IDC_USF_HLE))
		c->usf_hle_audio = IsDlgButtonChecked(h, IDC_USF_HLE) ? 1 : 0;
	if (GetDlgItem(h, IDC_LOOPS))
		c->loop_count = get_int_item(h, IDC_LOOPS, c->loop_count);
	if (GetDlgItem(h, IDC_FADE))
		c->fade_ms = get_int_item(h, IDC_FADE, c->fade_ms);
}

static INT_PTR live_mix_tab(HWND h, UINT m, WPARAM w, LPARAM l, int lo, int hi,
                            void (*fill)(HWND, const gc_config *),
                            void (*readfn)(HWND, gc_config *))
{
	if (m == WM_INITDIALOG) {
		g_ui_ready = 0;
		if (fill)
			fill(h, &g_cfg_edit);
		else
			fill_mixer_range(h, &g_cfg_edit, lo, hi);
		g_ui_ready = 1;
		return TRUE;
	}
	if (m == WM_NOTIFY)
		return page_notify(h, l, readfn);
	if (m == WM_COMMAND && g_ui_ready &&
	    (HIWORD(w) == BN_CLICKED || HIWORD(w) == EN_CHANGE ||
	     HIWORD(w) == CBN_SELCHANGE)) {
		if (readfn)
			readfn(h, &g_cfg_edit);
		apply_live();
	}
	(void)lo;
	(void)hi;
	return FALSE;
}

static INT_PTR CALLBACK TabGbProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
	return live_mix_tab(h, m, w, l, 0, 3, fill_gb, read_gb);
}
static INT_PTR CALLBACK TabKssProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
	return live_mix_tab(h, m, w, l, 0, 3, fill_kss, read_kss);
}
static INT_PTR CALLBACK TabHesProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
	return live_mix_tab(h, m, w, l, 0, 6, fill_hes, read_hes);
}
static INT_PTR CALLBACK TabAyProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
	return live_mix_tab(h, m, w, l, 0, 3, fill_ay, read_ay);
}
static INT_PTR CALLBACK TabSpcProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
	return live_mix_tab(h, m, w, l, 0, 7, fill_spc, read_spc);
}
static INT_PTR CALLBACK TabVgmProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
	return live_mix_tab(h, m, w, l, 0, 7, fill_vgm, read_vgm);
}
static INT_PTR CALLBACK TabVrc6Proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
	return live_mix_tab(h, m, w, l, 6, 8, NULL,
	                    [](HWND hw, gc_config *c) { read_mixer_range(hw, c, 6, 8); });
}
static INT_PTR CALLBACK TabMmc5Proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
	return live_mix_tab(h, m, w, l, 9, 11, NULL,
	                    [](HWND hw, gc_config *c) { read_mixer_range(hw, c, 9, 11); });
}
static INT_PTR CALLBACK TabN163Proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
	return live_mix_tab(h, m, w, l, 12, 19, fill_n163, read_n163);
}
static INT_PTR CALLBACK TabVrc7Proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
	return live_mix_tab(h, m, w, l, 20, 25, NULL,
	                    [](HWND hw, gc_config *c) { read_mixer_range(hw, c, 20, 25); });
}
static INT_PTR CALLBACK Tab5bProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
	return live_mix_tab(h, m, w, l, 26, 28, NULL,
	                    [](HWND hw, gc_config *c) { read_mixer_range(hw, c, 26, 28); });
}
static INT_PTR CALLBACK TabGsfProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
	return live_mix_tab(h, m, w, l, 0, 0, fill_gsf_page, read_gsf_page);
}
static INT_PTR CALLBACK TabUsfProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
	return live_mix_tab(h, m, w, l, 0, 0, fill_usf_page, read_usf_page);
}

static INT_PTR CALLBACK TabInfoProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
	char text[4096];
	(void)w;
	if (m == WM_INITDIALOG) {
		format_file_info(text, sizeof text);
		SetDlgItemTextA(h, IDC_INFOBOX, text);
		return TRUE;
	}
	if (m == WM_NOTIFY)
		return page_notify(h, l, NULL);
	return FALSE;
}

enum {
	P_GEN = 0, P_NSF, P_NES, P_VRC6, P_MMC5, P_N163, P_VRC7, P_5B,
	P_GB, P_KSS, P_HES, P_AY, P_SPC, P_VGM, P_GSF, P_USF, P_INFO
};

static int tab_wanted(int fileinfo, int kind)
{
	gc_format f;
	int exp;
	if (!fileinfo)
		return 1;
	f = playing_format();
	if (f == GC_FMT_UNKNOWN)
		return kind == P_GEN || kind == P_INFO;
	exp = playing_nsf_exp();
	switch (kind) {
	case P_GEN:
	case P_INFO:
		return 1;
	case P_NSF:
	case P_NES:
		return fmt_is_nsf(f);
	case P_VRC6:
		return fmt_is_nsf(f) && (exp & GC_NSF_VRC6) != 0;
	case P_MMC5:
		return fmt_is_nsf(f) && (exp & GC_NSF_MMC5) != 0;
	case P_N163:
		return fmt_is_nsf(f) && (exp & GC_NSF_N163) != 0;
	case P_VRC7:
		return fmt_is_nsf(f) && (exp & GC_NSF_VRC7) != 0;
	case P_5B:
		return fmt_is_nsf(f) && (exp & GC_NSF_5B) != 0;
	case P_GB:
		return f == GC_FMT_GBS || f == GC_FMT_GBR;
	case P_KSS:
		return f == GC_FMT_KSS;
	case P_HES:
		return f == GC_FMT_HES;
	case P_AY:
		return f == GC_FMT_AY || f == GC_FMT_CPC || f == GC_FMT_SGC;
	case P_SPC:
		return f == GC_FMT_SPC || f == GC_FMT_RSN;
	case P_VGM:
		return f == GC_FMT_VGM || f == GC_FMT_VGZ || f == GC_FMT_GYM;
	case P_GSF:
		return f == GC_FMT_GSF || f == GC_FMT_MINIGSF;
	case P_USF:
		return f == GC_FMT_USF || f == GC_FMT_MINIUSF;
	default:
		return 1;
	}
}

static int gc_open_sheet(HWND parent, int fileinfo)
{
	PROPSHEETPAGEA psp[20];
	PROPSHEETHEADERA psh;
	INITCOMMONCONTROLSEX icc;
	int i, n = 0, info_at = 0;
	struct {
		const char *title;
		WORD id;
		DLGPROC proc;
		int kind;
	} pages[] = {
		{ "General", IDD_TAB_GENERAL, TabGeneralProc, P_GEN },
		{ "NSF", IDD_TAB_NSF, TabNsfProc, P_NSF },
		{ "NES mixer", IDD_TAB_MIX, TabMixProc, P_NES },
		{ "VRC6", IDD_TAB_VRC6, TabVrc6Proc, P_VRC6 },
		{ "MMC5", IDD_TAB_MMC5, TabMmc5Proc, P_MMC5 },
		{ "N163", IDD_TAB_N163, TabN163Proc, P_N163 },
		{ "VRC7", IDD_TAB_VRC7, TabVrc7Proc, P_VRC7 },
		{ "FME-07", IDD_TAB_5B, Tab5bProc, P_5B },
		{ "Game Boy", IDD_TAB_GB, TabGbProc, P_GB },
		{ "MSX / KSS", IDD_TAB_KSS, TabKssProc, P_KSS },
		{ "HES / PCE", IDD_TAB_HES, TabHesProc, P_HES },
		{ "AY / CPC / SGC", IDD_TAB_AY, TabAyProc, P_AY },
		{ "SPC", IDD_TAB_SPC, TabSpcProc, P_SPC },
		{ "VGM / GYM", IDD_TAB_VGM, TabVgmProc, P_VGM },
		{ "GSF", IDD_TAB_GSF, TabGsfProc, P_GSF },
		{ "USF", IDD_TAB_USF, TabUsfProc, P_USF },
		{ "File Info", IDD_TAB_INFO, TabInfoProc, P_INFO }
	};
	if (!g_hinst)
		return 0;
	ensure_cfg();
	g_cfg_edit = g_cfg;
	g_sheet_fileinfo = fileinfo;
	/* Config keeps every tab enabled so defaults can be set while a tune plays.
	   File Info hides unused tabs and greys leftover knobs for this file.
	   g_last_ok is also set by playlist GetFileInfo and must not lock Config. */
	g_sheet_restrict = fileinfo ? 1 : 0;
	icc.dwSize = sizeof icc;
	icc.dwICC = ICC_WIN95_CLASSES;
	InitCommonControlsEx(&icc);
	memset(psp, 0, sizeof psp);
	for (i = 0; i < (int)(sizeof pages / sizeof pages[0]); ++i) {
		if (!tab_wanted(fileinfo, pages[i].kind))
			continue;
		if (pages[i].kind == P_INFO)
			info_at = n;
		psp[n].dwSize = sizeof(PROPSHEETPAGEA);
		psp[n].dwFlags = PSP_USETITLE;
		psp[n].hInstance = g_hinst;
		psp[n].pszTemplate = MAKEINTRESOURCEA(pages[i].id);
		psp[n].pszTitle = pages[i].title;
		psp[n].pfnDlgProc = pages[i].proc;
		n++;
	}
	if (n < 1)
		return 0;
	memset(&psh, 0, sizeof psh);
	psh.dwSize = sizeof(PROPSHEETHEADERA);
	psh.dwFlags = PSH_PROPSHEETPAGE | PSH_NOAPPLYNOW | PSH_NOCONTEXTHELP |
	              0x00000010; /* PSH_MULTILINETABS */
	psh.hwndParent = parent;
	psh.hInstance = g_hinst;
	psh.pszCaption = fileinfo ? "Game Music — File Info" : "Game Music";
	psh.nPages = (UINT)n;
	psh.nStartPage = (fileinfo || g_play) ? (UINT)info_at : 0u;
	psh.ppsp = psp;
	return (int)PropertySheetA(&psh);
}

static INT_PTR CALLBACK OptionsProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
	NMHDR *nm;
	if (m == WM_COMMAND && LOWORD(w) == IDC_OPEN_SHEET) {
		gc_open_sheet(h, 0);
		return TRUE;
	}
	if (m == WM_COMMAND && LOWORD(w) == IDC_OPEN_INFO) {
		gc_open_sheet(h, 0);
		return TRUE;
	}
	if (m == WM_NOTIFY) {
		nm = (NMHDR *)l;
		if (nm && nm->code == PSN_APPLY) {
			SetWindowLongPtr(h, DWLP_MSGRESULT, PSNRET_NOERROR);
			return TRUE;
		}
	}
	return FALSE;
}
#endif

static void WINAPI gc_Config(HWND win)
{
	ensure_cfg();
#ifdef _WIN32
	if (g_hinst)
		/* Decoders → Config: every tab so defaults can be set. */
		gc_open_sheet(win, 0);
	else
		MessageBoxA(win, "Config resource missing.", PLUGIN_NAME, MB_OK);
#else
	(void)win;
#endif
}

static BOOL WINAPI gc_CheckFile(const char *filename, XMPFILE file)
{
	unsigned char *data = NULL;
	size_t len = 0;
	int opened = 0;
	int ok;
	gc_format fmt;

	ensure_cfg();
	remember_hint(filename);
	file = open_if_needed(filename, file, &opened);
	if (!file)
		return FALSE;
	if (!slurp_xmpfile(file, &data, &len)) {
		close_if_opened(file, opened);
		return FALSE;
	}
	close_if_opened(file, opened);
	if (gc_is_sap(data, len, filename)) {
		free(data);
		return FALSE;
	}
	fmt = gc_probe(data, len, filename);
	if (fmt == GC_FMT_ZIP || fmt == GC_FMT_SEVENZ || fmt == GC_FMT_GZIP ||
	    fmt == GC_FMT_VGZ || fmt == GC_FMT_NSZ || fmt == GC_FMT_NEZ)
		ok = archive_holds_music(data, len, filename);
	else
		ok = gc_format_claimed(fmt);
	free(data);
	return ok ? TRUE : FALSE;
}

static gc_player *open_from(const char *filename, unsigned char *data, size_t len)
{
	char *m3u = NULL;
	size_t m3u_len = 0;
	gc_player *pl;
	ensure_cfg();
	load_sidecar_m3u(filename && filename[0] ? filename : g_name_hint, &m3u, &m3u_len);
	pl = gc_player_open(data, len, filename, m3u, m3u_len, &g_cfg);
	free(m3u);
	return pl;
}

static DWORD WINAPI gc_GetFileInfo(const char *filename, XMPFILE file,
                                   float **length, char **tags)
{
	unsigned char *data = NULL;
	size_t len = 0;
	int opened = 0;
	gc_player *pl;
	gc_info inf;
	int n, i;

	if (length) *length = NULL;
	if (tags) *tags = NULL;
	ensure_cfg();
	remember_hint(filename);
	if (filename && filename[0] && g_cfg.len_cache_path[0]) {
		gc_len_rec rec;
		uint64_t sz = gc_file_size(filename);
		int64_t mt = gc_file_mtime(filename);
		if (sz && gc_len_cache_get(g_cfg.len_cache_path, filename, sz, mt, &rec) &&
		    rec.track_count > 0) {
			n = rec.track_count;
			if (n < 1)
				n = 1;
			if (length) {
				float *lens = (float *)xmp_alloc((DWORD)(sizeof(float) * (size_t)n));
				if (lens) {
					/* Confident cache hit, else 10-min placeholder — never a
					   short sync false-measure. */
					for (i = 0; i < n; ++i) {
						int ms = rec.duration_ms[i];
						if (ms < GC_M3U_MIN_MS)
							ms = GC_DEFAULT_PLAY_MS;
						lens[i] = (float)ms / 1000.0f;
					}
				}
				*length = lens;
			}
			if (tags) {
				memset(&inf, 0, sizeof inf);
				inf.format = rec.format;
				inf.track_count = n;
				memcpy(inf.game, rec.game, sizeof inf.game);
				memcpy(inf.artist, rec.artist, sizeof inf.artist);
				memcpy(inf.copyright, rec.copyright, sizeof inf.copyright);
				memcpy(inf.length_src, rec.src, sizeof inf.length_src);
				*tags = build_tags(&inf, 0);
			}
			return (DWORD)n | XMPIN_INFO_NOSUBTAGS;
		}
	}
	file = open_if_needed(filename, file, &opened);
	if (!file)
		return 0;
	if (!slurp_xmpfile(file, &data, &len)) {
		close_if_opened(file, opened);
		return 0;
	}
	close_if_opened(file, opened);
	pl = open_from(filename, data, len);
	free(data);
	if (!pl)
		return 0;
	gc_player_info(pl, &inf);
	remember_info(&inf, gc_player_engine_name(pl));
	n = inf.track_count;
	if (n < 1)
		n = 1;
	if (length) {
		float *lens = (float *)xmp_alloc((DWORD)(sizeof(float) * (size_t)n));
		if (lens) {
			for (i = 0; i < n; ++i) {
				int ms = inf.tracks[i].duration_ms;
				/* Never advertise 0 / absurd crumbs — 10-min placeholder.
				   Tagged SFX (250ms–15s) and music lengths pass through. */
				if (ms < GC_M3U_MIN_MS)
					ms = GC_DEFAULT_PLAY_MS;
				lens[i] = (float)ms / 1000.0f;
			}
		}
		*length = lens;
	}
	if (tags)
		*tags = build_tags(&inf, 0);
	gc_player_close(pl);
	return (DWORD)n | XMPIN_INFO_NOSUBTAGS;
}

static DWORD WINAPI gc_Open(const char *filename, XMPFILE file)
{
	unsigned char *data = NULL;
	size_t len = 0;
	int opened = 0;

	unload_playback();
	ensure_cfg();
	remember_hint(filename);
	file = open_if_needed(filename, file, &opened);
	if (!file)
		return 0;
	if (!slurp_xmpfile(file, &data, &len)) {
		close_if_opened(file, opened);
		return 0;
	}
	close_if_opened(file, opened);
	g_play = open_from(filename, data, len);
	free(data);
	if (!g_play)
		return 0;
	{
		gc_info inf;
		gc_player_info(g_play, &inf);
		remember_info(&inf, gc_player_engine_name(g_play));
	}
	set_length_now(gc_player_length_ms(g_play));
	return 2;
}

static void WINAPI gc_Close(void)
{
	unload_playback();
}

static void WINAPI gc_SetFormat(XMPFORMAT *form)
{
	if (!form)
		return;
	if (!g_play) {
		form->rate = 0;
		form->chan = 0;
		form->res = 0;
		form->chanmask = 0;
		return;
	}
	form->rate = (DWORD)gc_player_rate(g_play);
	form->chan = 2;
	form->res = 4;
	form->chanmask = 0;
}

static char *WINAPI gc_GetTags(void)
{
	gc_info inf;
	if (!g_play)
		return NULL;
	gc_player_info(g_play, &inf);
	return build_tags(&inf, gc_player_current_track(g_play));
}

static void WINAPI gc_GetInfoText(char *format, char *length)
{
	char tmp[256];
	int m, s, play, t, n;
	if (format) format[0] = '\0';
	if (length) length[0] = '\0';
	if (!g_play)
		return;
	t = gc_player_current_track(g_play) + 1;
	n = gc_player_track_count(g_play);
	if (format) {
		snprintf(tmp, sizeof tmp, "%s  %s  %d/%d",
		         gc_format_name(gc_player_format(g_play)),
		         gc_player_engine_name(g_play), t, n);
		sanitize_line(tmp);
		bounded_copy(format, 256, tmp);
	}
	if (length) {
		play = gc_player_length_ms(g_play);
		if (play > 0) {
			m = play / 60000;
			s = (play / 1000) % 60;
			snprintf(tmp, sizeof tmp, "%d:%02d", m, s);
		} else {
			snprintf(tmp, sizeof tmp, "?");
		}
		sanitize_line(tmp);
		bounded_copy(length, 256, tmp);
	}
}

static void WINAPI gc_GetGeneralInfo(char *buf)
{
	char local[4096];
	char *p, *end;
	char num[32];
	gc_info inf;
	if (!buf)
		return;
	buf[0] = '\0';
	if (!g_play)
		return;
	gc_player_info(g_play, &inf);
	p = local;
	end = local + sizeof local - 2;
	local[0] = '\0';
	write_kv(&p, end, "Title", inf.game);
	write_kv(&p, end, "Artist", inf.artist);
	write_kv(&p, end, "Copyright", inf.copyright);
	write_kv(&p, end, "Ripper", inf.ripper);
	write_kv(&p, end, "Comment", inf.comment);
	write_kv(&p, end, "Format", gc_format_name(inf.format));
	write_kv(&p, end, "Engine", gc_player_engine_name(g_play));
	write_kv(&p, end, "System", inf.system);
	snprintf(num, sizeof num, "%d / %d", gc_player_current_track(g_play) + 1,
	         gc_player_track_count(g_play));
	write_kv(&p, end, "Track", num);
	{
		int ms = gc_player_length_ms(g_play);
		if (ms > 0)
			snprintf(num, sizeof num, "%d:%02d", ms / 60000, (ms / 1000) % 60);
		else
			snprintf(num, sizeof num, "unknown");
		write_kv(&p, end, "Length", num);
	}
	write_kv(&p, end, "Length source", inf.length_src[0] ? inf.length_src : "unknown");
	if (inf.lib_path[0])
		write_kv(&p, end,
		         (inf.format == GC_FMT_USF || inf.format == GC_FMT_MINIUSF)
		             ? "usflib" : "gsflib",
		         inf.lib_path);
	else if (inf.lib_name[0])
		write_kv(&p, end, "Library", inf.lib_name);
	if (inf.nsf_load || inf.nsf_init || inf.nsf_play) {
		char addr[80];
		snprintf(addr, sizeof addr, "$%04X / $%04X / $%04X",
		         inf.nsf_load & 0xFFFF, inf.nsf_init & 0xFFFF, inf.nsf_play & 0xFFFF);
		write_kv(&p, end, "NSF load/init/play", addr);
		if (inf.nsf_has_bank) {
			snprintf(addr, sizeof addr, "$%02X $%02X $%02X $%02X $%02X $%02X $%02X $%02X",
			         inf.nsf_bank[0], inf.nsf_bank[1], inf.nsf_bank[2], inf.nsf_bank[3],
			         inf.nsf_bank[4], inf.nsf_bank[5], inf.nsf_bank[6], inf.nsf_bank[7]);
			write_kv(&p, end, "NSF banks", addr);
		}
		{
			char chips[80];
			chips[0] = '\0';
			if (inf.nsf_exp & GC_NSF_VRC6) strcat(chips, " VRC6");
			if (inf.nsf_exp & GC_NSF_VRC7) strcat(chips, " VRC7");
			if (inf.nsf_exp & GC_NSF_FDS) strcat(chips, " FDS");
			if (inf.nsf_exp & GC_NSF_MMC5) strcat(chips, " MMC5");
			if (inf.nsf_exp & GC_NSF_N163) strcat(chips, " N163");
			if (inf.nsf_exp & GC_NSF_5B) strcat(chips, " 5B");
			snprintf(addr, sizeof addr, "v%d  chips $%02X%s  NSF2 $%02X",
			         inf.nsf_ver, inf.nsf_exp & 0xFF,
			         chips[0] ? chips : "", inf.nsf2_bits & 0xFF);
			write_kv(&p, end, "NSF", addr);
		}
	}
	write_kv(&p, end, "Player", PLUGIN_NAME " " PLUGIN_VERSION);
	write_kv(&p, end, "Note", "Native XMPlay Game Music — not xmp-gme / in_nez");
	bounded_copy(buf, INFO_WRITE_MAX, local);
}

static void WINAPI gc_GetMessage(char *buf)
{
	if (!buf)
		return;
	format_file_info(buf, INFO_WRITE_MAX);
}

static double WINAPI gc_GetGranularity(void)
{
	return 0.001;
}

static double WINAPI gc_SetPosition(DWORD pos)
{
	int sub, ms;
	if (!g_play)
		return -1.0;
	if (pos == (DWORD)XMPIN_POS_LOOP || pos == (DWORD)XMPIN_POS_AUTOLOOP)
		return -2.0;
	if (pos & XMPIN_POS_SUBSONG) {
		/* LOWORD is a signed subsong index: absolute >=0, or a relative
		   step (Shift+Left sends -1). Same contract as xmp-pokey/xmp-tfmx. */
		sub = (short)(pos & 0xFFFFu);
		if (sub < 0)
			sub = gc_player_current_track(g_play) + sub;
		if (sub < 0 || sub >= gc_player_track_count(g_play))
			return -1.0;
		if (gc_player_set_track(g_play, sub) != 0)
			return -1.0;
		set_length_now(gc_player_length_ms(g_play));
		if (xmpfin && xmpfin->UpdateTitle)
			xmpfin->UpdateTitle(NULL);
		return 0.0;
	}
	ms = gc_player_seek_ms(g_play, (int)pos);
	if (ms < 0)
		return -1.0;
	return (double)ms / 1000.0;
}

static DWORD WINAPI gc_Process(float *buf, DWORD count)
{
	int frames, got, ms;
	if (!buf || !g_play)
		return 0;
	frames = (int)(count / 2u);
	if (frames <= 0)
		return 0;
	got = gc_player_process(g_play, buf, frames);
	/* 1.0.9: allow live SetLength shrink for short SFX silence-end (<15s).
	   Long-music deferred measure stays cache-only (no dirty / no shrink). */
	if (gc_player_length_updated(g_play, &ms)) {
		int cur = gc_player_length_ms(g_play);
		if (ms > 0 && (cur <= 0 || ms >= cur || ms < GC_MEAS_SFX_MAX_MS))
			set_length_now(ms);
	}
	if (got <= 0)
		return 0;
	return (DWORD)got * 2u;
}

static DWORD WINAPI gc_GetSubSongs(float *length)
{
	int n, i, sum = 0;
	gc_info inf;
	if (!g_play)
		return 0;
	n = gc_player_track_count(g_play);
	if (n < 1)
		n = 1;
	gc_player_info(g_play, &inf);
	for (i = 0; i < n; ++i)
		sum += inf.tracks[i].duration_ms > 0 ? inf.tracks[i].duration_ms : 0;
	if (length)
		*length = (float)sum / 1000.0f;
	return (DWORD)n;
}

static const char g_exts[] =
	"Game Music\0"
	"ay/gbs/gbr/gym/hes/kss/nsf/nsfe/nez/nsz/nsd/rsn/sgc/spc/vgm/vgz/cpc/"
	"gsf/minigsf/usf/miniusf/zip/gz/7z";

static XMPIN g_xmpin = {
	XMPIN_FLAG_CONFIG | XMPIN_FLAG_OPTIONS,
	PLUGIN_NAME " " PLUGIN_VERSION,
	g_exts,
	gc_About,
	gc_Config,
	gc_CheckFile,
	gc_GetFileInfo,
	gc_Open,
	gc_Close,
	NULL,
	gc_SetFormat,
	gc_GetTags,
	gc_GetInfoText,
	gc_GetGeneralInfo,
	gc_GetMessage,
	gc_SetPosition,
	gc_GetGranularity,
	NULL,
	gc_Process,
	NULL,
	NULL,
	gc_GetSubSongs,
	NULL,
	NULL,
	NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL,
	NULL,
#ifdef _WIN32
	OptionsProc
#else
	NULL
#endif
};

static XMPIN *WINAPI xmpin_get_interface_impl(DWORD face, InterfaceProc faceproc)
{
	if (face != XMPIN_FACE)
		return NULL;
	if (!faceproc)
		return NULL;
	xmpfin = (XMPFUNC_IN *)faceproc(XMPFUNC_IN_FACE);
	xmpfmisc = (XMPFUNC_MISC *)faceproc(XMPFUNC_MISC_FACE);
	xmpffile = (XMPFUNC_FILE *)faceproc(XMPFUNC_FILE_FACE);
	if (!xmpfin || !xmpfmisc || !xmpffile)
		return NULL;
	if (!xmpfmisc->Alloc || !xmpffile->Read)
		return NULL;
	ensure_cfg();
	(void)PLUGIN_XMPVER;
	return &g_xmpin;
}

extern "C" {

BOOL WINAPI DllMain(HINSTANCE hDLL, DWORD reason, LPVOID reserved)
{
	(void)reserved;
	if (reason == DLL_PROCESS_ATTACH) {
#ifdef _WIN32
		g_hinst = hDLL;
		DisableThreadLibraryCalls(hDLL);
#else
		(void)hDLL;
#endif
	}
	return TRUE;
}

#if defined(__GNUC__) && defined(_WIN32) && !defined(_WIN64)
XMPIN *WINAPI XMPIN_GetInterface_(DWORD face, InterfaceProc faceproc)
{
	return xmpin_get_interface_impl(face, faceproc);
}
#if __GNUC__ >= 8
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wattribute-alias"
#endif
__attribute__((dllexport)) void XMPIN_GetInterface(void)
	__attribute__((alias("XMPIN_GetInterface_@8")));
#if __GNUC__ >= 8
#pragma GCC diagnostic pop
#endif
#else
__declspec(dllexport) XMPIN *WINAPI XMPIN_GetInterface(DWORD face, InterfaceProc faceproc)
{
	return xmpin_get_interface_impl(face, faceproc);
}
#endif

} /* extern "C" */
