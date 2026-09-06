#include "m3u.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void copy_trim(char *dst, size_t cap, const char *src, size_t n)
{
	size_t i, a, b;
	if (!dst || cap == 0)
		return;
	dst[0] = '\0';
	if (!src)
		return;
	a = 0;
	while (a < n && (src[a] == ' ' || src[a] == '\t'))
		a++;
	b = n;
	while (b > a && (src[b - 1] == ' ' || src[b - 1] == '\t' || src[b - 1] == '\r'))
		b--;
	if (b > a) {
		if ((size_t)(b - a) >= cap)
			b = a + cap - 1;
		memcpy(dst, src + a, (size_t)(b - a));
		dst[b - a] = '\0';
	}
}

static int ieq_n(const char *a, const char *b, size_t n)
{
	size_t i;
	for (i = 0; i < n; ++i) {
		unsigned char ca = (unsigned char)a[i];
		unsigned char cb = (unsigned char)b[i];
		if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca - 'A' + 'a');
		if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb - 'A' + 'a');
		if (ca != cb)
			return 0;
	}
	return 1;
}

static const char *basename_of(const char *path)
{
	const char *s = path, *p;
	if (!path)
		return "";
	for (p = path; *p; ++p) {
		if (*p == '/' || *p == '\\')
			s = p + 1;
	}
	return s;
}

/* NEZplug / nsfe2m3u playlist times:
   1 field = seconds (possibly fractional),
   2 fields = M:SS[.frac],
   3 fields = H:MM:SS[.frac]  — NOT min:sec:ms (that made 0:02:09 → ~2s). */
int gc_parse_mmss(const char *s)
{
	double a = 0.0, b = 0.0, c = 0.0;
	int n;
	if (!s || !s[0])
		return 0;
	while (*s == ' ' || *s == '\t')
		s++;
	if (!strchr(s, ':')) {
		/* plain seconds, e.g. "12" or "2.5" / EXTINF */
		return (int)(atof(s) * 1000.0 + 0.5);
	}
	n = sscanf(s, "%lf:%lf:%lf", &a, &b, &c);
	if (n == 3)
		return (int)((a * 3600.0 + b * 60.0 + c) * 1000.0 + 0.5);
	if (n == 2)
		return (int)((a * 60.0 + b) * 1000.0 + 0.5);
	if (n == 1)
		return (int)(a * 1000.0 + 0.5);
	return 0;
}

static int path_ieq(const char *a, const char *b)
{
	while (*a && *b) {
		unsigned char ca = (unsigned char)*a++;
		unsigned char cb = (unsigned char)*b++;
		if (ca == '\\') ca = '/';
		if (cb == '\\') cb = '/';
		if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca - 'A' + 'a');
		if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb - 'A' + 'a');
		if (ca != cb)
			return 0;
	}
	return *a == 0 && *b == 0;
}

/* Zip paths in M3U: "songs/game.gbs" matches "game.gbs" or "pack/songs/game.gbs". */
static int name_matches(const char *entry_file, const char *music_name)
{
	const char *a, *b, *ea, *eb;
	size_t na, nb;
	if (!entry_file || !entry_file[0] || !music_name || !music_name[0])
		return 1;
	if (path_ieq(entry_file, music_name))
		return 1;
	a = basename_of(entry_file);
	b = basename_of(music_name);
	if (path_ieq(a, b))
		return 1;
	/* suffix: music_name ends with entry_file (or the reverse) */
	ea = entry_file;
	eb = music_name;
	na = strlen(ea);
	nb = strlen(eb);
	if (na > 1 && nb > na) {
		const char *p = eb + (nb - na);
		if ((p[-1] == '/' || p[-1] == '\\') && path_ieq(p, ea))
			return 1;
	}
	if (nb > 1 && na > nb) {
		const char *p = ea + (na - nb);
		if ((p[-1] == '/' || p[-1] == '\\') && path_ieq(p, eb))
			return 1;
	}
	return 0;
}

