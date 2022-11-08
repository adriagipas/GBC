/*
 * Copyright 2011-2012,2022 Adrià Giménez Pastor.
 *
 * This file is part of adriagipas/GBC.
 *
 * adriagipas/GBC is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * adriagipas/GBC is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Foobar.  If not, see <https://www.gnu.org/licenses/>.
 */
/*
 *  timers.c - Mòdul que implementa el temporitzador i el divisor.
 *
 */


#include <stddef.h>
#include <stdlib.h>

#include "GBC.h"




/**********/
/* MACROS */
/**********/

#define SAVE(VAR)                                               \
  if ( fwrite ( &(VAR), sizeof(VAR), 1, f ) != 1 ) return -1

#define LOAD(VAR)                                               \
  if ( fread ( &(VAR), sizeof(VAR), 1, f ) != 1 ) return -1

#define CHECK(COND)                             \
  if ( !(COND) ) return -1;




/*********/
/* ESTAT */
/*********/

/* Divisor a 16384 Hz (Cada 256 cicles rellotge). */
static struct
{
  
  GBCu8 reg;
  int   cc;
  
} _divider;

/* Temporitzador. */
static struct
{
  
  GBCu8    control;
  GBCu8    counter;
  GBCu8    modulo;
  GBC_Bool enabled;
  int      cc;
  int      freq;
  
} _timer;




/**********************/
/* FUNCIONS PÚBLIQUES */
/**********************/

void
GBC_timers_clock (
        	  const int cc
        	  )
{
  
  /* Divider. */
  if ( (_divider.cc+= cc) >= 256 )
    {
      _divider.cc-= 256;
      ++_divider.reg;
    }
  
  /* Timer. */
  if ( _timer.enabled )
    {
      _timer.cc+= cc;
      while ( _timer.cc >= _timer.freq )
        {
          _timer.cc-= _timer.freq;
          if ( ++_timer.counter == 0x00 )
            {
              _timer.counter= _timer.modulo;
              GBC_cpu_request_timer_int ();
            }
        }
    }
  
} /* end GBC_timers_clock */


GBCu8
GBC_timers_divider_read (void)
{
  return _divider.reg;
} /* end GBC_timers_divider_read */


void
GBC_timers_divider_write (
        		  GBCu8 data
        		  )
{
  _divider.reg= 0x00;
} /* end GBC_timers_divider_write */


void
GBC_timers_init (void)
{
  
  /* Divisor. */
  _divider.reg= 0;
  _divider.cc= 0;
  
  /* Temporitzador. */
  _timer.control= 0x00;
  _timer.counter= 0x00;
  _timer.modulo= 0x00;
  _timer.enabled= GBC_FALSE;
  _timer.cc= 0;
  _timer.freq= 1024;
  
} /* end GBC_timers_init */


GBCu8
GBC_timers_timer_control_read (void)
{
  return _timer.control;
} /* end GBC_timers_timer_control_read */


void
GBC_timers_timer_control_write (
        			GBCu8 data
        			)
{
  
  _timer.enabled= ((data&0x04)!=0);
  if ( !_timer.enabled ) _timer.cc= 0; /* Açò és idea meua. */
  switch ( data&0x3 )
    {
    case 0: _timer.freq= 1024; /* 4096 Hz */ break;
    case 1: _timer.freq= 16; /* 262144 Hz */ break;
    case 2: _timer.freq= 64; /* 65536 Hz */ break;
    case 3: _timer.freq= 256; /* 16384 Hz */ break;
    }
  _timer.control= data;
  
} /* end GBC_timers_timer_control_write */


GBCu8
GBC_timers_timer_counter_read (void)
{
  return _timer.counter;
} /* end GBC_timers_timer_counter_read */


void
GBC_timers_timer_counter_write (
        			GBCu8 data
        			)
{
  _timer.counter= data;
} /* end GBC_timers_timer_counter_write */


GBCu8
GBC_timers_timer_modulo_read (void)
{
  return _timer.modulo;
} /* end GBC_timers_timer_modulo_read */


void
GBC_timers_timer_modulo_write (
        		       GBCu8 data
        		       )
{
  _timer.modulo= data;
} /* end GBC_timers_timer_modulo_write */


int
GBC_timers_save_state (
        	       FILE *f
        	       )
{

  SAVE ( _divider );
  SAVE ( _timer );

  return 0;
  
} /* end GBC_timers_save_state */


int
GBC_timers_load_state (
        	       FILE *f
        	       )
{

  LOAD ( _divider );
  CHECK ( _divider.cc >= 0 && _divider.cc < 256 );
  LOAD ( _timer );
  CHECK ( _timer.cc >= 0 && _timer.cc < _timer.freq );
  CHECK ( _timer.freq >= 0 );
  
  return 0;
  
} /* end GBC_timers_load_state */
