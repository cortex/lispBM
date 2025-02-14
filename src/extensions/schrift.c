/* This file is part of libschrift.
 *
 * © 2019-2022 Thomas Oltmann and contributors
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE. */

/* Adapted to LBM by Joel Svensson in 2025 */

#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "schrift.h"

// LBM interoperation
#include <extensions/display_extensions.h>
#include <lbm_memory.h>

#define SCHRIFT_VERSION "0.10.2"

#define FILE_MAGIC_ONE             0x00010000
#define FILE_MAGIC_TWO             0x74727565

#define HORIZONTAL_KERNING         0x01
#define MINIMUM_KERNING            0x02
#define CROSS_STREAM_KERNING       0x04
#define OVERRIDE_KERNING           0x08

#define POINT_IS_ON_CURVE          0x01
#define X_CHANGE_IS_SMALL          0x02
#define Y_CHANGE_IS_SMALL          0x04
#define REPEAT_FLAG                0x08
#define X_CHANGE_IS_ZERO           0x10
#define X_CHANGE_IS_POSITIVE       0x10
#define Y_CHANGE_IS_ZERO           0x20
#define Y_CHANGE_IS_POSITIVE       0x20

#define OFFSETS_ARE_LARGE          0x001
#define ACTUAL_XY_OFFSETS          0x002
#define GOT_A_SINGLE_SCALE         0x008
#define THERE_ARE_MORE_COMPONENTS  0x020
#define GOT_AN_X_AND_Y_SCALE       0x040
#define GOT_A_SCALE_MATRIX         0x080

/* macros */
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define SIGN(x)   (((x) > 0) - ((x) < 0))
/* Allocate values on the stack if they are small enough, else spill to heap. */
#define STACK_ALLOC(var, type, thresh, count) \
	type var##_stack_[thresh]; \
	var = (count) <= (thresh) ? var##_stack_ : calloc(sizeof(type), count);
