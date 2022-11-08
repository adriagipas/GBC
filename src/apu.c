/*
 * Copyright 2011-2012,2015,2019,2020,2022 Adrià Giménez Pastor.
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
 *  apu.c - Mòdul que implementa el xip de so.
 *
 *  NOTA: El canal de soroll està subsamplejat.
 *  NOTA: Ignore l'estat Zombi del Volume Envelope.
 *
 */


#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

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


/* 4096 samples -> 256 Hz. */
#define LC_AUX_DIV 4096

/* 8192/4 */
#define TURNON_LC_AUX_DIV 2048

#define LC_INIT_CLOCK(CH)        					\
  do {        								\
    if ( (CH).lc_aux_div > LC_AUX_DIV/2 )        			\
      {        								\
        if ( (CH).lc_counter != 0 && --(CH).lc_counter == 0 )		\
          (CH).enabled= GBC_FALSE;					\
      }        								\
  } while(0)




/*************/
/* CONSTANTS */
/*************/

static const char DUTY_PAT[4][8]=
  {
    { 0, 0, 0, 0, 1, 0, 0, 0 },
    { 0, 0, 0, 0, 1, 1, 0, 0 },
    { 0, 0, 1, 1, 1, 1, 0, 0 },
    { 1, 1, 1, 1, 0, 0, 1, 1 }
  };

static const double _volume_table[16]=
  {
    0.0000, 0.0167, 0.0333, 0.0500, 0.0667, 0.0833, 0.1000, 0.1167,
    0.1333, 0.1500, 0.1667, 0.1833, 0.2000, 0.2167, 0.2333, 0.2500
  };




/*********/
/* ESTAT */
/*********/

/* Estat provisional pera VIN. */
static GBCu8 _vin;

/* Patrons duty amb grunalaritat 12. */
/* MOTIU: Upon the channel INIT trigger bit being set for either
   channel 1 or 2, the wave position's incrementing will be delayed by
   1/12 of a full cycle. */
static char _duty_pat[4][96];

/* Canal 1. */
static struct
{
  
  GBC_Bool enabled;
  
  /* Programmable timer. */
  GBCu16   pt_freq;
  GBCu16   pt_counter;
  
  /* Master Channel Control Switch. */
  GBC_Bool mccswitch;
  
  /* Length counter. */
  int      lc_aux_div;    /* Divisor auxiliar. */
  GBCu8    lc_counter;
  GBC_Bool lc_enabled;
  
  /* Sweep Unit. */
  int      sw_aux_div;    /* Dividix lc_aux_div */
  GBC_Bool sw_enabled;
  GBCu16   sw_freq;
  GBCu8    sw_time;
  GBCu8    sw_counter;
  GBC_Bool sw_increase;
  GBCu8    sw_shift;
  GBC_Bool sw_neg_used;
  
  /* Duty Cicle. */
  GBCu8    dc_wave_pattern;
  int      dc_pos;
  char     dc_out;
  
  /* Volumne Envelope. */
  int      ve_aux_div;    /* Divisor auxiliar que utilitza amb sw_aux_div. */
  GBCu8    ve_vol_reg;
  GBCu8    ve_vol;
  GBC_Bool ve_increase_reg;
  GBC_Bool ve_increase;
  GBCu8    ve_step;
  GBCu8    ve_counter;
  
} _ch1;

/* Canal 2. */
static struct
{
  
  GBC_Bool enabled;
  
  /* Programmable timer. */
  GBCu16   pt_freq;
  GBCu16   pt_counter;
  
  /* Master Channel Control Switch. */
  GBC_Bool mccswitch;
  
  /* Length counter. */
  int      lc_aux_div;    /* Divisor auxiliar. */
  GBCu8    lc_counter;
  GBC_Bool lc_enabled;
  
  /* Duty Cicle. */
  GBCu8    dc_wave_pattern;
  int      dc_pos;
  char     dc_out;
  
  /* Volumne Envelope. */
  int      ve_aux_div;    /* Divisor auxiliar que utilitza amb lc_aux_div. */
  GBCu8    ve_vol_reg;
  GBCu8    ve_vol;
  GBC_Bool ve_increase_reg;
  GBC_Bool ve_increase;
  GBCu8    ve_step;
  GBCu8    ve_counter;
  
} _ch2;

/* Canal 3. */
static struct
{
  
  GBC_Bool enabled;
  
  /* Programmable timer. */
  GBCu16   pt_freq;
  GBCu16   pt_counter;
  
  /* Master Channel Control Switch. */
  GBC_Bool mccswitch;
  
  /* Length counter. */
  int      lc_aux_div;    /* Divisor auxiliar. */
  GBCu16   lc_counter;
  GBC_Bool lc_enabled;
  
  /* Wave Pattern RAM. */
  char     ram[32];
  
  /* Wave Pattern Playback / Shifter Unit. */
  int      su_pos;
  int      su_val;
  
} _ch3;

/* Canal 4. */
static struct
{
  
  GBC_Bool enabled;
  
  /* Configurable timer. */
  GBCu16   ct_3bcounter;   /* 2*524288 / (2*r) / 2^(s+1) */
  int      ct_16bcounter;
  GBCu8    ct_ratio;
  GBCu8    ct_scfreq;
  
  /* Master Channel Control Switch. */
  GBC_Bool mccswitch;
  
  /* Length counter. */
  int      lc_aux_div;    /* Divisor auxiliar. */
  GBCu8    lc_counter;
  GBC_Bool lc_enabled;
  
  /* Volumne Envelope. */
  int      ve_aux_div;    /* Divisor auxiliar que utilitza amb lc_aux_div. */
  GBCu8    ve_vol_reg;
  GBCu8    ve_vol;
  GBC_Bool ve_increase_reg;
  GBC_Bool ve_increase;
  GBCu8    ve_step;
  GBCu8    ve_counter;
  
  /* PseudoRandom Number Generator. */
  GBCu16   pr_prng;
  char     pr_out;
  GBC_Bool pr_mode15b;
  
} _ch4;

/* Indica que el so està activat o no. */
static GBC_Bool _sound_on;
static GBC_Bool _stop;

/* Buffers per a cada canal. Açò es abans de convertir al valor
   real. */
static GBCu8 _buffer[4][GBC_APU_BUFFER_SIZE];

/* Comptadors i cicles per processar. */
static struct
{
  
  int pos;          /* Següent sample a generar, on 0 és el primer. */
  int cc;           /* Cicles de UCP acumulats. */
  int cctoFrame;    /* Cicles que falten per a plenar el buffer. */
  
} _timing;

/* Buffers d'eixida. */
static double _left[GBC_APU_BUFFER_SIZE];
static double _right[GBC_APU_BUFFER_SIZE];

