#include "player.h"
#include "probe.h"
#include "m3u.h"
#include "archive.h"
#include "vfs.h"
#include "measure.h"
#include "lencache.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct gc_player {
	const gc_eng_ops *ops;
	gc_eng_state *eng;
	gc_format format;
	gc_engine engine;
	gc_info info;
	gc_volume vol;
	int rate;
	int track;
	int frames_played;
	int length_ms;
	int cap_frames;
	int fade_ms;
	int silence_ms;          /* post-audio auto-advance only; never TIME */
	int protect_tagged;
	int heard_audio;
	int silent_frames;
	unsigned char track_tagged[GC_MAX_TRACKS];
	unsigned char track_pending_measure[GC_MAX_TRACKS];
	int untagged_fallback_ms;
	int measure_untagged;
	int defer_cap_ms;
	int defer_fade_ms;
	int defer_active;        /* side-engine poll in flight for p->track */
	int length_dirty;
	int length_dirty_ms;
	char cache_path[GC_MAX_PATH];
	char cache_key[GC_MAX_PATH];
	uint64_t cache_size;
	int64_t cache_mtime;
};

static const gc_eng_ops *ops_for(gc_engine e)
{
	switch (e) {
	case GC_ENG_NEZ: return gc_eng_nez();
	case GC_ENG_GME: return gc_eng_gme();
	case GC_ENG_NSFPLAY: return gc_eng_nsfplay();
	case GC_ENG_FATSO: return gc_eng_fatso();
	case GC_ENG_GSF: return gc_eng_gsf();
	case GC_ENG_USF: return gc_eng_usf();
	case GC_ENG_HA: return gc_eng_ha();
	default: return NULL;
	}
}

/* Default try-order. First is the preferred engine for that format. */
static void fallback_list(gc_format fmt, gc_engine forced, gc_engine *out, int *n)
{
	int i = 0;
	if (forced != GC_ENG_AUTO) {
		out[i++] = forced;
		/* still allow soft fallback if forced fails at open */
	}
	switch (fmt) {
	case GC_FMT_GBS:
	case GC_FMT_GBR:
		out[i++] = GC_ENG_NEZ;
		out[i++] = GC_ENG_GME;
		break;
	case GC_FMT_KSS:
		/* Official GME KSS is AY/SCC/SMS PSG only — no OPLL/YM2413/Y8950.
		   Never default and never auto-fallback: FM KSS is empty/wrong on GME. */
		out[i++] = GC_ENG_NEZ;
		break;
	case GC_FMT_AY:
	case GC_FMT_HES:
	case GC_FMT_CPC:
	case GC_FMT_SGC:
	case GC_FMT_NSD:
		out[i++] = GC_ENG_NEZ;
		out[i++] = GC_ENG_GME;
		break;
	case GC_FMT_NSF:
	case GC_FMT_NSFE:
	case GC_FMT_NEZ:
	case GC_FMT_NSZ:
		out[i++] = GC_ENG_NSFPLAY;
		out[i++] = GC_ENG_FATSO;
		out[i++] = GC_ENG_GME;
		out[i++] = GC_ENG_NEZ;
		break;
	case GC_FMT_SPC:
	case GC_FMT_VGM:
	case GC_FMT_VGZ:
	case GC_FMT_GYM:
	case GC_FMT_RSN:
		out[i++] = GC_ENG_GME;
		break;
	case GC_FMT_GSF:
	case GC_FMT_MINIGSF:
		/* kode54 VIOGSF library first — not a wrap of 2004 in_gsf.dll.
		   Highly Advanced 0.11 C core is the optional in_gsf-compat fallback. */
		out[i++] = GC_ENG_GSF;
		out[i++] = GC_ENG_HA;
		break;
	case GC_FMT_USF:
	case GC_FMT_MINIUSF:
		out[i++] = GC_ENG_USF;
		break;
	default:
		out[i++] = GC_ENG_NEZ;
		out[i++] = GC_ENG_GME;
		out[i++] = GC_ENG_NSFPLAY;
		break;
	}
	*n = i;
}

static int dedup_engines(gc_engine *list, int n)
{
	int w = 0, r, k, dup;
	for (r = 0; r < n; ++r) {
		dup = 0;
		for (k = 0; k < w; ++k) {
			if (list[k] == list[r]) {
				dup = 1;
				break;
			}
		}
		if (!dup)
			list[w++] = list[r];
	}
	return w;
}

