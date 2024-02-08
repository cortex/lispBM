/*
    Copyright 2018, 2021, 2022, 2024 Joel Svensson  svenssonjoel@yahoo.se

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

#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <pthread.h>
#include <sys/time.h>
#include <unistd.h>
#include <termios.h>
#include <ctype.h>
#include <getopt.h>
#include <readline/readline.h>
#include <readline/history.h>

#include "lispbm.h"
#include "lbm_flat_value.h"
#include "lbm_prof.h"
#include "extensions/array_extensions.h"
#include "extensions/string_extensions.h"
#include "extensions/math_extensions.h"
#include "extensions/runtime_extensions.h"

#include "lbm_custom_type.h"
#include "lbm_channel.h"
#include "lbm_version.h"

#include "repl_exts.h"

#define GC_STACK_SIZE 256
#define PRINT_STACK_SIZE 256
#define EXTENSION_STORAGE_SIZE 256
#define WAIT_TIMEOUT 2500
#define STR_SIZE 1024
#define CONSTANT_MEMORY_SIZE 32*1024
#define PROF_DATA_NUM 100

lbm_extension_t extensions[EXTENSION_STORAGE_SIZE];
lbm_uint constants_memory[CONSTANT_MEMORY_SIZE];
lbm_prof_t prof_data[100];

bool const_heap_write(lbm_uint ix, lbm_uint w) {
  if (ix >= CONSTANT_MEMORY_SIZE) return false;
  if (constants_memory[ix] == 0xffffffff) {
    constants_memory[ix] = w;
    return true;
  } else if (constants_memory[ix] == w) {
    return true;
  }

  char buf[1024];
  lbm_print_value(buf, 1024, constants_memory[ix]);
  printf("prev: %x | %s\n", constants_memory[ix], buf);
  lbm_print_value(buf, 1024, w);
  printf("curr: %x | %s\n", w, buf);
  return false;
}

static volatile bool allow_print = true;

static lbm_char_channel_t string_tok;
static lbm_string_channel_state_t string_tok_state;

void new_prompt() {
  printf("\33[2K\r");
  printf("# ");
  fflush(stdout);
}

void erase() {
  printf("\33[2K\r");
  fflush(stdout);
}

void *eval_thd_wrapper(void *v) {

  printf("Lisp REPL started! (LBM Version: %u.%u.%u)\n", LBM_MAJOR_VERSION, LBM_MINOR_VERSION, LBM_PATCH_VERSION);
  printf("Type :quit to exit.\n");
  printf("     :info for statistics.\n");
  printf("     :load [filename] to load lisp source.\n");
  lbm_run_eval();
  new_prompt();
  printf("Closing down evaluator thread\n");
  return NULL;
}

void critical(void) {
  printf("CRITICAL ERROR\n");
  exit(0);
}

void done_callback(eval_context_t *ctx) {
  char output[1024];
  lbm_value t = ctx->r;
  lbm_print_value(output, 1024, t);
  erase();
  printf("> %s\n", output);
  new_prompt();
}

int error_print(const char *format, ...) {
  va_list args;
  va_start (args, format);
  erase();
  int n = vprintf(format, args);
  va_end(args);
  new_prompt();
  return n;
}

void sleep_callback(uint32_t us) {
  struct timespec s;
  struct timespec r;
  s.tv_sec = 0;
  s.tv_nsec = (long)us * 1000;
  nanosleep(&s, &r);
}

static bool prof_running = false;

void *prof_thd(void *v) {
  while (prof_running) {
    lbm_prof_sample();
    sleep_callback(200);
  }
  return NULL;
}

lbm_value ext_print(lbm_value *args, lbm_uint argn) {
  if (argn < 1) return lbm_enc_sym(SYM_NIL);

  if (!allow_print) return lbm_enc_sym(SYM_TRUE);

  char output[1024];

  for (unsigned int i = 0; i < argn; i ++) {
    lbm_value t = args[i];

    if (lbm_is_ptr(t) && lbm_type_of(t) == LBM_TYPE_ARRAY) {
      lbm_array_header_t *array = (lbm_array_header_t *)lbm_car(t);
      char *data = (char*)array->data;
      printf("%s", data);
    } else {
      lbm_print_value(output, 1024, t);
      printf("%s", output);
    }
  }
  printf("\n");
  return lbm_enc_sym(SYM_TRUE);
}

/* load a file, caller is responsible for freeing the returned string */
char * load_file(char *filename) {
  char *file_str = NULL;
  //size_t str_len = strlen(filename);
  //filename[str_len-1] = 0;
  int i = 0;
  while (filename[i] == ' ' && filename[i] != 0) {
    i ++;
  }
  FILE *fp;
  printf("filename: %s\n", &filename[i]);

  if (strlen(&filename[i]) > 0) {
    errno = 0;
    fp = fopen(&filename[i], "r");
    if (!fp) {
      return NULL;
    }
    long fsize_long;
    unsigned int fsize;
    fseek(fp, 0, SEEK_END);
    fsize_long = ftell(fp);
    if (fsize_long <= 0) {
      return NULL;
    }
    fsize = (unsigned int) fsize_long;
    fseek(fp, 0, SEEK_SET);
    file_str = malloc(fsize+1);
    memset(file_str, 0 , fsize+1);
    if (fread(file_str,1,fsize,fp) != fsize) {
      free(file_str);
      file_str = NULL;
    }
    fclose(fp);
  }
  return file_str;
}