/* Màscares dels canals. */
static int _left_mask;
static int _right_mask;

/* Callback. */
static GBC_PlaySound *_play_sound;
static void *_udata;




/*********************/
/* FUNCIONS PRIVADES */
/*********************/

/* Hi ha un cas en el que pot parar el canal. */
static void
calc_sweep (
            const bool update
            )
{

  GBCu16 tmp;
  
  
  if ( _ch1.sw_increase )
    tmp= _ch1.sw_freq + (_ch1.sw_freq>>_ch1.sw_shift);
  else
    {
      tmp= _ch1.sw_freq - (_ch1.sw_freq>>_ch1.sw_shift);
      _ch1.sw_neg_used= GBC_TRUE;
    }
  if ( tmp > 0x7FF ) _ch1.enabled= GBC_FALSE;
  if ( update )
    {
      _ch1.sw_freq= tmp;
      _ch1.pt_freq= tmp;
    }
  
} /* end calc_sweep */

static void
render_off (
            GBCu8     buffer[GBC_APU_BUFFER_SIZE],
            const int begin,
            const int end
            )
{
  
  int i;
  
  
  for ( i= begin; i < end; ++i )
    buffer[i]= 0x00;
  
} /* end render_off */


static void
render_ch1 (
            GBCu8     buffer[GBC_APU_BUFFER_SIZE],
            const int begin,
            const int end
            )
{
  
  int i;
  GBCu8 vol;
  
  
  vol= (_ch1.enabled && _ch1.dc_out ) ? _ch1.ve_vol : 0x0;
  for ( i= begin; i < end; ++i )
    {
      /* Length Counter, Sweep i Envelope. */
      if ( --_ch1.lc_aux_div == 0 )
        {
          _ch1.lc_aux_div= LC_AUX_DIV;
          if ( _ch1.lc_enabled && _ch1.lc_counter != 0 )
            {
              if ( --_ch1.lc_counter == 0 )
        	{
        	  _ch1.enabled= GBC_FALSE;
        	  vol= 0x00;
        	}
            }
          /* Sweep i Envelope. */
          if ( --_ch1.sw_aux_div == 0 )
            {
              _ch1.sw_aux_div= 2; /* 128 Hz. */
              if ( _ch1.sw_enabled )
        	{
        	  if ( --_ch1.sw_counter == 0 )
        	    {
        	      _ch1.sw_counter= _ch1.sw_time;
                      if ( _ch1.sw_counter == 0 ) _ch1.sw_counter= 8;
        	      if ( _ch1.sw_time != 0 && _ch1.sw_enabled )
        		{
        		  calc_sweep ( true );
                          calc_sweep ( false );
        		  // Açò és sols perquè en prepare_sweep es
        		  // pot desactivar el canal.
        		  if ( !_ch1.enabled ) vol= 0x0;
        		}
        	    }
        	}
              /* Envelope. */
              if ( --_ch1.ve_aux_div == 0 )
        	{
        	  _ch1.ve_aux_div= 2; /* 128/2 -> 64 Hz. */
        	  if ( _ch1.ve_counter != 0 )
        	    {
        	      if ( --_ch1.ve_counter == 0 )
        		{
        		  _ch1.ve_counter= _ch1.ve_step;
        		  if ( _ch1.ve_increase )
        		    {
        		      if ( _ch1.ve_vol != 0xF ) ++_ch1.ve_vol;
        		    }
        		  else
        		    {
        		      if ( _ch1.ve_vol != 0x0 ) --_ch1.ve_vol;
        		    }
        		}
        	      vol= (_ch1.enabled && _ch1.dc_out) ? _ch1.ve_vol : 0x0;
        	    }
        	}
            }
        }
      /* Línia principal. */
      if ( _ch1.pt_counter == 0 )
        {
          _ch1.pt_counter= 2048 - _ch1.pt_freq;
          if ( _ch1.enabled )
            {
              if ( (_ch1.dc_pos+= 12) >= 96 ) _ch1.dc_pos-= 96;
              _ch1.dc_out= _duty_pat[_ch1.dc_wave_pattern][_ch1.dc_pos];
              vol= _ch1.dc_out ? _ch1.ve_vol : 0x0;
            }
        }
      else --_ch1.pt_counter;
      // Freqüències inaudibles.
      if ( _ch1.pt_freq >= 0x7FA ) vol= 0x0;
      buffer[i]= vol;
    }
  
} /* end render_ch1 */


static void
render_ch2 (
            GBCu8     buffer[GBC_APU_BUFFER_SIZE],
            const int begin,
            const int end
            )
{
  
  int i;
  GBCu8 vol;
  
  
  vol= (_ch2.enabled && _ch2.dc_out) ? _ch2.ve_vol : 0x0;
  for ( i= begin; i < end; ++i )
    {
      /* Length Counter i Envelope. */
      if ( --_ch2.lc_aux_div == 0 )
        {
          _ch2.lc_aux_div= LC_AUX_DIV;
          if ( _ch2.lc_enabled && _ch2.lc_counter != 0 )
            {
              if ( --_ch2.lc_counter == 0 )
        	{
        	  _ch2.enabled= GBC_FALSE;
        	  vol= 0x0;
        	}
            }
          /* Envelope. */
          if ( --_ch2.ve_aux_div == 0 )
            {
              _ch2.ve_aux_div= 4; /* 256/4 -> 64 Hz. */
              if ( _ch2.ve_counter != 0 )
        	{
        	  if ( --_ch2.ve_counter == 0 )
        	    {
        	      _ch2.ve_counter= _ch2.ve_step;
        	      if ( _ch2.ve_increase )
        		{
        		  if ( _ch2.ve_vol != 0xF ) ++_ch2.ve_vol;
        		}
        	      else
        		{
        		  if ( _ch2.ve_vol != 0x0 ) --_ch2.ve_vol;
        		}
        	    }
        	  vol= (_ch2.enabled && _ch2.dc_out) ? _ch2.ve_vol : 0x0;
        	}
            }
        }
      /* Línia principal. */
      if ( _ch2.pt_counter == 0 )
        {
          _ch2.pt_counter= 2048 - _ch2.pt_freq;
          if ( _ch2.enabled /* ALIAS: !_ch2.lc_enabled || _ch2.lc_counter!=0*/ )
            {
              if ( (_ch2.dc_pos+= 12) >= 96 ) _ch2.dc_pos-= 96;
              _ch2.dc_out= _duty_pat[_ch2.dc_wave_pattern][_ch2.dc_pos];
              vol= _ch2.dc_out ? _ch2.ve_vol : 0x0;
            }
        }
      else --_ch2.pt_counter;
      // Freqüències inaudibles.
      if ( _ch2.pt_freq >= 0x7FA ) vol= 0x0;
      buffer[i]= vol;
    }
  
} /* end render_ch2 */