static int chip_needs_default_play(gc_format fmt)
{
	switch (fmt) {
	case GC_FMT_NSF:
	case GC_FMT_NSFE:
	case GC_FMT_NEZ:
	case GC_FMT_NSZ:
	case GC_FMT_GBS:
	case GC_FMT_GBR:
	case GC_FMT_KSS:
	case GC_FMT_AY:
	case GC_FMT_HES:
	case GC_FMT_CPC:
	case GC_FMT_SGC:
	case GC_FMT_NSD:
		return 1;
	default:
		return 0;
	}
}

static int default_chip_play_ms(const gc_config *cfg, const gc_track_info *tr)
{
	int fade = cfg ? cfg->fade_ms : GC_DEFAULT_FADE_MS;
	int loops = cfg && cfg->loop_count > 0 ? cfg->loop_count : 1;
	if (fade < 0)
		fade = 0;
	if (tr && tr->loop_ms > 0) {
		int ms = tr->loop_ms * loops + fade;
		if (ms > GC_CAP_MS)
			ms = GC_CAP_MS;
		if (ms < 1)
			ms = gc_config_untagged_fallback_ms(cfg);
		if (gc_is_dummy_length_ms(ms))
			ms += 1;
		return ms;
	}
	return gc_config_untagged_fallback_ms(cfg);
}

/* Accept only confident measured lengths:
   - long one-loop / song (>= 55s), or
   - short SFX silence-end (< 15s, >= 250ms).
   15–55s phrase repeats are discarded (keep 10-min default). */
static int length_is_confident(int ms)
{
	if (ms < GC_MEAS_SFX_MIN_MS)
		return 0;
	if (ms < GC_MEAS_SFX_MAX_MS)
		return 1;
	if (ms >= GC_MEAS_MIN_LOOP_MS)
		return 1;
	return 0;
}

static int measure_one_track(gc_player *p, const gc_config *cfg, int track0)
{
	int cap = gc_config_untagged_cap_ms(cfg);
	int fade = cfg ? cfg->fade_ms : GC_DEFAULT_FADE_MS;
	int ms = 0;
	if (fade < 0)
		fade = 0;
	if (p->ops && p->ops->measure_ms)
		ms = p->ops->measure_ms(p->eng, track0, cap, fade);
	if (ms > 0 && !length_is_confident(ms))
		ms = 0;
	if (ms <= 0)
		ms = gc_measure_pcm_ms(p->ops, p->eng, track0, p->rate, cap, fade);
	if (ms > 0 && !length_is_confident(ms))
		ms = 0;
	if (ms <= 0)
		return 0;
	if (ms > GC_CAP_MS)
		ms = GC_CAP_MS;
	if (gc_is_dummy_length_ms(ms))
		ms += 1;
	return ms;
}

static void apply_chip_defaults(gc_player *p, const gc_config *cfg, const gc_len_rec *cache)
{
	int i, fade, did_cache = 0, used_fallback = 0;
	if (!p || !chip_needs_default_play(p->format))
		return;
	fade = cfg ? cfg->fade_ms : GC_DEFAULT_FADE_MS;
	if (fade < 0)
		fade = 0;
	p->measure_untagged = cfg && cfg->measure_untagged ? 1 : 0;
	p->defer_cap_ms = gc_config_untagged_cap_ms(cfg);
	p->defer_fade_ms = fade;
	p->untagged_fallback_ms = gc_config_untagged_fallback_ms(cfg);
	for (i = 0; i < p->info.track_count && i < GC_MAX_TRACKS; ++i) {
		int ms = p->info.tracks[i].duration_ms;
		p->track_pending_measure[i] = 0;
		if (gc_is_dummy_length_ms(ms))
			ms = 0;
		/* Absurd shorts (<250ms) are never trusted as chip TIME (parse junk). */
		if (ms > 0 && ms < GC_M3U_MIN_MS)
			ms = 0;
		if (ms > 0) {
			p->track_tagged[i] = 1;
			p->info.tracks[i].duration_ms = ms;
			continue;
		}
		p->track_tagged[i] = 0;
		if (cache && i < cache->track_count &&
		    length_is_confident(cache->duration_ms[i]) &&
		    !gc_is_dummy_length_ms(cache->duration_ms[i])) {
			p->info.tracks[i].duration_ms = cache->duration_ms[i];
			did_cache = 1;
			continue;
		}
		/* Do not block Open/GetFileInfo on heavy one-loop measure.
		   Advertise ~10 minutes now; measure during Process (NSFPlay) or
		   on first set_track (other engines). */
		p->info.tracks[i].duration_ms = default_chip_play_ms(cfg, &p->info.tracks[i]);
		used_fallback = 1;
		if (p->measure_untagged)
			p->track_pending_measure[i] = 1;
	}
	p->length_ms = p->info.tracks[p->track].duration_ms;
	if (p->length_ms > 0)
		p->cap_frames = (int)((int64_t)p->length_ms * p->rate / 1000);
	if (p->track_tagged[p->track]) {
		if (!p->info.length_src[0] || !strcmp(p->info.length_src, "unknown"))
			snprintf(p->info.length_src, sizeof p->info.length_src, "embedded/tag");
	} else if (did_cache) {
		snprintf(p->info.length_src, sizeof p->info.length_src, "measured");
	} else {
		snprintf(p->info.length_src, sizeof p->info.length_src, "untagged-max");
	}
	(void)used_fallback;
}

