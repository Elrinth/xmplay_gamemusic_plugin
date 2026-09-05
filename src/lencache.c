#include "lencache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static uint32_t fnv1a(const char *s)
{
	uint32_t h = 2166136261u;
	if (!s)
		return h;
	while (*s) {
		h ^= (unsigned char)*s++;
		h *= 16777619u;
	}
	return h;
}

void gc_len_cache_path_from_ini(char *out, size_t cap, const char *ini_path)
{
	const char *slash;
	size_t n;
	if (!out || cap < 8)
		return;
	out[0] = '\0';
	if (!ini_path || !ini_path[0])
		return;
	slash = strrchr(ini_path, '/');
#ifdef _WIN32
	{
		const char *b = strrchr(ini_path, '\\');
		if (b && (!slash || b > slash))
			slash = b;
	}
#endif
	if (slash) {
		n = (size_t)(slash + 1 - ini_path);
		if (n >= cap)
			n = cap - 1;
		memcpy(out, ini_path, n);
		out[n] = '\0';
	}
	n = strlen(out);
	if (n + 28 >= cap)
		return;
	memcpy(out + n, "xmp-gamemusic-lengths.ini", 26);
}

int64_t gc_file_mtime(const char *path)
{
#ifdef _WIN32
	struct _stat st;
	if (!path || !path[0] || _stat(path, &st) != 0)
		return 0;
	return (int64_t)st.st_mtime;
#else
	struct stat st;
	if (!path || !path[0] || stat(path, &st) != 0)
		return 0;
	return (int64_t)st.st_mtime;
#endif
}

uint64_t gc_file_size(const char *path)
{
#ifdef _WIN32
	struct _stat st;
	if (!path || !path[0] || _stat(path, &st) != 0)
		return 0;
	return (uint64_t)st.st_size;
#else
	struct stat st;
	if (!path || !path[0] || stat(path, &st) != 0)
		return 0;
	return (uint64_t)st.st_size;
#endif
}

void gc_len_rec_from_info(gc_len_rec *out, const gc_info *inf)
{
	int i;
	if (!out)
		return;
	memset(out, 0, sizeof *out);
	if (!inf)
		return;
	out->format = inf->format;
	out->track_count = inf->track_count;
	if (out->track_count > GC_MAX_TRACKS)
		out->track_count = GC_MAX_TRACKS;
	for (i = 0; i < out->track_count; ++i)
		out->duration_ms[i] = inf->tracks[i].duration_ms;
	memcpy(out->game, inf->game, sizeof out->game);
	memcpy(out->artist, inf->artist, sizeof out->artist);
	memcpy(out->copyright, inf->copyright, sizeof out->copyright);
	memcpy(out->src, inf->length_src, sizeof out->src);
}

static int parse_ms_csv(char *csv, int *ms, int max_n)
{
	int n = 0;
	char *tok = csv;
	while (tok && n < max_n) {
		char *comma = strchr(tok, ',');
		if (comma)
			*comma = '\0';
		ms[n++] = atoi(tok);
		tok = comma ? comma + 1 : NULL;
	}
	return n;
}

int gc_len_cache_get(const char *cache_file, const char *path,
                     uint64_t size, int64_t mtime, gc_len_rec *out)
{
	FILE *f;
	char line[8192];
	uint32_t want;
	int found = 0;
	if (!cache_file || !cache_file[0] || !out || !path || !path[0])
		return 0;
	want = fnv1a(path);
	f = fopen(cache_file, "r");
	if (!f)
		return 0;
	memset(out, 0, sizeof *out);
	while (fgets(line, sizeof line, f)) {
		unsigned hex = 0;
		unsigned long long sz = 0;
		long long mt = 0;
		int fmt = 0, n = 0;
		char src[32], csv[4096], game[GC_MAX_TITLE], artist[GC_MAX_TITLE];
		src[0] = csv[0] = game[0] = artist[0] = '\0';
		if (line[0] == '#' || line[0] == '\n' || line[0] == '\r')
			continue;
		if (strncmp(line, "v2 ", 3) == 0) {
			if (sscanf(line + 3, "%x %llu %lld %d %d %31s %4095s",
			           &hex, &sz, &mt, &fmt, &n, src, csv) < 6)
				continue;
			if (hex != want || (uint64_t)sz != size || (int64_t)mt != mtime)
				continue;
			if (n < 1 || n > GC_MAX_TRACKS)
				continue;
			memset(out, 0, sizeof *out);
			out->format = (gc_format)fmt;
			out->track_count = n;
			parse_ms_csv(csv, out->duration_ms, n);
			{
				size_t i;
				for (i = 0; src[i] && i + 1 < sizeof out->src; ++i)
					out->src[i] = src[i];
				out->src[i] = '\0';
			}
			found = 1;
		} else if (strncmp(line, "t ", 2) == 0 && found) {
			unsigned hex2 = 0;
			char *rest;
			if (sscanf(line + 2, "%x", &hex2) != 1 || hex2 != want)
				continue;
			rest = strchr(line + 2, ' ');
			if (!rest)
				continue;
			rest++;
			{
				char *bar = strchr(rest, '|');
				char *bar2;
				if (bar)
					*bar = '\0';
				strncpy(out->game, rest, sizeof out->game - 1);
				if (bar) {
					bar2 = strchr(bar + 1, '|');
					if (bar2)
						*bar2 = '\0';
					strncpy(out->artist, bar + 1, sizeof out->artist - 1);
					if (bar2) {
						char *nl = strchr(bar2 + 1, '\n');
						if (nl)
							*nl = '\0';
						strncpy(out->copyright, bar2 + 1,
						        sizeof out->copyright - 1);
					}
				}
			}
		}
	}
	fclose(f);
	return found && out->track_count > 0;
}

int gc_len_cache_put(const char *cache_file, const char *path,
                     uint64_t size, int64_t mtime, const gc_len_rec *in)
{
	FILE *f;
	uint32_t hex;
	int i, n;
	if (!cache_file || !cache_file[0] || !path || !path[0] || !in)
		return 0;
	if (in->track_count < 1)
		return 0;
	hex = fnv1a(path);
	f = fopen(cache_file, "a");
	if (!f)
		return 0;
	n = in->track_count;
	if (n > GC_MAX_TRACKS)
		n = GC_MAX_TRACKS;
	fprintf(f, "v2 %08x %llu %lld %d %d %s ",
	        hex, (unsigned long long)size, (long long)mtime,
	        (int)in->format, n, in->src[0] ? in->src : "measured");
	for (i = 0; i < n; ++i)
		fprintf(f, "%s%d", i ? "," : "", in->duration_ms[i]);
	fprintf(f, "\n");
	fprintf(f, "t %08x %s|%s|%s\n", hex,
	        in->game[0] ? in->game : "",
	        in->artist[0] ? in->artist : "",
	        in->copyright[0] ? in->copyright : "");
	fclose(f);
	return 1;
}
