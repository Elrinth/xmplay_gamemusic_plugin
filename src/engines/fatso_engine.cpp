#include "engine.h"

#ifndef _WIN32
/* Host/Linux: NotSo Fatso core is Windows-typed. Soft-fallback to NSFPlay/GME/NEZ. */
#include <stdlib.h>
#include <string.h>

extern "C" {

static int fatso_can(gc_format fmt)
{
	return fmt == GC_FMT_NSF || fmt == GC_FMT_NSFE;
}

static gc_eng_state *fatso_open(const unsigned char *, size_t, int, const gc_config *)
{
	return NULL;
}

static void fatso_close(gc_eng_state *) {}
static int fatso_info(gc_eng_state *, gc_info *) { return 0; }
static int fatso_set_track(gc_eng_state *, int) { return -1; }
static int fatso_render(gc_eng_state *, float *, int) { return 0; }
static int fatso_seek(gc_eng_state *, int) { return -1; }
static int fatso_len(gc_eng_state *, int) { return 0; }
static int fatso_mute(gc_eng_state *, const unsigned char *, const float *) { return 0; }
static int fatso_voices(gc_eng_state *) { return 0; }
static const char *fatso_vname(gc_eng_state *, int) { return ""; }

static const gc_eng_ops ops = {
	"NotSo Fatso",
	GC_ENG_FATSO,
	fatso_can,
	fatso_open,
	fatso_close,
	fatso_info,
	fatso_set_track,
	fatso_render,
	fatso_seek,
	fatso_len,
	fatso_mute,
	NULL,
	fatso_voices,
	fatso_vname,
	NULL
};

const gc_eng_ops *gc_eng_fatso(void)
{
	return &ops;
}

} /* extern "C" */

#else

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "NSF_Core.h"
#include "NSF_File.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct fatso_state {
	CNSFCore *core;
	CNSFFile *file;
	int rate;
	int track;
	int length_ms;
	int channels;
};

