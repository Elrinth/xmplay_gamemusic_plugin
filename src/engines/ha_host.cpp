/* Host glue for the Highly Advanced 0.11 C core.
   Does not wrap in_gsf.dll. ROM is assembled by psflib + gc_vfs. */
#include "ha_host.h"

#include "GBA.h"
#include "Sound.h"
#include "Util.h"
#include "snd_interp.h"

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <vector>

int emulating;
bool debugger;
bool systemSoundOn;
int soundInitialized;

int sndBitsPerSample = 16;
int sndSamplesPerSec = 44100;
int sndNumChannels = 2;
int cpupercent;
int relvolume = 1000;
int TrackLength = 0x7fffffff;
int FadeLength = 0;
int IgnoreTrackLength = 1;
int DefaultLength = 0;
int playforever = 1;
int TrailingSilence = 0;
int DetectSilence;
int silencedetected;
int silencelength = 5;
int didseek;
double playtime;
double decode_pos_ms;
int seek_needed = -1;

static int ha_locked;
static int ha_interp_ready;
static std::vector<int16_t> ha_buf;

extern "C" void writeSound(void);
extern void setupSound(void);

void setupSound(void)
{
	sndNumChannels = 2;
	soundQuality = 1;
	sndSamplesPerSec = 44100;
	soundBufferLen = 576 * 2 * 2;
	sndBitsPerSample = 16;
	systemSoundOn = true;
}

void systemWriteDataToSoundBuffer()
{
	writeSound();
}

void systemSoundShutdown() {}
void systemSoundPause() {}
void systemSoundReset() {}
void systemSoundResume() {}
bool systemSoundInit() { return true; }
bool systemCanChangeSoundQuality() { return true; }
void systemScreenMessage(const char *) {}
void systemMessage(int, const char *, ...) {}

extern "C" void DisplayError(char *Message, ...)
{
	(void)Message;
}

extern "C" void end_of_track(void) {}

void winlog(const char *, ...) {}

bool utilIsGBAImage(const char *) { return true; }
void utilGetBaseName(const char *file, char *buf)
{
	const char *s, *e;
	if (!buf)
		return;
	buf[0] = '\0';
	if (!file)
		return;
	s = file;
	e = file;
	while (*e)
		e++;
	while (e > file && e[-1] != '/' && e[-1] != '\\')
		e--;
	s = e;
	e = s;
	while (*e)
		e++;
	while (e > s && *e != '.')
		e--;
	if (e <= s)
		e = s + strlen(s);
	memcpy(buf, s, (size_t)(e - s));
	buf[e - s] = '\0';
}
void utilGetBasePath(const char *, char *buffer)
{
	if (buffer)
		buffer[0] = '\0';
}
IMAGE_TYPE utilFindType(const char *) { return IMAGE_GBA; }
u8 *utilLoad(const char *, bool (*)(const char *), u8 *, int &) { return NULL; }
void utilPutDword(u8 *, u32) {}
void utilPutWord(u8 *, u16) {}
void utilWriteData(gzFile, variable_desc *) {}
void utilReadData(gzFile, variable_desc *) {}
int utilReadInt(gzFile) { return 0; }
void utilWriteInt(gzFile, int) {}
gzFile utilGzOpen(const char *, const char *) { return NULL; }
gzFile utilMemGzOpen(char *, int, char *) { return NULL; }
int utilGzWrite(gzFile, const voidp, unsigned int) { return 0; }
int utilGzRead(gzFile, voidp, unsigned int) { return 0; }
int utilGzClose(gzFile) { return 0; }
long utilGzMemTell(gzFile) { return 0; }
void utilGBAFindSave(const u8 *, const int) {}

extern "C" void writeSound(void)
{
	int n = soundBufferLen / (int)sizeof(int16_t);
	const int16_t *src = (const int16_t *)soundFinalWave;
	if (n <= 0 || !src)
		return;
	ha_buf.insert(ha_buf.end(), src, src + n);
	if (sndSamplesPerSec > 0)
		decode_pos_ms += ((n / 2) * 1000.0) / (double)sndSamplesPerSec;
}

int ha_core_busy(void)
{
	return ha_locked;
}

int ha_core_open(const uint8_t *rom_data, int size, int interpolation)
{
	if (ha_locked || !rom_data || size <= 0)
		return 0;
	ha_locked = 1;
	ha_buf.clear();
	decode_pos_ms = 0;
	seek_needed = -1;
	didseek = 0;
	playforever = 1;
	IgnoreTrackLength = 1;
	DetectSilence = 0;
	TrackLength = 0x7fffffff;
	FadeLength = 0;
	cpuIsMultiBoot = false;
	soundInterpolation = interpolation ? 4 : 0;
	if (!CPULoadRomMem(rom_data, size)) {
		ha_locked = 0;
		return 0;
	}
	if (soundInitialized) {
		soundReset();
	} else {
		if (!soundOffFlag)
			soundInit();
		soundInitialized = 1;
	}
	if (ha_interp_ready)
		interp_cleanup();
	interp_setup(soundInterpolation);
	ha_interp_ready = 1;
	CPUInit((char *)NULL, false);
	CPUReset();
	soundReset();
	soundResume();
	emulating = 1;
	return 1;
}

void ha_core_close(void)
{
	if (!ha_locked)
		return;
	soundShutdown();
	CPUCleanUp();
	if (ha_interp_ready) {
		interp_cleanup();
		ha_interp_ready = 0;
	}
	soundInitialized = 0;
	emulating = 0;
	ha_buf.clear();
	ha_locked = 0;
}

void ha_core_reset(void)
{
	if (!ha_locked)
		return;
	ha_buf.clear();
	decode_pos_ms = 0;
	seek_needed = -1;
	CPUReset();
	soundReset();
	soundResume();
}

int ha_core_render_i16(int16_t *stereo, int frames)
{
	int need, guard = 0;
	if (!ha_locked || !stereo || frames <= 0)
		return 0;
	need = frames * 2;
	while ((int)ha_buf.size() < need && guard++ < 128 && emulating)
		CPULoop(250000);
	if ((int)ha_buf.size() < 2)
		return 0;
	if ((int)ha_buf.size() < need)
		need = (int)ha_buf.size() & ~1;
	frames = need / 2;
	memcpy(stereo, ha_buf.data(), (size_t)need * sizeof(int16_t));
	ha_buf.erase(ha_buf.begin(), ha_buf.begin() + need);
	return frames;
}

void ha_core_set_enable(int mask)
{
	if (!ha_locked)
		return;
	soundDisable(0x30f);
	soundEnable(mask & 0x30f);
}