static void
render_ch3 (
            GBCu8     buffer[GBC_APU_BUFFER_SIZE],
            const int begin,
            const int end
            )
{
  
  int div= 0;
  int i;
  GBCu8 vol;
  
  
  vol= _ch3.enabled ? (_ch3.ram[_ch3.su_pos]>>_ch3.su_val) : 0x0;
  for ( i= begin; i < end; ++i )
    {
      /* Length Counter. */
      if ( --_ch3.lc_aux_div == 0 )
        {
          _ch3.lc_aux_div= LC_AUX_DIV;
          if ( _ch3.lc_enabled && _ch3.lc_counter != 0 )
            {
              if ( --_ch3.lc_counter == 0 )
        	{
        	  _ch3.enabled= GBC_FALSE;
        	  vol= 0x0;
        	}
            }
        }
      /* Línia principal. */
      for ( div= 0; div < 2; ++div)
        {
          if ( _ch3.pt_counter == 0 )
            {
              _ch3.pt_counter= 2048 - _ch3.pt_freq;
              if ( _ch3.enabled )
        	{
        	  if ( ++_ch3.su_pos == 32 ) _ch3.su_pos= 0;
        	  vol= (_ch3.ram[_ch3.su_pos]>>_ch3.su_val);
        	}
            }
          else --_ch3.pt_counter;
        }
      // Freqüències inaudibles.
      if ( _ch3.pt_freq >= 0x7FA ) vol= 0x0;
      buffer[i]= vol;
    }
  
} /* end render_ch3 */


static void
render_ch4 (
            GBCu8     buffer[GBC_APU_BUFFER_SIZE],
            const int begin,
            const int end
            )
{
  
  int i;
  GBCu8 vol;
  GBCu16 xor;
  
  
  vol= (_ch4.enabled && _ch4.pr_out) ? _ch4.ve_vol : 0x0;
  for ( i= begin; i < end; ++i )
    {
      /* Length Counter i Envelope. */
      if ( --_ch4.lc_aux_div == 0 )
        {
          _ch4.lc_aux_div= LC_AUX_DIV;
          if ( _ch4.lc_enabled && _ch4.lc_counter != 0 )
            {
              if ( --_ch4.lc_counter == 0 )
                {
                  _ch4.enabled= GBC_FALSE;
                  vol= 0x0;
                }
            }
          /* Envelope. */
          if ( --_ch4.ve_aux_div == 0 )
            {
              _ch4.ve_aux_div= 4; /* 256/4 -> 64 Hz. */
              if ( _ch4.ve_counter != 0 )
                {
                  if ( --_ch4.ve_counter == 0 )
                    {
                      _ch4.ve_counter= _ch4.ve_step;
                      if ( _ch4.ve_increase )
                        {
                          if ( _ch4.ve_vol != 0xF ) ++_ch4.ve_vol;
                        }
                      else
                        {
                          if ( _ch4.ve_vol != 0x0 ) --_ch4.ve_vol;
                        }
                    }
                  vol= (_ch4.enabled && _ch4.pr_out) ? _ch4.ve_vol : 0x0;
                }
            }
        }
      /* Línia principal. */
      if ( --_ch4.ct_3bcounter == 0 )
        {
          _ch4.ct_3bcounter= (_ch4.ct_ratio==0) ? 1 : 2*_ch4.ct_ratio;
          if ( --_ch4.ct_16bcounter == 0 )
            {
              _ch4.ct_16bcounter= 2<<_ch4.ct_scfreq;
              _ch4.pr_out= (_ch4.pr_prng&0x1)^0x1;
              xor= (_ch4.pr_prng^(_ch4.pr_prng>>1))&0x1;
              _ch4.pr_prng= (_ch4.pr_prng>>1)|(xor<<14);
              if ( !_ch4.pr_mode15b )
        	_ch4.pr_prng= (_ch4.pr_prng&0x7FBF) | (xor<<6);
              vol= (_ch4.enabled && _ch4.pr_out) ? _ch4.ve_vol : 0x0;
            }
        }
      buffer[i]= vol;
    }
  
} /* end render_ch4 */


static void
join_channels (
               int     mask,
               double *channel,
               double  master_vol
               )
{
  
  int sel, i, j;
  const GBCu8 *buffer;
  
  
  for ( i= 0; i < GBC_APU_BUFFER_SIZE; ++i ) channel[i]= 0.0;
  for ( sel= 0x1, j= 0; j < 4; sel<<= 1, ++j )
    if ( mask&sel )
      {
        buffer= _buffer[j];
        for ( i= 0; i < GBC_APU_BUFFER_SIZE; ++i )
          channel[i]+= master_vol*_volume_table[buffer[i]];
      }
  
} // end join_channels


static void
run (
     const int begin,
     const int end
     )
{

  double master_vol_l,master_vol_r;

  
  if ( _sound_on && !_stop )
    {
      render_ch1 ( _buffer[0], begin, end );
      render_ch2 ( _buffer[1], begin, end );
      render_ch3 ( _buffer[2], begin, end );
      render_ch4 ( _buffer[3], begin, end );
    }
  else
    {
      render_off ( _buffer[0], begin, end );
      render_off ( _buffer[1], begin, end );
      render_off ( _buffer[2], begin, end );
      render_off ( _buffer[3], begin, end );
    }
  
  if ( end == GBC_APU_BUFFER_SIZE )
    {
      master_vol_l= ((_vin>>4)&0x7)/7.0;
      master_vol_r= (_vin&0x7)/7.0;
      join_channels ( _left_mask, _left, master_vol_l );
      join_channels ( _right_mask, _right, master_vol_r );
      _play_sound ( _left, _right, _udata );
    }
  
} // end run


static void
clock (void)
{
  
  int npos;
  
  
  /*
   * NOTA: 32 cicles de UCP és una mostra.
   */
  _timing.cctoFrame-= _timing.cc;
  npos= _timing.pos + _timing.cc/4;
  _timing.cc%= 4;
  _timing.cctoFrame+= _timing.cc;
  while ( npos >= GBC_APU_BUFFER_SIZE )
    {
      run ( _timing.pos, GBC_APU_BUFFER_SIZE );
      npos-= GBC_APU_BUFFER_SIZE;
      _timing.pos= 0;
    }
  run ( _timing.pos, npos );
  _timing.pos= npos;
  if ( _timing.cctoFrame <= 0 )
    _timing.cctoFrame= (GBC_APU_BUFFER_SIZE-_timing.pos)*4;
  
} /* end clock */


