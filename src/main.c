/*
 * Copyright 2011-2013,2015,2022 Adrià Giménez Pastor.
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
 *  main.c - Implementació del mòdul principal.
 *
 */


#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "GBC.h"




/*************/
/* CONSTANTS */
/*************/

/* Un valor un poc arreu. Si cada T és correspon amb un cicle de
   rellotge que va a 4.2MHz approx, tenim que és comprova cada 1/100
   segons. */
static const int CCTOCHECK= 42000;

static const char GBCSTATE[]= "GBCSTATE\n";




/*********/
/* ESTAT */
/*********/

/* Rom. */
static const GBC_Rom *_rom;

/* Inidica si hi ha bios. */
static GBC_Bool _use_fake_bios;

/* Senyals. */
static GBC_Bool _stop;
static GBC_Bool _button_pressed;
static GBC_Bool _direction_pressed;

/* Frontend. */
static GBC_CheckSignals *_check;
static GBC_Warning *_warning;
static void *_udata;

/* Velocitat (1 - Doble velocitat). */
static int _speed;

/* Callback per a la UCP. */
static GBC_CPUStep *_cpu_step;




/*********************/
/* FUNCIONS PRIVADES */
/*********************/

static void
fake_bios (void)
{
  
  GBC_Bool cgb_mode;

  
  /* CGB MODE. */
  cgb_mode= ((_rom->banks[0][0x143]&0x80)!=0);
  GBC_cpu_set_cgb_mode ( cgb_mode );
  GBC_lcd_set_cgb_mode ( cgb_mode );
  if ( !cgb_mode ) GBC_lcd_init_gray_pal ();
  
  /* Power Up Sequence. */
  GBC_cpu_power_up ();
  GBC_apu_power_up ();
  GBC_mem_write ( 0xFF05, 0x00 );
  GBC_mem_write ( 0xFF06, 0x00 );
  GBC_mem_write ( 0xFF07, 0x00 );
  GBC_mem_write ( 0xFF40, 0x91 );
  GBC_mem_write ( 0xFF42, 0x00 );
  GBC_mem_write ( 0xFF43, 0x00 );
  GBC_mem_write ( 0xFF45, 0x00 );
  GBC_mem_write ( 0xFF47, 0xFC );
  GBC_mem_write ( 0xFF48, 0xFF );
  GBC_mem_write ( 0xFF49, 0xFF );
  GBC_mem_write ( 0xFF4A, 0x00 );
  GBC_mem_write ( 0xFF4B, 0x00 );
  GBC_mem_write ( 0xFFFF, 0x00 );
      
} /* end fake_bios */




/**********************/
/* FUNCIONS PÚBLIQUES */
/**********************/

void
GBC_main_switch_speed (void)
{
  _speed^= 1;
} /* end GBC_main_switch_speed */


GBC_Error
GBC_init (
          const GBCu8         bios[0x900],
          const GBC_Rom      *rom,
          const GBC_Frontend *frontend,
          void               *udata
          )
{
  
  GBC_Error err;
  
  
  _speed= 0;
  _check= frontend->check;
  _warning= frontend->warning;
  _udata= udata;
  _cpu_step= frontend->trace!=NULL ?
    frontend->trace->cpu_step:NULL;
  _rom= rom;
  _use_fake_bios= (bios==NULL);
  
  err= GBC_mapper_init ( rom, bios==NULL,
        		 frontend->get_external_ram,
        		 frontend->update_rumble,
        		 frontend->trace!=NULL?
        		 frontend->trace->mapper_changed:NULL,
        		 udata );
  if ( err != GBC_NOERROR ) return err;
  GBC_mem_init ( bios,
        	 frontend->trace!=NULL?
        	 frontend->trace->mem_access:NULL,
        	 udata );
  GBC_cpu_init ( frontend->warning, udata );
  GBC_lcd_init ( frontend->update_screen, frontend->warning, udata );
  GBC_timers_init ();
  GBC_joypad_init ( frontend->check_buttons, udata );
  GBC_apu_init ( frontend->play_sound, udata );
  
  if ( _use_fake_bios ) fake_bios ();
  
  return GBC_NOERROR;
  
} /* end GBC_init */


int
GBC_iter (
          GBC_Bool *stop
          )
{

  static int CC= 0;
  int cc;
  GBC_Bool button_pressed, direction_pressed;
  
  
  cc= (GBC_cpu_run ()>>_speed);
  cc+= GBC_lcd_clock ( cc );
  GBC_apu_clock ( cc );
  GBC_mapper_clock ( cc );
  GBC_timers_clock ( cc<<_speed );
  CC+= cc;
  if ( CC >= CCTOCHECK && _check != NULL )
    {
      CC-= CCTOCHECK;
      button_pressed= direction_pressed= GBC_FALSE;
      _check ( stop, &button_pressed, &direction_pressed, _udata );
      GBC_joypad_key_pressed ( button_pressed, direction_pressed );
    }
  
  return cc;
  
} /* end GBC_iter */


