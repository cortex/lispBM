/*
    Copyright 2025 Joel Svensson  svenssonjoel@yahoo.se

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef LBM_IMAGE_H_
#define LBM_IMAGE_H_

typedef bool (*lbm_image_write_fun)(uint32_t data, int32_t index);
typedef bool (*lbm_image_clear_fun)(void);

// C interface to image manipulation
uint32_t *lbm_image_get_image(void);
lbm_value lbm_image_get_startup(void);
int32_t lbm_image_get_write_index(void);

uint32_t lbm_image_get_size(void);
bool lbm_image_has_startup(void);
bool lbm_image_save_startup(lbm_value sym);
bool lbm_image_save_global_env(void);
bool lbm_image_save_constant_heap_ix(void);

lbm_uint *lbm_image_add_symbol(char *name, lbm_uint id, lbm_uint symlist);

bool lbm_image_is_empty(void);
void lbm_image_clear(void);

// startup initialization
void lbm_image_init(uint32_t *image_mem_addr,
                    uint32_t  image_size,
                    lbm_image_write_fun  image_write_fun);

void lbm_image_create(void);
bool lbm_image_exists(void);
bool lbm_image_boot(void);

#endif
