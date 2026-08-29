#ifndef GAMECHIP_VOLUME_H
#define GAMECHIP_VOLUME_H

#include "gamechip.h"
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gc_volume {
	float gain;
	float peak;
	float sumsq;
	int samples;
	int measured;
	int auto_norm;
	int soft_lim;
	int mono;
	float target;
	float limiter_g;
	float width;
	int hpf;
	float hpf_r;
	float hpf_x[2];
	float hpf_y[2];
} gc_volume;

void gc_volume_init(gc_volume *v, const gc_config *cfg, gc_format fmt,
                    gc_engine eng, int rate);
void gc_volume_process(gc_volume *v, float *stereo, int frames);

#ifdef __cplusplus
}
#endif

#endif