static void cache_store_lengths(gc_player *p)
{
	gc_len_rec rec;
	int i, any = 0;
	if (!p || !p->cache_path[0] || !p->cache_key[0])
		return;
	gc_len_rec_from_info(&rec, &p->info);
	/* Cache v3 stores only confident lengths; untagged default / pending -> 0. */
	for (i = 0; i < rec.track_count && i < GC_MAX_TRACKS; ++i) {
		int ms = rec.duration_ms[i];
		if (p->track_pending_measure[i] ||
		    (p->untagged_fallback_ms > 0 && ms == p->untagged_fallback_ms) ||
		    !length_is_confident(ms)) {
			rec.duration_ms[i] = 0;
		} else {
			any = 1;
		}
	}
	if (!any)
		return;
	snprintf(rec.src, sizeof rec.src, "measured");
	gc_len_cache_put(p->cache_path, p->cache_key, p->cache_size, p->cache_mtime, &rec);
}

/* Commit a confident measure to info + length cache.
   live_ok: apply to length_ms/cap_frames and notify XMPlay (SetLength).
   Long-music deferred results pass live_ok=0 (cache only; next set_track).
   Short SFX silence-end (<15s) may pass live_ok=1 to shrink the 10-min
   placeholder on first play without requiring Shift+Left revisit. */
static void apply_measured_length(gc_player *p, int track0, int ms, int live_ok)
{
	if (!p || track0 < 0 || track0 >= GC_MAX_TRACKS || ms <= 0)
		return;
	if (ms > GC_CAP_MS)
		ms = GC_CAP_MS;
	if (gc_is_dummy_length_ms(ms))
		ms += 1;
	if (!length_is_confident(ms)) {
		/* Fragile / mid-range phrase — keep 10-min default, stop retrying. */
		p->track_pending_measure[track0] = 0;
		return;
	}
	p->info.tracks[track0].duration_ms = ms;
	p->track_pending_measure[track0] = 0;
	cache_store_lengths(p);
	if (!live_ok)
		return; /* cache only — current play keeps placeholder TIME/cap */
	if (track0 == p->track) {
		p->length_ms = ms;
		p->cap_frames = (int)((int64_t)ms * p->rate / 1000);
		snprintf(p->info.length_src, sizeof p->info.length_src, "measured");
		/* Mid-play SFX: mark dirty so Process calls SetLength. set_track/Open
		   also call set_length_now; a second SetLength is harmless. */
		p->length_dirty = 1;
		p->length_dirty_ms = ms;
	}
}

