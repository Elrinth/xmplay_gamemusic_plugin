#include "probe.h"

#include <string.h>

static int ieq_ch(unsigned char a, unsigned char b)
{
	if (a >= 'A' && a <= 'Z') a = (unsigned char)(a - 'A' + 'a');
	if (b >= 'A' && b <= 'Z') b = (unsigned char)(b - 'A' + 'a');
	return a == b;
}

static int ext_is(const char *filename, const char *ext)
{
	const char *dot = NULL, *p;
	size_t i;
	if (!filename || !ext)
		return 0;
	for (p = filename; *p; ++p) {
		if (*p == '.' || *p == '/' || *p == '\\')
			dot = (*p == '.') ? p : NULL;
	}
	if (!dot)
		return 0;
	for (i = 0; ext[i] || dot[i]; ++i) {
		if (!ieq_ch((unsigned char)dot[i], (unsigned char)ext[i]))
			return 0;
	}
	return 1;
}

static unsigned le32(const unsigned char *p)
{
	return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}

int gc_is_sap(const unsigned char *data, size_t len, const char *filename)
{
	if (ext_is(filename, ".sap"))
		return 1;
	if (data && len >= 4 && data[0] == 'S' && data[1] == 'A' && data[2] == 'P' && data[3] == 0x0D)
		return 1;
	return 0;
}

int gc_is_psf_lib(const char *filename)
{
	return ext_is(filename, ".gsflib") || ext_is(filename, ".usflib");
}

gc_format gc_probe(const unsigned char *data, size_t len, const char *filename)
{
	if (gc_is_sap(data, len, filename))
		return GC_FMT_UNKNOWN;
	/* .gsflib / .usflib are PSF 0x22 / 0x21 but are libraries, not tunes. */
	if (gc_is_psf_lib(filename))
		return GC_FMT_UNKNOWN;

	if (data && len >= 4) {
		if (data[0] == 'N' && data[1] == 'E' && data[2] == 'S' && data[3] == 'M' &&
		    (len < 5 || data[4] == 0x1A))
			return GC_FMT_NSF;
		if (data[0] == 'N' && data[1] == 'S' && data[2] == 'F' && data[3] == 'E')
			return GC_FMT_NSFE;
		if (data[0] == 'G' && data[1] == 'B' && data[2] == 'S' && data[3] == 0x01)
			return GC_FMT_GBS;
		if (data[0] == 'G' && data[1] == 'B' && data[2] == 'R' && data[3] == 'F')
			return GC_FMT_GBR;
		if (data[0] == 'G' && data[1] == 'Y' && data[2] == 'M' && data[3] == 'X')
			return GC_FMT_GYM;
		if (data[0] == 'H' && data[1] == 'E' && data[2] == 'S' && data[3] == 'M')
			return GC_FMT_HES;
		if (len > 0x220 && data[0x200] == 'H' && data[0x201] == 'E' &&
		    data[0x202] == 'S' && data[0x203] == 'M')
			return GC_FMT_HES;
		if ((data[0] == 'K' && data[1] == 'S' && data[2] == 'C' && data[3] == 'C') ||
		    (data[0] == 'K' && data[1] == 'S' && data[2] == 'S' && data[3] == 'X'))
			return GC_FMT_KSS;
		if (data[0] == 'Z' && data[1] == 'X' && data[2] == 'A' && data[3] == 'Y')
			return GC_FMT_AY;
		if (data[0] == 'N' && data[1] == 'E' && data[2] == 'S' && data[3] == 'L' &&
		    (len < 5 || data[4] == 0x1A))
			return GC_FMT_NSD;
		if (data[0] == 'S' && data[1] == 'G' && data[2] == 'C')
			return GC_FMT_SGC;
		if (data[0] == 'S' && data[1] == 'N' && data[2] == 'E' && data[3] == 'S')
			return GC_FMT_SPC;
		if (data[0] == 'V' && data[1] == 'g' && data[2] == 'm' && data[3] == ' ')
			return GC_FMT_VGM;
		if (data[0] == 0x1F && data[1] == 0x8B)
			return ext_is(filename, ".vgz") ? GC_FMT_VGZ : GC_FMT_GZIP;
		if (data[0] == 'P' && data[1] == 'K' && (data[2] == 0x03 || data[2] == 0x05))
			return GC_FMT_ZIP;
		if (len >= 6 && data[0] == '7' && data[1] == 'z' && data[2] == 0xBC &&
		    data[3] == 0xAF && data[4] == 0x27 && data[5] == 0x1C)
			return GC_FMT_SEVENZ;
		if (len >= 7 && data[0] == 'R' && data[1] == 'a' && data[2] == 'r' && data[3] == '!')
			return GC_FMT_RSN;
		if (data[0] == 'P' && data[1] == 'S' && data[2] == 'F') {
			if (data[3] == 0x22)
				return ext_is(filename, ".minigsf") ? GC_FMT_MINIGSF : GC_FMT_GSF;
			if (data[3] == 0x21)
				return ext_is(filename, ".miniusf") ? GC_FMT_MINIUSF : GC_FMT_USF;
		}
		(void)le32;
	}

	if (ext_is(filename, ".nsf")) return GC_FMT_NSF;
	if (ext_is(filename, ".nsfe")) return GC_FMT_NSFE;
	if (ext_is(filename, ".gbs")) return GC_FMT_GBS;
	if (ext_is(filename, ".gbr")) return GC_FMT_GBR;
	if (ext_is(filename, ".gym")) return GC_FMT_GYM;
	if (ext_is(filename, ".hes")) return GC_FMT_HES;
	if (ext_is(filename, ".kss")) return GC_FMT_KSS;
	if (ext_is(filename, ".ay")) return GC_FMT_AY;
	if (ext_is(filename, ".cpc")) return GC_FMT_CPC;
	if (ext_is(filename, ".nez")) return GC_FMT_NEZ;
	if (ext_is(filename, ".nsz")) return GC_FMT_NSZ;
	if (ext_is(filename, ".nsd")) return GC_FMT_NSD;
	if (ext_is(filename, ".rsn")) return GC_FMT_RSN;
	if (ext_is(filename, ".sgc")) return GC_FMT_SGC;
	if (ext_is(filename, ".spc")) return GC_FMT_SPC;
	if (ext_is(filename, ".vgm")) return GC_FMT_VGM;
	if (ext_is(filename, ".vgz")) return GC_FMT_VGZ;
	if (ext_is(filename, ".gsf")) return GC_FMT_GSF;
	if (ext_is(filename, ".minigsf")) return GC_FMT_MINIGSF;
	if (ext_is(filename, ".usf")) return GC_FMT_USF;
	if (ext_is(filename, ".miniusf")) return GC_FMT_MINIUSF;
	if (ext_is(filename, ".zip")) return GC_FMT_ZIP;
	if (ext_is(filename, ".7z")) return GC_FMT_SEVENZ;
	if (ext_is(filename, ".gz")) return GC_FMT_GZIP;
	return GC_FMT_UNKNOWN;
}