static void
init_duty_pat (void)
{
  
  int i, j, k, p;
  char val;
  
  
  for ( i= 0; i < 4; ++i )
    for ( j= p= 0; j < 8; ++j )
      {
        val= DUTY_PAT[i][j];
        for ( k= 0; k < 12; ++k, ++p )
          _duty_pat[i][p]= val;
      }
  
} /* end init_duty_pat */




/**********************/
/* FUNCIONS PÚBLIQUES */
/**********************/

void
GBC_apu_ch1_freq_hi (
        	     GBCu8 data
        	     )
{
  
  GBC_Bool prev_lc;
  
  
  if ( !_sound_on ) return;
  clock ();
  
  /* NOTA: L'ordre és important. */
  _ch1.pt_freq&= 0x0FF;
  _ch1.pt_freq|= ((GBCu16) (data&0x7))<<8;
  /* Counter/consecutive selection. */
  prev_lc= _ch1.lc_enabled;
  _ch1.lc_enabled= ((data&0x40)!=0);
  if ( !prev_lc && _ch1.lc_enabled ) LC_INIT_CLOCK ( _ch1 );
  if ( data&0x80 ) /* INIT */
    {
      _ch1.pt_counter= 2048 - _ch1.pt_freq;
      if ( _ch1.mccswitch ) _ch1.enabled= GBC_TRUE;
      if ( _ch1.lc_counter == 0 )
        {
          _ch1.lc_counter= 64;
          if ( _ch1.lc_enabled ) LC_INIT_CLOCK ( _ch1 );
        }
      _ch1.sw_freq= _ch1.pt_freq;
      _ch1.sw_enabled= _ch1.sw_time != 0 || _ch1.sw_shift != 0;
      _ch1.sw_counter= _ch1.sw_time;
      if ( _ch1.sw_counter == 0 ) _ch1.sw_counter= 8;
      if ( _ch1.sw_shift != 0 ) calc_sweep ( true );
      _ch1.dc_pos= 96-8; /* 1/12*8 steps de delay. */
      _ch1.dc_out= _duty_pat[_ch1.dc_wave_pattern][_ch1.dc_pos];
      _ch1.ve_counter= _ch1.ve_step;
      _ch1.ve_vol= _ch1.ve_vol_reg;
      _ch1.ve_increase= _ch1.ve_increase_reg;
    }
  
} /* end GBC_apu_ch1_freq_hi */


void
GBC_apu_ch1_freq_lo (
        	     GBCu8 data
        	     )
{
  
  if ( !_sound_on ) return;
  clock ();
  
  _ch1.pt_freq&= 0x700;
  _ch1.pt_freq|= data;
  
} /* end GBC_apu_ch1_freq_lo */


GBCu8
GBC_apu_ch1_get_lc_status (void)
{

  GBCu8 ret;

  
  clock ();
  
  ret= 0xBF | (_ch1.lc_enabled ? 0x40 : 0x00);
  
  return ret;
  
} // end GBC_apu_ch1_get_lc_status


GBCu8
GBC_apu_ch1_get_wave_pattern_duty (void)
{

  GBCu8 ret;

  
  // Estos valors no es poden modificar durant l'execució, per tant no
  // faig clock.
  ret= (_ch1.dc_wave_pattern<<6) | 0x3F;
  
  return ret;
  
} // end GBC_apu_ch1_get_wave_pattern_duty


void
GBC_apu_ch1_set_length_wave_pattern_dutty (
        				   GBCu8 data
        				   )
{
  
  if ( !_sound_on ) return;
  clock ();
  
  _ch1.dc_wave_pattern= data>>6;
  _ch1.dc_out= _duty_pat[_ch1.dc_wave_pattern][_ch1.dc_pos];
  _ch1.lc_counter= 64 - (data&0x3F);
  
} /* end GBC_apu_ch1_set_length_wave_patter_duty */


GBCu8
GBC_apu_ch1_sweep_read (void)
{

  GBCu8 ret;

  
  // Els valors d'aquest registre no es poden modificar durant el
  // renderitzat, per tant no cal fer clock.
  ret= 0x80 | (_ch1.sw_time<<4) |
    (_ch1.sw_increase?0x00:0x08) | _ch1.sw_shift;
  
  return ret;
  
} // end GBC_apu_ch1_sweep_read


void
GBC_apu_ch1_sweep_write (
        		 GBCu8 data
        		 )
{
  
  GBC_Bool old_increase;
  
  
  if ( !_sound_on ) return;
  clock ();
  
  _ch1.sw_time= (data>>4)&0x7;
  old_increase= _ch1.sw_increase;
  _ch1.sw_increase= ((data&0x08)==0);
  _ch1.sw_shift= data&0x7;
  if ( !old_increase && _ch1.sw_increase && _ch1.sw_neg_used )
    _ch1.enabled= GBC_FALSE;
  _ch1.sw_neg_used= GBC_FALSE;
  
} /* end GBC_apu_ch1_sweep_write */


GBCu8
GBC_apu_ch1_volume_envelope_read (void)
{

  GBCu8 ret;

  
  // Estos valors no es modifiquen en el renderitzat i per tant no
  // faig clock.
  ret= (_ch1.ve_vol_reg<<4) | (_ch1.ve_increase_reg?0x08:0x00) | _ch1.ve_step;
  
  return ret;
  
} // end GBC_apu_ch1_volume_envelope_read


void
GBC_apu_ch1_volume_envelope_write (
        			   GBCu8 data
        			   )
{
  
  if ( !_sound_on ) return;
  clock ();
  
  _ch1.ve_vol_reg= data>>4;
  _ch1.mccswitch= ((data&0xF8)!=0);
  if ( !_ch1.mccswitch ) _ch1.enabled= GBC_FALSE;
  _ch1.ve_increase_reg= ((data&0x08)!=0);
  _ch1.ve_step= data&0x7;
  //_ch1.mccswitch= _ch1.ve_vol_reg!=0 && _ch1.ve_step!=0;
  //if ( !_ch1.mccswitch ) _ch1.enabled= GBC_FALSE;
  
} /* end GBC_apu_ch1_volume_envelope_write */