static void defer_measure_start_track(gc_player *p, int track0)
{
	if (!p || !p->ops || track0 < 0 || track0 >= GC_MAX_TRACKS)
		return;
	if (!p->track_pending_measure[track0] || !p->measure_untagged)
		return;
	if (p->ops->defer_measure_cancel)
		p->ops->defer_measure_cancel(p->eng);
	p->defer_active = 0;
	if (p->ops->defer_measure_start &&
	    p->ops->defer_measure_start(p->eng, track0, p->defer_cap_ms, p->defer_fade_ms)) {
		p->defer_active = 1;
		return;
	}
	/* Engines without side-measure: one-track sync measure on first select. */
	if (p->ops->measure_ms) {
		gc_config tmp;
		int ms, saved = p->track;
		gc_config_defaults(&tmp);
		tmp.measure_untagged = 1;
		tmp.untagged_max_sec = p->defer_cap_ms / 1000;
		if (tmp.untagged_max_sec < 10)
			tmp.untagged_max_sec = 10;
		tmp.fade_ms = p->defer_fade_ms;
		ms = measure_one_track(p, &tmp, track0);
		if (p->ops->set_track)
			p->ops->set_track(p->eng, saved);
		p->track = saved;
		if (ms > 0)
			apply_measured_length(p, track0, ms, 1);
		else
			p->track_pending_measure[track0] = 0;
	}
}

static void defer_measure_poll(gc_player *p)
{
	int r;
	if (!p || !p->defer_active || !p->ops || !p->ops->defer_measure_poll)
		return;
	r = p->ops->defer_measure_poll(p->eng);
	if (r == 0)
		return;
	p->defer_active = 0;
	if (r > 0) {
		/* Live shrink only for short SFX silence-end; long loops stay cache-only. */
		int live = (r < GC_MEAS_SFX_MAX_MS) ? 1 : 0;
		apply_measured_length(p, p->track, r, live);
	} else {
		/* Detector gave up — keep 10-min. Clear pending so we do not spin. */
		p->track_pending_measure[p->track] = 0;
	}
}

static void reset_silence_state(gc_player *p)
{
	if (!p)
		return;
	p->heard_audio = 0;
	p->silent_frames = 0;
}

/* M3U play TIME: trust tagged lengths from 250ms up (SFX fanfares ~2–6s,
   music ≥15s). Only reject absurd <250ms junk / failed parse crumbs.
   Titles always merge regardless. */
static int m3u_duration_ok(gc_format fmt, int ms)
{
	if (ms < GC_M3U_MIN_MS)
		return 0;
	(void)fmt;
	return 1;
}

static void merge_m3u(gc_info *info, const gc_info *m3u)
{
	int i, n;
	if (!info || !m3u || m3u->track_count < 1)
		return;
	n = m3u->track_count;
	if (n > GC_MAX_TRACKS)
		n = GC_MAX_TRACKS;
	if (n > info->track_count)
		info->track_count = n;
	for (i = 0; i < n; ++i) {
		if (m3u->tracks[i].title[0])
			memcpy(info->tracks[i].title, m3u->tracks[i].title,
			       sizeof info->tracks[i].title);
		if (m3u->tracks[i].duration_ms > 0 &&
		    m3u_duration_ok(info->format, m3u->tracks[i].duration_ms))
			info->tracks[i].duration_ms = m3u->tracks[i].duration_ms;
		if (m3u->tracks[i].loop_ms > 0)
			info->tracks[i].loop_ms = m3u->tracks[i].loop_ms;
		if (m3u->tracks[i].fade_ms > 0)
			info->tracks[i].fade_ms = m3u->tracks[i].fade_ms;
	}
}

