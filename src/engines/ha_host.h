#ifndef GC_HA_HOST_H
#define GC_HA_HOST_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Highly Advanced 0.11 C core is global / non-reentrant. */
int ha_core_busy(void);
int ha_core_open(const uint8_t *rom, int size, int interpolation);
void ha_core_close(void);
void ha_core_reset(void);
/* Native HA output is 44100 Hz stereo s16. */
int ha_core_render_i16(int16_t *stereo, int frames);
void ha_core_set_enable(int mask);

#ifdef __cplusplus
}
#endif

#endif
