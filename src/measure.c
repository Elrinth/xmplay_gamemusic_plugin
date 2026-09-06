#include "measure.h"

#include <stdlib.h>
#include <string.h>

#define GC_MEAS_TAIL_MS       400
#define GC_MEAS_MAX_SIG      8192

static uint32_t window_sig(const float *st, int n)
{
	int i, zc = 0, half;
	float e0 = 0.0f, e1 = 0.0f, pk = 0.0f, prev = 0.0f;
	unsigned pkq, e0q, e1q, zq;
	if (n < 1)
		return 0;
	half = n / 2;
	if (half < 1)
		half = 1;
	for (i = 0; i < n; ++i) {
		float m = 0.5f * (st[i * 2] + st[i * 2 + 1]);
		float a = m < 0.0f ? -m : m;
		if (a > pk)
			pk = a;
		if (i < half)
			e0 += a;
		else
			e1 += a;
		if (i && ((prev < 0.0f && m >= 0.0f) || (prev >= 0.0f && m < 0.0f)))
			zc++;
		prev = m;
	}
	e0 /= (float)half;
	e1 /= (float)(n - half > 0 ? n - half : 1);
	pkq = (unsigned)(pk * 31.0f + 0.5f);
	e0q = (unsigned)(e0 * 31.0f + 0.5f);
	e1q = (unsigned)(e1 * 31.0f + 0.5f);
	zq = (unsigned)(zc * 31 / n);
	if (pkq > 31) pkq = 31;
	if (e0q > 31) e0q = 31;
	if (e1q > 31) e1q = 31;
	if (zq > 31) zq = 31;
	return pkq | (e0q << 5) | (e1q << 10) | (zq << 15);
}

static int is_hush(uint32_t sig)
{
	return (sig & 0x7FFF) == 0;
}

/* Confident lengths only: one-loop (>=15s) or short SFX silence-end (<15s). */
static int confident_ms(int ms, int from_loop)
{
	if (ms < GC_MEAS_MIN_SANE_MS)
		return 0;
	if (from_loop)
		return ms >= GC_MEAS_MIN_LOOP_MS ? ms : 0;
	/* Silence / song-end path — SFX / one-shots only. */
	if (ms >= GC_MEAS_SFX_MAX_MS)
		return 0;
	return ms;
}

int gc_measure_pcm_ms(const gc_eng_ops *ops, gc_eng_state *eng,
                      int track0, int rate, int cap_ms, int fade_ms)
{
	float *buf;
	uint32_t *sig;
	int *when;
	int win, nsig = 0, heard = 0, last_peak = 0, silent_ms = 0;
	int played = 0, got, i;
	int result = 0, from_loop = 0;

	if (!ops || !ops->render || !eng || rate < 8000 || cap_ms < 500)
		return 0;
	if (ops->set_track && ops->set_track(eng, track0) != 0)
		return 0;

	win = rate / 50;
	if (win < 64)
		win = 64;
	if (win > 2048)
		win = 2048;
	buf = (float *)malloc((size_t)win * 2u * sizeof(float));
	sig = (uint32_t *)malloc((size_t)GC_MEAS_MAX_SIG * sizeof(uint32_t));
	when = (int *)malloc((size_t)GC_MEAS_MAX_SIG * sizeof(int));
	if (!buf || !sig || !when) {
		free(buf);
		free(sig);
		free(when);
		return 0;
	}

	while (played < cap_ms) {
		uint32_t s;
		got = ops->render(eng, buf, win);
		if (got <= 0) {
			if (heard) {
				result = last_peak + (fade_ms > 0 ? fade_ms : GC_MEAS_TAIL_MS);
				if (result < last_peak + GC_MEAS_TAIL_MS)
					result = last_peak + GC_MEAS_TAIL_MS;
			}
			break;
		}
		played += (int)((int64_t)got * 1000 / rate);
		s = window_sig(buf, got);
		if (!heard) {
			if (is_hush(s))
				continue;
			heard = 1;
			last_peak = played;
			silent_ms = 0;
		} else if (is_hush(s)) {
			silent_ms += (int)((int64_t)got * 1000 / rate);
			if (silent_ms >= GC_MEAS_SILENCE_MS) {
				result = last_peak + (fade_ms > 0 ? fade_ms : GC_MEAS_TAIL_MS);
				break;
			}
		} else {
			last_peak = played;
			silent_ms = 0;
		}
		if (nsig < GC_MEAS_MAX_SIG) {
			sig[nsig] = s;
			when[nsig] = played;
			for (i = nsig - 1; i >= 0; --i) {
				int period = when[nsig] - when[i];
				int j, k, ok;
				if (period < GC_MEAS_MIN_LOOP_MS)
					continue;
				if (sig[i] != s)
					continue;
				/* Confirm a second period so a drone does not look like a loop. */
				ok = 0;
				for (j = i - 1; j >= 0; --j) {
					if (when[i] - when[j] < period - 80)
						continue;
					if (when[i] - when[j] > period + 80)
						break;
					if (sig[j] == sig[i]) {
						ok = 1;
						break;
					}
				}
				if (!ok)
					continue;
				/* Same signature two periods back, or matching neighbours. */
				k = 0;
				for (j = 0; j < 3 && nsig - j >= 0 && i - j >= 0; ++j) {
					if (sig[nsig - j] == sig[i - j])
						k++;
				}
				if (k < 2)
					continue;
				result = when[nsig] + (fade_ms > 0 ? fade_ms : 0);
				from_loop = 1;
				goto done;
			}
			nsig++;
		}
	}

done:
	free(buf);
	free(sig);
	free(when);
	if (ops->set_track)
		ops->set_track(eng, track0);
	if (result > cap_ms + (fade_ms > 0 ? fade_ms : 0))
		result = cap_ms + (fade_ms > 0 ? fade_ms : 0);
	result = confident_ms(result, from_loop);
	if (gc_is_dummy_length_ms(result))
		result += 1;
	return result;
}