void print_ctx_info(eval_context_t *ctx, void *arg1, void *arg2) {
  (void) arg1;
  (void) arg2;

  char output[1024];

  int print_ret = lbm_print_value(output, 1024, ctx->r);

  printf("--------------------------------\n");
  printf("ContextID: %"PRI_UINT"\n", ctx->id);
  printf("Stack SP: %"PRI_UINT"\n",  ctx->K.sp);
  printf("Stack SP max: %"PRI_UINT"\n", ctx->K.max_sp);
  if (print_ret) {
    printf("Value: %s\n", output);
  } else {
    printf("Error: %s\n", output);
  }
}

void ctx_exists(eval_context_t *ctx, void *arg1, void *arg2) {

  lbm_cid id = *(lbm_cid*)arg1;
  bool *exists = (bool*)arg2;

  if (ctx->id == id) {
    *exists = true;
  }
}

void lookup_local(eval_context_t *ctx, void *arg1, void *arg2) {


  char output[1024];
  lbm_value res;
  if (lbm_env_lookup_b(&res, (lbm_value)arg1, ctx->curr_env)) {

    lbm_print_value(output, 1024, res);
    printf("CTX %d: %s = %s\n", (int32_t)ctx->id, (char *)arg2, output);
  } else {
    printf("not found\n");
  }

}


void sym_it(const char *str) {
  bool sym_name_flash = lbm_symbol_in_flash((char *)str);
  bool sym_entry_flash = lbm_symbol_list_entry_in_flash((char *)str);
  printf("[%s, %s]: %s\n",
         sym_name_flash ? "FLASH" : "LBM_MEM",
         sym_entry_flash ? "FLASH" : "LBM_MEM",
         str);
}

static lbm_uint memory[LBM_MEMORY_SIZE_1M];
static lbm_uint bitmap[LBM_MEMORY_BITMAP_SIZE_1M];

pthread_t lispbm_thd = 0;
unsigned int heap_size = 2048; // default
lbm_cons_t *heap_storage = NULL;
lbm_heap_state_t heap_state;
lbm_const_heap_t const_heap;

void parse_opts(int argc, char **argv) {

  int c;
  opterr = 1;
  while ((c = getopt(argc, argv, "h:")) != -1) {
    switch (c) {
    case 'h':
      heap_size = (unsigned int)atoi((char*)optarg);
      break;
    default:
      break;
    }
  }
}

