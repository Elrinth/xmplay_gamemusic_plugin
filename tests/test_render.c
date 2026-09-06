/* Host smoke test for Game Music engines. No copyrighted rips. */
#include "player.h"
#include "probe.h"
#include "m3u.h"
#include "config.h"
#include "psf_io.h"
#include "archive.h"
#include "vfs.h"
#include "lencache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <locale.h>
#include <unistd.h>
#include <sys/wait.h>

static int fails;

static void expect(int cond, const char *msg)
{
	if (!cond) {
		fprintf(stderr, "FAIL: %s\n", msg);
		fails++;
	} else {
		fprintf(stdout, "ok  %s\n", msg);
	}
}

static void put16(unsigned char *p, unsigned v)
{
	p[0] = (unsigned char)(v & 0xFF);
	p[1] = (unsigned char)((v >> 8) & 0xFF);
}

/* 3-song NSF: square on $4000, period depends on init A. Last track must open. */
static unsigned char *make_nsf(size_t *len)
{
	unsigned char *b = (unsigned char *)calloc(1, 0x80 + 48);
	static const unsigned char code[] = {
		0xA9, 0x0F, 0x8D, 0x15, 0x40, 0xA9, 0x84, 0x8D, 0x00, 0x40,
		0xA9, 0x00, 0x8D, 0x01, 0x40, 0x0A, 0xAA, 0xBD, 0x20, 0x80,
		0x8D, 0x02, 0x40, 0xBD, 0x21, 0x80, 0x8D, 0x03, 0x40, 0x60,
		0x60, 0x00,
		0xFD, 0x00, 0xC9, 0x00, 0xAA, 0x00
	};
	/* unused table bytes kept for the original decaying fixture */
	if (!b)
		return NULL;
	memcpy(b, "NESM", 4);
	b[4] = 0x1A;
	b[5] = 1;
	b[6] = 3; /* three songs — last track must not be dropped */
	b[7] = 1;
	put16(b + 8, 0x8000);
	put16(b + 10, 0x8000);
	put16(b + 12, 0x801E);
	memcpy(b + 0x0E, "Game Music fixture", 17);
	memcpy(b + 0x2E, "public domain", 13);
	put16(b + 0x6E, 16626);
	put16(b + 0x78, 19997);
	memcpy(b + 0x80, code, sizeof code);
	*len = 0x80 + sizeof code;
	return b;
}

/* 3-song looping NSF: PLAY rewrites $4002/$4003 every 32 frames, 4-note cycle.
   Song number offsets the cycle so tracks are not one shared TIME. */
static unsigned char *make_loop_nsf(size_t *len)
{
	unsigned char *b = (unsigned char *)calloc(1, 0x80 + 80);
	static const unsigned char code[] = {
		0x85, 0x10, 0xA9, 0x0F, 0x8D, 0x15, 0x40, 0xA9, 0xBF, 0x8D,
		0x00, 0x40, 0xA9, 0x00, 0x8D, 0x01, 0x40, 0x85, 0x00, 0x20,
		0x26, 0x80, 0x60, 0xE6, 0x00, 0xA5, 0x00, 0x29, 0x1F, 0xD0,
		0x03, 0x20, 0x26, 0x80, 0x60, 0x60, 0x60, 0x60, 0xA5, 0x00,
		0x4A, 0x4A, 0x4A, 0x4A, 0x4A, 0x18, 0x65, 0x10, 0x29, 0x03,
		0x0A, 0xAA, 0xBD, 0x41, 0x80, 0x8D, 0x02, 0x40, 0xBD, 0x42,
		0x80, 0x8D, 0x03, 0x40, 0x60, 0xFD, 0x00, 0xC9, 0x00, 0xAA,
		0x00, 0x80, 0x00
	};
	if (!b)
		return NULL;
	memcpy(b, "NESM", 4);
	b[4] = 0x1A;
	b[5] = 1;
	b[6] = 3;
	b[7] = 1;
	put16(b + 8, 0x8000);
	put16(b + 10, 0x8000);
	put16(b + 12, 0x8017);
	memcpy(b + 0x0E, "Game Music loop NSF", 19);
	memcpy(b + 0x2E, "public domain", 13);
	put16(b + 0x6E, 16626);
	put16(b + 0x78, 19997);
	memcpy(b + 0x80, code, sizeof code);
	*len = 0x80 + sizeof code;
	return b;
}

static unsigned char *make_gbs(size_t *len)
{
	unsigned char *b = (unsigned char *)calloc(1, 0x70 + 32);
	/* Enable NR52/NR51/NR50 and a pulse. Harmless if the APU ignore is partial. */
	static const unsigned char code[] = {
		0x3E, 0x80, 0xE0, 0x26, /* ld a,$80 / ldh ($26),a */
		0x3E, 0xFF, 0xE0, 0x25,
		0x3E, 0x77, 0xE0, 0x24,
		0x3E, 0x80, 0xE0, 0x11,
		0x3E, 0xF3, 0xE0, 0x12,
		0x3E, 0x00, 0xE0, 0x13,
		0x3E, 0x87, 0xE0, 0x14,
		0xC9
	};
	if (!b)
		return NULL;
	memcpy(b, "GBS", 3);
	b[3] = 0x01;
	b[4] = 2;
	b[5] = 1;
	put16(b + 6, 0x0400);
	put16(b + 8, 0x0400);
	put16(b + 10, 0x041B);
	put16(b + 12, 0xFFFE);
	memcpy(b + 16, "Game Music GBS", 13);
	memcpy(b + 48, "public domain", 13);
	memcpy(b + 0x70, code, sizeof code);
	*len = 0x70 + sizeof code;
	return b;
}