void
GBC_apu_ch2_freq_hi (
        	     GBCu8 data
        	     )
{
  
  GBC_Bool prev_lc;
  
  
  if ( !_sound_on ) return;
  clock ();
  
  /* NOTA: L'ordre és important. */
  _ch2.pt_freq&= 0x0FF;
  _ch2.pt_freq|= ((GBCu16) (data&0x7))<<8;
  /* Counter/consecutive selection. */
  prev_lc= _ch2.lc_enabled;
  _ch2.lc_enabled= ((data&0x40)!=0);
  if ( !prev_lc && _ch2.lc_enabled ) LC_INIT_CLOCK ( _ch2 );
  if ( data&0x80 ) /* INIT */
    {
      _ch2.pt_counter= 2048 - _ch2.pt_freq;
      if ( _ch2.mccswitch ) _ch2.enabled= GBC_TRUE;
      if ( _ch2.lc_counter == 0 )
        {
          _ch2.lc_counter= 64;
          if ( _ch2.lc_enabled ) LC_INIT_CLOCK ( _ch2 );
        }
      _ch2.dc_pos= 96-8; /* 1/12*8 steps de delay. */
      _ch2.dc_out= _duty_pat[_ch2.dc_wave_pattern][_ch2.dc_pos];
      _ch2.ve_counter= _ch2.ve_step;
      _ch2.ve_vol= _ch2.ve_vol_reg;
      _ch2.ve_increase= _ch2.ve_increase_reg;
    }
  
} /* end GBC_apu_ch2_freq_hi */


void
GBC_apu_ch2_freq_lo (
        	     GBCu8 data
        	     )
{
  
  if ( !_sound_on ) return;
  clock ();
  
  _ch2.pt_freq&= 0x700;
  _ch2.pt_freq|= data;
  
} /* end GBC_apu_ch2_freq_lo */


GBCu8
GBC_apu_ch2_get_lc_status (void)
{
  
  clock ();
  
  return 0xBF | (_ch2.lc_enabled ? 0x40 : 0x00);
  
} /* end GBC_apu_ch2_get_lc_status */


GBCu8
GBC_apu_ch2_get_wave_pattern_duty (void)
{
  
  /* Estos valors no es poden modificar durant l'execució, per tant no
     faig clock. */
  
  return (_ch2.dc_wave_pattern<<6) | 0x3F;
  
} /* end GBC_apu_ch2_get_wave_pattern_duty */


void
GBC_apu_ch2_set_length_wave_pattern_dutty (
        				   GBCu8 data
        				   )
{
  
  if ( !_sound_on ) return;
  clock ();
  
  _ch2.dc_wave_pattern= data>>6;
  _ch2.dc_out= _duty_pat[_ch2.dc_wave_pattern][_ch2.dc_pos];
  _ch2.lc_counter= 64 - (data&0x3F);
  
} /* end GBC_apu_ch2_set_length_wave_patter_duty */


GBCu8
GBC_apu_ch2_volume_envelope_read (void)
{
  
  /* Estos valors no es modifiquen en el renderitzat i per tant no
     faig clock. */
  return (_ch2.ve_vol_reg<<4) | (_ch2.ve_increase_reg?0x08:0x00) | _ch2.ve_step;
  
} /* end GBC_apu_ch2_volume_envelope_read */


void
GBC_apu_ch2_volume_envelope_write (
        			   GBCu8 data
        			   )
{
  
  if ( !_sound_on ) return;
  clock ();
  
  _ch2.ve_vol_reg= data>>4;
  _ch2.mccswitch= ((data&0xF8)!=0);
  if ( !_ch2.mccswitch ) _ch2.enabled= GBC_FALSE;
  _ch2.ve_increase_reg= ((data&0x08)!=0);
  _ch2.ve_step= data&0x7;
  //_ch2.mccswitch= _ch2.ve_vol_reg!=0 && _ch2.ve_step!=0;
  //if ( !_ch2.mccswitch ) _ch2.enabled= GBC_FALSE;
  
} // end GBC_apu_ch2_volume_envelope_write


void
GBC_apu_ch3_freq_hi (
        	     GBCu8 data
        	     )
{
  
  GBC_Bool prev_lc;
  
  
  if ( !_sound_on ) return;
  clock ();
  
  /* NOTA: L'ordre és important. */
  _ch3.pt_freq&= 0x0FF;
  _ch3.pt_freq|= ((GBCu16) (data&0x7))<<8;
  /* Counter/consecutive selection. */
  prev_lc= _ch3.lc_enabled;
  _ch3.lc_enabled= ((data&0x40)!=0);
  if ( !prev_lc && _ch3.lc_enabled ) LC_INIT_CLOCK ( _ch3 );
  if ( data&0x80 ) /* INIT */
    {
      _ch3.su_pos=0;
      _ch3.pt_counter= 2048 - _ch3.pt_freq;
      if ( _ch3.mccswitch ) _ch3.enabled= GBC_TRUE;
      if ( _ch3.lc_counter == 0 )
        {
          _ch3.lc_counter= 256;
          if ( _ch3.lc_enabled ) LC_INIT_CLOCK ( _ch3 );
        }
    }
  
} /* end GBC_apu_ch3_freq_hi */


void
GBC_apu_ch3_freq_lo (
        	     GBCu8 data
        	     )
{
  
  if ( !_sound_on ) return;
  clock ();
  
  _ch3.pt_freq&= 0x700;
  _ch3.pt_freq|= data;
  
} /* end GBC_apu_ch3_freq_lo */


GBCu8
GBC_apu_ch3_get_lc_status (void)
{
  
  clock ();
  
  return 0xBF | (_ch3.lc_enabled ? 0x40 : 0x00);
  
} /* end GBC_apu_ch3_get_lc_status */


GBCu8
GBC_apu_ch3_output_level_read (void)
{
  
  switch ( _ch3.su_val )
    {
    case 0: return 0xBF;
    case 1: return 0xDF;
    case 2: return 0xFF;
    case 4: return 0x9F;
    default: return 0xFF; /* CALLA!. */
    }
  
} /* end GBC_apu_ch3_output_level_read */


void
GBC_apu_ch3_output_level_write (
        			GBCu8 data
        			)
{
  
  if ( !_sound_on ) return;
  clock ();
  
  switch ( (data>>5)&0x3 )
    {
    case 0: _ch3.su_val= 4; break;
    case 1: _ch3.su_val= 0; break;
    case 2: _ch3.su_val= 1; break;
    case 3: _ch3.su_val= 2; break;
    }
  
} /* end GBC_apu_ch3_output_level_write */


GBCu8
GBC_apu_ch3_ram_read (
        	      int pos
        	      )
{
  
  int aux;
  
  
  clock ();
  
  aux= _ch3.enabled ? (_ch3.su_pos&0xFE) : pos<<1;
  
  return (((GBCu8) _ch3.ram[aux])<<4) | ((GBCu8) _ch3.ram[aux|1]);
  
} /* end GBC_apu_ch3_ram_read */


void
GBC_apu_ch3_ram_write (
        	       GBCu8 data,
        	       int   pos
        	       )
{
  
  int aux;
  
  
  if ( !_sound_on ) return;
  clock ();
  
  aux= _ch3.enabled ? (_ch3.su_pos&0xFE) : pos<<1;
  _ch3.ram[aux]= data>>4;
  _ch3.ram[aux|1]= data&0xF;
  
} /* end GBC_apu_ch3_ram_write */


