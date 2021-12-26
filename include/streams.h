/*
    Copyright 2021 Joel Svensson	svenssonjoel@yahoo.se

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

#ifndef STREAMS_H_
#define STREAMS_H_

#include <typedefs.h>

typedef struct stream_s{  
  void  *state;   /* stream implementation dependent state */
  VALUE (*more)(struct stream_s*);
  VALUE (*get)(struct stream_s*);
  VALUE (*peek)(struct stream_s*, unsigned int);
  void  (*drop)(struct stream_s*, unsigned int);
  void  (*put)(struct stream_s*, VALUE);
} stream_t;

#endif
