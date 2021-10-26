/*
    Copyright 2019, 2021 Joel Svensson	svenssonjoel@yahoo.se

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

#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "compression.h"

#define  KEY  0
#define  CODE 1

/* The codes are generated using python script in utils directory 
   - Depends on the Huffman library (pip3 install huffman)
   - exec(open('gen_codes.py').read()) 
   - print(make_c())
*/

#define NUM_CODES 67
#define MAX_KEY_LENGTH 6
#define MAX_CODE_LENGTH 7
char *codes[NUM_CODES][2] = {
    { "9", "100100" },
    { "8", "100101" },
    { "7", "010011" },
    { "6", "101010" },
    { "5", "011011" },
    { "4", "100110" },
    { "3", "010101" },
    { "2", "011000" },
    { "1", "011101" },
    { "0", "011001" },
    { "_", "101100" },
    { ",", "111011" },
    { "`", "110011" },
    { " ", "101011" },
    { "'", "110000" },
    { "\\", "110010" },
    { "\"", "111000" },
    { "#", "110001" },
    { ".", "111010" },
    { ">", "110110" },
    { "<", "110100" },
    { "=", "110101" },
    { "/", "110111" },
    { "*", "101101" },
    { "-", "101110" },
    { "+", "101111" },
    { "nil", "011110" },
    { "cdr", "011111" },
    { "car", "101000" },
    { "cons", "100010" },
    { "let", "100011" },
    { "define", "010110" },
    { "progn", "010111" },
    { "quote", "010100" },
    { "list", "101001" },
    { "if", "011010" },
    { "lambda", "100111" },
    { "((", "011100" },
    { "))", "100000" },
    { ")", "100001" },
    { "(", "010010" },
    { "z", "1111011" },
    { "y", "000010" },
    { "x", "1111100" },
    { "w", "1111101" },
    { "v", "000101" },
    { "u", "1111110" },
    { "t", "001100" },
    { "s", "001101" },
    { "r", "000000" },
    { "q", "000001" },
    { "p", "001011" },
    { "o", "010000" },
    { "n", "1111010" },
    { "m", "1111000" },
    { "l", "000011" },
    { "k", "000110" },
    { "j", "1111111" },
    { "i", "001110" },
    { "h", "001010" },
    { "g", "010001" },
    { "f", "1111001" },
    { "e", "000111" },
    { "d", "001111" },
    { "c", "001000" },
    { "b", "000100" },
    { "a", "001001" }
    };