int gc_format_claimed(gc_format fmt)
{
	switch (fmt) {
	case GC_FMT_AY:
	case GC_FMT_GBS:
	case GC_FMT_GBR:
	case GC_FMT_GYM:
	case GC_FMT_HES:
	case GC_FMT_KSS:
	case GC_FMT_NSF:
	case GC_FMT_NSFE:
	case GC_FMT_NEZ:
	case GC_FMT_NSZ:
	case GC_FMT_NSD:
	case GC_FMT_RSN:
	case GC_FMT_SGC:
	case GC_FMT_SPC:
	case GC_FMT_VGM:
	case GC_FMT_VGZ:
	case GC_FMT_CPC:
	case GC_FMT_GSF:
	case GC_FMT_MINIGSF:
	case GC_FMT_USF:
	case GC_FMT_MINIUSF:
	case GC_FMT_ZIP:
	case GC_FMT_SEVENZ:
	case GC_FMT_GZIP:
		return 1;
	default:
		return 0;
	}
}

const char *gc_format_name(gc_format fmt)
{
	switch (fmt) {
	case GC_FMT_AY: return "AY";
	case GC_FMT_GBS: return "GBS";
	case GC_FMT_GBR: return "GBR";
	case GC_FMT_GYM: return "GYM";
	case GC_FMT_HES: return "HES";
	case GC_FMT_KSS: return "KSS";
	case GC_FMT_NSF: return "NSF";
	case GC_FMT_NSFE: return "NSFE";
	case GC_FMT_NEZ: return "NEZ";
	case GC_FMT_NSZ: return "NSZ";
	case GC_FMT_NSD: return "NSD";
	case GC_FMT_RSN: return "RSN";
	case GC_FMT_SGC: return "SGC";
	case GC_FMT_SPC: return "SPC";
	case GC_FMT_VGM: return "VGM";
	case GC_FMT_VGZ: return "VGZ";
	case GC_FMT_CPC: return "CPC";
	case GC_FMT_GSF: return "GSF";
	case GC_FMT_MINIGSF: return "MINIGSF";
	case GC_FMT_USF: return "USF";
	case GC_FMT_MINIUSF: return "MINIUSF";
	case GC_FMT_ZIP: return "ZIP";
	case GC_FMT_SEVENZ: return "7Z";
	case GC_FMT_GZIP: return "GZ";
	default: return "UNK";
	}
}

const char *gc_format_ext(gc_format fmt)
{
	switch (fmt) {
	case GC_FMT_AY: return "ay";
	case GC_FMT_GBS: return "gbs";
	case GC_FMT_GBR: return "gbr";
	case GC_FMT_GYM: return "gym";
	case GC_FMT_HES: return "hes";
	case GC_FMT_KSS: return "kss";
	case GC_FMT_NSF: return "nsf";
	case GC_FMT_NSFE: return "nsfe";
	case GC_FMT_NEZ: return "nez";
	case GC_FMT_NSZ: return "nsz";
	case GC_FMT_NSD: return "nsd";
	case GC_FMT_RSN: return "rsn";
	case GC_FMT_SGC: return "sgc";
	case GC_FMT_SPC: return "spc";
	case GC_FMT_VGM: return "vgm";
	case GC_FMT_VGZ: return "vgz";
	case GC_FMT_CPC: return "cpc";
	case GC_FMT_GSF: return "gsf";
	case GC_FMT_MINIGSF: return "minigsf";
	case GC_FMT_USF: return "usf";
	case GC_FMT_MINIUSF: return "miniusf";
	default: return "";
	}
}