static int try_open(gc_player *p, const unsigned char *data, size_t len,
                    gc_format fmt, const gc_config *cfg)
{
	gc_engine list[8];
	int n, i;
	gc_engine forced = gc_config_engine_for(cfg, fmt);
	fallback_list(fmt, forced, list, &n);
	n = dedup_engines(list, n);
	for (i = 0; i < n; ++i) {
		const gc_eng_ops *ops = ops_for(list[i]);
		gc_eng_state *st;
		if (!ops || !ops->can_open(fmt))
			continue;
		st = ops->open(data, len, cfg ? cfg->rate : GC_DEFAULT_RATE, cfg);
		if (!st)
			continue;
		p->ops = ops;
		p->eng = st;
		p->engine = ops->kind;
		p->format = fmt;
		p->rate = cfg ? cfg->rate : GC_DEFAULT_RATE;
		if (p->rate < 8000)
			p->rate = GC_DEFAULT_RATE;
		if (ops->info)
			ops->info(st, &p->info);
		p->info.format = fmt;
		if (p->info.track_count < 1)
			p->info.track_count = 1;
		p->track = p->info.start_track;
		if (p->track < 0 || p->track >= p->info.track_count)
			p->track = 0;
		if (ops->set_track)
			ops->set_track(st, p->track);
		p->length_ms = p->info.tracks[p->track].duration_ms;
		if (gc_is_dummy_length_ms(p->length_ms))
			p->length_ms = 0;
		if (p->length_ms <= 0 && ops->length_ms)
			p->length_ms = ops->length_ms(st, p->track);
		if (gc_is_dummy_length_ms(p->length_ms))
			p->length_ms = 0;
		p->fade_ms = cfg ? cfg->fade_ms : GC_DEFAULT_FADE_MS;
		p->silence_ms = cfg ? cfg->fatso_silence_ms : 1200;
		p->protect_tagged = cfg ? cfg->fatso_no_silence_if_time : 1;
		reset_silence_state(p);
		/* PSF-style: fade tag 0 is a natural ending — do not fade silence. */
		if (p->info.length_src[0] && !strcmp(p->info.length_src, "PSF tag"))
			p->fade_ms = p->info.tracks[p->track].fade_ms;
		if (p->length_ms <= 0)
			p->cap_frames = (int)((int64_t)GC_CAP_MS * p->rate / 1000);
		else
			p->cap_frames = (int)((int64_t)p->length_ms * p->rate / 1000);
		gc_volume_init(&p->vol, cfg, fmt, p->engine, p->rate);
		if (cfg)
			gc_player_apply_mute(p, cfg);
		return 1;
	}
	return 0;
}