extern "C" {

static int fatso_can(gc_format fmt)
{
	return fmt == GC_FMT_NSF || fmt == GC_FMT_NSFE;
}

static void copy_field(char *dst, size_t cap, const char *src)
{
	size_t n;
	if (!dst || cap == 0)
		return;
	dst[0] = '\0';
	if (!src)
		return;
	n = strlen(src);
	if (n >= cap)
		n = cap - 1;
	memcpy(dst, src, n);
	dst[n] = '\0';
}

static int fill_nesm(CNSFFile *f, const unsigned char *data, size_t len)
{
	const unsigned char *h;
	int i;
	if (!f || !data || len < 0x80)
		return 0;
	if (!(data[0] == 'N' && data[1] == 'E' && data[2] == 'S' && data[3] == 'M'))
		return 0;
	h = data;
	f->bIsExtended = false;
	f->nTrackCount = h[6] ? h[6] : 1;
	f->nInitialTrack = h[7] ? (h[7] - 1) : 0;
	f->nLoadAddress = h[8] | (h[9] << 8);
	f->nInitAddress = h[10] | (h[11] << 8);
	f->nPlayAddress = h[12] | (h[13] << 8);
	f->nNTSC_PlaySpeed = h[0x6E] | (h[0x6F] << 8);
	f->nPAL_PlaySpeed = h[0x78] | (h[0x79] << 8);
	f->nIsPal = h[0x7A] & 1;
	f->nChipExtensions = h[0x7B];
	for (i = 0; i < 8; ++i)
		f->nBankswitch[i] = h[0x70 + i];
	f->nDataBufferSize = (int)(len - 0x80);
	f->pDataBuffer = new BYTE[f->nDataBufferSize];
	if (!f->pDataBuffer)
		return 0;
	memcpy(f->pDataBuffer, data + 0x80, (size_t)f->nDataBufferSize);
	if (h[0x0E]) {
		f->szGameTitle = new char[33];
		memcpy(f->szGameTitle, h + 0x0E, 32);
		f->szGameTitle[32] = 0;
	}
	if (h[0x2E]) {
		f->szArtist = new char[33];
		memcpy(f->szArtist, h + 0x2E, 32);
		f->szArtist[32] = 0;
	}
	if (h[0x4E]) {
		f->szCopyright = new char[33];
		memcpy(f->szCopyright, h + 0x4E, 32);
		f->szCopyright[32] = 0;
	}
	return 1;
}

static int read_le32(const unsigned char *p)
{
	return (int)p[0] | ((int)p[1] << 8) | ((int)p[2] << 16) | ((int)p[3] << 24);
}

static char *dup_str(const char *s, size_t n)
{
	char *d = new char[n + 1];
	if (!d)
		return NULL;
	memcpy(d, s, n);
	d[n] = 0;
	return d;
}

/* In-memory NSFe — Fatso LoadFile needs a path; we parse the same chunks. */
static int fill_nsfe(CNSFFile *f, const unsigned char *data, size_t len)
{
	size_t pos;
	int info_ok = 0, end_ok = 0;
	size_t data_off = 0, data_sz = 0;
	if (!f || !data || len < 8)
		return 0;
	if (!(data[0] == 'N' && data[1] == 'S' && data[2] == 'F' && data[3] == 'E'))
		return 0;
	pos = 4;
	f->nTrackCount = 1;
	f->nNTSC_PlaySpeed = 16639;
	f->nPAL_PlaySpeed = 19997;
	while (pos + 8 <= len && !end_ok) {
		int csz = read_le32(data + pos);
		const unsigned char *typ = data + pos + 4;
		const unsigned char *chunk;
		size_t used;
		if (csz < 0 || pos + 8 + (size_t)csz > len)
			return 0;
		chunk = data + pos + 8;
		if (memcmp(typ, "INFO", 4) == 0) {
			if (csz < 8)
				return 0;
			f->bIsExtended = true;
			f->nLoadAddress = chunk[0] | (chunk[1] << 8);
			f->nInitAddress = chunk[2] | (chunk[3] << 8);
			f->nPlayAddress = chunk[4] | (chunk[5] << 8);
			f->nIsPal = chunk[6] & 3;
			f->nChipExtensions = chunk[7];
			if (csz >= 9)
				f->nTrackCount = chunk[8] ? chunk[8] : 1;
			if (csz >= 10)
				f->nInitialTrack = chunk[9];
			info_ok = 1;
		} else if (memcmp(typ, "DATA", 4) == 0) {
			data_off = (size_t)(chunk - data);
			data_sz = (size_t)csz;
		} else if (memcmp(typ, "NEND", 4) == 0) {
			end_ok = 1;
		} else if (memcmp(typ, "time", 4) == 0 && info_ok) {
			int i, n = csz / 4;
			if (n > f->nTrackCount)
				n = f->nTrackCount;
			f->pTrackTime = new int[f->nTrackCount];
			if (!f->pTrackTime)
				return 0;
			for (i = 0; i < f->nTrackCount; ++i)
				f->pTrackTime[i] = -1;
			for (i = 0; i < n; ++i) {
				int ms = read_le32(chunk + (size_t)i * 4);
				if (gc_is_dummy_length_ms(ms) || ms < 0)
					ms = 0;
				f->pTrackTime[i] = ms;
			}
		} else if (memcmp(typ, "fade", 4) == 0 && info_ok) {
			int i, n = csz / 4;
			if (n > f->nTrackCount)
				n = f->nTrackCount;
			f->pTrackFade = new int[f->nTrackCount];
			if (!f->pTrackFade)
				return 0;
			for (i = 0; i < f->nTrackCount; ++i)
				f->pTrackFade[i] = -1;
			for (i = 0; i < n; ++i)
				f->pTrackFade[i] = read_le32(chunk + (size_t)i * 4);
		} else if (memcmp(typ, "BANK", 4) == 0) {
			used = (size_t)csz < 8 ? (size_t)csz : 8;
			memcpy(f->nBankswitch, chunk, used);
		} else if (memcmp(typ, "plst", 4) == 0 && csz > 0) {
			f->nPlaylistSize = csz;
			f->pPlaylist = new BYTE[csz];
			if (f->pPlaylist)
				memcpy(f->pPlaylist, chunk, (size_t)csz);
		} else if (memcmp(typ, "auth", 4) == 0) {
			const char *p = (const char *)chunk;
			const char *end = (const char *)chunk + csz;
			char **slot[4] = { &f->szGameTitle, &f->szArtist, &f->szCopyright, &f->szRipper };
			int i;
			for (i = 0; i < 4 && p < end; ++i) {
				size_t n = 0;
				while (p + n < end && p[n])
					n++;
				*slot[i] = dup_str(p, n);
				p += n + (p + n < end ? 1 : 0);
			}
		} else if (memcmp(typ, "tlbl", 4) == 0 && info_ok) {
			const char *p = (const char *)chunk;
			const char *end = (const char *)chunk + csz;
			int i;
			f->szTrackLabels = new char *[f->nTrackCount];
			if (!f->szTrackLabels)
				return 0;
			memset(f->szTrackLabels, 0, sizeof(char *) * (size_t)f->nTrackCount);
			for (i = 0; i < f->nTrackCount && p < end; ++i) {
				size_t n = 0;
				while (p + n < end && p[n])
					n++;
				f->szTrackLabels[i] = dup_str(p, n);
				p += n + (p + n < end ? 1 : 0);
			}
		} else if (typ[0] >= 'A' && typ[0] <= 'Z') {
			return 0; /* mandatory unknown chunk */
		}
		pos += 8 + (size_t)csz;
	}
	if (!info_ok || !data_sz)
		return 0;
	f->nDataBufferSize = (int)data_sz;
	f->pDataBuffer = new BYTE[data_sz];
	if (!f->pDataBuffer)
		return 0;
	memcpy(f->pDataBuffer, data + data_off, data_sz);
	return 1;
}

static gc_eng_state *fatso_open(const unsigned char *data, size_t len, int rate,
                                const gc_config *cfg)
{
	fatso_state *s;
	int loaded = 0;
	if (!data || len < 4)
		return NULL;
	if (rate < 8000)
		rate = 48000;
	s = (fatso_state *)calloc(1, sizeof *s);
	if (!s)
		return NULL;
	s->file = new CNSFFile();
	s->core = new CNSFCore();
	if (!s->core->Initialize()) {
		delete s->core;
		delete s->file;
		free(s);
		return NULL;
	}
	if (data[0] == 'N' && data[1] == 'S' && data[2] == 'F' && data[3] == 'E') {
		if (!fill_nsfe(s->file, data, len) || !s->core->LoadNSF(s->file)) {
			delete s->core;
			delete s->file;
			free(s);
			return NULL;
		}
		loaded = 1;
	}
	/* NSF2 (version >= 2): IRQ / mixe / N163 multiplex — NSFPlay only. */
	if (data[0] == 'N' && data[1] == 'E' && data[2] == 'S' && data[3] == 'M' &&
	    len >= 6 && data[5] >= 2) {
		delete s->core;
		delete s->file;
		free(s);
		return NULL;
	}
	if (!loaded && (!fill_nesm(s->file, data, len) || !s->core->LoadNSF(s->file))) {
		delete s->core;
		delete s->file;
		free(s);
		return NULL;
	}
	s->core->SetPlaybackOptions(rate, 2);
	s->core->SetMasterVolume(1.0f);
	if (cfg) {
		NSF_ADVANCEDOPTIONS opt;
		s->core->GetAdvancedOptions(&opt);
		if (cfg->fatso_silence_ms > 0)
			opt.nSilenceTrackMS = cfg->fatso_silence_ms;
		opt.nInvertCutoffHz = cfg->fatso_invert_hz > 0 ? cfg->fatso_invert_hz : 210;
		opt.bPALPreference = (BYTE)(cfg->fatso_pal ? 1 : 0);
		opt.bHighPassEnabled = (BYTE)(cfg->fatso_highpass || cfg->filter_mode == 1);
		opt.bLowPassEnabled = (BYTE)(cfg->fatso_lowpass || cfg->filter_mode == 2);
		opt.bPrePassEnabled = (BYTE)(cfg->fatso_prepass || cfg->filter_mode == 3);
		if (cfg->fatso_hpf_hz > 0) opt.nHighPassBase = cfg->fatso_hpf_hz;
		if (cfg->fatso_lpf_hz > 0) opt.nLowPassBase = cfg->fatso_lpf_hz;
		if (cfg->fatso_pre_hz > 0) opt.nPrePassBase = cfg->fatso_pre_hz;
		opt.bDMCPopReducer = (BYTE)(cfg->fatso_dmc_pop ? 1 : 0);
		opt.bN106PopReducer = (BYTE)(cfg->fatso_n106_pop ? 1 : 0);
		opt.bFDSPopReducer = (BYTE)(cfg->fatso_fds_pop ? 1 : 0);
		opt.bIgnore4011Writes = (BYTE)(cfg->fatso_ignore_4011 ? 1 : 0);
		opt.bResetDuty = (BYTE)(cfg->fatso_reset_duty ? 1 : 0);
		opt.bIgnoreBRK = (BYTE)(cfg->fatso_ignore_brk ? 1 : 0);
		opt.bIgnoreIllegalOps = (BYTE)(cfg->fatso_ignore_illegal ? 1 : 0);
		opt.bNoWaitForReturn = (BYTE)(cfg->fatso_no_wait_play ? 1 : 0);
		opt.bCleanAXY = (BYTE)(cfg->fatso_reset_regs ? 1 : 0);
		opt.nForce4017Write = (BYTE)(cfg->fatso_force_4017 == 1 ? 1 :
		                             cfg->fatso_force_4017 == 2 ? 2 : 0);
		opt.bNoSilenceIfTime = (BYTE)(cfg->fatso_no_silence_if_time ? 1 : 0);
		s->core->SetAdvancedOptions(&opt);
	}
	s->track = s->file->nInitialTrack;
	if (s->track < 0)
		s->track = 0;
	s->core->SetTrack((BYTE)s->track);
	s->rate = rate;
	s->channels = 2;
	s->length_ms = 0;
	if (s->file->pTrackTime && s->track < s->file->nTrackCount) {
		s->length_ms = s->file->pTrackTime[s->track];
		if (s->file->pTrackFade && s->file->pTrackFade[s->track] > 0)
			s->length_ms += s->file->pTrackFade[s->track];
		if (gc_is_dummy_length_ms(s->length_ms))
			s->length_ms = 0;
	}
	return (gc_eng_state *)s;
}

static void fatso_close(gc_eng_state *st)
{
	fatso_state *s = (fatso_state *)st;
	if (!s)
		return;
	delete s->core;
	delete s->file;
	free(s);
}

static int fatso_info(gc_eng_state *st, gc_info *out)
{
	fatso_state *s = (fatso_state *)st;
	int n, i;
	if (!s || !s->file || !out)
		return 0;
	memset(out, 0, sizeof *out);
	n = s->file->nTrackCount;
	if (n < 1)
		n = 1;
	if (n > GC_MAX_TRACKS)
		n = GC_MAX_TRACKS;
	out->track_count = n;
	out->start_track = s->file->nInitialTrack;
	copy_field(out->game, sizeof out->game, s->file->szGameTitle);
	copy_field(out->artist, sizeof out->artist, s->file->szArtist);
	copy_field(out->copyright, sizeof out->copyright, s->file->szCopyright);
	copy_field(out->system, sizeof out->system, "NES");
	out->nsf_load = s->file->nLoadAddress;
	out->nsf_init = s->file->nInitAddress;
	out->nsf_play = s->file->nPlayAddress;
	out->nsf_exp = s->file->nChipExtensions;
	{
		int b, any = 0;
		for (b = 0; b < 8; ++b) {
			out->nsf_bank[b] = s->file->nBankswitch[b];
			if (out->nsf_bank[b])
				any = 1;
		}
		out->nsf_has_bank = any;
	}
	{
		char extra[512];
		char chips[80];
		static const char *cn[] = { "VRC6", "VRC7", "FDS", "MMC5", "N163", "5B" };
		int b, p = 0;
		chips[0] = '\0';
		for (b = 0; b < 6; ++b) {
			if (out->nsf_exp & (1 << b)) {
				if (p)
					p += snprintf(chips + p, sizeof chips - (size_t)p, " ");
				p += snprintf(chips + p, sizeof chips - (size_t)p, "%s", cn[b]);
			}
		}
		if (!chips[0])
			snprintf(chips, sizeof chips, "2A03 only");
		snprintf(extra, sizeof extra,
		         "Load $%04X  Init $%04X  Play $%04X\r\nchips: %s",
		         out->nsf_load, out->nsf_init, out->nsf_play, chips);
		copy_field(out->extra, sizeof out->extra, extra);
	}
	if (s->file->bIsExtended && s->file->pTrackTime)
		copy_field(out->length_src, sizeof out->length_src, "NSFe");
	for (i = 0; i < n; ++i) {
		if (s->file->szTrackLabels && s->file->szTrackLabels[i])
			copy_field(out->tracks[i].title, sizeof out->tracks[i].title,
			           s->file->szTrackLabels[i]);
		if (s->file->pTrackTime) {
			int ms = s->file->pTrackTime[i];
			if (s->file->pTrackFade && s->file->pTrackFade[i] > 0)
				ms += s->file->pTrackFade[i];
			out->tracks[i].duration_ms = gc_is_dummy_length_ms(ms) ? 0 : ms;
		}
	}
	return 1;
}

static int fatso_set_track(gc_eng_state *st, int track0)
{
	fatso_state *s = (fatso_state *)st;
	if (!s || !s->core || !s->file)
		return -1;
	if (track0 < 0 || track0 >= s->file->nTrackCount)
		return -1;
	s->track = track0;
	s->core->SetTrack((BYTE)track0);
	s->length_ms = 0;
	if (s->file->pTrackTime) {
		s->length_ms = s->file->pTrackTime[track0];
		if (s->file->pTrackFade && s->file->pTrackFade[track0] > 0)
			s->length_ms += s->file->pTrackFade[track0];
		if (gc_is_dummy_length_ms(s->length_ms))
			s->length_ms = 0;
	}
	return 0;
}

static int fatso_render(gc_eng_state *st, float *stereo, int frames)
{
	fatso_state *s = (fatso_state *)st;
	int bytes, got, i;
	BYTE *buf;
	if (!s || !s->core || !stereo || frames <= 0)
		return 0;
	bytes = frames * 2 * 2; /* stereo s16 */
	buf = (BYTE *)malloc((size_t)bytes);
	if (!buf)
		return 0;
	got = s->core->GetSamples(buf, bytes);
	if (got <= 0) {
		free(buf);
		return 0;
	}
	got /= 4; /* frames */
	for (i = 0; i < got; ++i) {
		int16_t L = (int16_t)(buf[i * 4] | (buf[i * 4 + 1] << 8));
		int16_t R = (int16_t)(buf[i * 4 + 2] | (buf[i * 4 + 3] << 8));
		stereo[i * 2] = (float)L / 32768.0f;
		stereo[i * 2 + 1] = (float)R / 32768.0f;
	}
	free(buf);
	return got;
}

static int fatso_seek(gc_eng_state *st, int ms)
{
	fatso_state *s = (fatso_state *)st;
	if (!s || !s->core)
		return -1;
	s->core->SetWrittenTime((UINT)(ms < 0 ? 0 : ms), 0);
	return ms < 0 ? 0 : ms;
}

static int fatso_len(gc_eng_state *st, int track0)
{
	fatso_state *s = (fatso_state *)st;
	if (!s || !s->file)
		return 0;
	if (s->file->pTrackTime && track0 >= 0 && track0 < s->file->nTrackCount) {
		int ms = s->file->pTrackTime[track0];
		return gc_is_dummy_length_ms(ms) ? 0 : ms;
	}
	return 0;
}

/* Same logical grid as NSFPlay: 2A03, FDS, VRC6, MMC5, N163, VRC7, 5B. */
static const int fatso_from_logical[29] = {
	0, 1, 2, 3, 4, 28,
	5, 6, 7,
	8, 9, 10,
	11, 12, 13, 14, 15, 16, 17, 18,
	19, 20, 21, 22, 23, 24,
	25, 26, 27
};

static int fatso_mute(gc_eng_state *st, const unsigned char mute[GC_MAX_CHANNELS],
                      const float level[GC_MAX_CHANNELS])
{
	fatso_state *s = (fatso_state *)st;
	int i;
	if (!s || !s->core || !mute)
		return 0;
	for (i = 0; i < 29 && i < GC_MAX_CHANNELS; ++i) {
		int phys = fatso_from_logical[i];
		int vol = 255, pan = 0;
		if (level)
			vol = (int)(level[i] * 255.0f);
		if (mute[i])
			vol = 0;
		s->core->SetChannelOptions((UINT)phys, mute[i] ? 0 : 1, vol, pan, 0);
	}
	return 1;
}

static int fatso_mixer(gc_eng_state *st, const gc_config *cfg)
{
	fatso_state *s = (fatso_state *)st;
	int i;
	if (!s || !s->core || !cfg)
		return 0;
	for (i = 0; i < 29 && i < GC_MAX_CHANNELS; ++i) {
		int phys = fatso_from_logical[i];
		int vol = cfg->chan_vol[i];
		int pan = cfg->chan_pan[i];
		if (cfg->mute[i])
			vol = 0;
		if (vol < 0) vol = 0;
		if (vol > 255) vol = 255;
		if (pan < -127) pan = -127;
		if (pan > 127) pan = 127;
		s->core->SetChannelOptions((UINT)phys, cfg->mute[i] ? 0 : 1, vol, pan, 0);
	}
	return 1;
}

static int fatso_voices(gc_eng_state *st)
{
	(void)st;
	return 29;
}

static const char *fatso_vname(gc_eng_state *st, int i)
{
	static const char *n[] = {
		"Sq1", "Sq2", "Tri", "Noi", "DMC", "FDS",
		"VRC6 P0", "VRC6 P1", "VRC6 SAW",
		"MMC5 S0", "MMC5 S1", "MMC5 PCM",
		"N163 0", "N163 1", "N163 2", "N163 3",
		"N163 4", "N163 5", "N163 6", "N163 7",
		"VRC7 0", "VRC7 1", "VRC7 2", "VRC7 3", "VRC7 4", "VRC7 5",
		"5B 0", "5B 1", "5B 2"
	};
	(void)st;
	if (i < 0 || i >= 29)
		return "";
	return n[i];
}

static const gc_eng_ops ops = {
	"NotSo Fatso",
	GC_ENG_FATSO,
	fatso_can,
	fatso_open,
	fatso_close,
	fatso_info,
	fatso_set_track,
	fatso_render,
	fatso_seek,
	fatso_len,
	fatso_mute,
	fatso_mixer,
	fatso_voices,
	fatso_vname,
	NULL
};

const gc_eng_ops *gc_eng_fatso(void)
{
	return &ops;
}

} /* extern "C" */

#endif /* _WIN32 */
