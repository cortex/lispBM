/*
  Copyright 2025 Joel Svensson              svenssonjoel@yahoo.se

  LispBM is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  LispBM is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <extensions/ttf_extensions.h>
#include <extensions.h>

#include "schrift.h"

// Load a ttf font from an array
// Returns a list (font-value, font-data)
// font-data is a ref to the font-data backing the font-value.
static lbm_value ext_ttf_font(lbm_value *args, lbm_value argn) {
  lbm_value res = ENC_SYM_TERROR;
  if (argn == 3 &&
      lbm_is_number(args[0]) &&
      lbm_is_number(args[1]) &&
      lbm_is_array_r(args[2])) {
    lbm_value font_val = lbm_heap_allocate_list_init(4,
                                                     args[0],
                                                     args[1],
                                                     ENC_SYM_NIL,
                                                     ENC_SYM_NIL);
    if (lbm_is_ptr(font_val)) {
      lbm_value font_data;
      if (lbm_heap_allocate_array(&font_data, sizeof(SFT_Font))) {
        lbm_array_header_t *font_arr = (lbm_array_header_t*)lbm_car(font_data);
        SFT_Font *ft = (SFT_Font*)font_arr->data;
        lbm_array_header_t *arr = (lbm_array_header_t*)lbm_car(args[2]);
        ft->memory = (uint8_t*)arr->data;
        ft->size = (uint_fast32_t)arr->size;
        ft->unitsPerEm = 0;
        ft->locaFormat = 0;
        ft->numLongHmtx = 0;
        if (init_font(ft) < 0) {
          printf("init font fail\n");
          res = ENC_SYM_NIL; // gc will clean up the allocations.
        } else {
          lbm_value cddr = lbm_cdr(lbm_cdr(font_val));
          lbm_set_car(cddr, font_data); // The font object used in schrift.
          lbm_set_car(lbm_cdr(cddr), args[2]); // In order to remember the data if not global.
          res = font_val;
        }
        goto ttf_font_end;
      }
    }
    res = ENC_SYM_MERROR;
  }
 ttf_font_end:
  return res;
}

bool is_font_value(lbm_value font) {
  return (lbm_is_cons(font) &&
          lbm_is_number(lbm_car(font)) &&
          lbm_is_number(lbm_car(lbm_cdr(font))) &&
          lbm_is_array_r(lbm_car(lbm_cdr(lbm_cdr(font)))) &&
          lbm_is_array_r(lbm_cadr(lbm_cdr(lbm_cdr(font)))));
}

bool is_prepared_font_value(lbm_value font) {
  return (lbm_is_cons(font) &&
          lbm_is_number(lbm_car(font)) &&
          lbm_is_number(lbm_cadr(font)) &&
          lbm_is_array_r(lbm_cadr(lbm_cdr(font))) &&
          lbm_is_array_r(lbm_cadr(lbm_cdr(lbm_cdr(font)))) &&
          lbm_is_cons(lbm_cadr(lbm_cdr(lbm_cdr(lbm_cdr(font))))));
}



static uint32_t font_get_x_scale(lbm_value font_val) {
  return lbm_dec_u(lbm_car(font_val));
}

static uint32_t font_get_y_scale(lbm_value font_val) {
  return lbm_dec_u(lbm_car(lbm_cdr(font_val)));
}

static SFT_Font *font_get_font(lbm_value font_val) {
  lbm_value font = lbm_car(lbm_cdr(lbm_cdr(font_val)));
  lbm_array_header_t *font_header = (lbm_array_header_t*)lbm_car(font);
  return (SFT_Font*)font_header->data;
}

SFT mk_sft(lbm_value font_val) {

  SFT_Font *ft = font_get_font(font_val);
  SFT sft;
  sft.font = ft;
  sft.xScale = font_get_x_scale(font_val);
  sft.yScale = font_get_y_scale(font_val);
  sft.xOffset = 0;
  sft.yOffset = 0;
  sft.flags = SFT_DOWNWARD_Y;

  return sft;
}

lbm_value ext_ttf_glyph_dims(lbm_value *args, lbm_uint argn) {

  lbm_value res = ENC_SYM_TERROR;
  if (argn == 2 &&
      is_font_value(args[0]) &&
      lbm_is_number(args[1])) { // glyph id

    SFT sft = mk_sft(args[0]);
    SFT_GMetrics gmtx;
    SFT_Glyph gid = lbm_dec_as_u32(args[1]);

    if (sft_gmetrics(&sft, gid, &gmtx) < 0) {
      res = ENC_SYM_EERROR;
      goto glyph_dims_done;
    }
    res = lbm_heap_allocate_list_init(2,
                                      lbm_enc_u((uint32_t)((gmtx.minWidth + 3) & ~3)),
                                      lbm_enc_u((uint32_t)gmtx.minHeight));
  }
 glyph_dims_done:
  return res;
}

lbm_value ext_ttf_glyph_render(lbm_value *args, lbm_uint argn) {
  lbm_value res = ENC_SYM_TERROR;
  lbm_array_header_t *img_arr;
  if (argn == 3 &&
      (img_arr = get_image_buffer(args[0])) &&
      is_font_value(args[1]) &&
      lbm_is_number(args[2])) { // glyph id

    SFT sft = mk_sft(args[1]);
    SFT_GMetrics gmtx;
    SFT_Glyph gid = lbm_dec_as_u32(args[2]);

    if (sft_gmetrics(&sft, gid, &gmtx) < 0) {
      res = ENC_SYM_EERROR;
      goto glyph_render_done;
    }

    image_buffer_t img;
    img.width = image_buffer_width((uint8_t*)img_arr->data);
    img.height = image_buffer_height((uint8_t*)img_arr->data);
    img.fmt = image_buffer_format((uint8_t*)img_arr->data);
    img.mem_base = (uint8_t*)img_arr->data;
    img.data = image_buffer_data((uint8_t*)img_arr->data);

    if ((img.width < ((gmtx.minWidth + 3) & ~3)) ||
        (img.height < gmtx.minHeight)) {
      res = ENC_SYM_EERROR;
      goto glyph_render_done;
    }
    if (sft_render(&sft, gid, &img) < 0) {
      res = ENC_SYM_EERROR;
      goto glyph_render_done;
    }
    res = ENC_SYM_TRUE;
  }
 glyph_render_done:
  return res;
}

static lbm_value lookup_glyph_image(uint32_t gid, lbm_value ls) {
  lbm_value curr = ls;
  lbm_value res = ENC_SYM_NO_MATCH;
  while (lbm_is_cons(curr)) {
    lbm_value c = lbm_ref_cell(curr)->car;
    if (lbm_is_cons(c)) {
      if (lbm_dec_as_u32(lbm_ref_cell(c)->car) == gid) { // regular equality should work fine!
        res = lbm_car(lbm_ref_cell(c)->cdr);
        break;
      }
    } else {
      res = ENC_SYM_EERROR;
      break;
    }
    curr = lbm_ref_cell(curr)->cdr;
  }
  return res;
}


lbm_value ext_ttf_print(lbm_value *args, lbm_uint argn) {
  lbm_value res = ENC_SYM_TERROR;
  lbm_array_header_t *img_arr;
  if (argn == 5 &&
      (img_arr = get_image_buffer(args[0])) &&
      lbm_is_number(args[1]) && // x position
      lbm_is_number(args[2]) && // y position
      is_prepared_font_value(args[3]) &&
      lbm_is_array_r(args[4])) { // sequence of utf8 characters

    SFT sft = mk_sft(args[3]);

    image_buffer_t tgt;
    tgt.width = image_buffer_width((uint8_t*)img_arr->data);
    tgt.height = image_buffer_height((uint8_t*)img_arr->data);
    tgt.fmt = image_buffer_format((uint8_t*)img_arr->data);
    tgt.mem_base = (uint8_t*)img_arr->data;
    tgt.data = image_buffer_data((uint8_t*)img_arr->data);

    double x = lbm_dec_as_double(args[1]);
    double y = lbm_dec_as_double(args[2]);
    uint8_t *utf8 = (uint8_t*)lbm_dec_str(args[4]);
    uint32_t i = 0;
    uint32_t next_i = 0;
    SFT_Glyph prev = 0;
    bool has_prev = false;
    uint32_t utf32;

    lbm_value glyph_tab = lbm_index_list(args[3], 4);

    while (get_utf32(utf8, &utf32, i, &next_i)) {

      SFT_Glyph gid;
      if (sft_lookup(&sft, utf32, &gid) < 0) {
        res = ENC_SYM_EERROR;
        goto ttf_print_done;
      }
      SFT_GMetrics gmtx;
      if (sft_gmetrics(&sft, gid, &gmtx) < 0) {
        res = ENC_SYM_EERROR;
        goto ttf_print_done;
      }

      lbm_value glyph = lookup_glyph_image(gid, glyph_tab);
      if (!(lbm_is_array_r(glyph) || lbm_is_symbol_nil(glyph))) {
        res = ENC_SYM_EERROR;
        goto ttf_print_done;
      }

      double x_shift = 0;
      double y_shift = 0;
      if (has_prev) {
        SFT_Kerning kern;
        kern.xShift = 0.0;
        kern.yShift = 0.0;
        // silly kerning lookup by first trying to get
        // the kern from GPOS and if we are unable, we try kern table.
        if (sft.font->pairAdjustOffset) {
          sft_gpos_kerning(&sft, prev, gid, &kern);
        }
        if (kern.xShift == 0.0 && kern.yShift == 0.0) {
          sft_kerning(&sft, prev, gid, &kern);
        }

        x_shift = kern.xShift;
        y_shift = kern.yShift;
      }

      double x_n = x;
      double y_n = y;

      x_n+=gmtx.leftSideBearing;
      y_n+=gmtx.yOffset;
      x_n+=x_shift;
      y_n+=y_shift;

      if (!lbm_is_symbol_nil(glyph)) { // whitespaces have no pre-rendered glyph
        lbm_array_header_t *arr = (lbm_array_header_t*)lbm_car(glyph);
        image_buffer_t src;
        src.width = image_buffer_width((uint8_t*)arr->data);
        src.height = image_buffer_height((uint8_t*)arr->data);
        src.fmt = image_buffer_format((uint8_t*)arr->data);
        src.mem_base = (uint8_t*)arr->data;
        src.data = image_buffer_data((uint8_t*)arr->data);

        for (int j = 0; j < src.height; j++) {
          for (int i = 0; i < src.width; i ++) {
            int xi = (int)x_n;
            int yi = (int)y_n;
            if (yi + j > 0 && yi + j < tgt.height &&
                xi + i > 0 && xi + i < tgt.width) {
              uint32_t p = getpixel(&src, i, j);
              // TODO transform indexed format according to a mapping
              putpixel(&tgt, xi + i, yi + j, p);
            }
          }
        }
      }

      x = x_n + gmtx.advanceWidth;
      i = next_i;
      prev = gid;
      has_prev = true;
    }
    res = ENC_SYM_TRUE;
  }
 ttf_print_done:
  return res;
}

// Extract a glyph starting at an index in a byte array (string)
lbm_value ext_ttf_glyph_id(lbm_value *args, lbm_uint argn) {
  lbm_value res = ENC_SYM_TERROR;
  if (argn == 3 &&
      is_font_value(args[0]) &&
      lbm_is_array_r(args[1]) &&
      lbm_is_number(args[2])) {
    res = ENC_SYM_EERROR;


    uint32_t ix = lbm_dec_as_u32(args[2]);

    SFT sft = mk_sft(args[0]);

    uint8_t *utf8 = (uint8_t*)lbm_dec_str(args[1]);
    uint32_t utf32 = 0;
    uint32_t next_ix = 0;
    if (get_utf32(utf8, &utf32, ix, &next_ix)) {
      SFT_Glyph gid;
      if (sft_lookup(&sft, utf32, &gid) < 0) {
        res = ENC_SYM_EERROR;
        goto glyph_id_done;
      }
      // If encoding gid fails, allocate list should too.
      lbm_value ls = lbm_heap_allocate_list_init(2,
                                                 lbm_enc_u32(gid),
                                                 lbm_enc_u(next_ix));                             
      res = ls;
    } else {
      res = ENC_SYM_NIL;
    }
  }
 glyph_id_done:
  return res;
}


void lbm_ttf_extensions_init(void) {

  // Low level utilities
  lbm_add_extension("ttf-glyph-dims",ext_ttf_glyph_dims);
  lbm_add_extension("ttf-glyph-render", ext_ttf_glyph_render);
  lbm_add_extension("ttf-glyph-id", ext_ttf_glyph_id);

  // Create font and draw text.
  lbm_add_extension("ttf-font", ext_ttf_font);
  lbm_add_extension("ttf-text",ext_ttf_print);
}