gc_player *gc_player_open(const unsigned char *data, size_t len,
                          const char *filename, const char *m3u_text, size_t m3u_len,
                          const gc_config *cfg)
{
	gc_player *p;
	gc_format fmt;
	unsigned char *unpacked = NULL;
	size_t unpacked_len = 0;
	const unsigned char *use = data;
	size_t use_len = len;
	gc_blob zip_music, zip_m3u;
	gc_info m3u_info;
	int have_m3u = 0;
	gc_vfs vfs;
	gc_config local;
	const char *music_name = filename;

	if (!data || len < 4)
		return NULL;
	fmt = gc_probe(data, len, filename);
	if (gc_is_sap(data, len, filename))
		return NULL;

	memset(&zip_music, 0, sizeof zip_music);
	memset(&zip_m3u, 0, sizeof zip_m3u);
	memset(&m3u_info, 0, sizeof m3u_info);
	gc_vfs_init(&vfs);
	if (cfg)
		local = *cfg;
	else
		gc_config_defaults(&local);
	gc_vfs_set_dir_from_path(&vfs, filename);
	if (vfs.dir[0])
		memcpy(local.vfs_dir, vfs.dir, sizeof local.vfs_dir);
	local.vfs = &vfs;

	if (fmt == GC_FMT_GZIP || fmt == GC_FMT_VGZ) {
		if (!gc_inflate_gzip(data, len, &unpacked, &unpacked_len)) {
			gc_vfs_free(&vfs);
			return NULL;
		}
		use = unpacked;
		use_len = unpacked_len;
		fmt = gc_probe(use, use_len, filename);
		if (fmt == GC_FMT_UNKNOWN && filename)
			fmt = GC_FMT_VGM;
	} else if (fmt == GC_FMT_ZIP || fmt == GC_FMT_SEVENZ ||
	           fmt == GC_FMT_NSZ || fmt == GC_FMT_NEZ) {
		if (gc_archive_extract_all(data, len, &vfs) &&
		    gc_vfs_pick_music(&vfs, &zip_music, &zip_m3u)) {
			use = zip_music.data;
			use_len = zip_music.len;
			fmt = gc_probe(use, use_len, zip_music.name);
			music_name = zip_music.name;
			if (zip_m3u.data && zip_m3u.len) {
				have_m3u = gc_m3u_parse((const char *)zip_m3u.data, zip_m3u.len,
				                        zip_music.name, &m3u_info);
			}
		} else if (fmt == GC_FMT_ZIP || fmt == GC_FMT_SEVENZ) {
			free(unpacked);
			gc_vfs_free(&vfs);
			return NULL;
		}
	}

	if (m3u_text && m3u_len && !have_m3u) {
		have_m3u = gc_m3u_parse(m3u_text, m3u_len, filename, &m3u_info);
		/* Sidecar next to a renamed NSF: playlist paths may use the original
		   rip name. If nothing matched, apply all entries (single-file M3U). */
		if (!have_m3u)
			have_m3u = gc_m3u_parse(m3u_text, m3u_len, NULL, &m3u_info);
	}

	if (!gc_format_claimed(fmt) || fmt == GC_FMT_ZIP || fmt == GC_FMT_SEVENZ) {
		free(unpacked);
		free(zip_music.data);
		free(zip_m3u.data);
		gc_vfs_free(&vfs);
		return NULL;
	}

	if (music_name && music_name[0]) {
		size_t n = strlen(music_name);
		if (n >= sizeof local.vfs_uri)
			n = sizeof local.vfs_uri - 1;
		memcpy(local.vfs_uri, music_name, n);
		local.vfs_uri[n] = '\0';
	} else if (fmt == GC_FMT_GSF || fmt == GC_FMT_MINIGSF) {
		memcpy(local.vfs_uri, "tune.minigsf", 13);
	} else if (fmt == GC_FMT_USF || fmt == GC_FMT_MINIUSF) {
		memcpy(local.vfs_uri, "tune.miniusf", 13);
	}
	if (!gc_vfs_find(&vfs, local.vfs_uri[0] ? local.vfs_uri : "tune.bin")) {
		unsigned char *copy = (unsigned char *)malloc(use_len);
		if (copy) {
			memcpy(copy, use, use_len);
			if (!gc_vfs_add(&vfs, local.vfs_uri[0] ? local.vfs_uri : "tune.bin",
			                copy, use_len))
				free(copy);
		}
	}

	p = (gc_player *)calloc(1, sizeof *p);
	if (!p) {
		free(unpacked);
		free(zip_music.data);
		free(zip_m3u.data);
		gc_vfs_free(&vfs);
		return NULL;
	}
	if (!try_open(p, use, use_len, fmt, &local)) {
		free(p);
		free(unpacked);
		free(zip_music.data);
		free(zip_m3u.data);
		gc_vfs_free(&vfs);
		return NULL;
	}
	if (have_m3u) {
		if (!local.use_tag_length) {
			int i;
			for (i = 0; i < GC_MAX_TRACKS; ++i)
				m3u_info.tracks[i].duration_ms = 0;
		}
		merge_m3u(&p->info, &m3u_info);
		gc_info_apply_loops(&p->info, local.loop_count);
		if (p->info.tracks[p->track].fade_ms > 0)
			p->fade_ms = p->info.tracks[p->track].fade_ms;
	}
	if (have_m3u && local.use_tag_length && p->info.tracks[p->track].duration_ms > 0) {
		p->length_ms = p->info.tracks[p->track].duration_ms;
		p->cap_frames = (int)((int64_t)p->length_ms * p->rate / 1000);
		snprintf(p->info.length_src, sizeof p->info.length_src, "M3U");
	}
	{
		gc_len_rec cached;
		int have_cache = 0;
		const char *key = filename && filename[0] ? filename : music_name;
		uint64_t sz = gc_file_size(filename);
		int64_t mt = gc_file_mtime(filename);
		if (sz == 0)
			sz = (uint64_t)len;
		if (local.len_cache_path[0] && key && key[0]) {
			size_t kn = strlen(key);
			if (kn >= sizeof p->cache_key)
				kn = sizeof p->cache_key - 1;
			memcpy(p->cache_key, key, kn);
			p->cache_key[kn] = '\0';
			memcpy(p->cache_path, local.len_cache_path, sizeof p->cache_path);
			p->cache_size = sz;
			p->cache_mtime = mt;
			have_cache = gc_len_cache_get(local.len_cache_path, key, sz, mt, &cached);
		}
		apply_chip_defaults(p, &local, have_cache ? &cached : NULL);
		/* Deferred measure starts on first Process/set_track — avoids a second
		   NSFPlay Load during GetFileInfo open/close (iconv abort on this host). */
	}
	if (p->length_ms > 0 && !p->info.length_src[0])
		snprintf(p->info.length_src, sizeof p->info.length_src, "embedded/tag");
	else if (p->length_ms <= 0 && !p->info.length_src[0])
		snprintf(p->info.length_src, sizeof p->info.length_src, "unknown");
	free(unpacked);
	free(zip_music.data);
	free(zip_m3u.data);
	gc_vfs_free(&vfs);
	return p;
}

void gc_player_close(gc_player *p)
{
	if (!p)
		return;
	if (p->ops && p->ops->close)
		p->ops->close(p->eng);
	free(p);
}