void
GBC_key_pressed (
        	 GBC_Bool button_pressed,
        	 GBC_Bool direction_pressed
        	 )
{
  GBC_joypad_key_pressed ( button_pressed, direction_pressed );
} /* end GBC_key_pressed */


int
GBC_load_state (
        	FILE *f
        	)
{

  static char buf[sizeof(GBCSTATE)];
  
  
  _stop= _button_pressed= _direction_pressed= GBC_FALSE;
  
  /* GBCSTATE. */
  if ( fread ( buf, sizeof(GBCSTATE)-1, 1, f ) != 1 ) goto error;
  buf[sizeof(GBCSTATE)-1]= '\0';
  if ( strcmp ( buf, GBCSTATE ) ) goto error;

  /* _speed. */
  if ( fread ( &_speed, sizeof(_speed), 1, f ) != 1 ) goto error;
  if ( _speed != 0 && _speed != 1 ) goto error;
  
  /* Carrega. */
  if ( GBC_mapper_load_state ( f ) != 0 ) goto error;
  if ( GBC_mem_load_state ( f ) != 0 ) goto error;
  if ( GBC_cpu_load_state ( f ) != 0 ) goto error;
  if ( GBC_apu_load_state ( f ) != 0 ) goto error;
  if ( GBC_lcd_load_state ( f ) != 0 ) goto error;
  if ( GBC_joypad_load_state ( f ) != 0 ) goto error;
  if ( GBC_timers_load_state ( f ) != 0 ) goto error;
  
  return 0;
  
 error:
  _warning ( _udata,
             "error al carregar l'estat del simulador des d'un fitxer" );
  _speed= 0;
  GBC_mapper_init_state (); /* Ací no pot tornar error. */
  GBC_mem_init_state ();
  GBC_cpu_init_state ();
  GBC_apu_init_state ();
  GBC_lcd_init_state ();
  GBC_joypad_init_state ();
  GBC_timers_init ();
  if ( _use_fake_bios ) fake_bios ();
  return -1;
  
} /* end GBC_load_state */


void
GBC_loop (void)
{
  
  int cc, CC;
  
  
  _stop= _button_pressed= _direction_pressed= GBC_FALSE;
  if ( _check == NULL )
    {
      while ( !_stop )
        {
          cc= (GBC_cpu_run ()>>_speed);
          cc+= GBC_lcd_clock ( cc );
          GBC_apu_clock ( cc );
          GBC_mapper_clock ( cc );
          GBC_timers_clock ( cc<<_speed );
        }
    }
  else
    {
      CC= 0;
      for (;;)
        {
          cc= (GBC_cpu_run ()>>_speed);
          cc+= GBC_lcd_clock ( cc );
          GBC_apu_clock ( cc );
          GBC_mapper_clock ( cc );
          GBC_timers_clock ( cc<<_speed );
          CC+= cc;
          if ( CC >= CCTOCHECK )
            {
              CC-= CCTOCHECK;
              _check ( &_stop, &_button_pressed, &_direction_pressed, _udata );
              GBC_joypad_key_pressed ( _button_pressed, _direction_pressed );
              _button_pressed= _direction_pressed= GBC_FALSE;
              if ( _stop ) break;
            }
        }
    }
  _stop= GBC_FALSE;
  
} /* end GBC_loop */


int
GBC_save_state (
        	FILE *f
        	)
{

  if ( fwrite ( GBCSTATE, sizeof(GBCSTATE)-1, 1, f ) != 1 ) return -1;
  if ( fwrite ( &_speed, sizeof(_speed), 1, f ) != 1 ) return -1;
  if ( GBC_mapper_save_state ( f ) != 0 ) return -1;
  if ( GBC_mem_save_state ( f ) != 0 ) return -1;
  if ( GBC_cpu_save_state ( f ) != 0 ) return -1;
  if ( GBC_apu_save_state ( f ) != 0 ) return -1;
  if ( GBC_lcd_save_state ( f ) != 0 ) return -1;
  if ( GBC_joypad_save_state ( f ) != 0 ) return -1;
  if ( GBC_timers_save_state ( f ) != 0 ) return -1;
  
  return 0;
  
} /* end GBC_save_state */


void
GBC_stop (void)
{
  _stop= GBC_TRUE;
} /* end GBC_stop */


int
GBC_trace (void)
{
  
  int cc;
  GBCu16 addr;
  GBC_Step step;
  
  
  if ( _cpu_step != NULL )
    {
      addr= GBC_cpu_decode_next_step ( &step );
      _cpu_step ( &step, addr, _udata );
    }
  GBC_mem_set_mode_trace ( GBC_TRUE );
  cc= (GBC_cpu_run ()>>_speed);
  cc+= GBC_lcd_clock ( cc );
  GBC_apu_clock ( cc );
  GBC_mapper_clock ( cc );
  GBC_timers_clock ( cc<<_speed );
  GBC_mem_set_mode_trace ( GBC_FALSE );
  
  return cc;
  
} /* end GBC_trace */
