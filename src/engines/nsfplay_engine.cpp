#include "engine.h"

#include "xgm.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct nsf_state {
	xgm::NSF *nsf;
	xgm::NSFPlayer *player;
	xgm::NSFPlayerConfig *config;
	int rate;
	int track;
	int length_ms;
	int fade_ms;
	int loops;
	/* Side player for deferred one-loop measure (playback stays uninterrupted). */
	xgm::NSF *mnsf;
	xgm::NSFPlayer *mplayer;
	xgm::NSFPlayerConfig *mconfig;
	unsigned char *data_copy;
	size_t data_len;
	int m_active;
	int m_track;
	int m_elapsed;
	int m_cap;
	int m_fade;
	int m_phase; /* 0=detect Skip, 1=PCM silence scan */
	int m_heard;
	int m_last_peak;
	int m_silent;
	int m_detect_cap;
};

extern "C" {

/* NSFPlay CheckTerminal ends the stream from GetLength/FADE — not our cap_frames.
   Keep detection off and playtime unknown so IsStopped only follows a huge default
   (we end via player.c). Always clear leftover DetectLoop time_in_ms. */
static void nsf_playback_clear(nsf_state *s)
{
	int play_ms;
	if (!s || !s->config || !s->nsf)
		return;
	play_ms = GC_CAP_MS;
	(*s->config)["AUTO_DETECT"] = 0;
	(*s->config)["AUTO_STOP"] = 0;
	(*s->config)["PLAY_TIME"] = play_ms;
	(*s->config)["FADE_TIME"] = s->fade_ms > 0 ? s->fade_ms : GC_DEFAULT_FADE_MS;
	(*s->config)["LOOP_NUM"] = s->loops > 0 ? s->loops : 1;
	s->nsf->playtime_unknown = true;
	s->nsf->time_in_ms = -1;
	s->nsf->loop_in_ms = -1;
	s->nsf->fade_in_ms = s->fade_ms > 0 ? s->fade_ms : GC_DEFAULT_FADE_MS;
	s->nsf->loop_num = -1;
	s->nsf->playlist_mode = false;
	s->nsf->SetDefaults(play_ms, s->nsf->fade_in_ms, s->loops > 0 ? s->loops : 1);
}

static int nsf_can(gc_format fmt)
{
	return fmt == GC_FMT_NSF || fmt == GC_FMT_NSFE || fmt == GC_FMT_NEZ;
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

static int track_length_ms(xgm::NSF *nsf, int track0, int fade_ms, int loops)
{
	int s, ms;
	if (!nsf)
		return 0;
	s = track0;
	if (nsf->nsfe_plst && track0 >= 0 && track0 < nsf->nsfe_plst_size)
		s = nsf->nsfe_plst[track0];
	if (s < 0 || s >= (int)xgm::NSFE_ENTRIES)
		return 0;
	/* Only real NSFe / NSF2 time chunks. Never AUTO_STOP / silence / PLAY_TIME. */
	if (nsf->nsfe_entry[s].time < 0)
		return 0;
	ms = nsf->nsfe_entry[s].time;
	if (ms <= 0 || gc_is_dummy_length_ms(ms))
		return 0;
	if (nsf->nsfe_entry[s].fade >= 0)
		ms += nsf->nsfe_entry[s].fade;
	else if (fade_ms > 0)
		ms += fade_ms;
	(void)loops;
	if (ms > GC_CAP_MS)
		ms = GC_CAP_MS;
	return ms;
}

static void nsf_free_measure(nsf_state *s)
{
	if (!s)
		return;
	delete s->mplayer;
	delete s->mnsf;
	delete s->mconfig;
	s->mplayer = NULL;
	s->mnsf = NULL;
	s->mconfig = NULL;
	s->m_active = 0;
}

static int nsf_ensure_measure(nsf_state *s)
{
	if (!s || !s->data_copy || s->data_len < 8)
		return 0;
	if (s->mplayer && s->mnsf && s->mconfig)
		return 1;
	nsf_free_measure(s);
	s->mnsf = new xgm::NSF();
	s->mplayer = new xgm::NSFPlayer();
	s->mconfig = new xgm::NSFPlayerConfig();
	if (!s->mnsf->Load(s->data_copy, (xgm::UINT32)s->data_len)) {
		nsf_free_measure(s);
		return 0;
	}
	(*s->mconfig)["MASTER_VOLUME"] = 256;
	(*s->mconfig)["RATE"] = s->rate;
	(*s->mconfig)["NCH"] = 2;
	(*s->mconfig)["IRQ_ENABLE"] = (*s->config)["IRQ_ENABLE"].GetInt();
	(*s->mconfig)["REGION"] = (*s->config)["REGION"].GetInt();
	(*s->mconfig)["N163_OPTION0"] = (*s->config)["N163_OPTION0"].GetInt();
	s->mplayer->SetConfig(s->mconfig);
	if (!s->mplayer->Load(s->mnsf)) {
		nsf_free_measure(s);
		return 0;
	}
	s->mplayer->SetPlayFreq((double)s->rate);
	s->mplayer->SetChannels(2);
	return 1;
}

static gc_eng_state *nsf_open(const unsigned char *data, size_t len, int rate,
                              const gc_config *cfg)
{
	nsf_state *s;
	if (!data || len < 8)
		return NULL;
	if (rate < 8000)
		rate = 48000;
	s = (nsf_state *)calloc(1, sizeof *s);
	if (!s)
		return NULL;
	s->data_copy = (unsigned char *)malloc(len);
	if (!s->data_copy) {
		free(s);
		return NULL;
	}
	memcpy(s->data_copy, data, len);
	s->data_len = len;
	s->nsf = new xgm::NSF();
	s->player = new xgm::NSFPlayer();
	s->config = new xgm::NSFPlayerConfig();
	if (!s->nsf->Load((xgm::UINT8 *)data, (xgm::UINT32)len)) {
		delete s->player;
		delete s->nsf;
		delete s->config;
		free(s->data_copy);
		free(s);
		return NULL;
	}
	(*s->config)["MASTER_VOLUME"] = 256;
	(*s->config)["RATE"] = rate;
	(*s->config)["NCH"] = 2;
	(*s->config)["IRQ_ENABLE"] = (cfg && !cfg->nsfplay_irq) ? 0 : 1;
	(*s->config)["REGION"] = cfg ? cfg->nsfplay_region : 0;
	(*s->config)["N163_OPTION0"] = (cfg && !cfg->nsfplay_n163_mux) ? 0 : 1;
	(*s->config)["APU2_OPTION5"] = 0;
	(*s->config)["APU2_OPTION7"] = 0;
	s->fade_ms = cfg ? cfg->fade_ms : GC_DEFAULT_FADE_MS;
	s->loops = cfg ? cfg->loop_count : 1;
	if (s->loops < 1)
		s->loops = 1;
	nsf_playback_clear(s);
	s->player->SetConfig(s->config);
	if (!s->player->Load(s->nsf)) {
		delete s->player;
		delete s->nsf;
		delete s->config;
		free(s->data_copy);
		free(s);
		return NULL;
	}
	s->player->SetPlayFreq((double)rate);
	s->player->SetChannels(2);
	s->track = s->nsf->start ? (s->nsf->start - 1) : 0;
	if (s->track < 0)
		s->track = 0;
	nsf_playback_clear(s);
	s->player->SetSong(s->track);
	s->player->Reset();
	s->rate = rate;
	s->length_ms = track_length_ms(s->nsf, s->track, s->fade_ms, s->loops);
	return (gc_eng_state *)s;
}

static void nsf_close(gc_eng_state *st)
{
	nsf_state *s = (nsf_state *)st;
	if (!s)
		return;
#if defined(__linux__) && !defined(_WIN32)
	/* NSFPlay + glibc iconv can abort in title/SJIS teardown after a second
	   Load (deferred measure). Leak C++ objects on the Linux host harness;
	   Windows XMPlay frees normally. */
	(void)s->mplayer; (void)s->mnsf; (void)s->mconfig;
	(void)s->player; (void)s->nsf; (void)s->config;
#else
	nsf_free_measure(s);
	delete s->player;
	delete s->nsf;
	delete s->config;
#endif
	free(s->data_copy);
	free(s);
}

static int nsf_info(gc_eng_state *st, gc_info *out)
{
	nsf_state *s = (nsf_state *)st;
	int n, i;
	if (!s || !s->nsf || !out)
		return 0;
	memset(out, 0, sizeof *out);
	n = s->nsf->total_songs > 0 ? s->nsf->total_songs : s->nsf->songs;
	if (n < 1)
		n = 1;
	if (n > GC_MAX_TRACKS)
		n = GC_MAX_TRACKS;
	out->track_count = n;
	out->start_track = s->nsf->start ? (s->nsf->start - 1) : 0;
	copy_field(out->game, sizeof out->game, s->nsf->title);
	copy_field(out->artist, sizeof out->artist, s->nsf->artist);
	copy_field(out->copyright, sizeof out->copyright, s->nsf->copyright);
	copy_field(out->comment, sizeof out->comment, s->nsf->ripper);
	copy_field(out->ripper, sizeof out->ripper, s->nsf->ripper);
	copy_field(out->system, sizeof out->system, "NES");
	out->nsf_load = (int)s->nsf->load_address;
	out->nsf_init = (int)s->nsf->init_address;
	out->nsf_play = (int)s->nsf->play_address;
	out->nsf_exp = (int)s->nsf->soundchip;
	out->nsf_ver = (int)s->nsf->version;
	out->nsf2_bits = (int)s->nsf->nsf2_bits;
	{
		int b, any = 0;
		for (b = 0; b < 8; ++b) {
			out->nsf_bank[b] = s->nsf->bankswitch[b];
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
		         "Load $%04X  Init $%04X  Play $%04X\r\n"
		         "NSF v%d  chips: %s  NSF2 bits $%02X",
		         out->nsf_load, out->nsf_init, out->nsf_play,
		         out->nsf_ver, chips, out->nsf2_bits & 0xFF);
		copy_field(out->extra, sizeof out->extra, extra);
	}
	if (n > 0 && s->nsf->nsfe_entry[0].time >= 0)
		copy_field(out->length_src, sizeof out->length_src, "NSFe");
	for (i = 0; i < n; ++i) {
		if (s->nsf->nsfe_entry[i].tlbl && s->nsf->nsfe_entry[i].tlbl[0])
			copy_field(out->tracks[i].title, sizeof out->tracks[i].title,
			           s->nsf->nsfe_entry[i].tlbl);
		out->tracks[i].duration_ms = track_length_ms(s->nsf, i, 0, 1);
	}
	return 1;
}

static int nsf_set_track(gc_eng_state *st, int track0)
{
	nsf_state *s = (nsf_state *)st;
	int n;
	if (!s || !s->player || !s->nsf)
		return -1;
	n = s->nsf->total_songs > 0 ? s->nsf->total_songs : s->nsf->songs;
	if (n < 1)
		n = 1;
	if (track0 < 0 || track0 >= n)
		return -1;
	s->track = track0;
	nsf_playback_clear(s);
	s->player->SetSong(track0);
	s->player->Reset();
	s->length_ms = track_length_ms(s->nsf, track0, s->fade_ms, s->loops);
	return 0;
}

static int nsf_render(gc_eng_state *st, float *stereo, int frames)
{
	nsf_state *s = (nsf_state *)st;
	int16_t *tmp;
	int i;
	if (!s || !s->player || !stereo || frames <= 0)
		return 0;
	/* Never EOF on NSFPlay IsStopped — player.c ends via cap_frames only.
	   Re-arm after early FADE/Detect leftovers; if still stopped, emit silence
	   so a 10-min untagged placeholder survives (Zelda II track 1). */
	tmp = (int16_t *)malloc((size_t)frames * 2u * sizeof(int16_t));
	if (!tmp)
		return 0;
	if (s->player->IsStopped()) {
		nsf_playback_clear(s);
		s->player->SetSong(s->track);
		s->player->Reset();
	}
	if (s->player->IsStopped()) {
		memset(stereo, 0, (size_t)frames * 2u * sizeof(float));
		free(tmp);
		return frames;
	}
	s->player->Render(tmp, (xgm::UINT32)frames);
	for (i = 0; i < frames * 2; ++i)
		stereo[i] = (float)tmp[i] / 32768.0f;
	free(tmp);
	return frames;
}

static int nsf_seek(gc_eng_state *st, int ms)
{
	nsf_state *s = (nsf_state *)st;
	xgm::UINT32 samples;
	if (!s || !s->player)
		return -1;
	nsf_playback_clear(s);
	s->player->Reset();
	if (ms <= 0)
		return 0;
	samples = (xgm::UINT32)((int64_t)ms * s->rate / 1000);
	s->player->Skip(samples);
	return ms;
}

static int nsf_len(gc_eng_state *st, int track0)
{
	nsf_state *s = (nsf_state *)st;
	if (!s || !s->nsf)
		return 0;
	return track_length_ms(s->nsf, track0, s->fade_ms, s->loops);
}

static void nsf_measure_restore(nsf_state *s, int track)
{
	if (!s || !s->player || !s->nsf)
		return;
	nsf_playback_clear(s);
	s->player->SetSong(track);
	s->player->Reset();
	s->track = track;
}

/* NSFPlay APU-write loop detector (AUTO_DETECT / IsLooped). One-shot:
   LOOP_NUM=1 so GetLength is intro + one loop when a loop is found.
   Use NSFPlay's stock DETECT_TIME/INT (30s/5s). Commit only confident
   lengths: loop period (and end) >= 55s. Short phrase repeats are discarded. */
static int nsf_eval_detect(xgm::NSF *nsf, int fade_ms)
{
	int end, loop, ms = 0;
	if (!nsf)
		return 0;
	end = nsf->time_in_ms;
	loop = nsf->loop_in_ms;
	/* Loop commit: one-loop period and intro+loop end both >= 55s. */
	if (loop >= GC_MEAS_MIN_LOOP_MS && end >= GC_MEAS_MIN_LOOP_MS)
		ms = end + (fade_ms > 0 ? fade_ms : 0);
	/* Non-loop APU "end" is not used for SFX — silence-end is PCM-only. */
	(void)loop;
	if (ms > GC_CAP_MS)
		ms = GC_CAP_MS;
	if (ms > 0 && ms < GC_MEAS_MIN_SANE_MS)
		ms = 0;
	if (ms > 0 && ms < GC_MEAS_MIN_LOOP_MS)
		ms = 0;
	if (gc_is_dummy_length_ms(ms))
		ms += 1;
	return ms;
}

static int nsf_measure(gc_eng_state *st, int track0, int cap_ms, int fade_ms)
{
	nsf_state *s = (nsf_state *)st;
	int chunk, elapsed = 0, saved, ms = 0;
	if (!s || !s->player || !s->nsf || !s->config)
		return 0;
	if (cap_ms < 2000)
		cap_ms = 2000;
	saved = s->track;
	(*s->config)["AUTO_DETECT"] = 1;
	(*s->config)["AUTO_STOP"] = 0;
	(*s->config)["DETECT_TIME"] = 30000;
	(*s->config)["DETECT_INT"] = 5000;
	(*s->config)["DETECT_ALT"] = 0;
	(*s->config)["PLAY_TIME"] = cap_ms + 60000;
	(*s->config)["FADE_TIME"] = 0;
	(*s->config)["LOOP_NUM"] = 1;
	s->nsf->playtime_unknown = true;
	s->nsf->time_in_ms = -1;
	s->nsf->loop_in_ms = -1;
	s->nsf->fade_in_ms = fade_ms > 0 ? fade_ms : 0;
	s->nsf->loop_num = -1;
	s->nsf->playlist_mode = false;
	s->nsf->SetDefaults(cap_ms + 60000, fade_ms > 0 ? fade_ms : 0, 1);
	s->player->SetSong(track0);
	s->player->Reset();
	chunk = s->rate / 4;
	if (chunk < 256)
		chunk = 256;
	while (elapsed < cap_ms) {
		int step_ms;
		s->player->Skip((xgm::UINT32)chunk);
		step_ms = (int)((int64_t)chunk * 1000 / s->rate);
		if (step_ms < 1)
			step_ms = 1;
		elapsed += step_ms;
		if (!s->player->IsDetected())
			continue;
		ms = nsf_eval_detect(s->nsf, fade_ms);
		break;
	}
	nsf_measure_restore(s, saved);
	return ms;
}

static int nsf_defer_start(gc_eng_state *st, int track0, int cap_ms, int fade_ms)
{
	nsf_state *s = (nsf_state *)st;
	int n;
	if (!s || !nsf_ensure_measure(s))
		return 0;
	n = s->mnsf->total_songs > 0 ? s->mnsf->total_songs : s->mnsf->songs;
	if (n < 1)
		n = 1;
	if (track0 < 0 || track0 >= n)
		return 0;
	if (cap_ms < 2000)
		cap_ms = 2000;
	s->m_active = 1;
	s->m_track = track0;
	s->m_elapsed = 0;
	s->m_cap = cap_ms;
	s->m_fade = fade_ms > 0 ? fade_ms : 0;
	s->m_phase = 0;
	s->m_heard = 0;
	s->m_last_peak = 0;
	s->m_silent = 0;
	s->m_detect_cap = cap_ms;
	/* Zelda II title detects ~100s in; keep headroom below full 10-min Skip. */
	if (s->m_detect_cap > 180000)
		s->m_detect_cap = 180000;

	(*s->mconfig)["AUTO_DETECT"] = 1;
	(*s->mconfig)["AUTO_STOP"] = 0;
	(*s->mconfig)["DETECT_TIME"] = 30000;
	(*s->mconfig)["DETECT_INT"] = 5000;
	(*s->mconfig)["DETECT_ALT"] = 0;
	(*s->mconfig)["PLAY_TIME"] = cap_ms + 60000;
	(*s->mconfig)["FADE_TIME"] = 0;
	(*s->mconfig)["LOOP_NUM"] = 1;
	s->mnsf->playtime_unknown = true;
	s->mnsf->time_in_ms = -1;
	s->mnsf->loop_in_ms = -1;
	s->mnsf->fade_in_ms = s->m_fade;
	s->mnsf->loop_num = -1;
	s->mnsf->playlist_mode = false;
	s->mnsf->SetDefaults(cap_ms + 60000, s->m_fade, 1);
	s->mplayer->SetSong(track0);
	s->mplayer->Reset();
	return 1;
}

static int nsf_defer_poll(gc_eng_state *st)
{
	nsf_state *s = (nsf_state *)st;
	int chunk, step_ms, budget, advanced = 0;
	if (!s || !s->m_active || !s->mplayer || !s->mnsf)
		return -1;
	chunk = s->rate / 4;
	if (chunk < 256)
		chunk = 256;
	budget = s->rate; /* ~1s measure audio per poll — finish ahead of realtime */

	if (s->m_phase == 0) {
		int go_pcm = 0;
		while (advanced < budget && s->m_elapsed < s->m_detect_cap) {
			s->mplayer->Skip((xgm::UINT32)chunk);
			step_ms = (int)((int64_t)chunk * 1000 / s->rate);
			if (step_ms < 1)
				step_ms = 1;
			s->m_elapsed += step_ms;
			advanced += chunk;
			if (s->mplayer->IsDetected()) {
				int ms = nsf_eval_detect(s->mnsf, s->m_fade);
				if (ms > 0) {
					s->m_active = 0;
					return ms;
				}
				/* Short/fragile detect — do NOT abort. Fall through to PCM
				   silence scan for SFX; keep 10-min default for music. */
				go_pcm = 1;
				break;
			}
		}
		if (!go_pcm && s->m_elapsed < s->m_detect_cap)
			return 0;
		/* No confident APU loop — restart for PCM silence (SFX) on side player. */
		(*s->mconfig)["AUTO_DETECT"] = 0;
		(*s->mconfig)["AUTO_STOP"] = 0;
		s->mnsf->playtime_unknown = true;
		s->mnsf->time_in_ms = -1;
		s->mnsf->loop_in_ms = -1;
		s->mnsf->fade_in_ms = s->m_fade;
		s->mnsf->loop_num = -1;
		s->mnsf->SetDefaults(s->m_cap + 60000, s->m_fade, 1);
		s->mplayer->SetSong(s->m_track);
		s->mplayer->Reset();
		s->m_phase = 1;
		s->m_elapsed = 0;
		s->m_heard = 0;
		s->m_last_peak = 0;
		s->m_silent = 0;
	}

	/* PCM phase: Render on side player, last-peak + silence tail. */
	{
		int16_t *tmp = (int16_t *)malloc((size_t)chunk * 2u * sizeof(int16_t));
		if (!tmp)
			return -1;
		while (advanced < budget && s->m_elapsed < s->m_cap) {
			int i, loud = 0;
			s->mplayer->Render(tmp, (xgm::UINT32)chunk);
			step_ms = (int)((int64_t)chunk * 1000 / s->rate);
			if (step_ms < 1) step_ms = 1;
			s->m_elapsed += step_ms;
			advanced += chunk;
			for (i = 0; i < chunk * 2; ++i) {
				int v = tmp[i] < 0 ? -tmp[i] : tmp[i];
				if (v > 400) { loud = 1; break; }
			}
			if (!s->m_heard) {
				if (loud) {
					s->m_heard = 1;
					s->m_last_peak = s->m_elapsed;
					s->m_silent = 0;
				}
			} else if (!loud) {
				s->m_silent += step_ms;
				if (s->m_silent >= GC_MEAS_SILENCE_MS) {
					int ms = s->m_last_peak + (s->m_fade > 0 ? s->m_fade : 400);
					free(tmp);
					s->m_active = 0;
					/* SFX / one-shot only: heard audio, hush, total < 15s, no loop. */
					if (ms > GC_CAP_MS) ms = GC_CAP_MS;
					if (ms < GC_MEAS_MIN_SANE_MS || ms >= GC_MEAS_SFX_MAX_MS)
						return -1;
					if (gc_is_dummy_length_ms(ms)) ms += 1;
					return ms;
				}
			} else {
				s->m_last_peak = s->m_elapsed;
				s->m_silent = 0;
			}
		}
		free(tmp);
	}
	if (s->m_elapsed >= s->m_cap) {
		s->m_active = 0;
		return -1;
	}
	return 0;
}

static void nsf_defer_cancel(gc_eng_state *st)
{
	nsf_state *s = (nsf_state *)st;
	if (!s)
		return;
	s->m_active = 0;
}

static const int nsfplay_from_logical[29] = {
	0, 1, 2, 3, 4, 5,
	12, 13, 14,
	6, 7, 8,
	21, 22, 23, 24, 25, 26, 27, 28,
	15, 16, 17, 18, 19, 20,
	9, 10, 11
};

static int nsf_mute(gc_eng_state *st, const unsigned char mute[GC_MAX_CHANNELS],
                    const float level[GC_MAX_CHANNELS])
{
	nsf_state *s = (nsf_state *)st;
	int i, mask = 0;
	if (!s || !s->config || !mute)
		return 0;
	for (i = 0; i < 29 && i < GC_MAX_CHANNELS; ++i) {
		int phys = nsfplay_from_logical[i];
		int vol = level ? (int)(level[i] * 128.0f) : 128;
		if (mute[i]) {
			vol = 0;
			mask |= (1 << phys);
		}
		s->config->GetChannelConfig(phys, "VOL") = vol;
		s->config->GetChannelConfig(phys, "MUTE") = mute[i] ? 1 : 0;
	}
	(*s->config)["MASK"] = mask;
	if (s->player)
		s->player->Notify(-1);
	return 1;
}

static int nsf_mixer(gc_eng_state *st, const gc_config *cfg)
{
	nsf_state *s = (nsf_state *)st;
	int i, mask = 0;
	if (!s || !s->config || !cfg)
		return 0;
	for (i = 0; i < 29 && i < GC_MAX_CHANNELS; ++i) {
		int phys = nsfplay_from_logical[i];
		int vol = cfg->chan_vol[i];
		int pan = 128 + cfg->chan_pan[i];
		if (cfg->mute[i])
			vol = 0;
		if (vol < 0) vol = 0;
		if (vol > 255) vol = 255;
		if (pan < 0) pan = 0;
		if (pan > 255) pan = 255;
		s->config->GetChannelConfig(phys, "VOL") = vol * 128 / 255;
		s->config->GetChannelConfig(phys, "PAN") = pan;
		s->config->GetChannelConfig(phys, "MUTE") = cfg->mute[i] ? 1 : 0;
		if (cfg->mute[i])
			mask |= (1 << phys);
	}
	(*s->config)["MASK"] = mask;
	if (s->player)
		s->player->Notify(-1);
	return 1;
}

static int nsf_voices(gc_eng_state *st)
{
	(void)st;
	return xgm::NES_CHANNEL_MAX;
}

static const char *nsf_vname(gc_eng_state *st, int i)
{
	(void)st;
	static const char *logical[] = {
		"Sq1", "Sq2", "Tri", "Noi", "DMC", "FDS",
		"VRC6 P0", "VRC6 P1", "VRC6 SAW",
		"MMC5 S0", "MMC5 S1", "MMC5 PCM",
		"N163 0", "N163 1", "N163 2", "N163 3",
		"N163 4", "N163 5", "N163 6", "N163 7",
		"VRC7 0", "VRC7 1", "VRC7 2", "VRC7 3", "VRC7 4", "VRC7 5",
		"5B 0", "5B 1", "5B 2"
	};
	if (i < 0 || i >= 29)
		return "";
	return logical[i];
}

static const gc_eng_ops ops = {
	"NSFPlay",
	GC_ENG_NSFPLAY,
	nsf_can,
	nsf_open,
	nsf_close,
	nsf_info,
	nsf_set_track,
	nsf_render,
	nsf_seek,
	nsf_len,
	nsf_mute,
	nsf_mixer,
	nsf_voices,
	nsf_vname,
	nsf_measure,
	nsf_defer_start,
	nsf_defer_poll,
	nsf_defer_cancel
};

const gc_eng_ops *gc_eng_nsfplay(void)
{
	return &ops;
}

} /* extern "C" */