int gc_player_info(gc_player *p, gc_info *out)
{
	if (!p || !out)
		return 0;
	*out = p->info;
	return 1;
}

int gc_player_set_track(gc_player *p, int track0)
{
	if (!p || !p->ops || !p->ops->set_track)
		return -1;
	if (track0 < 0 || track0 >= p->info.track_count)
		return -1;
	if (p->ops->set_track(p->eng, track0) != 0)
		return -1;
	p->track = track0;
	p->frames_played = 0;
	reset_silence_state(p);
	if (p->info.length_src[0] && !strcmp(p->info.length_src, "PSF tag"))
		p->fade_ms = p->info.tracks[track0].fade_ms;
	else if (p->info.tracks[track0].fade_ms > 0)
		p->fade_ms = p->info.tracks[track0].fade_ms;
	p->length_ms = p->info.tracks[track0].duration_ms;
	if (gc_is_dummy_length_ms(p->length_ms))
		p->length_ms = 0;
	if (p->length_ms <= 0 && p->ops->length_ms) {
		int ms = p->ops->length_ms(p->eng, track0);
		if (!gc_is_dummy_length_ms(ms))
			p->length_ms = ms;
	}
	if (p->length_ms <= 0 && chip_needs_default_play(p->format)) {
		int fade = p->fade_ms > 0 ? p->fade_ms : 0;
		if (p->info.tracks[track0].loop_ms > 0)
			p->length_ms = p->info.tracks[track0].loop_ms + fade;
		else if (p->untagged_fallback_ms > 0)
			p->length_ms = p->untagged_fallback_ms;
		else
			p->length_ms = GC_DEFAULT_PLAY_MS + fade;
	}
	if (p->length_ms > 0) {
		p->info.tracks[track0].duration_ms = p->length_ms;
		p->cap_frames = (int)((int64_t)p->length_ms * p->rate / 1000);
	} else
		p->cap_frames = (int)((int64_t)GC_CAP_MS * p->rate / 1000);
	defer_measure_start_track(p, track0);
	return 0;
}

int gc_player_current_track(const gc_player *p)
{
	return p ? p->track : 0;
}

int gc_player_track_count(const gc_player *p)
{
	return p ? p->info.track_count : 0;
}

int gc_player_length_ms(gc_player *p)
{
	if (!p)
		return 0;
	return p->length_ms;
}