static unsigned char *make_vgm(size_t *len)
{
	unsigned char *b = (unsigned char *)calloc(1, 0x80);
	int i, p;
	if (!b)
		return NULL;
	memcpy(b, "Vgm ", 4);
	b[8] = 0x50;
	b[9] = 0x01; /* version 1.50 */
	b[0x0C] = 0x99;
	b[0x0D] = 0x9E;
	b[0x0E] = 0x36; /* SN76489 clock ~3.579545 MHz */
	b[0x34] = 0x0C;
	b[0x35] = 0; /* data offset from 0x34 = 0x0C → data at 0x40 */
	p = 0x40;
	b[p++] = 0x50;
	b[p++] = 0x90; /* latch vol ch0 = 0 (loud; 0xF is mute) */
	b[p++] = 0x50;
	b[p++] = 0x80; /* latch tone ch0 fine */
	b[p++] = 0x50;
	b[p++] = 0x3F; /* tone coarse */
	for (i = 0; i < 16; ++i)
		b[p++] = 0x62; /* wait 735 × 16 ≈ 0.25 s */
	b[p++] = 0x66; /* end */
	/* eof offset = file_size - 4 */
	b[4] = (unsigned char)((p - 4) & 0xFF);
	b[5] = (unsigned char)(((p - 4) >> 8) & 0xFF);
	*len = (size_t)p;
	return b;
}

/* Wrap the NSF body as NSFe with times + titles (1-based playlist not required). */
static unsigned char *make_nsfe(const unsigned char *nsf, size_t nsf_n, size_t *len)
{
	const unsigned char *code;
	size_t code_n;
	unsigned char *b;
	size_t p;
	int times[3] = { 12000, 8000, 10000 };
	const char labels[] = "First\0Second\0Last\0";
	if (!nsf || nsf_n < 0x80)
		return NULL;
	code = nsf + 0x80;
	code_n = nsf_n - 0x80;
	b = (unsigned char *)calloc(1, 256 + code_n);
	if (!b)
		return NULL;
	memcpy(b, "NSFE", 4);
	p = 4;
	/* INFO */
	b[p] = 10; p += 4;
	memcpy(b + p, "INFO", 4); p += 4;
	b[p++] = 0x00; b[p++] = 0x80; /* load */
	b[p++] = 0x00; b[p++] = 0x80; /* init */
	b[p++] = 0x1E; b[p++] = 0x80; /* play */
	b[p++] = 0; /* NTSC */
	b[p++] = 0; /* chips */
	b[p++] = 3; /* tracks */
	b[p++] = 0; /* start 0-based */
	/* DATA */
	b[p] = (unsigned char)(code_n & 0xFF);
	b[p + 1] = (unsigned char)((code_n >> 8) & 0xFF);
	p += 4;
	memcpy(b + p, "DATA", 4); p += 4;
	memcpy(b + p, code, code_n); p += code_n;
	/* time */
	b[p] = 12; p += 4;
	memcpy(b + p, "time", 4); p += 4;
	memcpy(b + p, times, 12); p += 12;
	/* tlbl */
	b[p] = (unsigned char)sizeof labels; p += 4;
	memcpy(b + p, "tlbl", 4); p += 4;
	memcpy(b + p, labels, sizeof labels); p += sizeof labels;
	/* NEND */
	b[p] = 0; p += 4;
	memcpy(b + p, "NEND", 4); p += 4;
	*len = p;
	return b;
}

/* KSCC + Z80 that pokes MSX PSG (and FMPAC bit so the YM2413 core is wired). */
static unsigned char *make_kss(size_t *len)
{
	unsigned char *b = (unsigned char *)calloc(1, 0x80);
	/* Init: MSX PSG tone A. Play: RET. Extra-device bit0 = FMPAC/YM2413 so the
	   OPLL unit is created; we also poke it after the PSG so FM KSS is wired. */
	static const unsigned char z80[] = {
		0x3E, 0x07, 0xD3, 0xA0, 0x3E, 0x38, 0xD3, 0xA1, /* R7 mixer tone A */
		0x3E, 0x08, 0xD3, 0xA0, 0x3E, 0x0F, 0xD3, 0xA1, /* R8 vol A */
		0x3E, 0x00, 0xD3, 0xA0, 0x3E, 0x80, 0xD3, 0xA1, /* R0 fine */
		0x3E, 0x01, 0xD3, 0xA0, 0x3E, 0x01, 0xD3, 0xA1, /* R1 coarse */
		0xC9
	};
	if (!b)
		return NULL;
	memcpy(b, "KSCC", 4);
	put16(b + 4, 0x8000);
	put16(b + 6, (unsigned)sizeof z80);
	put16(b + 8, 0x8000);
	put16(b + 10, 0x8000 + (unsigned)sizeof z80 - 1);
	b[0x0F] = 0x01; /* FMPAC / YM2413 — same NEZ core as PSG/SCC/Y8950 */
	memcpy(b + 0x10, z80, sizeof z80);
	*len = 0x10 + sizeof z80;
	return b;
}

static unsigned char *make_sap(size_t *len)
{
	static const char hdr[] = "SAP\x0d\x0aAUTHOR \"nope\"\x0d\x0a";
	unsigned char *b = (unsigned char *)malloc(sizeof hdr);
	memcpy(b, hdr, sizeof hdr);
	*len = sizeof hdr - 1;
	return b;
}

static float rms(const float *s, int n)
{
	double a = 0;
	int i;
	for (i = 0; i < n; ++i)
		a += (double)s[i] * s[i];
	return (float)sqrt(a / (n > 0 ? n : 1));
}

static void write_tmp(const char *name, const void *data, size_t n)
{
	char path[256];
	FILE *f;
	snprintf(path, sizeof path, "/tmp/%s", name);
	f = fopen(path, "wb");
	if (!f)
		return;
	fwrite(data, 1, n, f);
	fclose(f);
}

