#include "volume.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static float db_to_lin(float db)
{
	return powf(10.0f, db / 20.0f);
}

void gc_volume_init(gc_volume *v, const gc_config *cfg, gc_format fmt,
                    gc_engine eng, int rate)
{
	float g;
	if (!v)
		return;
	memset(v, 0, sizeof *v);
	v->limiter_g = 1.0f;
	v->soft_lim = 1;
	v->target = 0.89f; /* loud but under clip */
	if (rate < 8000)
		rate = GC_DEFAULT_RATE;
	if (cfg && cfg->gbs_highpass &&
	    (fmt == GC_FMT_GBS || fmt == GC_FMT_GBR)) {
		/* Game Boy output capacitor, ~90 Hz. */
		float fc = 90.0f;
		v->hpf = 1;
		v->hpf_r = expf(-2.0f * (float)M_PI * fc / (float)rate);
	}
	if (cfg) {
		v->auto_norm = cfg->auto_normalize;
		v->soft_lim = cfg->soft_limiter ? 1 : 0;
		v->mono = cfg->output_mono ? 1 : 0;
		v->width = cfg->stereo_width;
		if (cfg->loudness_db < -24.0f)
			v->target = 0.20f;
		else if (cfg->loudness_db < 0.0f)
			v->target = 0.45f;
		else if (cfg->loudness_db < 4.0f)
			v->target = 0.70f;
		else
			v->target = 0.89f;
		g = db_to_lin(cfg->loudness_db * 0.25f);
		if (fmt >= 0 && fmt < 32)
			g *= db_to_lin(cfg->trim_db[fmt]);
		if (eng >= 0 && eng < 8)
			g *= db_to_lin(cfg->engine_db[eng]);
	} else {
		v->auto_norm = 1;
		g = db_to_lin(1.5f);
		if (fmt == GC_FMT_KSS)
			g *= db_to_lin(6.0f);
	}
	if (g < 0.05f)
		g = 0.05f;
	if (g > 16.0f)
		g = 16.0f;
	v->gain = g;
}

void gc_volume_process(gc_volume *v, float *stereo, int frames)
{
	int i;
	float g, peak, lim;
	if (!v || !stereo || frames <= 0)
		return;
	g = v->gain;
	peak = v->peak;

	if (v->auto_norm && !v->measured) {
		for (i = 0; i < frames * 2; ++i) {
			float a = stereo[i] < 0 ? -stereo[i] : stereo[i];
			if (a > peak)
				peak = a;
			v->sumsq += stereo[i] * stereo[i];
		}
		v->peak = peak;
		v->samples += frames * 2;
		if (v->samples >= 48000) { /* ~0.5 s stereo at 48 k */
			float rms = sqrtf(v->sumsq / (float)v->samples);
			float want = v->target;
			float from = peak > rms * 4.0f ? peak : rms * 4.0f;
			if (from < 1.0e-5f)
				from = 1.0e-5f;
			g *= want / from;
			if (g > 16.0f)
				g = 16.0f;
			v->gain = g;
			v->measured = 1;
		}
	}

	lim = v->limiter_g;
	for (i = 0; i < frames; ++i) {
		float L = stereo[i * 2] * g;
		float R = stereo[i * 2 + 1] * g;
		float m, s, mx, absL, absR;

		if (v->hpf) {
			float xl = L, xr = R;
			L = xl - v->hpf_x[0] + v->hpf_r * v->hpf_y[0];
			R = xr - v->hpf_x[1] + v->hpf_r * v->hpf_y[1];
			v->hpf_x[0] = xl;
			v->hpf_x[1] = xr;
			v->hpf_y[0] = L;
			v->hpf_y[1] = R;
		}

		if (v->width > 0.001f || v->mono) {
			m = 0.5f * (L + R);
			if (v->mono) {
				L = R = m;
			} else {
				s = 0.5f * (L - R) * (1.0f + v->width);
				L = m + s;
				R = m - s;
			}
		}

		if (v->soft_lim) {
			absL = L < 0 ? -L : L;
			absR = R < 0 ? -R : R;
			mx = absL > absR ? absL : absR;
			if (mx * lim > 0.98f)
				lim = 0.98f / mx;
			else
				lim = lim * 0.9995f + 0.0005f;
			if (lim > 1.0f)
				lim = 1.0f;
			L *= lim;
			R *= lim;
		} else {
			if (L > 1.0f) L = 1.0f;
			if (L < -1.0f) L = -1.0f;
			if (R > 1.0f) R = 1.0f;
			if (R < -1.0f) R = -1.0f;
		}
		stereo[i * 2] = L;
		stereo[i * 2 + 1] = R;
	}
	v->limiter_g = lim;
}