int gc_player_process(gc_player *p, float *stereo, int frames)
{
	int got, left;
	if (!p || !p->ops || !p->ops->render || !stereo || frames <= 0)
		return 0;
	if (p->track < GC_MAX_TRACKS && p->track_pending_measure[p->track] &&
	    !p->defer_active)
		defer_measure_start_track(p, p->track);
	defer_measure_poll(p);
	if (p->cap_frames > 0) {
		left = p->cap_frames - p->frames_played;
		if (left <= 0)
			return 0;
		if (frames > left)
			frames = left;
	}
	got = p->ops->render(p->eng, stereo, frames);
	/* Until cap_frames: never treat engine stop as EOF for untagged chip
	   placeholders (NSFPlay IsStopped / early FADE). Fill silence and
	   keep counting so the 10-min TIME survives. */
	if (got <= 0) {
		int placeholder = (p->untagged_fallback_ms > 0 &&
		                   p->length_ms == p->untagged_fallback_ms) ||
		                  (p->track < GC_MAX_TRACKS &&
		                   p->track_pending_measure[p->track]) ||
		                  (p->format == GC_FMT_NSF || p->format == GC_FMT_NSFE ||
		                   p->format == GC_FMT_NEZ || p->format == GC_FMT_NSZ);
		if (placeholder && p->cap_frames > 0 &&
		    p->frames_played < p->cap_frames) {
			memset(stereo, 0, (size_t)frames * 2u * sizeof(float));
			got = frames;
		} else {
			return 0;
		}
	}
	/* Live silence-cut: OFF for NSF family (always — deferred lengths), and
	   OFF for any untagged chip placeholder / pending measure. Deferred
	   measure owns SFX silence-ends in the cache only. Never EOF early. */
	{
		int allow_sil = 0;
		if (p->silence_ms > 0) {
			switch (p->format) {
			case GC_FMT_NSF:
			case GC_FMT_NSFE:
			case GC_FMT_NEZ:
			case GC_FMT_NSZ:
				allow_sil = 0;
				break;
			default:
				allow_sil = 1;
				if (p->untagged_fallback_ms > 0 &&
				    p->length_ms == p->untagged_fallback_ms)
					allow_sil = 0;
				if (p->track < GC_MAX_TRACKS &&
				    p->track_pending_measure[p->track])
					allow_sil = 0;
				if (p->protect_tagged && p->track < GC_MAX_TRACKS &&
				    p->track_tagged[p->track])
					allow_sil = 0;
				break;
			}
		}
		if (allow_sil) {
			int i, keep = got;
			float thr = 1.5e-3f;
			for (i = 0; i < got; ++i) {
				float aL = stereo[i * 2];
				float aR = stereo[i * 2 + 1];
				if (aL < 0) aL = -aL;
				if (aR < 0) aR = -aR;
				if (aL > thr || aR > thr) {
					p->heard_audio = 1;
					p->silent_frames = 0;
				} else if (p->heard_audio) {
					p->silent_frames++;
					if ((int)((int64_t)p->silent_frames * 1000 / p->rate) >=
					    p->silence_ms) {
						keep = i + 1;
						p->frames_played += keep;
						gc_volume_process(&p->vol, stereo, keep);
						p->cap_frames = p->frames_played;
						return keep;
					}
				}
			}
		}
	}
	/* Short fade in the last fade_ms of a known length. */
	if (p->length_ms > 0 && p->fade_ms > 0) {
		int i;
		int played_ms = (int)((int64_t)p->frames_played * 1000 / p->rate);
		int fade_start = p->length_ms - p->fade_ms;
		for (i = 0; i < got; ++i) {
			int t = played_ms + (int)((int64_t)i * 1000 / p->rate);
			if (t > fade_start) {
				float a = (float)(p->length_ms - t) / (float)p->fade_ms;
				if (a < 0.0f) a = 0.0f;
				if (a > 1.0f) a = 1.0f;
				stereo[i * 2] *= a;
				stereo[i * 2 + 1] *= a;
			}
		}
	}
	gc_volume_process(&p->vol, stereo, got);
	p->frames_played += got;
	return got;
}

int gc_player_seek_ms(gc_player *p, int ms)
{
	int r;
	if (!p || !p->ops || !p->ops->seek_ms)
		return -1;
	if (ms < 0)
		ms = 0;
	r = p->ops->seek_ms(p->eng, ms);
	if (r < 0)
		return -1;
	p->frames_played = (int)((int64_t)ms * p->rate / 1000);
	reset_silence_state(p);
	return r;
}

gc_format gc_player_format(const gc_player *p)
{
	return p ? p->format : GC_FMT_UNKNOWN;
}

gc_engine gc_player_engine(const gc_player *p)
{
	return p ? p->engine : GC_ENG_AUTO;
}

const char *gc_player_engine_name(const gc_player *p)
{
	if (p && p->ops)
		return p->ops->name;
	return "";
}

int gc_player_rate(const gc_player *p)
{
	return p ? p->rate : GC_DEFAULT_RATE;
}

int gc_player_voices(gc_player *p)
{
	if (p && p->ops && p->ops->voice_count)
		return p->ops->voice_count(p->eng);
	return 0;
}

const char *gc_player_voice_name(gc_player *p, int i)
{
	if (p && p->ops && p->ops->voice_name)
		return p->ops->voice_name(p->eng, i);
	return "";
}

void gc_player_apply_mute(gc_player *p, const gc_config *cfg)
{
	float level[GC_MAX_CHANNELS];
	int i;
	if (!p || !p->ops || !cfg)
		return;
	for (i = 0; i < GC_MAX_CHANNELS; ++i)
		level[i] = cfg->mute[i] ? 0.0f : (float)cfg->chan_vol[i] / 255.0f;
	if (p->ops->set_mixer)
		p->ops->set_mixer(p->eng, cfg);
	else if (p->ops->set_mute)
		p->ops->set_mute(p->eng, cfg->mute, level);
}


int gc_player_length_updated(gc_player *p, int *out_ms)
{
	if (!p || !p->length_dirty)
		return 0;
	p->length_dirty = 0;
	if (out_ms)
		*out_ms = p->length_dirty_ms;
	return 1;
}