/*
#define NUM_CODES 66
#define MAX_KEY_LENGTH 6
#define MAX_CODE_LENGTH 7
char *codes[NUM_CODES][2] = {
    { "9", "010111" },
    { "8", "101110" },
    { "7", "100001" },
    { "6", "100010" },
    { "5", "100100" },
    { "4", "011000" },
    { "3", "100101" },
    { "2", "011001" },
    { "1", "011101" },
    { "0", "011011" },
    { ",", "110101" },
    { "`", "110110" },
    { " ", "110000" },
    { "'", "110111" },
    { "\\", "101111" },
    { "\"", "111000" },
    { "#", "110010" },
    { ".", "111001" },
    { ">", "110011" },
    { "<", "111100" },
    { "=", "111101" },
    { "/", "111010" },
    { "*", "110001" },
    { "-", "111011" },
    { "+", "110100" },
    { "nil", "101000" },
    { "cdr", "101001" },
    { "car", "101010" },
    { "cons", "100110" },
    { "let", "100111" },
    { "define", "011110" },
    { "progn", "011111" },
    { "quote", "010110" },
    { "list", "101100" },
    { "if", "011100" },
    { "lambda", "100011" },
    { "((", "101101" },
    { "))", "011010" },
    { ")", "101011" },
    { "(", "100000" },
    { "z", "001111" },
    { "y", "000100" },
    { "x", "000000" },
    { "w", "000001" },
    { "v", "010011" },
    { "u", "000010" },
    { "t", "001000" },
    { "s", "001001" },
    { "r", "010000" },
    { "q", "010001" },
    { "p", "1111111" },
    { "o", "001100" },
    { "n", "001110" },
    { "m", "1111100" },
    { "l", "000101" },
    { "k", "000110" },
    { "j", "000011" },
    { "i", "001010" },
    { "h", "1111110" },
    { "g", "001101" },
    { "f", "1111101" },
    { "e", "000111" },
    { "d", "001011" },
    { "c", "010100" },
    { "b", "010010" },
    { "a", "010101" }
    };
*/
/*
#define NUM_CODES 64
#define MAX_KEY_LENGTH 6
#define MAX_CODE_LENGTH 10
char *codes[NUM_CODES][2] = {
    { "nil", "010011100" },
    { "cdr", "0100111010" },
    { "car", "0100111011" },
    { "cons", "0100111100" },
    { "let", "0100111101" },
    { "define", "0100111110" },
    { "progn", "0100111111" },
    { "quote", "010011000" },
    { "list", "010011001" },
    { "if", "010011010" },
    { "lambda", "010011011" },
    { "((", "1110" },
    { "))", "001" },
    { ")", "1111" },
    { "(", "000" },
    { "z", "110110" },
    { "y", "011101" },
    { "x", "110100" },
    { "w", "101110" },
    { "v", "100110" },
    { "u", "011100" },
    { "t", "100111" },
    { "s", "101100" },
    { "r", "101101" },
    { "q", "101111" },
    { "p", "011110" },
    { "o", "011111" },
    { "n", "100011" },
    { "m", "110111" },
    { "l", "110000" },
    { "k", "100000" },
    { "j", "01000" },
    { "i", "101001" },
    { "h", "101010" },
    { "g", "011011" },
    { "f", "100100" },
    { "e", "101000" },
    { "d", "110010" },
    { "c", "100010" },
    { "b", "100101" },
    { "a", "110001" },
    { "9", "1101011" },
    { "8", "1000010" },
    { "7", "1000011" },
    { "6", "1010110" },
    { "5", "1010111" },
    { "4", "1100110" },
    { "3", "1100111" },
    { "2", "010010" },
    { "1", "1101010" },
    { "0", "0110101" },
    { " ", "0110011" },
    { "'", "0101011" },
    { "\\", "0101100" },
    { "\"", "0101110" },
    { "#", "0101111" },
    { ".", "0110001" },
    { ">", "0110010" },
    { "<", "0101000" },
    { "=", "0101101" },
    { "/", "0110100" },
    { "*", "0101010" },
    { "-", "0110000" },
    { "+", "0101001" }
    };
*/
/*
#define NUM_CODES 66
#define MAX_KEY_LENGTH 6
#define MAX_CODE_LENGTH 7
char *codes[NUM_CODES][2] = {
    { "9", "101101" },
    { "8", "101000" },
    { "7", "101001" },
    { "6", "100111" },
    { "5", "101100" },
    { "4", "101010" },
    { "3", "110000" },
    { "2", "101011" },
    { "1", "101111" },
    { "0", "101110" },
    { " ", "110001" },
    { "'", "111011" },
    { "\\", "110010" },
    { "\"", "110011" },
    { "#", "111100" },
    { ".", "110110" },
    { ">", "110111" },
    { "<", "111000" },
    { "=", "111101" },
    { "/", "111001" },
    { "*", "110100" },
    { "-", "110101" },
    { "+", "111010" },
    { "nil", "000001" },
    { "))))", "001010" },
    { ")))", "001011" },
    { "))", "000010" },
    { ")", "000011" },
    { "((", "1111110" },
    { "(", "1111111" },
    { "cdr", "001000" },
    { "car", "001001" },
    { "cons", "000100" },
    { "let", "000101" },
    { "define", "001100" },
    { "progn", "1111100" },
    { "quote", "1111101" },
    { "list", "000110" },
    { "if", "000111" },
    { "lambda", "000000" },
    { "z", "001110" },
    { "y", "001111" },
    { "x", "011111" },
    { "w", "010100" },
    { "v", "100000" },
    { "u", "100001" },
    { "t", "011100" },
    { "s", "100100" },
    { "r", "011001" },
    { "q", "100010" },
    { "p", "100110" },
    { "o", "001101" },
    { "n", "010110" },
    { "m", "010000" },
    { "l", "011110" },
    { "k", "010101" },
    { "j", "010011" },
    { "i", "011000" },
    { "h", "100011" },
    { "g", "011011" },
    { "f", "010001" },
    { "e", "010010" },
    { "d", "100101" },
    { "c", "011010" },
    { "b", "011101" },
    { "a", "010111" }
    };
*/
int match_longest_key(char *string) {

  int longest_match_ix = -1;
  unsigned int longest_match_length = 0;
  unsigned int n = strlen(string);

  for (int i = 0; i < NUM_CODES; i ++) {
    unsigned int s_len = strlen(codes[i][KEY]);
    if (s_len <= n) {
      if (strncmp(codes[i][KEY], string, s_len) == 0) {
	if (s_len > longest_match_length) {
	  longest_match_ix = i;
	  longest_match_length = s_len;
	}
      }
    }
  }
  return longest_match_ix;
}