int init_repl() {

  if (lispbm_thd && lbm_get_eval_state() != EVAL_CPS_STATE_DEAD) {
    int thread_r = 0;
    lbm_kill_eval();
    pthread_join(lispbm_thd, (void*)&thread_r);
    lispbm_thd = 0;
  }

  if (heap_storage) {
    free(heap_storage);
    heap_storage = NULL;
  }

  heap_storage = (lbm_cons_t*)malloc(sizeof(lbm_cons_t) * heap_size);

  if (heap_storage == NULL) {
    return 0;
  }

  if (!lbm_init(heap_storage, heap_size,
                memory, LBM_MEMORY_SIZE_1M,
                bitmap, LBM_MEMORY_BITMAP_SIZE_1M,
                GC_STACK_SIZE,
                PRINT_STACK_SIZE,
                extensions,
                EXTENSION_STORAGE_SIZE)) {
    return 0;
  }

  if (!lbm_eval_init_events(20)) {
    return 0;
  }

  memset(constants_memory, 0xFF, CONSTANT_MEMORY_SIZE * sizeof(lbm_uint));
  if (!lbm_const_heap_init(const_heap_write,
                           &const_heap,constants_memory,
                           CONSTANT_MEMORY_SIZE)) {
    return 0;
  }

  lbm_set_critical_error_callback(critical);
  lbm_set_ctx_done_callback(done_callback);
  lbm_set_timestamp_us_callback(timestamp);
  lbm_set_usleep_callback(sleep_callback);
  lbm_set_dynamic_load_callback(dynamic_loader);
  lbm_set_printf_callback(error_print);

  init_exts();

  if (!lbm_add_extension("print", ext_print)) {
    return 0;
  }

  if (pthread_create(&lispbm_thd, NULL, eval_thd_wrapper, NULL)) {
    printf("Error creating evaluation thread\n");
    return 1;
  }
  return 1;
}