void
GBC_apu_ch3_set_length (
        		GBCu8 data
        		)
{
  
  if ( !_sound_on ) return;
  clock ();
  
  _ch3.lc_counter= 256 - (GBCu16) data;
  
} /* end GBC_apu_ch3_set_length */


GBCu8
GBC_apu_ch3_sound_on_off_read (void)
{
  return _ch3.mccswitch ? 0xFF : 0x7F;
} /* end GBC_apu_ch3_sound_on_off_read */


void
GBC_apu_ch3_sound_on_off_write (
        			GBCu8 data
        			)
{
  
  if ( !_sound_on ) return;
  clock ();

  _ch3.mccswitch= ((data&0x80)!=0);
  if ( !_ch3.mccswitch ) _ch3.enabled= GBC_FALSE;
  
} /* end GBC_apu_ch3_sound_on_off_write */


GBCu8
GBC_apu_ch4_get_lc_status (void)
{
  
  clock ();
  
  return 0xBF | (_ch4.lc_enabled ? 0x40 : 0x00);
  
} /* end GBC_apu_ch4_get_lc_status */


void
GBC_apu_ch4_init (
        	  GBCu8 data
        	  )
{
  
  GBC_Bool prev_lc;
  
  
  if ( !_sound_on ) return;
  clock ();
  
  /* NOTA: L'ordre és important. */
  /* Counter/consecutive selection. */
  prev_lc= _ch4.lc_enabled;
  _ch4.lc_enabled= ((data&0x40)!=0);
  if ( !prev_lc && _ch4.lc_enabled ) LC_INIT_CLOCK ( _ch4 );
  if ( data&0x80 ) /* INIT */
    {
      if ( _ch4.mccswitch ) _ch4.enabled= GBC_TRUE;
      if ( _ch4.lc_counter == 0 )
        {
          _ch4.lc_counter= 64;
          if ( _ch4.lc_enabled ) LC_INIT_CLOCK ( _ch4 );
        }
      _ch4.ct_3bcounter= (_ch4.ct_ratio==0) ? 1 : 2*_ch4.ct_ratio;
      _ch4.ct_16bcounter= 2<<_ch4.ct_scfreq;
      _ch4.ve_counter= _ch4.ve_step;
      _ch4.ve_vol= _ch4.ve_vol_reg;
      _ch4.ve_increase= _ch4.ve_increase_reg;
      _ch4.pr_prng= 0x7FFF;
      _ch4.pr_out= 0;
    }
  
} /* end GBC_apu_ch4_init */


GBCu8
GBC_apu_ch4_polynomial_counter_read (void)
{
  return (_ch4.ct_scfreq<<4) | (_ch4.pr_mode15b?0x00:0x08) | _ch4.ct_ratio;
} /* end GBC_apu_ch4_polynomial_counter_read */


void
GBC_apu_ch4_polynomial_counter_write (
        			      GBCu8 data
        			      )
{
  
  if ( !_sound_on ) return;
  clock ();
  
  _ch4.ct_scfreq= data>>4;
  _ch4.pr_mode15b= ((data&0x08)==0);
  _ch4.ct_ratio= data&0x7;
  
} /* end GBC_apu_ch4_polynomial_counter_write */


void
GBC_apu_ch4_set_length (
        		GBCu8 data
        		)
{
  
  if ( !_sound_on ) return;
  clock ();
  
  _ch4.lc_counter= 64 - (data&0x3F);
  
} /* end GBC_apu_ch4_set_length */


GBCu8
GBC_apu_ch4_volume_envelope_read (void)
{
  
  /* Estos valors no es modifiquen en el renderitzat i per tant no
     faig clock. */
  return (_ch4.ve_vol_reg<<4) | (_ch4.ve_increase_reg?0x08:0x00) | _ch4.ve_step;
  
} /* end GBC_apu_ch4_volume_envelope_read */


void
GBC_apu_ch4_volume_envelope_write (
        			   GBCu8 data
        			   )
{
  
  if ( !_sound_on ) return;
  clock ();
  
  _ch4.ve_vol_reg= data>>4;
  _ch4.mccswitch= ((data&0xF8)!=0);
  if ( !_ch4.mccswitch ) _ch4.enabled= GBC_FALSE;
  _ch4.ve_increase_reg= ((data&0x08)!=0);
  _ch4.ve_step= data&0x7;
  //_ch4.mccswitch= _ch4.ve_vol_reg!=0 && _ch4.ve_step!=0;
  //if ( !_ch4.mccswitch ) _ch4.enabled= GBC_FALSE;
  
} // end GBC_apu_ch4_volume_envelope_write


void
GBC_apu_clock (
               const int cc
               )
{
  
  if ( (_timing.cc+= cc) >= _timing.cctoFrame )
    clock ();
  
} /* end GBC_apu_clock */


GBCu8
GBC_apu_get_status (void)
{
  
  clock ();
  
  return
    0x70 |
    (_sound_on?0x80:0x00) |
    (_ch4.enabled?0x08:0x00) |
    (_ch3.enabled?0x04:0x00) |
    (_ch2.enabled?0x02:0x00) |
    (_ch1.enabled?0x01:0x00);
  
} /* end GBC_apu_get_status */


void
GBC_apu_init (
              GBC_PlaySound *play_sound,
              void          *udata
              )
{
  
  init_duty_pat ();
  _play_sound= play_sound;
  _udata= udata;
  GBC_apu_init_state ();
  
} /* end GBC_apu_init */