int match_longest_code(char *string, uint32_t start_bit, uint32_t total_bits) {

  uint32_t bits_left = total_bits - start_bit;
  int longest_match_ix = -1;
  unsigned int longest_match_length = 0;

  for (int i = 0; i < NUM_CODES; i++) {
    unsigned int s_len = strlen(codes[i][CODE]);
    if ((uint32_t)s_len <= bits_left) {
      bool match = true;
      for (uint32_t b = 0; b < (uint32_t)s_len; b ++) {
	uint32_t byte_ix = (start_bit + b) / 8;
	uint32_t bit_ix  = (start_bit + b) % 8;

	char *code_str = codes[i][CODE];

	if (((string[byte_ix] & (1 << bit_ix)) ? '1' : '0') !=
	      code_str[b]) {
	  match = false;
	}
      }
      if (match && (s_len > longest_match_length)) {
	longest_match_length = s_len;
	longest_match_ix = i;
      }
    }
  }
  return longest_match_ix;
}

int compressed_length(char *string) {
  uint32_t i = 0;

  uint32_t n = strlen(string);
  int comp_len = 0; // in bits

  bool string_mode = false;
  bool gobbling_whitespace = false;

  while (i < n) {
    if (string_mode) {
      if (string[i] == '\"'  &&
	  !(string[i-1] == '\\')) {
	string_mode = false;
	comp_len += 8;
	i++;
      } else {
	comp_len += 8;
	i++;
      }

    } else {

      // Gobble up any comments
      if (string[i] == ';' ) {
	while (string[i] && string[i] != '\n') {
	  i++;
	}
	continue;
      }

      if ( string[i] == '\n' ||
	   string[i] == ' '  ||
	   string[i] == '\t' ||
	   string[i] == '\r') {
	gobbling_whitespace = true;
	i ++;
	continue;
      } else if (gobbling_whitespace) {
	gobbling_whitespace = false;
	i--;
      }

      if (string[i] == '\"') string_mode = true;

      int ix;
      if (isspace(string[i])) {
	ix = match_longest_key(" ");
      } else {
	ix = match_longest_key(string + i);
      }

      if (ix == -1)return -1;
      unsigned int code_len = strlen(codes[ix][1]);
      comp_len += (int)code_len;
      i += strlen(codes[ix][0]);
    }
  }
  return comp_len;
}

void set_bit(char *c, int bit_pos, bool set) {
  char bval = 0;
  if (bit_pos <= 7) {
    bval = (char)(1 << bit_pos);
  }
  if (set) {
    *c = (char)(*c | bval);
  } else {
    *c = (char)(*c & ~bval);
  }
}

void emit_string_char_code(char *compressed, char c, int *bit_pos) {

  for (int i = 0; i < 8; i ++) {
    int byte_ix = (*bit_pos) / 8;
    int bit_ix  = (*bit_pos) % 8;
    bool s = (c & (1 << i));
    set_bit(&compressed[byte_ix], bit_ix, s);
    *bit_pos = *bit_pos + 1;
  }
}

void emit_code(char *compressed, char *code, int *bit_pos) {
  unsigned int n = strlen(code);

  for (unsigned int i = 0; i < n; i ++) {
    int byte_ix = (*bit_pos) / 8;
    int bit_ix  = (*bit_pos) % 8;
    bool s = (code[i] == '1');
    set_bit(&compressed[byte_ix], bit_ix, s);
    *bit_pos = *bit_pos + 1;
  }
}

void emit_key(char *dest, char *key, int nk, uint32_t *char_pos) {

  for (int i = 0; i < nk; i ++) {
    dest[*char_pos] = key[i];
    *char_pos = *char_pos + 1;
  }
}

char read_character(char *src, uint32_t *bit_pos) {

  char c = 0;

  for (int i = 0; i < 8; i ++) {
    uint32_t byte_ix = (*bit_pos)/8;
    uint32_t bit_ix  = (*bit_pos)%8;
    bool s = src[byte_ix] & (1 << bit_ix);
    set_bit(&c, i, s);
    *bit_pos = *bit_pos + 1;
  }
  return c;
}