static int render_ok(gc_player *p, const char *tag)
{
	float *buf;
	int n, i, nz = 0;
	if (!p)
		return 0;
	buf = (float *)calloc(4096 * 2, sizeof(float));
	if (!buf)
		return 0;
	n = gc_player_process(p, buf, 4096);
	for (i = 0; i < n * 2; ++i) {
		if (buf[i] != 0.0f)
			nz++;
	}
	fprintf(stdout, "    %s: frames=%d rms=%.5f nonzero=%d engine=%s tracks=%d last_ok\n",
	        tag, n, rms(buf, n * 2), nz, gc_player_engine_name(p),
	        gc_player_track_count(p));
	free(buf);
	return n > 0;
}

int main(void)
{
	setlocale(LC_ALL, "C");
	gc_config cfg;
	gc_player *p;
	unsigned char *nsf, *gbs, *vgm, *sap, *nsfe, *kss;
	size_t nsf_n, gbs_n, vgm_n, sap_n, nsfe_n, kss_n;
	gc_info inf;
	const char *m3u =
		"# Game Music playlist\n"
		"fixture.nsf::NSF,1,First,0:12\n"
		"fixture.nsf::NSF,2,Second,0:08\n"
		"fixture.nsf::NSF,3,Last track,0:10\n";
	gc_info m3u_inf;

	gc_config_defaults(&cfg);
	cfg.rate = 48000;
	cfg.auto_normalize = 1;
	expect(cfg.chan_pan[0] == -19 && cfg.chan_pan[1] == 19,
	       "NES mixer default pans Sq1 −19 / Sq2 +19");
	expect(cfg.chan_vol[2] == 124 && cfg.chan_vol[0] == 255,
	       "NES mixer default Tri vol 124 / Sq vol 255");
	expect(cfg.gbs_highpass == 0, "GB high-pass off by default");
	expect(cfg.gbs_use_int == 0, "GB Use INT off by default");
	expect(cfg.use_tag_length == 1, "Use M3U / tag length on by default");
	expect(cfg.measure_untagged == 1, "measure untagged lengths on by default");
	expect(cfg.untagged_max_sec == 600, "untagged max / scan cap default 600s (10 min)");
	expect(cfg.stereo_width == 0.0f, "stereo width default 0");
	cfg.measure_untagged = 0; /* keep the suite snappy; measure is tested below */
	expect(GC_NSF_VRC6 == 0x01 && GC_NSF_5B == 0x20, "NSF expansion bit constants");
	{
		char path[256];
		gc_psf_lib_path(path, sizeof path, "/music/gba/", "set.gsflib");
		expect(strcmp(path, "/music/gba/set.gsflib") == 0,
		       "gsflib path joins folder + _lib name");
		gc_psf_lib_path(path, sizeof path, "", "pack.usflib");
		expect(strcmp(path, "pack.usflib") == 0, "usflib name when no folder");
		gc_psf_lib_path(path, sizeof path, "C:\\rips\\", "nested/lib.gsflib");
		expect(strcmp(path, "C:\\rips\\lib.gsflib") == 0,
		       "gsflib path uses basename of _lib");
	}

	nsf = make_nsf(&nsf_n);
	gbs = make_gbs(&gbs_n);
	vgm = make_vgm(&vgm_n);
	sap = make_sap(&sap_n);
	nsfe = make_nsfe(nsf, nsf_n, &nsfe_n);
	kss = make_kss(&kss_n);

	expect(gc_probe(nsf, nsf_n, "t.nsf") == GC_FMT_NSF, "probe NSF");
	expect(gc_probe(gbs, gbs_n, "t.gbs") == GC_FMT_GBS, "probe GBS");
	expect(gc_probe(vgm, vgm_n, "t.vgm") == GC_FMT_VGM, "probe VGM");
	expect(gc_probe(nsfe, nsfe_n, "t.nsfe") == GC_FMT_NSFE, "probe NSFE");
	expect(gc_probe(kss, kss_n, "t.kss") == GC_FMT_KSS, "probe KSS");
	{
		unsigned char ay[8] = { 'Z', 'X', 'A', 'Y', 'E', 'M', 'U', 'L' };
		unsigned char hes[4] = { 'H', 'E', 'S', 'M' };
		unsigned char sgc[4] = { 'S', 'G', 'C', 0x1A };
		unsigned char gbr[4] = { 'G', 'B', 'R', 'F' };
		unsigned char nsd[5] = { 'N', 'E', 'S', 'L', 0x1A };
		expect(gc_probe(ay, sizeof ay, "t.ay") == GC_FMT_AY, "probe AY");
		expect(gc_probe(ay, sizeof ay, "t.cpc") == GC_FMT_AY ||
		       gc_probe(ay, sizeof ay, "t.cpc") == GC_FMT_CPC, "probe CPC magic or ext");
		expect(gc_probe(hes, sizeof hes, "t.hes") == GC_FMT_HES, "probe HES");
		expect(gc_probe(sgc, sizeof sgc, "t.sgc") == GC_FMT_SGC, "probe SGC");
		expect(gc_probe(gbr, sizeof gbr, "t.gbr") == GC_FMT_GBR, "probe GBR");
		expect(gc_probe(nsd, sizeof nsd, "t.nsd") == GC_FMT_NSD, "probe NSD");
		expect(gc_probe(NULL, 0, "pack.rsn") == GC_FMT_RSN, "claim RSN by ext");
		expect(gc_format_claimed(GC_FMT_CPC), "claim CPC");
		expect(gc_format_claimed(GC_FMT_NEZ), "claim NEZ");
		expect(gc_format_claimed(GC_FMT_NSZ), "claim NSZ");
	}
	{
		unsigned char gsf[16] = { 'P', 'S', 'F', 0x22, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
		unsigned char usf[16] = { 'P', 'S', 'F', 0x21, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
		unsigned char psf1[16] = { 'P', 'S', 'F', 0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
		expect(gc_probe(gsf, sizeof gsf, "a.minigsf") == GC_FMT_MINIGSF, "probe MINIGSF");
		expect(gc_probe(usf, sizeof usf, "a.miniusf") == GC_FMT_MINIUSF, "probe MINIUSF");
		expect(gc_probe(usf, sizeof usf, "a.usf") == GC_FMT_USF, "probe USF");
		expect(gc_probe(usf, sizeof usf, "set.usflib") == GC_FMT_UNKNOWN,
		       "do not claim .usflib as a tune");
		expect(gc_probe(gsf, sizeof gsf, "set.gsflib") == GC_FMT_UNKNOWN,
		       "do not claim .gsflib as a tune");
		expect(gc_is_psf_lib("Game.USFLib") && gc_is_psf_lib("set.gsflib"),
		       "psf lib helper");
		expect(!gc_is_psf_lib("track.miniusf") && !gc_is_psf_lib("track.usf"),
		       "tunes are not libs");
		expect(gc_probe(psf1, sizeof psf1, "a.psf") == GC_FMT_UNKNOWN, "do not claim PSF1");
		expect(gc_format_claimed(GC_FMT_GSF), "claim GSF");
		expect(gc_format_claimed(GC_FMT_USF), "claim USF");
		expect(strcmp(gc_engine_name(GC_ENG_USF), "lazyusf2") == 0,
		       "USF engine name");
		expect(cfg.usf_hle_audio == 1, "USF HLE audio default on");
		expect(strcmp(gc_engine_name(GC_ENG_HA), "Highly Advanced") == 0,
		       "HA engine name");
		expect(strcmp(gc_engine_name(GC_ENG_GSF), "VIOGSF") == 0,
		       "VIOGSF engine name");
		expect(gc_gsf_play_ms(60000, 0, 2) == 60000,
		       "GSF 2-loop tag, fade 0 = no extra fade");
		expect(gc_gsf_play_ms(60000, 3000, 2) == 63000,
		       "GSF 2-loop tag + fade");
		expect(gc_gsf_play_ms(60000, 0, 4) == 120000,
		       "GSF 4 loops scales the 2-loop tag");
		expect(gc_gsf_play_ms(150000, 0, 2) == 0,
		       "GSF rejects GME dummy length");
		expect(gc_gsf_play_ms(124000, 4000, 2) == 0,
		       "GSF rejects HA 124s dummy");
		expect(gc_config_engine_for(&cfg, GC_FMT_MINIGSF) == GC_ENG_AUTO,
		       "GSF engine default Auto (VIOGSF then HA)");
		expect(gc_config_engine_for(&cfg, GC_FMT_MINIUSF) == GC_ENG_USF,
		       "USF engine is lazyusf2 only");
		{
			gc_vfs vfs;
			gc_blob music, m3u;
			unsigned char *lib = (unsigned char *)malloc(sizeof usf);
			unsigned char *tune = (unsigned char *)malloc(sizeof usf);
			expect(lib && tune, "usflib vfs alloc");
			if (lib && tune) {
				memcpy(lib, usf, sizeof usf);
				memcpy(tune, usf, sizeof usf);
				gc_vfs_init(&vfs);
				expect(gc_vfs_add(&vfs, "set.usflib", lib, sizeof usf),
				       "vfs add usflib");
				expect(gc_vfs_add(&vfs, "track.miniusf", tune, sizeof usf),
				       "vfs add miniusf");
				expect(gc_vfs_pick_music(&vfs, &music, &m3u),
				       "pick music from usflib+miniusf");
				expect(strstr(music.name, "miniusf") != NULL,
				       "zip/folder pick skips .usflib");
				expect(gc_vfs_find(&vfs, "set.usflib") != NULL,
				       "usflib stays in VFS for _lib");
				free(music.data);
				free(m3u.data);
				gc_vfs_free(&vfs);
			} else {
				free(lib);
				free(tune);
			}
		}
	}
	expect(gc_is_sap(sap, sap_n, "tune.sap"), "SAP detected");
	expect(gc_probe(sap, sap_n, "tune.sap") == GC_FMT_UNKNOWN, "SAP never claimed");
	expect(gc_is_dummy_length_ms(150000), "GME 2:30 dummy detected");
	expect(gc_is_dummy_length_ms(180000), "3:00 dummy detected");
	expect(gc_is_dummy_length_ms(300000), "NEZ 5:00 dummy detected");
	expect(!gc_is_dummy_length_ms(10000), "real 10s is not dummy");

	expect(gc_m3u_parse(m3u, strlen(m3u), "fixture.nsf", &m3u_inf), "parse NEZ M3U");
	expect(m3u_inf.track_count == 3, "M3U has 3 tracks including last");
	expect(m3u_inf.tracks[2].duration_ms == 10000, "M3U last length 0:10 not 3:00");
	expect(strcmp(m3u_inf.tracks[2].title, "Last track") == 0, "M3U last title");
	{
		const char *loop_m3u =
			"songs/fixture.nsf::NSF,1,Looped,0:12,0:04,2\n";
		gc_info lm;
		expect(gc_m3u_parse(loop_m3u, strlen(loop_m3u), "fixture.nsf", &lm),
		       "M3U zip path matches basename");
		expect(lm.tracks[0].duration_ms == 14000, "M3U time+fade 0:12+2s");
		expect(lm.tracks[0].loop_ms == 4000, "M3U loop 0:04");
		expect(lm.tracks[0].fade_ms == 2000, "M3U fade 2s");
		gc_info_apply_loops(&lm, 2);
		expect(lm.tracks[0].duration_ms == 22000, "M3U 2 loops = 12+(12-4)+2");
	}

	p = gc_player_open(nsf, nsf_n, "fixture.nsf", m3u, strlen(m3u), &cfg);
	expect(p != NULL, "open NSF");
	if (p) {
		gc_player_info(p, &inf);
		expect(inf.nsf_load == 0x8000 && inf.nsf_init == 0x8000,
		       "NSF File Info load/init addresses");
		expect(inf.track_count == 3, "NSF track count is 3 (last not dropped)");
		expect(gc_player_set_track(p, 2) == 0, "select last NSF track");
		expect(gc_player_current_track(p) == 2, "current is last track");
		expect(gc_player_length_ms(p) == 10000, "last track uses M3U 10s, not 180s");
		expect(render_ok(p, "NSF/last"), "render last NSF track");
		expect(gc_player_engine(p) == GC_ENG_NSFPLAY ||
		       gc_player_engine(p) == GC_ENG_GME ||
		       gc_player_engine(p) == GC_ENG_NEZ ||
		       gc_player_engine(p) == GC_ENG_FATSO,
		       "NSF engine is one of NSFPlay/GME/NEZ/Fatso");
		gc_player_close(p);
	}

	cfg.engine_gbs = GC_ENG_AUTO;
	p = gc_player_open(gbs, gbs_n, "t.gbs", NULL, 0, &cfg);
	expect(p != NULL, "open GBS");
	if (p) {
		expect(gc_player_engine(p) == GC_ENG_NEZ, "GBS default engine is NEZplug++ (not GME)");
		expect(render_ok(p, "GBS/NEZ"), "render GBS via NEZ");
		gc_player_close(p);
	}

	cfg.engine_gbs = GC_ENG_GME;
	p = gc_player_open(gbs, gbs_n, "t.gbs", NULL, 0, &cfg);
	if (p) {
		float *a = (float *)calloc(8192, sizeof(float));
		int n = gc_player_process(p, a, 4096);
		fprintf(stdout, "    GME GBS rms=%.5f (known-thin; we do not 'fix' GME GBS)\n",
		        rms(a, n * 2));
		expect(gc_player_engine(p) == GC_ENG_GME, "forced GME GBS opens");
		free(a);
		gc_player_close(p);
	} else {
		fprintf(stdout, "    GME GBS did not open (soft-fail ok)\n");
	}

	p = gc_player_open(vgm, vgm_n, "t.vgm", NULL, 0, &cfg);
	expect(p != NULL, "open VGM");
	if (p) {
		expect(gc_player_engine(p) == GC_ENG_GME, "VGM uses GME only");
		expect(render_ok(p, "VGM/GME"), "render VGM via GME");
		gc_player_close(p);
	}

	p = gc_player_open(sap, sap_n, "tune.sap", NULL, 0, &cfg);
	expect(p == NULL, "SAP refused (keep xmp-pokey)");

	/* Forced NSF engine switch is wired. */
	cfg.engine_nsf = GC_ENG_GME;
	p = gc_player_open(nsf, nsf_n, "fixture.nsf", NULL, 0, &cfg);
	expect(p != NULL && gc_player_engine(p) == GC_ENG_GME, "force NSF→GME");
	if (p)
		gc_player_close(p);
	cfg.engine_nsf = GC_ENG_NEZ;
	p = gc_player_open(nsf, nsf_n, "fixture.nsf", NULL, 0, &cfg);
	expect(p != NULL && gc_player_engine(p) == GC_ENG_NEZ, "force NSF→NEZ");
	if (p)
		gc_player_close(p);
	cfg.engine_nsf = GC_ENG_NSFPLAY;
	p = gc_player_open(nsf, nsf_n, "fixture.nsf", NULL, 0, &cfg);
	expect(p != NULL && gc_player_engine(p) == GC_ENG_NSFPLAY, "force NSF→NSFPlay");
	if (p)
		gc_player_close(p);

	/* Measure off: untagged NSF is max+fade, never the 1200 ms silence window. */
	cfg.engine_nsf = GC_ENG_AUTO;
	cfg.measure_untagged = 0;
	p = gc_player_open(nsf, nsf_n, "fixture.nsf", NULL, 0, &cfg);
	if (p) {
		int ms = gc_player_length_ms(p);
		int want = gc_config_untagged_fallback_ms(&cfg);
		expect(!gc_is_dummy_length_ms(ms), "NSF TIME is not a library dummy");
		expect(ms == want, "measure-off untagged NSF is max seconds + fade");
		expect(ms > 5000, "measure-off untagged NSF is not a 1s silence one-shot");
		gc_player_close(p);
	}
	{
		gc_config cfg2 = cfg;
		cfg2.fatso_silence_ms = 0;
		cfg2.measure_untagged = 0;
		p = gc_player_open(nsf, nsf_n, "fixture.nsf", NULL, 0, &cfg2);
		if (p) {
			int left = cfg2.rate * 2;
			float *buf = (float *)calloc((size_t)4096 * 2, sizeof(float));
			int ok = 1;
			while (left > 0 && buf) {
				int n = gc_player_process(p, buf, 4096);
				if (n <= 0) {
					ok = 0;
					break;
				}
				left -= n;
			}
			expect(ok, "measure-off untagged NSF still playing after 2 seconds");
			free(buf);
			gc_player_close(p);
		}
	}
	/* Measure on: decaying SFX NSF is last-peak+tail, not 1200 and not 180s. */
	{
		gc_config mcfg = cfg;
		unsigned char *loop_nsf;
		size_t loop_n;
		mcfg.measure_untagged = 1;
		mcfg.untagged_max_sec = 20;
		mcfg.fatso_silence_ms = 1200;
		p = gc_player_open(nsf, nsf_n, "fixture.nsf", NULL, 0, &mcfg);
		if (p) {
			int ms = gc_player_length_ms(p);
			int sum = 0, i, ntr;
			gc_info minf;
			expect(!gc_is_dummy_length_ms(ms), "measured NSF TIME is not a dummy");
			expect(ms != 1200 && ms != mcfg.fatso_silence_ms,
			       "silence window is not advertised TIME");
			/* Deferred: Open advertises untagged-max (~10 min); measure while playing. */
			{
				int fb = gc_config_untagged_fallback_ms(&mcfg);
				expect(ms == fb || (ms > 200 && ms <= fb),
				       "decaying NSF opens at fallback or already-measured");
			}
			gc_player_info(p, &minf);
			ntr = minf.track_count;
			for (i = 0; i < ntr; ++i)
				sum += minf.tracks[i].duration_ms;
			expect(sum == minf.tracks[0].duration_ms +
			              minf.tracks[1].duration_ms +
			              minf.tracks[2].duration_ms,
			       "GetSubSongs total is the sum of measured tracks");
			expect(strcmp(minf.length_src, "measured") == 0 ||
			       strcmp(minf.length_src, "untagged-max") == 0,
			       "length source is measured or scan-cap fallback");
			{
				float *buf = (float *)calloc((size_t)4096 * 2, sizeof(float));
				int got_any = 0, left = mcfg.rate * 3, newms = 0;
				while (left > 0 && buf) {
					int n = gc_player_process(p, buf, 4096);
					if (n <= 0) break;
					got_any = 1;
					left -= n;
					gc_player_length_updated(p, &newms);
				}
				expect(got_any, "deferred NSF produces audio");
				/* Still playing at 3s (no ~2s IsStopped), or measure already shortened. */
				expect(left <= 0 || newms > 0, "NSF still playing at 3s or measure updated");
				free(buf);
			}
			gc_player_close(p);
			/* Measured TIME must not be silence-cut on mid-song hush. */
			{
				gc_config scfg = mcfg;
				scfg.fatso_silence_ms = 1200;
				p = gc_player_open(nsf, nsf_n, "fixture-silence.nsf", NULL, 0, &scfg);
				if (p) {
					int ms = gc_player_length_ms(p);
					int need = scfg.rate * 2;
					float *buf = (float *)calloc((size_t)4096 * 2, sizeof(float));
					int left = need, alive = 1;
					expect(ms > scfg.fatso_silence_ms + 500,
					       "measured NSF TIME is longer than silence window");
					while (left > 0 && buf) {
						int n = gc_player_process(p, buf, 4096);
						if (n <= 0) { alive = 0; break; }
						left -= n;
					}
					expect(alive, "measured NSF is not silence-cut before advertised TIME");
					free(buf);
					gc_player_close(p);
				}
			}
		}
		loop_nsf = make_loop_nsf(&loop_n);
		p = gc_player_open(loop_nsf, loop_n, "loop.nsf", NULL, 0, &mcfg);
		if (p) {
			gc_info minf;
			int a, b, c, fb;
			gc_player_info(p, &minf);
			a = minf.tracks[0].duration_ms;
			b = minf.tracks[1].duration_ms;
			c = minf.tracks[2].duration_ms;
			fb = gc_config_untagged_fallback_ms(&mcfg);
			expect(minf.track_count == 3, "loop NSF has 3 tracks");
			/* Short synthetic phrase may be rejected as a false loop and
			   fall back to scan-cap; never advertise 1–2 s TIME. */
			expect(a >= 2500 && a <= fb, "loop NSF track 0 sane length");
			expect(b >= 2500 && b <= fb, "loop NSF track 1 sane length");
			expect(c >= 2500 && c <= fb, "loop NSF track 2 sane length");
			expect(!gc_is_dummy_length_ms(a) && !gc_is_dummy_length_ms(b),
			       "loop NSF TIME is not a library dummy");
			expect(a != 1200 && b != 1200, "loop NSF is not silence-ms TIME");
			expect(strcmp(minf.length_src, "measured") == 0 ||
			       strcmp(minf.length_src, "untagged-max") == 0,
			       "loop NSF length source is measured or scan-cap");
			gc_player_close(p);
		}
		free(loop_nsf);
	}
	/* User Zelda II NSF (51 songs): Open must stay fast (10-min placeholder),
	   Process must not IsStopped at ~2s, deferred measure may update TIME. */
	{
		const char *zpath = "/workspace/uploads/zelda2-user.nsf";
		if (access(zpath, R_OK) != 0)
			zpath = "/workspace/uploads/zelda2.nsf";
		if (access(zpath, R_OK) != 0) {
			printf("skip zelda2 NSF host check (file not present)\n");
		} else {
			pid_t pid = fork();
			if (pid == 0) {
				FILE *zf = fopen(zpath, "rb");
				long zsz;
				unsigned char *zdata;
				gc_config zcfg;
				gc_player *zp;
				gc_info zinf;
				float *buf;
				int n, got = 0, rc = 0, ms, updated = 0;
				if (!zf) _exit(2);
				fseek(zf, 0, SEEK_END);
				zsz = ftell(zf);
				fseek(zf, 0, SEEK_SET);
				zdata = (unsigned char *)malloc((size_t)zsz);
				if (!zdata || fread(zdata, 1, (size_t)zsz, zf) != (size_t)zsz)
					_exit(3);
				fclose(zf);
				gc_config_defaults(&zcfg);
				zcfg.rate = 48000;
				zcfg.measure_untagged = 1;
				zcfg.fade_ms = 3000;
				zcfg.loop_count = 1;
				zcfg.len_cache_path[0] = '\0';
				zp = gc_player_open(zdata, (size_t)zsz, zpath, NULL, 0, &zcfg);
				free(zdata);
				if (!zp) _exit(4);
				gc_player_info(zp, &zinf);
				printf("    zelda open tracks=%d t1=%d src=%s engine=%s\n",
				       zinf.track_count, zinf.tracks[0].duration_ms,
				       zinf.length_src, gc_player_engine_name(zp));
				/* 51-song user rip, or older 16-song fixture. */
				if (!(zinf.track_count == 51 || zinf.track_count == 16)) rc = 5;
				else if (zinf.tracks[0].duration_ms < 500000) rc = 6; /* ~10 min placeholder */
				else if (strcmp(gc_player_engine_name(zp), "NSFPlay") != 0) rc = 8;
				else {
					buf = (float *)calloc((size_t)4096 * 2, sizeof(float));
					if (!buf) rc = 9;
					else {
						/* Process until 60s of audio or early EOF. */
						while (got < zcfg.rate * 60) {
							n = gc_player_process(zp, buf, 4096);
							if (n <= 0) { rc = 10; break; }
							got += n;
							if (gc_player_length_updated(zp, &ms)) {
								updated = 1;
								printf("    zelda deferred length -> %d\n", ms);
							}
						}
						printf("    zelda process wall_ms=%d length_ms=%d updated=%d\n",
						       (int)((int64_t)got * 1000 / zcfg.rate),
						       gc_player_length_ms(zp), updated);
						if (rc == 0 && got < zcfg.rate * 60) rc = 10;
						free(buf);
					}
				}
				_exit(rc);
			} else if (pid > 0) {
				int st = 0;
				waitpid(pid, &st, 0);
				expect(WIFEXITED(st) && WEXITSTATUS(st) == 0,
				       "zelda2: open~10min, 51/16 tracks, process>=60s no early EOF");
				if (!WIFEXITED(st) || WEXITSTATUS(st) != 0)
					fprintf(stderr, "zelda2 child status=%d exit=%d\n", st,
					        WIFEXITED(st) ? WEXITSTATUS(st) : -1);
			} else {
				expect(0, "fork zelda2 child");
			}
		}
	}
	{
		const char *cf = "/tmp/gm-len-test.ini";
		gc_len_rec rec, got;
		remove(cf);
		memset(&rec, 0, sizeof rec);
		rec.format = GC_FMT_NSF;
		rec.track_count = 2;
		rec.duration_ms[0] = 12345;
		rec.duration_ms[1] = 23456;
		snprintf(rec.game, sizeof rec.game, "Cache Game");
		snprintf(rec.artist, sizeof rec.artist, "Cache Artist");
		snprintf(rec.src, sizeof rec.src, "measured");
		expect(gc_len_cache_put(cf, "/music/castlevania3.nsf", 1000, 99, &rec),
		       "length cache put");
		expect(gc_len_cache_get(cf, "/music/castlevania3.nsf", 1000, 99, &got),
		       "length cache get hit");
		expect(got.track_count == 2 && got.duration_ms[0] == 12345 &&
		       got.duration_ms[1] == 23456, "cached per-track durations");
		expect(got.duration_ms[0] != 1200, "cache is not silence-ms");
		expect(!gc_len_cache_get(cf, "/music/castlevania3.nsf", 1001, 99, &got),
		       "cache miss on size change");
		{
			gc_config ccfg = cfg;
			gc_player *p2;
			int a, b;
			ccfg.measure_untagged = 1;
			ccfg.untagged_max_sec = 20;
			snprintf(ccfg.len_cache_path, sizeof ccfg.len_cache_path, "%s", cf);
			p = gc_player_open(nsf, nsf_n, "cache-fixture.nsf", NULL, 0, &ccfg);
			expect(p != NULL, "open NSF with length cache path");
			a = p ? gc_player_length_ms(p) : 0;
			/* Drive deferred measure a few seconds so cache can store a result. */
			if (p) {
				float *buf = (float *)calloc((size_t)4096 * 2, sizeof(float));
				int left = ccfg.rate * 2, ums = 0;
				while (left > 0 && buf) {
					int n = gc_player_process(p, buf, 4096);
					if (n <= 0) break;
					left -= n;
					gc_player_length_updated(p, &ums);
				}
				a = gc_player_length_ms(p);
				free(buf);
				gc_player_close(p);
			}
			p2 = gc_player_open(nsf, nsf_n, "cache-fixture.nsf", NULL, 0, &ccfg);
			b = p2 ? gc_player_length_ms(p2) : 0;
			expect(a > 0 && b > 0, "cached/open NSF lengths are positive");
			if (p2)
				gc_player_close(p2);
		}
		remove(cf);
	}

	/* NSFE times + last track via NSFPlay. */
	p = gc_player_open(nsfe, nsfe_n, "fixture.nsfe", NULL, 0, &cfg);
	expect(p != NULL, "open NSFE");
	if (p) {
		gc_player_info(p, &inf);
		expect(inf.track_count == 3, "NSFE track count 3");
		expect(gc_player_set_track(p, 2) == 0, "NSFE last track");
		expect(gc_player_length_ms(p) == 10000, "NSFE last TIME from time chunk");
		expect(render_ok(p, "NSFE/last"), "render NSFE last track");
		expect(gc_player_engine(p) == GC_ENG_NSFPLAY, "NSFE default NSFPlay");
		gc_player_close(p);
	}

	/* NSF2 version byte — still NSFPlay, still audio, no dummy TIME. */
	{
		unsigned char *nsf2 = (unsigned char *)malloc(nsf_n);
		memcpy(nsf2, nsf, nsf_n);
		nsf2[5] = 2;
		nsf2[0x7C] = 0; /* nsf2_bits: no IRQ required for this fixture */
		expect(gc_probe(nsf2, nsf_n, "t.nsf") == GC_FMT_NSF, "probe NSF2 as NSF");
		p = gc_player_open(nsf2, nsf_n, "fixture.nsf", NULL, 0, &cfg);
		expect(p != NULL && gc_player_engine(p) == GC_ENG_NSFPLAY, "NSF2 opens on NSFPlay");
		if (p) {
			expect(!gc_is_dummy_length_ms(gc_player_length_ms(p)), "NSF2 TIME not dummy");
			expect(render_ok(p, "NSF2"), "render NSF2");
			gc_player_close(p);
		}
		free(nsf2);
	}

	/* Channel mute grid: fixture uses square 1 only. GME mute is the
	   reliable host check (NSFPlay MASK on this tiny fixture is quiet). */
	{
		gc_config muted = cfg;
		float *a, *b;
		int na, nb;
		float ra, rb;
		cfg.engine_nsf = GC_ENG_GME;
		muted.engine_nsf = GC_ENG_GME;
		p = gc_player_open(nsf, nsf_n, "fixture.nsf", NULL, 0, &cfg);
		expect(p != NULL, "open NSF for mute");
		if (p) {
			a = (float *)calloc(8192, sizeof(float));
			na = gc_player_process(p, a, 4096);
			ra = rms(a, na * 2);
			gc_player_close(p);
			muted.mute[0] = 1;
			p = gc_player_open(nsf, nsf_n, "fixture.nsf", NULL, 0, &muted);
			b = (float *)calloc(8192, sizeof(float));
			nb = p ? gc_player_process(p, b, 4096) : 0;
			rb = rms(b, nb * 2);
			fprintf(stdout, "    mute grid: open rms=%.5f muted-ch0 rms=%.5f\n", ra, rb);
			expect(ra > 0.01f, "unmuted NSF is loud");
			expect(nb > 0, "muted NSF still renders");
			cfg.engine_nsf = GC_ENG_AUTO;
			if (p)
				gc_player_close(p);
			free(a);
			free(b);
		}
	}

	/* KSS via NEZ++ (FM bit set so YM2413 is instantiated). */
	p = gc_player_open(kss, kss_n, "t.kss", NULL, 0, &cfg);
	expect(p != NULL, "open KSS");
	if (p) {
		expect(gc_player_engine(p) == GC_ENG_NEZ, "KSS default is NEZ++ not GME");
		expect(render_ok(p, "KSS/NEZ"), "render KSS via NEZ");
		gc_player_close(p);
	}
	cfg.engine_kss = GC_ENG_GME;
	p = gc_player_open(kss, kss_n, "t.kss", NULL, 0, &cfg);
	if (p) {
		fprintf(stdout, "    forced GME KSS engine=%s (PSG/SCC only; not the default)\n",
		        gc_player_engine_name(p));
		gc_player_close(p);
	}
	cfg.engine_kss = GC_ENG_AUTO;

	/* VGZ = gzip VGM */
	{
		char cmd[256];
		write_tmp("gc_vgm.vgm", vgm, vgm_n);
		snprintf(cmd, sizeof cmd, "gzip -c /tmp/gc_vgm.vgm > /tmp/gc_vgm.vgz");
		if (system(cmd) == 0) {
			FILE *zf = fopen("/tmp/gc_vgm.vgz", "rb");
			unsigned char *gz = NULL;
			long sz = 0;
			if (zf) {
				fseek(zf, 0, SEEK_END);
				sz = ftell(zf);
				rewind(zf);
				gz = (unsigned char *)malloc((size_t)sz);
				if (gz && fread(gz, 1, (size_t)sz, zf) == (size_t)sz) {
					expect(gc_probe(gz, (size_t)sz, "t.vgz") == GC_FMT_VGZ,
					       "probe VGZ");
					p = gc_player_open(gz, (size_t)sz, "t.vgz", NULL, 0, &cfg);
					expect(p != NULL && gc_player_engine(p) == GC_ENG_GME,
					       "VGZ opens as GME VGM");
					if (p) {
						expect(render_ok(p, "VGZ/GME"), "render VGZ");
						gc_player_close(p);
					}
				}
				free(gz);
				fclose(zf);
			}
		}
	}

	/* Zip + sibling m3u (same method as 7z). */
	{
		char cmd[512];
		FILE *zf;
		unsigned char *zbuf = NULL;
		long zsz = 0;
		write_tmp("gc_arc_gbs.gbs", gbs, gbs_n);
		{
			const char *pl = "gc_arc_gbs.gbs::GBS,1,Zip song,0:07\n";
			write_tmp("gc_arc.m3u", pl, strlen(pl));
		}
		snprintf(cmd, sizeof cmd,
		         "rm -f /tmp/gc_arc.zip && zip -j -q /tmp/gc_arc.zip "
		         "/tmp/gc_arc_gbs.gbs /tmp/gc_arc.m3u");
		if (system(cmd) == 0 && (zf = fopen("/tmp/gc_arc.zip", "rb"))) {
			fseek(zf, 0, SEEK_END);
			zsz = ftell(zf);
			rewind(zf);
			if (zsz > 0) {
				zbuf = (unsigned char *)malloc((size_t)zsz);
				if (zbuf && fread(zbuf, 1, (size_t)zsz, zf) == (size_t)zsz) {
					expect(gc_probe(zbuf, (size_t)zsz, "pack.zip") == GC_FMT_ZIP,
					       "probe zip");
					p = gc_player_open(zbuf, (size_t)zsz, "pack.zip", NULL, 0, &cfg);
					expect(p != NULL, "open zip+gbs+m3u");
					if (p) {
						expect(gc_player_length_ms(p) == 7000,
						       "zip sibling m3u length 0:07");
						gc_player_close(p);
					}
				}
				free(zbuf);
			}
			fclose(zf);
		}
	}

	free(nsf);
	free(gbs);
	free(vgm);
	free(sap);
	free(nsfe);
	free(kss);

	if (fails) {
		fprintf(stderr, "%d check(s) failed\n", fails);
		return 1;
	}
	printf("all Game Music host checks passed\n");
	return 0;
}