void
GBC_apu_init_state (void)
{
  
  int i;
  
  
  /* Canal 1. */
  _ch1.enabled= GBC_FALSE;
  _ch1.pt_freq= 0x0000;
  _ch1.pt_counter= 0;
  _ch1.mccswitch= GBC_FALSE;
  _ch1.lc_aux_div= 4096; /* 4096 samples correpson amb 256Hz. */
  _ch1.lc_counter= 0;
  _ch1.lc_enabled= GBC_FALSE;
  _ch1.sw_aux_div= 2;
  _ch1.sw_enabled= GBC_FALSE;
  _ch1.sw_freq= 0x0000;
  _ch1.sw_time= 0x00;
  _ch1.sw_counter= 0x00;
  _ch1.sw_increase= GBC_TRUE;
  _ch1.sw_shift= 0x00;
  _ch1.sw_neg_used= GBC_FALSE;
  _ch1.dc_wave_pattern= 0;
  _ch1.dc_pos= 0;
  _ch1.dc_out= _duty_pat[_ch1.dc_wave_pattern][_ch1.dc_pos];
  _ch1.ve_aux_div= 2; /* Per a dividir sw_aux_div (64 Hz). */
  _ch1.ve_vol_reg= 0x0;
  _ch1.ve_vol= 0x0;
  _ch1.ve_increase_reg= GBC_FALSE;
  _ch1.ve_increase= GBC_FALSE;
  _ch1.ve_step= 0x00;
  _ch1.ve_counter= 0x00;
  
  /* Canal 2. */
  _ch2.enabled= GBC_FALSE;
  _ch2.mccswitch= GBC_FALSE;
  _ch2.pt_freq= 0x0000;
  _ch2.pt_counter= 0;
  _ch2.lc_aux_div= 4096; /* 4096 samples correpsona bm 256Hz. */
  _ch2.lc_counter= 0;
  _ch2.lc_enabled= GBC_FALSE;
  _ch2.dc_wave_pattern= 0;
  _ch2.dc_pos= 0;
  _ch2.dc_out= _duty_pat[_ch2.dc_wave_pattern][_ch2.dc_pos];
  _ch2.ve_aux_div= 4; /* Per a dividir lc_aux_div (64 Hz). */
  _ch2.ve_vol_reg= 0x0;
  _ch2.ve_vol= 0x0;
  _ch2.ve_increase_reg= GBC_FALSE;
  _ch2.ve_increase= GBC_FALSE;
  _ch2.ve_step= 0x00;
  _ch2.ve_counter= 0x00;
  
  /* Canal 3. */
  _ch3.enabled= GBC_FALSE;
  _ch3.mccswitch= GBC_FALSE;
  _ch3.pt_freq= 0x0000;
  _ch3.pt_counter= 0;
  _ch3.lc_aux_div= 4096; /* 4096 samples correpson amb 256Hz. */
  _ch3.lc_counter= 0;
  _ch3.lc_enabled= GBC_FALSE;
  for ( i= 0; i < 32; ++i )
    _ch3.ram[i]= (i&0x2) ? 0xF : 0x0;
  _ch3.su_pos= 0;
  _ch3.su_val= 4;
  
  /* Canal 4. */
  _ch4.enabled= GBC_FALSE;
  _ch4.ct_3bcounter= 0x1;
  _ch4.ct_16bcounter= 0x1;
  _ch4.ct_ratio= 0x00;
  _ch4.ct_scfreq= 0x00;
  _ch4.mccswitch= GBC_FALSE;
  _ch4.lc_aux_div= 4096; /* 4096 samples correpsona bm 256Hz. */
  _ch4.lc_counter= 0;
  _ch4.lc_enabled= GBC_FALSE;
  _ch4.ve_aux_div= 4; /* Per a dividir lc_aux_div (64 Hz). */
  _ch4.ve_vol_reg= 0x0;
  _ch4.ve_vol= 0x0;
  _ch4.ve_increase_reg= GBC_FALSE;
  _ch4.ve_increase= GBC_FALSE;
  _ch4.ve_step= 0x00;
  _ch4.ve_counter= 0x00;
  _ch4.pr_prng= 0x0001;
  _ch4.pr_out= 1;
  _ch4.pr_mode15b= GBC_TRUE;
  
  /* Estat so. */
  _sound_on= GBC_FALSE;
  _stop= GBC_FALSE;
  
  /* Buffers. */
  for ( i= 0; i < 4; ++i )
    memset ( _buffer[i], 0, GBC_APU_BUFFER_SIZE );
  
  /* Timing. */
  _timing.pos= 0;
  _timing.cc= 0;
  _timing.cctoFrame= GBC_APU_BUFFER_SIZE*4;
  
  /* Buffers d'eixida. */
  memset ( _left, 0, sizeof(_left) );
  memset ( _right, 0, sizeof(_right) );
  
  /* Màscares. */
  _left_mask= 0xf;
  _right_mask= 0xf;
  
  _vin= 0x00;
  
} /* end GBC_apu_init_state */


void
GBC_apu_power_up (void)
{
  
  /* Canal 1. */
  /* --> FF10= 0x80. Igual que init. */
  /* --> FF11= 0xBF. dc_pos= 2. */
  _ch1.dc_wave_pattern= 2;
  _ch1.dc_out= _duty_pat[_ch1.dc_wave_pattern][_ch1.dc_pos];
  /* --> FF12= 0xF3. Volumen i pas. */
  _ch1.ve_vol_reg= 0xF;
  _ch1.mccswitch= GBC_TRUE;
  _ch1.ve_step= 0x3;
  /* --> FF14= 0xBF. Length counter parat. */
  _ch1.lc_enabled= GBC_TRUE;
  
  /* Canal 2. */
  /* --> FF16= 0x3F. Igual. */
  /* --> FF17= 0x00. Igual. */
  /* --> FF19= 0xBF. Length counter parat. */
  _ch2.lc_enabled= GBC_TRUE;
  
  /* Canal 3. */
  /* --> FF1A= 0x7F. Igual. */
  /* --> FF1B= 0xFF. Igual. */
  /* --> FF1C= 0x9F. Igual. */
  /* --> FF1E= 0xBF. Length counter parat. */
  _ch3.lc_enabled= GBC_TRUE;
  
  /* Canal 4. */
  /* --> FF20= 0xFF. Igual. */
  /* --> FF21= 0x00. Igual. */
  /* --> FF22= 0x00. Igual. */
  /* --> FF23= 0xBF. Length counter parat. */
  _ch4.lc_enabled= GBC_TRUE;
  
  /* Global. */
  /* --> FF24= 0x77. */
  _vin= 0x77;
  /* --> FF25= 0xF3. Canvis en la màscara dreta. */
  _right_mask= 0x3;
  /* --> FF26= 0xF1. So actiu i canal 1. */
  _sound_on= GBC_TRUE;
  _ch1.enabled= GBC_TRUE;
  
} /* end GBC_apu_power_up */


GBCu8
GBC_apu_select_out_read (void)
{
  return (_left_mask<<4) | _right_mask;
} /* end GBC_apu_select_out_read */


void
GBC_apu_select_out_write (
        		  GBCu8 data
        		  )
{
  
  /* SO1 -> right; SO2 -> left. */
  if ( !_sound_on ) return;
  clock ();
  _left_mask= data>>4;
  _right_mask= data&0xF;
  
} /* end GBC_apu_select_out_write */


void
GBC_apu_stop (
              const GBC_Bool state
              )
{
  
  clock ();
  _stop= state;
  
} /* end GBC_apu_stop */