int main(int argc, char **argv) {
  parse_opts(argc, argv);

  using_history();

  if (!init_repl()) {
    printf ("Failed to initialize REPL\n");
    return -1;
  }

  char output[1024];

  while (1) {
    erase();
    char *str = readline("# ");
    add_history(str);
    unsigned int n = strlen(str);

    if (n >= 5 && strncmp(str, ":info", 5) == 0) {
      printf("--(LISP HEAP)-----------------------------------------------\n");
      lbm_get_heap_state(&heap_state);
      printf("Heap size: %u Bytes\n", heap_size * 8);
      printf("Used cons cells: %"PRI_INT"\n", heap_size - lbm_heap_num_free());
      printf("Free cons cells: %"PRI_INT"\n", lbm_heap_num_free());
      printf("GC counter: %"PRI_INT"\n", heap_state.gc_num);
      printf("Recovered: %"PRI_INT"\n", heap_state.gc_recovered);
      printf("Recovered arrays: %"PRI_UINT"\n", heap_state.gc_recovered_arrays);
      printf("Marked: %"PRI_INT"\n", heap_state.gc_marked);
      printf("GC stack size: %"PRI_UINT"\n", lbm_get_gc_stack_size());
      printf("GC SP max: %"PRI_UINT"\n", lbm_get_gc_stack_max());
      printf("--(Symbol and Array memory)---------------------------------\n");
      printf("Memory size: %"PRI_UINT" Words\n", lbm_memory_num_words());
      printf("Memory free: %"PRI_UINT" Words\n", lbm_memory_num_free());
      printf("Allocated arrays: %"PRI_UINT"\n", heap_state.num_alloc_arrays);
      printf("Symbol table size RAM: %"PRI_UINT" Bytes\n", lbm_get_symbol_table_size());
      printf("Symbol names size RAM: %"PRI_UINT" Bytes\n", lbm_get_symbol_table_size_names());
      printf("Symbol table size FLASH: %"PRI_UINT" Bytes\n", lbm_get_symbol_table_size_flash());
      printf("Symbol names size FLASH: %"PRI_UINT" Bytes\n", lbm_get_symbol_table_size_names_flash());
      free(str);
    } else if (strncmp(str, ":prof start", 11) == 0) {
      lbm_prof_init(prof_data,
                    PROF_DATA_NUM);
      pthread_t thd; // just forget this id.
      prof_running = true;
      if (pthread_create(&thd, NULL, prof_thd, NULL)) {
        printf("Error creating profiler thread\n");
        free(str);
        continue;
      }
      printf("Profiler started\n");
      free(str);
    } else if (strncmp(str, ":prof stop", 10) == 0) {
      prof_running = false;
      printf("Profiler stopped. Issue command ':prof report' for statistics\n.");
      free(str);
    } else if (strncmp(str, ":prof report", 12) == 0) {
      lbm_uint num_sleep = lbm_prof_get_num_sleep_samples();
      lbm_uint num_system = lbm_prof_get_num_system_samples();
      lbm_uint tot_samples = lbm_prof_get_num_samples();
      lbm_uint tot_gc = 0;
      printf("CID\tName\tSamples\t%%Load\t%%GC\n");
      for (int i = 0; i < PROF_DATA_NUM; i ++) {
        if (prof_data[i].cid == -1) break;
        tot_gc += prof_data[i].gc_count;
        printf("%d\t%s\t%u\t%f\t%f\n",
               prof_data[i].cid,
               prof_data[i].name,
               prof_data[i].count,
               100.0 * ((float)prof_data[i].count) / (float) tot_samples,
               100.0 * ((float)prof_data[i].gc_count) / (float)prof_data[i].count);
      }
      printf("\n");
      printf("GC:\t%u\t%f%%\n", tot_gc, 100.0 * ((float)tot_gc / (float)tot_samples));
      printf("System:\t%u\t%f%%\n", num_system, 100.0 * ((float)num_system / (float)tot_samples));
      printf("Sleep:\t%u\t%f%%\n", num_sleep, 100.0 * ((float)num_sleep / (float)tot_samples));
      printf("Total:\t%u samples\n", tot_samples);
      free(str);
    } else if (strncmp(str, ":env", 4) == 0) {
      for (int i = 0; i < GLOBAL_ENV_ROOTS; i ++) {
        lbm_value *env = lbm_get_global_env();
        lbm_value curr = env[i];
        printf("Environment [%d]:\r\n", i);
        while (lbm_type_of(curr) == LBM_TYPE_CONS) {
          lbm_print_value(output,1024, lbm_car(curr));
          curr = lbm_cdr(curr);
          printf("  %s\r\n",output);
        }
      }
      free(str);
    } else if (strncmp(str, ":state", 6) == 0) {
      lbm_uint state = lbm_get_eval_state();
      switch (state) {
      case EVAL_CPS_STATE_DEAD:
        printf("DEAD\n");
        break;
      case EVAL_CPS_STATE_PAUSED:
        printf("PAUSED\n");
        break;
      case EVAL_CPS_STATE_NONE:
        printf("NO STATE\n");
        break;
      case EVAL_CPS_STATE_RUNNING:
        printf("RUNNING\n");
        break;
      case EVAL_CPS_STATE_KILL:
        printf("KILLING\n");
        break;
      }
      free(str);
    }
    else if (n >= 5 && strncmp(str, ":load", 5) == 0) {

      char *file_str = load_file(&str[5]);
      if (file_str) {

        lbm_create_string_char_channel(&string_tok_state,
                                       &string_tok,
                                       file_str);

        /* Get exclusive access to the heap */
        lbm_pause_eval_with_gc(50);
        while(lbm_get_eval_state() != EVAL_CPS_STATE_PAUSED) {
          sleep_callback(10);
        }

        (void)lbm_load_and_eval_program_incremental(&string_tok, NULL);
        lbm_continue_eval();

        //printf("started ctx: %"PRI_UINT"\n", cid);
      }
      free(str);
    } else if (n >= 5 && strncmp(str, ":verb", 5) == 0) {
      lbm_toggle_verbose();
      free(str);
      continue;
    } else if (n >= 4 && strncmp(str, ":pon", 4) == 0) {
      allow_print = true;
      free(str);
      continue;
    } else if (n >= 5 && strncmp(str, ":poff", 5) == 0) {
      allow_print = false;
      free(str);
      continue;
    } else if (strncmp(str, ":ctxs", 5) == 0) {
      printf("****** Running contexts ******\n");
      lbm_running_iterator(print_ctx_info, NULL, NULL);
      printf("****** Blocked contexts ******\n");
      lbm_blocked_iterator(print_ctx_info, NULL, NULL);
      free(str);
    }  else if (n >= 5 && strncmp(str, ":quit", 5) == 0) {
      free(str);
      break;
    } else if (strncmp(str, ":symbols", 8) == 0) {
      lbm_symrepr_name_iterator(sym_it);
      free(str);
    } else if (strncmp(str, ":heap", 5) == 0) {
      int size = atoi(str + 5);
      if (size > 0) {
        heap_size = (unsigned int)size;
        if (!init_repl()) {
          printf("Failed to initialize REPL after heap resize\n");
          return -1;
        }
      }
      free(str);
    } else if (strncmp(str, ":reset", 6) == 0) {
      if (!init_repl()) {
        printf ("Failed to initialize REPL\n");
        return -1;
      }
      free(str);
    } else if (strncmp(str, ":send", 5) == 0) {
      int id;
      int i_val;

      if (sscanf(str + 5, "%d%d", &id, &i_val) == 2) {
        lbm_pause_eval_with_gc(50);
        while(lbm_get_eval_state() != EVAL_CPS_STATE_PAUSED) {
          sleep_callback(10);
        }

        if (lbm_send_message((lbm_cid)id, lbm_enc_i(i_val)) == 0) {
          printf("Could not send message\n");
        }

        lbm_continue_eval();
      } else {
        printf("Incorrect arguments to send\n");
      }
      free(str);
    } else if (strncmp(str, ":pause", 6) == 0) {
      lbm_pause_eval_with_gc(30);
      while(lbm_get_eval_state() != EVAL_CPS_STATE_PAUSED) {
        sleep_callback(10);
      }
      printf("Evaluator paused\n");
      free(str);
    } else if (strncmp(str, ":continue", 9) == 0) {
      lbm_continue_eval();
      free(str);
    } else if (strncmp(str, ":inspect", 8) == 0) {

      int i = 8;
      if (strlen(str) >= 8) {
        while (str[i] == ' ') i++;
      }
      char *sym = str + i;
      lbm_uint sym_id = 0;
      if (lbm_get_symbol_by_name(sym, &sym_id)) {
        lbm_running_iterator(lookup_local, (void*)lbm_enc_sym(sym_id), (void*)sym);
        lbm_blocked_iterator(lookup_local, (void*)lbm_enc_sym(sym_id), (void*)sym);
      } else {
        printf("symbol does not exist\n");
      }
    } else if (strncmp(str, ":undef", 6) == 0) {
      lbm_pause_eval_with_gc(50);
      while(lbm_get_eval_state() != EVAL_CPS_STATE_PAUSED) {
        sleep_callback(10);
      }
      char *sym = str + 7;
      printf("undefining: %s\n", sym);
      printf("%s\n", lbm_undefine(sym) ? "Cleared bindings" : "No definition found");
      lbm_continue_eval();
      free(str);
    } else {
      /* Get exclusive access to the heap */
      lbm_pause_eval_with_gc(50);
      while(lbm_get_eval_state() != EVAL_CPS_STATE_PAUSED) {
        sleep_callback(10);
      }
      lbm_create_string_char_channel(&string_tok_state,
                                     &string_tok,
                                     str);
      (void)lbm_load_and_eval_expression(&string_tok);
      lbm_continue_eval();
    }
  }
  free(heap_storage);
  return 0;
}