char *compression_compress(char *string, uint32_t *res_size) {

  int c_size_bits_i = compressed_length(string);
  uint32_t c_size_bits = 0;
  if ( c_size_bits_i >= 0 ) {
    c_size_bits = (uint32_t)compressed_length(string);
  }

  uint32_t c_size_bytes = 4 + (c_size_bits/8);
  if (c_size_bits % 8 > 0) {
    c_size_bytes += 1;
  }
  
  uint32_t header_value = c_size_bits;

  if (header_value == 0) return NULL;

  char *compressed = malloc(c_size_bytes);
  if (!compressed) return NULL;
  memset(compressed, 0, c_size_bytes);
  *res_size = c_size_bytes;
  int bit_pos = 0;

  compressed[0] = (char)header_value;
  compressed[1] = (char)(header_value >> 8);
  compressed[2] = (char)(header_value >> 16);
  compressed[3] = (char)(header_value >> 24);
  bit_pos = 32;

  bool string_mode = false;
  bool gobbling_whitespace = false;
  uint32_t n = strlen(string);
  uint32_t i = 0;

  while (i < n) {
    if (string_mode) {

      if (string[i] == '\"' &&
	  !(string[i-1] == '\\')) {
	emit_string_char_code(compressed, '\"', &bit_pos);
	i ++;
	string_mode = false;
	continue;
      } else {
	emit_string_char_code(compressed, string[i], &bit_pos);
	i++;
      }

    } else {

      // Gobble up any comments
      if (string[i] == ';' ) {
	while (string[i] && string[i] != '\n') {
	  i++;
	}
	continue;
      }

      // gobble up whitespaces
      if ( string[i] == '\n' ||
	   string[i] == ' '  ||
	   string[i] == '\t' ||
	   string[i] == '\r') {
	gobbling_whitespace = true;
	*(string + i) = ' ';
	i ++;
	continue;
      } else if (gobbling_whitespace) {
	gobbling_whitespace = false;
	i--;
      }

      /* Compress string-starting " character */
      if (string[i] == '\"') {
	string_mode = true;
      }
      int ix = match_longest_key(&string[i]);

      if (ix == -1) {
	free(compressed);
	return NULL;
      }

      emit_code(compressed, codes[ix][CODE], &bit_pos);

      i += strlen(codes[ix][0]);
    }
  }

  return compressed;
}


void compression_init_state(decomp_state *s, char *src) {
  memcpy(&s->compressed_bits, src, 4);
  s->i = 32;
  s->string_mode = false;
  s->last_string_char = 0;
  s->src = src;
}

int compression_decompress_incremental(decomp_state *s, char *dest_buff, uint32_t dest_n) {

  memset(dest_buff, 0, dest_n);
  uint32_t char_pos = 0;

  if (s->i < s->compressed_bits + 32) {
     if (s->string_mode) {
      char c = read_character(s->src, &s->i);
      if (c == '\"') {
	if (s->last_string_char != '\\') {
	  s->string_mode = false;
	  s->last_string_char = 0;
	}
      }
      s->last_string_char = c;
      dest_buff[0] = c;
      return 1;
    }

    int ix = match_longest_code(s->src, s->i, (s->compressed_bits + 32));
    if (ix == -1) {
      return -1;
    }

    if( strlen(codes[ix][KEY]) == 1 &&
	strncmp(codes[ix][KEY], "\"", 1) == 0) {
      s->string_mode = true;
      s->last_string_char = 0;
    }

    unsigned int n_bits_decoded = strlen(codes[ix][CODE]);
    emit_key(dest_buff, codes[ix][KEY], (int)strlen(codes[ix][KEY]), &char_pos);
    s->i+=n_bits_decoded;
    return (int)char_pos;

  } else {
    return 0;
  }

}

bool compression_decompress(char *dest, uint32_t dest_n, char *src) {

  uint32_t char_pos = 0;

  char dest_buff[32];
  int num_chars = 0;
  decomp_state s;

  memset(dest, 0, dest_n);

  compression_init_state(&s, src);

  while (true) {
    
    num_chars = compression_decompress_incremental(&s, dest_buff, 32);
    if (num_chars == 0) break;
    if (num_chars == -1) return false; 
    
    for (int i = 0; i < num_chars; i ++) {
      dest[char_pos++] = dest_buff[i];
    }
  }
  return true;
}