void
GBC_apu_turn_on (
        	 GBCu8 data
        	 )
{
  
  GBC_Bool old;
  
  
  clock ();
  
  old= _sound_on;
  _sound_on= (data&0x80)!=0;
  if ( old == _sound_on ) return;
  if ( _sound_on )
    {
      _ch1.pt_freq= 2048;
      _ch1.lc_aux_div= TURNON_LC_AUX_DIV;
      _ch1.sw_aux_div= 2;
      _ch1.ve_aux_div= 2;
      _ch2.pt_freq= 2048;
      _ch2.lc_aux_div= TURNON_LC_AUX_DIV;
      _ch2.ve_aux_div= 4;
      _ch3.pt_freq= 2048;
      _ch3.lc_aux_div= TURNON_LC_AUX_DIV;
      _ch4.lc_aux_div= TURNON_LC_AUX_DIV;
      _ch4.ve_aux_div= 4;
      _ch4.ct_3bcounter= 1;
      _ch4.ct_16bcounter= 2;
    }
  else
    {
      /* Reinicialitza tots els registres que poden fer so o que són
         accesibles des de fora. */
      /* CH1 */
      _ch1.enabled= GBC_FALSE;
      _ch1.mccswitch= GBC_FALSE;
      _ch1.lc_counter= 0;
      _ch1.lc_enabled= GBC_FALSE;
      _ch1.sw_enabled= GBC_FALSE;
      _ch1.sw_time= 0;
      _ch1.sw_increase= GBC_TRUE;
      _ch1.sw_shift= 0;
      _ch1.sw_neg_used= GBC_FALSE;
      _ch1.dc_wave_pattern= 0;
      _ch1.dc_out= _duty_pat[_ch1.dc_wave_pattern][_ch1.dc_pos];
      _ch1.ve_vol_reg= 0x0;
      _ch1.ve_vol= 0x0;
      _ch1.ve_increase_reg= GBC_FALSE;
      _ch1.ve_step= 0x00;
      _ch1.ve_counter= 0x00;
      /* CH2 */
      _ch2.enabled= GBC_FALSE;
      _ch2.mccswitch= GBC_FALSE;
      _ch2.lc_counter= 0;
      _ch2.lc_enabled= GBC_FALSE;
      _ch2.dc_wave_pattern= 0;
      _ch2.dc_out= _duty_pat[_ch2.dc_wave_pattern][_ch2.dc_pos];
      _ch2.ve_vol_reg= 0x0;
      _ch2.ve_vol= 0x0;
      _ch2.ve_increase_reg= GBC_FALSE;
      _ch2.ve_step= 0x00;
      _ch2.ve_counter= 0x00;
      /* CH3 */
      _ch3.enabled= GBC_FALSE;
      _ch3.mccswitch= GBC_FALSE;
      _ch3.lc_counter= 0;
      _ch3.lc_enabled= GBC_FALSE;
      _ch3.su_val= 4;
      /* CH4 */
      _ch4.enabled= GBC_FALSE;
      _ch4.mccswitch= GBC_FALSE;
      _ch4.lc_counter= 0;
      _ch4.lc_enabled= GBC_FALSE;
      _ch4.ve_vol_reg= 0x0;
      _ch4.ve_vol= 0x0;
      _ch4.ve_increase_reg= GBC_FALSE;
      _ch4.ve_step= 0x00;
      _ch4.ve_counter= 0x00;
      _ch4.ct_scfreq= 0x00;
      _ch4.pr_mode15b= GBC_TRUE;
      _ch4.ct_ratio= 0x00;
      /* VIN. */
      _vin= 0x00;
      /* SELECT MASKS. */
      _left_mask= 0x00;
      _right_mask= 0x00;
    }
  
} /* end GBC_apu_turn_on */


GBCu8
GBC_apu_vin_read (void)
{
  return _vin;
} /* end GBC_apu_vin_read */


void
GBC_apu_vin_write (
        	   GBCu8 data
        	   )
{
  
  if ( !_sound_on ) return;
  _vin= data;
  
} /* end GBC_apu_vin_write */


int
GBC_apu_save_state (
        	    FILE *f
        	    )
{

  SAVE ( _vin );
  SAVE ( _ch1 );
  SAVE ( _ch2 );
  SAVE ( _ch3 );
  SAVE ( _ch4 );
  SAVE ( _sound_on );
  SAVE ( _stop );
  SAVE ( _buffer );
  SAVE ( _timing );
  SAVE ( _left );
  SAVE ( _right );
  SAVE ( _left_mask );
  SAVE ( _right_mask );

  return 0;
  
} /* end GBC_apu_save_state */


int
GBC_apu_load_state (
        	    FILE *f
        	    )
{

  int i, j;

  
  LOAD ( _vin );
  LOAD ( _ch1 );
  CHECK ( _ch1.dc_pos >= 0 && _ch1.dc_pos < 96 );
  CHECK ( _ch1.dc_wave_pattern >= 0 && _ch1.dc_wave_pattern < 4 );
  CHECK ( _ch1.ve_vol >= 0 && _ch1.ve_vol <= 0xF );
  LOAD ( _ch2 );
  CHECK ( _ch2.dc_pos >= 0 && _ch2.dc_pos < 96 );
  CHECK ( _ch2.dc_wave_pattern >= 0 && _ch2.dc_wave_pattern < 4 );
  CHECK ( _ch2.ve_vol >= 0 && _ch2.ve_vol <= 0xF );
  LOAD ( _ch3 );
  for ( i= 0; i < 32; ++i )
    CHECK ( _ch3.ram[i] >= 0 && _ch3.ram[i] <= 0xF );
  CHECK ( _ch3.su_pos >= 0 && _ch3.su_pos < 32 );
  CHECK ( _ch3.su_val == 0 || _ch3.su_val == 1 ||
          _ch3.su_val == 2 || _ch3.su_val == 4 );
  LOAD ( _ch4 );
  CHECK ( _ch4.ve_vol >= 0 && _ch4.ve_vol <= 0xF );
  LOAD ( _sound_on );
  LOAD ( _stop );
  LOAD ( _buffer );
  for ( i= 0; i < 4; ++i )
    for ( j= 0; j < GBC_APU_BUFFER_SIZE; ++j )
      CHECK ( _buffer[i][j] >= 0 && _buffer[i][j] <= 0xF );
  LOAD ( _timing );
  CHECK ( _timing.pos >= 0 && _timing.pos < GBC_APU_BUFFER_SIZE );
  CHECK ( _timing.cc >= 0 );
  LOAD ( _left );
  for ( i= 0; i < GBC_APU_BUFFER_SIZE; ++i )
    if ( _left[i] < 0.0 || _left[i] > 1.0 )
      return -1;
  LOAD ( _right );
  for ( i= 0; i < GBC_APU_BUFFER_SIZE; ++i )
    if ( _right[i] < 0.0 || _right[i] > 1.0 )
      return -1;
  LOAD ( _left_mask );
  LOAD ( _right_mask );
  
  return 0;
  
} /* end GBC_apu_load_state */
