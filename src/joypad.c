/*
 * Copyright 2011-2012,2015,2022 Adrià Giménez Pastor.
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
 * along with adriagipas/GBC.  If not, see <https://www.gnu.org/licenses/>.
 */
/*
 *  joypad.c - Mòdul que implementa el mando.
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


#define BUTTON 0x20
#define DIRECTION 0x10




/*********/
/* ESTAT */
/*********/

/* Callbacks. */
static GBC_CheckButtons *_check_buttons;
static void *_udata;

/* Selecci i bits ignorats. */
static GBCu8 _sel;




/**********************/
/* FUNCIONS PÚBLIQUES */
/**********************/

void
GBC_joypad_init (
        	 GBC_CheckButtons *check_buttons,
                 void             *udata
        	 )
{
  
  _check_buttons= check_buttons;
  _udata= udata;
  GBC_joypad_init_state ();
  
} /* end GBC_joypad_init */


void
GBC_joypad_init_state (void)
{
  _sel= 0;
} /* end GBC_joypad_init_state */


void
GBC_joypad_key_pressed (
        		GBC_Bool button_pressed,
        		GBC_Bool direction_pressed
        		)
{
  
  if ( (button_pressed && _sel&BUTTON) ||
       (direction_pressed && _sel&DIRECTION) )
    GBC_cpu_request_joypad_int ();
  
} /* end GBC_joypad_key_presed */


GBCu8
GBC_joypad_read (void)
{
  
  /* NOTA: No sé que fa quan els dos estan seleccionats. */
  
  if ( !(_sel&BUTTON) )
    return _sel | ((~(_check_buttons ( _udata )>>4))&0xF);
  else if ( !(_sel&DIRECTION) )
    return _sel | ((~_check_buttons ( _udata ))&0xF);
  else
    return _sel | 0xF;
  
} /* end GBC_joypad_read */


void
GBC_joypad_write (
        	  GBCu8 data
        	  )
{
  _sel= data&0xF0;
} /* end GBC_joypad_write */


int
GBC_joypad_save_state (
        	       FILE *f
        	       )
{

  SAVE ( _sel );

  return 0;
  
} /* end GBC_joypad_save_state */


int
GBC_joypad_load_state (
        	       FILE *f
        	       )
{

  LOAD ( _sel );

  return 0;
  
} /* end GBC_joypad_load_state */