#define STACK_FREE(var) \
	if (var != var##_stack_) free(var);

/* structs */
typedef struct Point   Point;
typedef struct Line    Line;
typedef struct Curve   Curve;
typedef struct Cell    Cell;
typedef struct Outline Outline;
typedef struct Raster  Raster;

struct Point { double x, y; };
struct Line  { uint_least16_t beg, end; };
struct Curve { uint_least16_t beg, end, ctrl; };
struct Cell  { double area, cover; };

struct Outline
{
	Point *points;
	Curve *curves;
	Line  *lines;
	uint_least16_t numPoints;
	uint_least16_t capPoints;
	uint_least16_t numCurves;
	uint_least16_t capCurves;
	uint_least16_t numLines;
	uint_least16_t capLines;
};

struct Raster
{
	Cell *cells;
	int   width;
	int   height;
};

// ////////////////////////////////////////////////////////////
// Utils

// extract an utf32 value from an utf8 string starting at index ix.
bool get_utf32(uint8_t *utf8, uint32_t *utf32, uint32_t ix, uint32_t *next_ix) {
  uint8_t *u = &utf8[ix];
  uint32_t c = 0;

  if (u[0] == 0) return false;

  if (!(u[0] & 0x80U)) {
    *utf32 = u[0];
    *next_ix = ix + 1;
  } else if ((u[0] & 0xe0U) == 0xc0U) {
    c = (u[0] & 0x1fU) << 6;
    if ((u[1] & 0xc0U) != 0x80U) return false;
    *utf32 = c + (u[1] & 0x3fU);
    *next_ix = ix + 2;
  } else if ((u[0] & 0xf0U) == 0xe0U) {
    c = (u[0] & 0x0fU) << 12;
    if ((u[1] & 0xc0U) != 0x80U) return false;
    c += (u[1] & 0x3fU) << 6;
    if ((u[2] & 0xc0U) != 0x80U) return false;
    *utf32 = c + (u[2] & 0x3fU);
    *next_ix = ix + 3;
  } else if ((u[0] & 0xf8U) == 0xf0U) {
    c = (u[0] & 0x07U) << 18;
    if ((u[1] & 0xc0U) != 0x80U) return false;
    c += (u[1] & 0x3fU) << 12;
    if ((u[2] & 0xc0U) != 0x80U) return false;
    c += (u[2] & 0x3fU) << 6;
    if ((u[3] & 0xc0U) != 0x80U) return false;
    c += (u[3] & 0x3fU);
    if ((c & 0xFFFFF800U) == 0xD800U) return false;
    *utf32 = c;
    *next_ix = ix + 4;
  } else return false;
  return true;
}



/* function declarations */
/* generic utility functions */
static inline int fast_floor(double x);
static inline int fast_ceil (double x);
/* simple mathematical operations */
static Point midpoint(Point a, Point b);
static void transform_points(unsigned int numPts, Point *points, double trf[6]);
static void clip_points(unsigned int numPts, Point *points, int width, int height);
/* 'outline' data structure management */
static int  init_outline(Outline *outl);
static void free_outline(Outline *outl);
static int  grow_points (Outline *outl);
static int  grow_curves (Outline *outl);
static int  grow_lines  (Outline *outl);
/* TTF parsing utilities */
static inline int is_safe_offset(SFT_Font *font, uint_fast32_t offset, uint_fast32_t margin);
static void *csearch(const void *key, const void *base,
	size_t nmemb, size_t size, int (*compar)(const void *, const void *));
static int  cmpu16(const void *a, const void *b);
static int  cmpu32(const void *a, const void *b);
static inline uint_least8_t  getu8 (SFT_Font *font, uint_fast32_t offset);
static inline int_least8_t   geti8 (SFT_Font *font, uint_fast32_t offset);
static inline uint_least16_t getu16(SFT_Font *font, uint_fast32_t offset);
static inline int_least16_t  geti16(SFT_Font *font, uint_fast32_t offset);
static inline uint_least32_t getu32(SFT_Font *font, uint_fast32_t offset);
static int gettable(SFT_Font *font, char tag[4], uint_fast32_t *offset);
/* codepoint to glyph id translation */
static int  cmap_fmt4(SFT_Font *font, uint_fast32_t table, SFT_UChar charCode, uint_fast32_t *glyph);
static int  cmap_fmt6(SFT_Font *font, uint_fast32_t table, SFT_UChar charCode, uint_fast32_t *glyph);
static int  glyph_id(SFT_Font *font, SFT_UChar charCode, uint_fast32_t *glyph);
/* glyph metrics lookup */
static int  hor_metrics(SFT_Font *font, uint_fast32_t glyph, int *advanceWidth, int *leftSideBearing);
static int  glyph_bbox(const SFT *sft, uint_fast32_t outline, int box[4]);
/* decoding outlines */
static int  outline_offset(SFT_Font *font, uint_fast32_t glyph, uint_fast32_t *offset);
static int  simple_flags(SFT_Font *font, uint_fast32_t *offset, uint_fast16_t numPts, uint8_t *flags);
static int  simple_points(SFT_Font *font, uint_fast32_t offset, uint_fast16_t numPts, uint8_t *flags, Point *points);
static int  decode_contour(uint8_t *flags, uint_fast16_t basePoint, uint_fast16_t count, Outline *outl);
static int  simple_outline(SFT_Font *font, uint_fast32_t offset, unsigned int numContours, Outline *outl);
static int  compound_outline(SFT_Font *font, uint_fast32_t offset, int recDepth, Outline *outl);
static int  decode_outline(SFT_Font *font, uint_fast32_t offset, int recDepth, Outline *outl);
/* tesselation */
static int  is_flat(Outline *outl, Curve curve);
static int  tesselate_curve(Curve curve, Outline *outl);
static int  tesselate_curves(Outline *outl);
/* silhouette rasterization */
static void draw_line(Raster buf, Point origin, Point goal);
static void draw_lines(Outline *outl, Raster buf);
/* post-processing */
static void post_process(Raster buf, image_buffer_t *image);
/* glyph rendering */
static int  render_outline(Outline *outl, double transform[6], image_buffer_t * image);

/* function implementations */

const char *
sft_version(void)
{
	return SCHRIFT_VERSION;
}

int
sft_lmetrics(const SFT *sft, SFT_LMetrics *metrics)
{
	double factor;
	uint_fast32_t hhea;
	memset(metrics, 0, sizeof *metrics);
	if (gettable(sft->font, "hhea", &hhea) < 0)
		return -1;
	if (!is_safe_offset(sft->font, hhea, 36))
		return -1;
	factor = sft->yScale / sft->font->unitsPerEm;
	metrics->ascender  = geti16(sft->font, hhea + 4) * factor;
	metrics->descender = geti16(sft->font, hhea + 6) * factor;
	metrics->lineGap   = geti16(sft->font, hhea + 8) * factor;
	return 0;
}

int
sft_lookup(const SFT *sft, SFT_UChar codepoint, SFT_Glyph *glyph)
{
	return glyph_id(sft->font, codepoint, glyph);
}

int
sft_gmetrics(const SFT *sft, SFT_Glyph glyph, SFT_GMetrics *metrics)
{
	int adv, lsb;
	double xScale = sft->xScale / sft->font->unitsPerEm;
	uint_fast32_t outline;
	int bbox[4];

	memset(metrics, 0, sizeof *metrics);

	if (hor_metrics(sft->font, glyph, &adv, &lsb) < 0)
		return -1;
	metrics->advanceWidth    = adv * xScale;
	metrics->leftSideBearing = lsb * xScale + sft->xOffset;

	if (outline_offset(sft->font, glyph, &outline) < 0)
		return -1;
	if (!outline)
		return 0;
	if (glyph_bbox(sft, outline, bbox) < 0)
		return -1;
	metrics->minWidth  = bbox[2] - bbox[0] + 1;
	metrics->minHeight = bbox[3] - bbox[1] + 1;
	metrics->yOffset   = sft->flags & SFT_DOWNWARD_Y ? -bbox[3] : bbox[1];

	return 0;
}

int
sft_kerning(const SFT *sft, SFT_Glyph leftGlyph, SFT_Glyph rightGlyph,
            SFT_Kerning *kerning)
{
	void *match;
	uint_fast32_t offset;
	unsigned int numTables, numPairs, length, format, flags;
	int value;
	uint8_t key[4];

	memset(kerning, 0, sizeof *kerning);

	if (gettable(sft->font, "kern", &offset) < 0)
		return 0;

	/* Read kern table header. */
	if (!is_safe_offset(sft->font, offset, 4))
		return -1;
	if (getu16(sft->font, offset) != 0)
		return 0;
	numTables = getu16(sft->font, offset + 2);
	offset += 4;

	while (numTables > 0) {
		/* Read subtable header. */
		if (!is_safe_offset(sft->font, offset, 6))
			return -1;
		length = getu16(sft->font, offset + 2);
		format = getu8 (sft->font, offset + 4);
		flags  = getu8 (sft->font, offset + 5);
		offset += 6;

		if (format == 0 && (flags & HORIZONTAL_KERNING) && !(flags & MINIMUM_KERNING)) {
			/* Read format 0 header. */
			if (!is_safe_offset(sft->font, offset, 8))
				return -1;
			numPairs = getu16(sft->font, offset);
			offset += 8;
			/* Look up character code pair via binary search. */
			key[0] = (leftGlyph  >> 8) & 0xFF;
			key[1] =  leftGlyph  & 0xFF;
			key[2] = (rightGlyph >> 8) & 0xFF;
			key[3] =  rightGlyph & 0xFF;
			if ((match = bsearch(key, sft->font->memory + offset,
				numPairs, 6, cmpu32)) != NULL) {
				
				value = geti16(sft->font, (uint_fast32_t) ((uint8_t *) match - sft->font->memory + 4));
				if (flags & CROSS_STREAM_KERNING) {
					kerning->yShift += value;
				} else {
					kerning->xShift += value;
				}
			}

		}

		offset += length;
		--numTables;
	}

	kerning->xShift = kerning->xShift / sft->font->unitsPerEm * sft->xScale;
	kerning->yShift = kerning->yShift / sft->font->unitsPerEm * sft->yScale;

	return 0;
}

int
sft_render(const SFT *sft, SFT_Glyph glyph, image_buffer_t * image)
{
	uint_fast32_t outline;
	double transform[6];
	int bbox[4];
	Outline outl;

	if (outline_offset(sft->font, glyph, &outline) < 0)
		return -1;
	if (!outline)
		return 0;
	if (glyph_bbox(sft, outline, bbox) < 0)
		return -1;
	/* Set up the transformation matrix such that
	 * the transformed bounding boxes min corner lines
	 * up with the (0, 0) point. */
	transform[0] = sft->xScale / sft->font->unitsPerEm;
	transform[1] = 0.0;
	transform[2] = 0.0;
	transform[4] = sft->xOffset - bbox[0];
	if (sft->flags & SFT_DOWNWARD_Y) {
		transform[3] = -sft->yScale / sft->font->unitsPerEm;
		transform[5] = bbox[3] - sft->yOffset;
	} else {
		transform[3] = +sft->yScale / sft->font->unitsPerEm;
		transform[5] = sft->yOffset - bbox[1];
	}
	
	memset(&outl, 0, sizeof outl);
	if (init_outline(&outl) < 0)
		goto failure;

	if (decode_outline(sft->font, outline, 0, &outl) < 0)
		goto failure;
	if (render_outline(&outl, transform, image) < 0)
		goto failure;

	free_outline(&outl);
	return 0;

failure:
	free_outline(&outl);
	return -1;
}

/* TODO maybe we should use long here instead of int. */
static inline int
fast_floor(double x)
{
	int i = (int) x;
	return i - (i > x);
}

static inline int
fast_ceil(double x)
{
	int i = (int) x;
	return i + (i < x);
}

int
init_font(SFT_Font *font)
{
	uint_fast32_t scalerType, head, hhea;

	if (!is_safe_offset(font, 0, 12))
		return -1;
	/* Check for a compatible scalerType (magic number). */
	scalerType = getu32(font, 0);
	if (scalerType != FILE_MAGIC_ONE && scalerType != FILE_MAGIC_TWO)
		return -1;

	if (gettable(font, "head", &head) < 0)
		return -1;
	if (!is_safe_offset(font, head, 54))
		return -1;
	font->unitsPerEm = getu16(font, head + 18);
	font->locaFormat = geti16(font, head + 50);
	
	if (gettable(font, "hhea", &hhea) < 0)
		return -1;
	if (!is_safe_offset(font, hhea, 36))
		return -1;
	font->numLongHmtx = getu16(font, hhea + 34);

	return 0;
}

static Point
midpoint(Point a, Point b)
{
	return (Point) {
		0.5 * (a.x + b.x),
		0.5 * (a.y + b.y)
	};
}

/* Applies an affine linear transformation matrix to a set of points. */
static void
transform_points(unsigned int numPts, Point *points, double trf[6])
{
	Point pt;
	unsigned int i;
	for (i = 0; i < numPts; ++i) {
		pt = points[i];
		points[i] = (Point) {
			pt.x * trf[0] + pt.y * trf[2] + trf[4],
			pt.x * trf[1] + pt.y * trf[3] + trf[5]
		};
	}
}

static void
clip_points(unsigned int numPts, Point *points, int width, int height)
{
	Point pt;
	unsigned int i;

	for (i = 0; i < numPts; ++i) {
		pt = points[i];

		if (pt.x < 0.0) {
			points[i].x = 0.0;
		}
		if (pt.x >= width) {
			points[i].x = nextafter(width, 0.0);
		}
		if (pt.y < 0.0) {
			points[i].y = 0.0;
		}
		if (pt.y >= height) {
			points[i].y = nextafter(height, 0.0);
		}
	}
}

static int
init_outline(Outline *outl)
{
	/* TODO Smaller initial allocations */
	outl->numPoints = 0;
	outl->capPoints = 64;
	if (!(outl->points = lbm_malloc(outl->capPoints * sizeof *outl->points)))
		return -1;
	outl->numCurves = 0;
	outl->capCurves = 64;
	if (!(outl->curves = lbm_malloc(outl->capCurves * sizeof *outl->curves)))
		return -1;
	outl->numLines = 0;
	outl->capLines = 64;
	if (!(outl->lines = lbm_malloc(outl->capLines * sizeof *outl->lines)))
		return -1;
	return 0;
}

static void
free_outline(Outline *outl)
{
	lbm_free(outl->points);
	lbm_free(outl->curves);
	lbm_free(outl->lines);
}

static int
grow_points(Outline *outl)
{
	uint_fast16_t cap;
	assert(outl->capPoints);
	/* Since we use uint_fast16_t for capacities, we have to be extra careful not to trigger integer overflow. */
	if (outl->capPoints > UINT16_MAX / 2)
		return -1;
	cap = (uint_fast16_t) (2U * outl->capPoints);
        Point *ps;
        if (!(ps = (Point*)lbm_malloc(cap * sizeof(Point))))
          return -1;
        memset(ps,0, sizeof(Point) * cap);
        memcpy(ps,outl->points, sizeof(Point) * outl->capPoints);
        lbm_free(outl->points);
        outl->points = ps;
	outl->capPoints = (uint_least16_t) cap;
	return 0;
}

static int
grow_curves(Outline *outl)
{
	uint_fast16_t cap;
	assert(outl->capCurves);
	if (outl->capCurves > UINT16_MAX / 2)
		return -1;
	cap = (uint_fast16_t) (2U * outl->capCurves);
        Curve *cs;
        if (!(cs = (Curve*)lbm_malloc(cap * sizeof(Curve))))
          return -1;
        memset(cs, 0, sizeof(Curve) * cap);
        memcpy(cs,outl->curves, sizeof(Curve) * outl->capCurves);
        lbm_free(outl->curves);
        outl->curves = cs;
	outl->capCurves = (uint_least16_t) cap;
	return 0;
}

static int
grow_lines(Outline *outl)
{
	uint_fast16_t cap;
	assert(outl->capLines);
	if (outl->capLines > UINT16_MAX / 2)
		return -1;
	cap = (uint_fast16_t) (2U * outl->capLines);
        Line *ls;
        if (!(ls = lbm_malloc(cap * sizeof(Line))))
          return -1;
        memset(ls, 0, sizeof(Line) * cap);
        memcpy(ls, outl->lines, sizeof(Line) * outl->capLines);
        lbm_free(outl->lines);
        outl->lines = ls;
	outl->capLines = (uint_least16_t) cap;
	return 0;
}

static inline int
is_safe_offset(SFT_Font *font, uint_fast32_t offset, uint_fast32_t margin)
{
	if (offset > font->size) return 0;
	if (font->size - offset < margin) return 0;
	return 1;
}

/* Like bsearch(), but returns the next highest element if key could not be found. */
static void *
csearch(const void *key, const void *base,
	size_t nmemb, size_t size,
	int (*compar)(const void *, const void *))
{
	const uint8_t *bytes = base, *sample;
	size_t low = 0, high = nmemb - 1, mid;
	if (!nmemb) return NULL;
	while (low != high) {
		mid = low + (high - low) / 2;
		sample = bytes + mid * size;
		if (compar(key, sample) > 0) {
			low = mid + 1;
		} else {
			high = mid;
		}
	}
	return (uint8_t *) bytes + low * size;
}

/* Used as a comparison function for [bc]search(). */
static int
cmpu16(const void *a, const void *b)
{
	return memcmp(a, b, 2);
}

/* Used as a comparison function for [bc]search(). */
static int
cmpu32(const void *a, const void *b)
{
	return memcmp(a, b, 4);
}

static inline uint_least8_t
getu8(SFT_Font *font, uint_fast32_t offset)
{
	assert(offset + 1 <= font->size);
	return *(font->memory + offset);
}

static inline int_least8_t
geti8(SFT_Font *font, uint_fast32_t offset)
{
	return (int_least8_t) getu8(font, offset);
}

static inline uint_least16_t
getu16(SFT_Font *font, uint_fast32_t offset)
{
	assert(offset + 2 <= font->size);
	const uint8_t *base = font->memory + offset;
	uint_least16_t b1 = base[0], b0 = base[1]; 
	return (uint_least16_t) (b1 << 8 | b0);
}

static inline int16_t
geti16(SFT_Font *font, uint_fast32_t offset)
{
	return (int_least16_t) getu16(font, offset);
}

static inline uint32_t
getu32(SFT_Font *font, uint_fast32_t offset)
{
	assert(offset + 4 <= font->size);
	const uint8_t *base = font->memory + offset;
	uint_least32_t b3 = base[0], b2 = base[1], b1 = base[2], b0 = base[3]; 
	return (uint_least32_t) (b3 << 24 | b2 << 16 | b1 << 8 | b0);
}

static int
gettable(SFT_Font *font, char tag[4], uint_fast32_t *offset)
{
	void *match;
	unsigned int numTables;
	/* No need to bounds-check access to the first 12 bytes - this gets already checked by init_font(). */
	numTables = getu16(font, 4);
	if (!is_safe_offset(font, 12, (uint_fast32_t) numTables * 16))
		return -1;
	if (!(match = bsearch(tag, font->memory + 12, numTables, 16, cmpu32)))
		return -1;
	*offset = getu32(font, (uint_fast32_t) ((uint8_t *) match - font->memory + 8));
	return 0;
}

static int
cmap_fmt4(SFT_Font *font, uint_fast32_t table, SFT_UChar charCode, SFT_Glyph *glyph)
{
	const uint8_t *segPtr;
	uint_fast32_t segIdxX2;
	uint_fast32_t endCodes, startCodes, idDeltas, idRangeOffsets, idOffset;
	uint_fast16_t segCountX2, idRangeOffset, startCode, shortCode, idDelta, id;
	uint8_t key[2] = { (uint8_t) (charCode >> 8), (uint8_t) charCode };
	/* cmap format 4 only supports the Unicode BMP. */
	if (charCode > 0xFFFF) {
		*glyph = 0;
		return 0;
	}
	shortCode = (uint_fast16_t) charCode;
	if (!is_safe_offset(font, table, 8))
		return -1;
	segCountX2 = getu16(font, table);
	if ((segCountX2 & 1) || !segCountX2)
		return -1;
	/* Find starting positions of the relevant arrays. */
	endCodes       = table + 8;
	startCodes     = endCodes + segCountX2 + 2;
	idDeltas       = startCodes + segCountX2;
	idRangeOffsets = idDeltas + segCountX2;
	if (!is_safe_offset(font, idRangeOffsets, segCountX2))
		return -1;
	/* Find the segment that contains shortCode by binary searching over
	 * the highest codes in the segments. */
	segPtr = csearch(key, font->memory + endCodes, segCountX2 / 2, 2, cmpu16);
	segIdxX2 = (uint_fast32_t) (segPtr - (font->memory + endCodes));
	/* Look up segment info from the arrays & short circuit if the spec requires. */
	if ((startCode = getu16(font, startCodes + segIdxX2)) > shortCode)
		return 0;
	idDelta = getu16(font, idDeltas + segIdxX2);
	if (!(idRangeOffset = getu16(font, idRangeOffsets + segIdxX2))) {
		/* Intentional integer under- and overflow. */
		*glyph = (shortCode + idDelta) & 0xFFFF;
		return 0;
	}
	/* Calculate offset into glyph array and determine ultimate value. */
	idOffset = idRangeOffsets + segIdxX2 + idRangeOffset + 2U * (unsigned int) (shortCode - startCode);
	if (!is_safe_offset(font, idOffset, 2))
		return -1;
	id = getu16(font, idOffset);
	/* Intentional integer under- and overflow. */
	*glyph = id ? (id + idDelta) & 0xFFFF : 0;
	return 0;
}

static int
cmap_fmt6(SFT_Font *font, uint_fast32_t table, SFT_UChar charCode, SFT_Glyph *glyph)
{
	unsigned int firstCode, entryCount;
	/* cmap format 6 only supports the Unicode BMP. */
	if (charCode > 0xFFFF) {
		*glyph = 0;
		return 0;
	}
	if (!is_safe_offset(font, table, 4))
		return -1;
	firstCode  = getu16(font, table);
	entryCount = getu16(font, table + 2);
	if (!is_safe_offset(font, table, 4 + 2 * entryCount))
		return -1;
	if (charCode < firstCode)
		return -1;
	charCode -= firstCode;
	if (!(charCode < entryCount))
		return -1;
	*glyph = getu16(font, table + 4 + 2 * charCode);
	return 0;
}

static int
cmap_fmt12_13(SFT_Font *font, uint_fast32_t table, SFT_UChar charCode, SFT_Glyph *glyph, int which)
{
	uint32_t len, numEntries;
	uint_fast32_t i;

	*glyph = 0;

    /* check that the entire header is present */
	if (!is_safe_offset(font, table, 16))
		return -1;

	len = getu32(font, table + 4);

	/* A minimal header is 16 bytes */
	if (len < 16)
		return -1;

	if (!is_safe_offset(font, table, len))
		return -1;

	numEntries = getu32(font, table + 12);

	for (i = 0; i < numEntries; ++i) {
		uint32_t firstCode, lastCode, glyphOffset;
		firstCode = getu32(font, table + (i * 12) + 16);
		lastCode = getu32(font, table + (i * 12) + 16 + 4);
		if (charCode < firstCode || charCode > lastCode)
			continue;
		glyphOffset = getu32(font, table + (i * 12) + 16 + 8);
		if (which == 12)
			*glyph = (charCode-firstCode) + glyphOffset;
		else
			*glyph = glyphOffset;
		return 0;
	}

	return 0;
}

/* Maps Unicode code points to glyph indices. */
static int
glyph_id(SFT_Font *font, SFT_UChar charCode, SFT_Glyph *glyph)
{
	uint_fast32_t cmap, entry, table;
	unsigned int idx, numEntries;
	int type, format;
	
	*glyph = 0;

	if (gettable(font, "cmap", &cmap) < 0)
		return -1;

	if (!is_safe_offset(font, cmap, 4))
		return -1;
	numEntries = getu16(font, cmap + 2);

	if (!is_safe_offset(font, cmap, 4 + numEntries * 8))
		return -1;

	/* First look for a 'full repertoire'/non-BMP map. */
	for (idx = 0; idx < numEntries; ++idx) {
		entry = cmap + 4 + idx * 8;
		type = getu16(font, entry) * 0100 + getu16(font, entry + 2);
		/* Complete unicode map */
		if (type == 0004 || type == 0312) {
			table = cmap + getu32(font, entry + 4);
			if (!is_safe_offset(font, table, 8))
				return -1;
			/* Dispatch based on cmap format. */
			format = getu16(font, table);
			switch (format) {
			case 12:
				return cmap_fmt12_13(font, table, charCode, glyph, 12);
			default:
				return -1;
			}
		}
	}

	/* If no 'full repertoire' cmap was found, try looking for a BMP map. */
	for (idx = 0; idx < numEntries; ++idx) {
		entry = cmap + 4 + idx * 8;
		type = getu16(font, entry) * 0100 + getu16(font, entry + 2);
		/* Unicode BMP */
		if (type == 0003 || type == 0301) {
			table = cmap + getu32(font, entry + 4);
			if (!is_safe_offset(font, table, 6))
				return -1;
			/* Dispatch based on cmap format. */
			switch (getu16(font, table)) {
			case 4:
				return cmap_fmt4(font, table + 6, charCode, glyph);
			case 6:
				return cmap_fmt6(font, table + 6, charCode, glyph);
			default:
				return -1;
			}
		}
	}

	return -1;
}

static int
hor_metrics(SFT_Font *font, SFT_Glyph glyph, int *advanceWidth, int *leftSideBearing)
{
	uint_fast32_t hmtx, offset, boundary;
	if (gettable(font, "hmtx", &hmtx) < 0)
		return -1;
	if (glyph < font->numLongHmtx) {
		/* glyph is inside long metrics segment. */
		offset = hmtx + 4 * glyph;
		if (!is_safe_offset(font, offset, 4))
			return -1;
		*advanceWidth = getu16(font, offset);
		*leftSideBearing = geti16(font, offset + 2);
		return 0;
	} else {
		/* glyph is inside short metrics segment. */
		boundary = hmtx + 4U * (uint_fast32_t) font->numLongHmtx;
		if (boundary < 4)
			return -1;
		
		offset = boundary - 4;
		if (!is_safe_offset(font, offset, 4))
			return -1;
		*advanceWidth = getu16(font, offset);
		
		offset = boundary + 2 * (glyph - font->numLongHmtx);
		if (!is_safe_offset(font, offset, 2))
			return -1;
		*leftSideBearing = geti16(font, offset);
		return 0;
	}
}

static int
glyph_bbox(const SFT *sft, uint_fast32_t outline, int box[4])
{
	double xScale, yScale;
	/* Read the bounding box from the font file verbatim. */
	if (!is_safe_offset(sft->font, outline, 10))
		return -1;
	box[0] = geti16(sft->font, outline + 2);
	box[1] = geti16(sft->font, outline + 4);
	box[2] = geti16(sft->font, outline + 6);
	box[3] = geti16(sft->font, outline + 8);
	if (box[2] <= box[0] || box[3] <= box[1])
		return -1;
	/* Transform the bounding box into SFT coordinate space. */
	xScale = sft->xScale / sft->font->unitsPerEm;
	yScale = sft->yScale / sft->font->unitsPerEm;
	box[0] = (int) floor(box[0] * xScale + sft->xOffset);
	box[1] = (int) floor(box[1] * yScale + sft->yOffset);
	box[2] = (int) ceil (box[2] * xScale + sft->xOffset);
	box[3] = (int) ceil (box[3] * yScale + sft->yOffset);
	return 0;
}

/* Returns the offset into the font that the glyph's outline is stored at. */
static int
outline_offset(SFT_Font *font, SFT_Glyph glyph, uint_fast32_t *offset)
{
	uint_fast32_t loca, glyf;
	uint_fast32_t base, this, next;

	if (gettable(font, "loca", &loca) < 0)
		return -1;
	if (gettable(font, "glyf", &glyf) < 0)
		return -1;

	if (font->locaFormat == 0) {
		base = loca + 2 * glyph;

		if (!is_safe_offset(font, base, 4))
			return -1;
		
		this = 2U * (uint_fast32_t) getu16(font, base);
		next = 2U * (uint_fast32_t) getu16(font, base + 2);
	} else {
		base = loca + 4 * glyph;

		if (!is_safe_offset(font, base, 8))
			return -1;

		this = getu32(font, base);
		next = getu32(font, base + 4);
	}

	*offset = this == next ? 0 : glyf + this;
	return 0;
}

/* For a 'simple' outline, determines each point of the outline with a set of flags. */
static int
simple_flags(SFT_Font *font, uint_fast32_t *offset, uint_fast16_t numPts, uint8_t *flags)
{
	uint_fast32_t off = *offset;
	uint_fast16_t i;
	uint8_t value = 0, repeat = 0;
	for (i = 0; i < numPts; ++i) {
		if (repeat) {
			--repeat;
		} else {
			if (!is_safe_offset(font, off, 1))
				return -1;
			value = getu8(font, off++);
			if (value & REPEAT_FLAG) {
				if (!is_safe_offset(font, off, 1))
					return -1;
				repeat = getu8(font, off++);
			}
		}
		flags[i] = value;
	}
	*offset = off;
	return 0;
}

/* For a 'simple' outline, decodes both X and Y coordinates for each point of the outline. */
static int
simple_points(SFT_Font *font, uint_fast32_t offset, uint_fast16_t numPts, uint8_t *flags, Point *points)
{
	long accum, value, bit;
	uint_fast16_t i;

	accum = 0L;
	for (i = 0; i < numPts; ++i) {
		if (flags[i] & X_CHANGE_IS_SMALL) {
			if (!is_safe_offset(font, offset, 1))
				return -1;
			value = (long) getu8(font, offset++);
			bit = !!(flags[i] & X_CHANGE_IS_POSITIVE);
			accum -= (value ^ -bit) + bit;
		} else if (!(flags[i] & X_CHANGE_IS_ZERO)) {
			if (!is_safe_offset(font, offset, 2))
				return -1;
			accum += geti16(font, offset);
			offset += 2;
		}
		points[i].x = (double) accum;
	}

	accum = 0L;
	for (i = 0; i < numPts; ++i) {
		if (flags[i] & Y_CHANGE_IS_SMALL) {
			if (!is_safe_offset(font, offset, 1))
				return -1;
			value = (long) getu8(font, offset++);
			bit = !!(flags[i] & Y_CHANGE_IS_POSITIVE);
			accum -= (value ^ -bit) + bit;
		} else if (!(flags[i] & Y_CHANGE_IS_ZERO)) {
			if (!is_safe_offset(font, offset, 2))
				return -1;
			accum += geti16(font, offset);
			offset += 2;
		}
		points[i].y = (double) accum;
	}

	return 0;
}

static int
decode_contour(uint8_t *flags, uint_fast16_t basePoint, uint_fast16_t count, Outline *outl)
{
	uint_fast16_t i;
	uint_least16_t looseEnd, beg, ctrl, center, cur;
	unsigned int gotCtrl;

	/* Skip contours with less than two points, since the following algorithm can't handle them and
	 * they should appear invisible either way (because they don't have any area). */
	if (count < 2) return 0;

	assert(basePoint <= UINT16_MAX - count);

	if (flags[0] & POINT_IS_ON_CURVE) {
		looseEnd = (uint_least16_t) basePoint++;
		++flags;
		--count;
	} else if (flags[count - 1] & POINT_IS_ON_CURVE) {
		looseEnd = (uint_least16_t) (basePoint + --count);
	} else {
		if (outl->numPoints >= outl->capPoints && grow_points(outl) < 0)
			return -1;

		looseEnd = outl->numPoints;
		outl->points[outl->numPoints++] = midpoint(
			outl->points[basePoint],
			outl->points[basePoint + count - 1]);
	}
	beg = looseEnd;
	gotCtrl = 0;
	for (i = 0; i < count; ++i) {
		/* cur can't overflow because we ensure that basePoint + count < 0xFFFF before calling decode_contour(). */
		cur = (uint_least16_t) (basePoint + i);
		/* NOTE clang-analyzer will often flag this and another piece of code because it thinks that flags and
		 * outl->points + basePoint don't always get properly initialized -- even when you explicitly loop over both
		 * and set every element to zero (but not when you use memset). This is a known clang-analyzer bug:
		 * http://clang-developers.42468.n3.nabble.com/StaticAnalyzer-False-positive-with-loop-handling-td4053875.html */
		if (flags[i] & POINT_IS_ON_CURVE) {
			if (gotCtrl) {
				if (outl->numCurves >= outl->capCurves && grow_curves(outl) < 0)
					return -1;
				outl->curves[outl->numCurves++] = (Curve) { beg, cur, ctrl };
			} else {
				if (outl->numLines >= outl->capLines && grow_lines(outl) < 0)
					return -1;
				outl->lines[outl->numLines++] = (Line) { beg, cur };
			}
			beg = cur;
			gotCtrl = 0;
		} else {
			if (gotCtrl) {
				center = outl->numPoints;
				if (outl->numPoints >= outl->capPoints && grow_points(outl) < 0)
					return -1;
				outl->points[center] = midpoint(outl->points[ctrl], outl->points[cur]);
				++outl->numPoints;

				if (outl->numCurves >= outl->capCurves && grow_curves(outl) < 0)
					return -1;
				outl->curves[outl->numCurves++] = (Curve) { beg, center, ctrl };

				beg = center;
			}
			ctrl = cur;
			gotCtrl = 1;
		}
	}
	if (gotCtrl) {
		if (outl->numCurves >= outl->capCurves && grow_curves(outl) < 0)
			return -1;
		outl->curves[outl->numCurves++] = (Curve) { beg, looseEnd, ctrl };
	} else {
		if (outl->numLines >= outl->capLines && grow_lines(outl) < 0)
			return -1;
		outl->lines[outl->numLines++] = (Line) { beg, looseEnd };
	}

	return 0;
}

static int
simple_outline(SFT_Font *font, uint_fast32_t offset, unsigned int numContours, Outline *outl)
{
	uint_fast16_t *endPts = NULL;
	uint8_t *flags = NULL;
	uint_fast16_t numPts;
	unsigned int i;

	assert(numContours > 0);

	uint_fast16_t basePoint = outl->numPoints;

	if (!is_safe_offset(font, offset, numContours * 2 + 2))
		goto failure;
	numPts = getu16(font, offset + (numContours - 1) * 2);
	if (numPts >= UINT16_MAX)
		goto failure;
	numPts++;
	if (outl->numPoints > UINT16_MAX - numPts)
		goto failure;

	while (outl->capPoints < basePoint + numPts) {
		if (grow_points(outl) < 0)
			goto failure;
	}

        endPts = lbm_malloc(numContours * sizeof(uint_fast16_t));
        memset(endPts,0,numContours * sizeof(uint_fast16_t));
	if (endPts == NULL)
		goto failure;
        flags = lbm_malloc(numPts);
        memset(flags, 0, numPts);
	if (flags == NULL)
		goto failure;

	for (i = 0; i < numContours; ++i) {
		endPts[i] = getu16(font, offset);
		offset += 2;
	}
	/* Ensure that endPts are never falling.
	 * Falling endPts have no sensible interpretation and most likely only occur in malicious input.
	 * Therefore, we bail, should we ever encounter such input. */
	for (i = 0; i < numContours - 1; ++i) {
		if (endPts[i + 1] < endPts[i] + 1)
			goto failure;
	}
	offset += 2U + getu16(font, offset);

	if (simple_flags(font, &offset, numPts, flags) < 0)
		goto failure;
	if (simple_points(font, offset, numPts, flags, outl->points + basePoint) < 0)
		goto failure;
	outl->numPoints = (uint_least16_t) (outl->numPoints + numPts);

	uint_fast16_t beg = 0;
	for (i = 0; i < numContours; ++i) {
		uint_fast16_t count = endPts[i] - beg + 1;
		if (decode_contour(flags + beg, basePoint + beg, count, outl) < 0)
			goto failure;
		beg = endPts[i] + 1;
	}

	lbm_free(endPts);
	lbm_free(flags);
	return 0;
failure:
	lbm_free(endPts);
	lbm_free(flags);
	return -1;
}

static int
compound_outline(SFT_Font *font, uint_fast32_t offset, int recDepth, Outline *outl)
{
	double local[6];
	uint_fast32_t outline;
	unsigned int flags, glyph, basePoint;
	/* Guard against infinite recursion (compound glyphs that have themselves as component). */
	if (recDepth >= 4)
		return -1;
	do {
		memset(local, 0, sizeof local);
		if (!is_safe_offset(font, offset, 4))
			return -1;
		flags = getu16(font, offset);
		glyph = getu16(font, offset + 2);
		offset += 4;
		/* We don't implement point matching, and neither does stb_truetype for that matter. */
		if (!(flags & ACTUAL_XY_OFFSETS))
			return -1;
		/* Read additional X and Y offsets (in FUnits) of this component. */
		if (flags & OFFSETS_ARE_LARGE) {
			if (!is_safe_offset(font, offset, 4))
				return -1;
			local[4] = geti16(font, offset);
			local[5] = geti16(font, offset + 2);
			offset += 4;
		} else {
			if (!is_safe_offset(font, offset, 2))
				return -1;
			local[4] = geti8(font, offset);
			local[5] = geti8(font, offset + 1);
			offset += 2;
		}
		if (flags & GOT_A_SINGLE_SCALE) {
			if (!is_safe_offset(font, offset, 2))
				return -1;
			local[0] = geti16(font, offset) / 16384.0;
			local[3] = local[0];
			offset += 2;
		} else if (flags & GOT_AN_X_AND_Y_SCALE) {
			if (!is_safe_offset(font, offset, 4))
				return -1;
			local[0] = geti16(font, offset + 0) / 16384.0;
			local[3] = geti16(font, offset + 2) / 16384.0;
			offset += 4;
		} else if (flags & GOT_A_SCALE_MATRIX) {
			if (!is_safe_offset(font, offset, 8))
				return -1;
			local[0] = geti16(font, offset + 0) / 16384.0;
			local[1] = geti16(font, offset + 2) / 16384.0;
			local[2] = geti16(font, offset + 4) / 16384.0;
			local[3] = geti16(font, offset + 6) / 16384.0;
			offset += 8;
		} else {
			local[0] = 1.0;
			local[3] = 1.0;
		}
		/* At this point, Apple's spec more or less tells you to scale the matrix by its own L1 norm.
		 * But stb_truetype scales by the L2 norm. And FreeType2 doesn't scale at all.
		 * Furthermore, Microsoft's spec doesn't even mention anything like this.
		 * It's almost as if nobody ever uses this feature anyway. */
		if (outline_offset(font, glyph, &outline) < 0)
			return -1;
		if (outline) {
			basePoint = outl->numPoints;
			if (decode_outline(font, outline, recDepth + 1, outl) < 0)
				return -1;
			transform_points(outl->numPoints - basePoint, outl->points + basePoint, local);
		}
	} while (flags & THERE_ARE_MORE_COMPONENTS);

	return 0;
}

static int
decode_outline(SFT_Font *font, uint_fast32_t offset, int recDepth, Outline *outl)
{
	int numContours;
	if (!is_safe_offset(font, offset, 10))
		return -1;
	numContours = geti16(font, offset);
	if (numContours > 0) {
		/* Glyph has a 'simple' outline consisting of a number of contours. */
		return simple_outline(font, offset + 10, (unsigned int) numContours, outl);
	} else if (numContours < 0) {
		/* Glyph has a compound outline combined from mutiple other outlines. */
		return compound_outline(font, offset + 10, recDepth, outl);
	} else {
		return 0;
	}
}

/* A heuristic to tell whether a given curve can be approximated closely enough by a line. */
static int
is_flat(Outline *outl, Curve curve)
{
	const double maxArea2 = 2.0;
	Point a = outl->points[curve.beg];
	Point b = outl->points[curve.ctrl];
	Point c = outl->points[curve.end];
	Point g = { b.x-a.x, b.y-a.y };
	Point h = { c.x-a.x, c.y-a.y };
	double area2 = fabs(g.x*h.y-h.x*g.y);
	return area2 <= maxArea2;
}

static int
tesselate_curve(Curve curve, Outline *outl)
{
	/* From my tests I can conclude that this stack barely reaches a top height
	 * of 4 elements even for the largest font sizes I'm willing to support. And
	 * as space requirements should only grow logarithmically, I think 10 is
	 * more than enough. */
#define STACK_SIZE 10
	Curve stack[STACK_SIZE];
	unsigned int top = 0;
	for (;;) {
		if (is_flat(outl, curve) || top >= STACK_SIZE) {
			if (outl->numLines >= outl->capLines && grow_lines(outl) < 0)
				return -1;
			outl->lines[outl->numLines++] = (Line) { curve.beg, curve.end };
			if (top == 0) break;
			curve = stack[--top];
		} else {
			uint_least16_t ctrl0 = outl->numPoints;
			if (outl->numPoints >= outl->capPoints && grow_points(outl) < 0)
				return -1;
			outl->points[ctrl0] = midpoint(outl->points[curve.beg], outl->points[curve.ctrl]);
			++outl->numPoints;

			uint_least16_t ctrl1 = outl->numPoints;
			if (outl->numPoints >= outl->capPoints && grow_points(outl) < 0)
				return -1;
			outl->points[ctrl1] = midpoint(outl->points[curve.ctrl], outl->points[curve.end]);
			++outl->numPoints;

			uint_least16_t pivot = outl->numPoints;
			if (outl->numPoints >= outl->capPoints && grow_points(outl) < 0)
				return -1;
			outl->points[pivot] = midpoint(outl->points[ctrl0], outl->points[ctrl1]);
			++outl->numPoints;

			stack[top++] = (Curve) { curve.beg, pivot, ctrl0 };
			curve = (Curve) { pivot, curve.end, ctrl1 };
		}
	}
	return 0;
#undef STACK_SIZE
}

static int
tesselate_curves(Outline *outl)
{
	unsigned int i;
	for (i = 0; i < outl->numCurves; ++i) {
		if (tesselate_curve(outl->curves[i], outl) < 0)
			return -1;
	}
	return 0;
}

/* Draws a line into the buffer. Uses a custom 2D raycasting algorithm to do so. */
static void
draw_line(Raster buf, Point origin, Point goal)
{
	Point delta;
	Point nextCrossing;
	Point crossingIncr;
	double halfDeltaX;
	double prevDistance = 0.0, nextDistance;
	double xAverage, yDifference;
	struct { int x, y; } pixel;
	struct { int x, y; } dir;
	int step, numSteps = 0;
	Cell *restrict cptr, cell;

	delta.x = goal.x - origin.x;
	delta.y = goal.y - origin.y;
	dir.x = SIGN(delta.x);
	dir.y = SIGN(delta.y);

	if (!dir.y) {
		return;
	}
	
	crossingIncr.x = dir.x ? fabs(1.0 / delta.x) : 1.0;
	crossingIncr.y = fabs(1.0 / delta.y);

	if (!dir.x) {
		pixel.x = fast_floor(origin.x);
		nextCrossing.x = 100.0;
	} else {
		if (dir.x > 0) {
			pixel.x = fast_floor(origin.x);
			nextCrossing.x = (origin.x - pixel.x) * crossingIncr.x;
			nextCrossing.x = crossingIncr.x - nextCrossing.x;
			numSteps += fast_ceil(goal.x) - fast_floor(origin.x) - 1;
		} else {
			pixel.x = fast_ceil(origin.x) - 1;
			nextCrossing.x = (origin.x - pixel.x) * crossingIncr.x;
			numSteps += fast_ceil(origin.x) - fast_floor(goal.x) - 1;
		}
	}

	if (dir.y > 0) {
		pixel.y = fast_floor(origin.y);
		nextCrossing.y = (origin.y - pixel.y) * crossingIncr.y;
		nextCrossing.y = crossingIncr.y - nextCrossing.y;
		numSteps += fast_ceil(goal.y) - fast_floor(origin.y) - 1;
	} else {
		pixel.y = fast_ceil(origin.y) - 1;
		nextCrossing.y = (origin.y - pixel.y) * crossingIncr.y;
		numSteps += fast_ceil(origin.y) - fast_floor(goal.y) - 1;
	}

	nextDistance = MIN(nextCrossing.x, nextCrossing.y);
	halfDeltaX = 0.5 * delta.x;

	for (step = 0; step < numSteps; ++step) {
		xAverage = origin.x + (prevDistance + nextDistance) * halfDeltaX;
		yDifference = (nextDistance - prevDistance) * delta.y;
		cptr = &buf.cells[pixel.y * buf.width + pixel.x];
		cell = *cptr;
		cell.cover += yDifference;
		xAverage -= (double) pixel.x;
		cell.area += (1.0 - xAverage) * yDifference;
		*cptr = cell;
		prevDistance = nextDistance;
		int alongX = nextCrossing.x < nextCrossing.y;
		pixel.x += alongX ? dir.x : 0;
		pixel.y += alongX ? 0 : dir.y;
		nextCrossing.x += alongX ? crossingIncr.x : 0.0;
		nextCrossing.y += alongX ? 0.0 : crossingIncr.y;
		nextDistance = MIN(nextCrossing.x, nextCrossing.y);
	}

	xAverage = origin.x + (prevDistance + 1.0) * halfDeltaX;
	yDifference = (1.0 - prevDistance) * delta.y;
	cptr = &buf.cells[pixel.y * buf.width + pixel.x];
	cell = *cptr;
	cell.cover += yDifference;
	xAverage -= (double) pixel.x;
	cell.area += (1.0 - xAverage) * yDifference;
	*cptr = cell;
}

static void
draw_lines(Outline *outl, Raster buf)
{
	unsigned int i;
	for (i = 0; i < outl->numLines; ++i) {
		Line  line   = outl->lines[i];
		Point origin = outl->points[line.beg];
		Point goal   = outl->points[line.end];
		draw_line(buf, origin, goal);
	}
}

static const uint8_t indexed4_mask[4] = {0x03, 0x0C, 0x30, 0xC0};
static const uint8_t indexed4_shift[4] = {0, 2, 4, 6};
static const uint8_t indexed16_mask[2] = {0x0F, 0xF0};
static const uint8_t indexed16_shift[2] = {0, 4};

/* Integrate the values in the buffer to arrive at the final grayscale image. */
static void post_process(Raster buf, image_buffer_t *image)
{
  Cell cell;
  double accum = 0.0, value;
  unsigned int i, num;
  num = (unsigned int) buf.width * (unsigned int) buf.height;
  uint8_t *image_data = image->data;

  switch(image->fmt) {
  case indexed2: {
    for (i = 0; i < num; ++i) {
      cell     = buf.cells[i];
      value    = fabs(accum + cell.area);
      value    = MIN(value, 1.0);
      uint32_t byte = i >> 3;
      uint32_t bit  = 7 - (i & 0x7);
      if (value > 0.5) {
        image_data[byte] |= (uint8_t)(1 << bit);
      } else {
        image_data[byte] &= (uint8_t)~(1 << bit);
      }
      accum += cell.cover;
    }
  } break;
  case indexed4: {
    for (i = 0; i < num; ++i) {
      cell     = buf.cells[i];
      value    = fabs(accum + cell.area);
      value    = MIN(value, 1.0);
      uint32_t byte = i >> 2;
      uint32_t ix  = 3 - (i & 0x3);
      uint8_t c = (uint8_t)(value * 4);
      if (c == 4) c = 3;
      image_data[byte] = (uint8_t)((uint8_t)(image_data[byte] & ~indexed4_mask[ix]) | (uint8_t)(c << indexed4_shift[ix]));
      accum += cell.cover;
    }
  } break;
  case indexed16: {
    for (i = 0; i < num; ++i) {
      cell     = buf.cells[i];
      value    = fabs(accum + cell.area);
      value    = MIN(value, 1.0);
      uint32_t byte = i >> 1;
      uint32_t ix  = 1 - (i & 0x1);
      uint8_t c = (uint8_t)(value * 16);
      if (c == 16) c = 15;
      image_data[byte] = (uint8_t)((uint8_t)(image_data[byte] & ~indexed16_mask[ix]) | (uint8_t)(c << indexed16_shift[ix]));
      accum += cell.cover;
    }
  } break;
  case rgb332: {
    for (i = 0; i < num; ++i) {
      cell     = buf.cells[i];
      value    = fabs(accum + cell.area);
      value    = MIN(value, 1.0);
      uint8_t r = 0;
      uint8_t g = 0;
      uint8_t b = 0;
      if (value < 0.24) {
      } else if (value < 0.30) {
        r = 3;
        g = 3;
        b = 1;
      } else if (value < 0.55) {
        r = 5;
        g = 5;
        b = 2;
      } else {
        r = 7;
        g = 7;
        b = 3;
      }
      image_data[i] = r << 5 | g << 2 | b;
      accum += cell.cover;
    }
  } break;
  case rgb565: {
    for (i = 0; i < num; ++i) {
      cell     = buf.cells[i];
      value    = fabs(accum + cell.area);
      value    = MIN(value, 1.0);
      uint16_t r = (uint16_t)(value * 31);
      uint16_t g = (uint16_t)(value * 63);
      uint16_t b = (uint16_t)(value * 31);
      uint16_t c = r << 11 | g << 5 | b;
      image_data[i * 2] = (uint8_t)(c >> 8);
      image_data[i * 2 + 1] = (uint8_t)c;
      accum += cell.cover;
    }
  } break;
  case rgb888: {
    for (i = 0; i < num; ++i) {
      cell     = buf.cells[i];
      value    = fabs(accum + cell.area);
      value    = MIN(value, 1.0);
      uint8_t r = (uint8_t)(value * 255);
      uint8_t g = (uint8_t)(value * 255);
      uint8_t b = (uint8_t)(value * 255);
      image_data[(i * 3)] = r;
      image_data[(i * 3) + 1] = g;
      image_data[(i * 3) + 2] = b;
      accum += cell.cover;
    }
  } break;
  default:
    break;
  }
}

/* for (i = 0; i < num; ++i) { */
/*   cell     = buf.cells[i]; */
/*   value    = fabs(accum + cell.area); */
/*   value    = MIN(value, 1.0); */
/*   value    = value * 255.0 + 0.5; */
/*   image[i] = (uint8_t) value; */
/*   accum   += cell.cover; */
/*  } */

static int
render_outline(Outline *outl, double transform[6], image_buffer_t * image)
{
	Cell *cells = NULL;
	Raster buf;
	unsigned int numPixels;
	
	numPixels = (unsigned int) image->width * (unsigned int) image->height;

	STACK_ALLOC(cells, Cell, 128 * 128, numPixels);
	if (!cells) {
		return -1;
	}
	memset(cells, 0, numPixels * sizeof *cells);
	buf.cells  = cells;
	buf.width  = image->width;
	buf.height = image->height;

	transform_points(outl->numPoints, outl->points, transform);

	clip_points(outl->numPoints, outl->points, image->width, image->height);

	if (tesselate_curves(outl) < 0) {
		STACK_FREE(cells);
		return -1;
	}

	draw_lines(outl, buf);

	post_process(buf, image);

	STACK_FREE(cells);
	return 0;
}