int gc_m3u_parse(const char *text, size_t len, const char *music_name, gc_info *out)
{
	size_t i = 0;
	int count = 0;
	int matched = 0;

	if (!text || !out)
		return 0;
	memset(out, 0, sizeof *out);
	out->start_track = 0;

	while (i < len) {
		size_t line_start = i, line_len;
		const char *line;
		char file[GC_MAX_PATH], type[16], title[GC_MAX_TITLE];
		int track = 1, duration = 0, loop_ms = 0, fade = 0;
		const char *p, *comma[8];
		int ncomma = 0;

		while (i < len && text[i] != '\n')
			i++;
		line_len = i - line_start;
		if (i < len)
			i++;
		while (line_len && (text[line_start + line_len - 1] == '\r' ||
		                    text[line_start + line_len - 1] == ' '))
			line_len--;
		if (line_len == 0)
			continue;
		line = text + line_start;
		if (line[0] == '#') {
			if (line_len > 8 && ieq_n(line, "#EXTINF:", 8)) {
				/* #EXTINF:seconds,title — applied to next file line */
				const char *c = memchr(line, ',', line_len);
				int secs = atoi(line + 8);
				if (c) {
					copy_trim(title, sizeof title, c + 1,
					          (size_t)(line + line_len - (c + 1)));
					if (out->tracks[count].title[0] == 0 && count < GC_MAX_TRACKS)
						memcpy(out->tracks[count].title, title, sizeof title);
				}
				if (secs > 0 && count < GC_MAX_TRACKS)
					out->tracks[count].duration_ms = secs * 1000;
			}
			continue;
		}

		/* file::TYPE,track,title,time[,loop,fade]  or  file,track,title,time */
		p = line;
		{
			size_t k;
			for (k = 0; k < line_len; ++k) {
				if (p[k] == ',' && ncomma < 7)
					comma[ncomma++] = p + k;
			}
		}

		file[0] = type[0] = title[0] = '\0';
		{
			const char *sep = NULL;
			size_t k;
			for (k = 0; k + 1 < line_len; ++k) {
				if (p[k] == ':' && p[k + 1] == ':') {
					sep = p + k;
					break;
				}
			}
			if (sep) {
				copy_trim(file, sizeof file, p, (size_t)(sep - p));
				{
					const char *t = sep + 2;
					const char *end = ncomma ? comma[0] : p + line_len;
					copy_trim(type, sizeof type, t, (size_t)(end - t));
				}
			} else if (ncomma) {
				copy_trim(file, sizeof file, p, (size_t)(comma[0] - p));
			} else {
				copy_trim(file, sizeof file, p, line_len);
			}
		}

		if (ncomma >= 1)
			track = atoi(comma[0] + 1);
		if (ncomma >= 2) {
			const char *ts = comma[1] + 1;
			const char *te = (ncomma >= 3) ? comma[2] : p + line_len;
			copy_trim(title, sizeof title, ts, (size_t)(te - ts));
		}
		if (ncomma >= 3)
			duration = gc_parse_mmss(comma[2] + 1);
		/* file::TYPE,track,title,time[,loop,fade] */
		if (ncomma >= 4) {
			const char *ls = comma[3] + 1;
			if (ls[0] && ls[0] != '-' && ls[0] != ' ')
				loop_ms = gc_parse_mmss(ls);
		}
		if (ncomma >= 5)
			fade = gc_parse_mmss(comma[4] + 1);
		if (gc_is_dummy_length_ms(duration))
			duration = 0;
		if (duration > 0 && fade > 0)
			duration += fade;

		if (music_name && music_name[0] && file[0] && !name_matches(file, music_name))
			continue;
		matched = 1;

		{
			int idx = (track > 0) ? (track - 1) : count;
			if (idx < 0)
				idx = 0;
			if (idx >= GC_MAX_TRACKS)
				continue;
			if (title[0])
				memcpy(out->tracks[idx].title, title, sizeof out->tracks[idx].title);
			if (duration > 0)
				out->tracks[idx].duration_ms = duration;
			if (loop_ms > 0)
				out->tracks[idx].loop_ms = loop_ms;
			if (fade > 0)
				out->tracks[idx].fade_ms = fade;
			if (idx + 1 > count)
				count = idx + 1;
			if (out->start_track == 0 && track > 0)
				out->start_track = track - 1;
			(void)type;
		}
	}

	if (!matched && count == 0)
		return 0;
	out->track_count = count > 0 ? count : 1;
	return 1;
}

int gc_m3u_parse_file(const char *path, const char *music_name, gc_info *out)
{
	FILE *f;
	char *buf;
	long sz;
	int ok;
	if (!path)
		return 0;
	f = fopen(path, "rb");
	if (!f)
		return 0;
	if (fseek(f, 0, SEEK_END) != 0) {
		fclose(f);
		return 0;
	}
	sz = ftell(f);
	if (sz <= 0 || sz > 2 * 1024 * 1024) {
		fclose(f);
		return 0;
	}
	rewind(f);
	buf = (char *)malloc((size_t)sz + 1);
	if (!buf) {
		fclose(f);
		return 0;
	}
	if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
		free(buf);
		fclose(f);
		return 0;
	}
	buf[sz] = '\0';
	fclose(f);
	ok = gc_m3u_parse(buf, (size_t)sz, music_name, out);
	free(buf);
	return ok;
}

void gc_info_apply_loops(gc_info *inf, int loop_count)
{
	int i;
	if (!inf || loop_count <= 1)
		return;
	for (i = 0; i < inf->track_count && i < GC_MAX_TRACKS; ++i) {
		int fade, play, llen, loop;
		if (inf->tracks[i].duration_ms <= 0 || inf->tracks[i].loop_ms <= 0)
			continue;
		fade = inf->tracks[i].fade_ms > 0 ? inf->tracks[i].fade_ms : 0;
		play = inf->tracks[i].duration_ms - fade;
		if (play <= 0)
			continue;
		loop = inf->tracks[i].loop_ms;
		llen = (loop < play) ? (play - loop) : loop;
		inf->tracks[i].duration_ms = play + llen * (loop_count - 1) + fade;
		if (inf->tracks[i].duration_ms > GC_CAP_MS)
			inf->tracks[i].duration_ms = GC_CAP_MS;
	}
}
