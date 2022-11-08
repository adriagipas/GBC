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
 *  mapper.c - Implementació del mòdul encarregat del mapper.
 *
 *  NOTES: La implementació del rumble en el MBC5 segueix una
 *  implementació molt bàsica. Assumisc que l'estan emprant bé i que
 *  el valor de rumble és actualitzat 60 vegades per segon, al voltant
 *  de una vegada cada 69905 cicles. El que faré és ficar un valor
 *  màxim i mínim de cicles, si està per davall igonre
 *  l'actualització, si pasa molt de temps sense actualitzar-se el
 *  valor pasa a estar parat. Cada 3 frames de rumble actualitzaré
 *  l'estat, considerant 4 nivells de rumble, siguent el 0 parat.
 *
 */


#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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


#define RAM_BANK_SIZE 8192
#define RAM_NBANKS 16

#define RBL_MIN_CICLES 60000
#define RBL_MAX_CICLES 80000




/*********/
/* TIPUS */
/*********/

/* MBC1. */
typedef struct
{
  
  GBC_Bool     ram_2KB;
  GBCu8       *ram[4];
  GBCu16       nbanks_ram;
  GBCu8       *cram;          /* Bank actual. */
  GBC_Bool     ram_enabled;
  GBCu8        rom_num;
  const GBCu8 *rom0;
  const GBCu8 *rom1;
  GBCu8        low;
  GBCu8        high;
  GBC_Bool     mode0;
  
} mbc1_t;


// MBC2.
typedef struct
{
  
  GBCu8        *ram;
  GBC_Bool     ram_enabled;
  GBCu8        rom_num;
  const GBCu8 *rom0;
  const GBCu8 *rom1;
  
} mbc2_t;


typedef struct
{
  
  int      ss;
  int      mm;
  int      hh;
  int      dd;
  GBC_Bool carry;
  
} mbc3_time_t;


/* MBC3. */
typedef struct
{
  
  GBCu8       *ram[4];
  GBCu8       *cram;
  GBC_Bool     ram_enabled;
  enum {
    MBC3_MODE_RAM,
    MBC3_MODE_NONE,
    MBC3_MODE_RTC_S,
    MBC3_MODE_RTC_M,
    MBC3_MODE_RTC_H,
    MBC3_MODE_RTC_DL,
    MBC3_MODE_RTC_DH
  }            ram_mode;
  GBCu16       rom_num;
  const GBCu8 *rom0;
  const GBCu8 *rom1;
  mbc3_time_t  counters;
  mbc3_time_t  latch;
  GBC_Bool     latch_flag;
  clock_t      cc;
  clock_t      remaincc;
  GBC_Bool     timer_enabled;
  
} mbc3_t;


/* MBC5. */
typedef struct
{
  
  GBCu8       *ram[4];
  GBCu16       nbanks_ram;
  GBCu8       *cram;          /* Bank actual. */
  GBC_Bool     ram_enabled;
  GBCu16       rom_num;
  const GBCu8 *rom0;
  const GBCu8 *rom1;
  GBC_Bool     rumble;
  int          cc;
  int          rumble_level;    /* Últim nivell actiu. */
  int          rumble_state;
  int          rumble_nframes;
  
} mbc5_t;




/****************/
/* ESTAT PRIVAT */
/****************/

/* Callbacks. */
static GBC_MapperChanged *_mapper_changed;
static GBC_UpdateRumble *_update_rumble;
static GBC_GetExternalRAM *_get_external_ram;
static void *_udata;

/* RAM que no estàtica. */
static GBCu8 _ram[RAM_NBANKS][RAM_BANK_SIZE];


/* L'estat. */
struct
{
  
  const GBC_Rom *rom;      /* ROM */
  GBC_Mapper     mapper;
  union
  {
    mbc1_t mbc1;
    mbc2_t mbc2;
    mbc3_t mbc3;
    mbc5_t mbc5;
  }              s;
  
} _state;




/*********************/
/* FUNCIONS PRIVADES */
/*********************/

static GBCu8
read_ram_empty (
        	const GBCu16 addr
        	)
{
  return 0x00;
} /* end read_ream_empty */


static void
write_ram_empty (
        	 const GBCu16 addr,
        	 const GBCu8  data
        	 )
{
} /* end write_ram_empty */


static void
mapper_clock_empty (
        	    const int cc
        	    )
{
} /* end mapper_clock_empty */


static void
init_static_ram (void)
{
  memset ( &(_ram[0][0]), 0, RAM_NBANKS*RAM_BANK_SIZE );
} /* end init_static_ram */




/*******/
/* ROM */
/*******/

static GBCu8
read_rom (
          const GBCu16 addr
          )
{
  
  if ( addr < 0x4000 ) return _state.rom->banks[0][addr];
  else                 return _state.rom->banks[1][addr&0x3FFF];
  
} /* end read_rom */


static void
write_rom (
           const GBCu16 addr,
           const GBCu8  data
           )
{
} /* end write_rom */


static int
get_bank1_rom (void)
{
  return 1;
} /* end get_bank1_rom */


static GBC_Error
rom_init (void)
{
  
  if ( _state.rom->nbanks != 2  )
    return GBC_WRONGROMSIZE;
  
  GBC_mapper_read_ram= read_ram_empty;
  GBC_mapper_write_ram= write_ram_empty;
  GBC_mapper_read= read_rom;
  GBC_mapper_write= write_rom;
  GBC_mapper_get_bank1= get_bank1_rom;
  GBC_mapper_clock= mapper_clock_empty;
  
  return GBC_NOERROR;
  
} /* end rom_init */




/********/
/* MBC1 */
/********/

static void
mbc1_update_banks (void)
{
  
  GBCu8 ram_num;
  
  
  if ( _state.s.mbc1.mode0 )
    {
      _state.s.mbc1.rom_num=
        (_state.s.mbc1.high<<5) | _state.s.mbc1.low;
      if ( _state.s.mbc1.rom_num == 0x20 ) _state.s.mbc1.rom_num= 0x21;
      else if ( _state.s.mbc1.rom_num == 0x40 ) _state.s.mbc1.rom_num= 0x41;
      else if ( _state.s.mbc1.rom_num == 0x60 ) _state.s.mbc1.rom_num= 0x61;
      ram_num= 0x00;
    }
  else
    {
      _state.s.mbc1.rom_num= _state.s.mbc1.low;
      ram_num= _state.s.mbc1.high;
    }
  if ( _state.s.mbc1.rom_num == 0x0 ) _state.s.mbc1.rom_num= 0x1;
  _state.s.mbc1.rom1=
    &(_state.rom->banks[_state.s.mbc1.rom_num%_state.rom->nbanks][0]);
  if ( _state.s.mbc1.nbanks_ram > 0 )
    _state.s.mbc1.cram= _state.s.mbc1.ram[ram_num%_state.s.mbc1.nbanks_ram];
  
} /* end mbc1_update_banks */


static int
get_bank1_mbc1 (void)
{
  return _state.s.mbc1.rom_num;
} /* end get_bank1_mbc1 */


static GBCu8
read_ram_mbc1 (
               const GBCu16 addr
               )
{
  
  if ( !_state.s.mbc1.ram_enabled ) return 0xFF;
  if ( _state.s.mbc1.ram_2KB && addr >= 0x800 ) return 0xFF;
  
  return _state.s.mbc1.cram[addr];
  
} /* end read_ream_mbc1 */


static void
write_ram_mbc1 (
                const GBCu16 addr,
                const GBCu8  data
                )
{
  
  if ( !_state.s.mbc1.ram_enabled ) return;
  if ( _state.s.mbc1.ram_2KB && addr >= 0x800 ) return;
  _state.s.mbc1.cram[addr]= data;
  
} /* end write_ram_mbc1 */


static GBCu8
read_mbc1 (
           const GBCu16 addr
           )
{
  
  if ( addr < 0x4000 ) return _state.s.mbc1.rom0[addr];
  else                 return _state.s.mbc1.rom1[addr&0x3FFF];
  
} /* end read_mbc1 */


static void
write_mbc1 (
            const GBCu16 addr,
            const GBCu8  data
            )
{
  
  /* RAMG. */
  if ( addr < 0x2000 )
    _state.s.mbc1.ram_enabled= ((data&0x0A)==0xA);
  
  /* ROM Bank Number. */
  else if ( addr < 0x4000 )
    {
      _state.s.mbc1.low= data&0x1F;
      mbc1_update_banks ();
    }
  
  /* RAM Bank Number or Upper Bits of ROM Bank Number. */
  else if ( addr < 0x6000 )
    {
      _state.s.mbc1.high= data&0x3;
      mbc1_update_banks ();
    }
  
  /* ROM/RAM Mode Select. */
  else if ( addr < 0x8000 )
    {
      _state.s.mbc1.mode0= ((data&0x1)==0);
      mbc1_update_banks ();
    }
  
} /* end write_mbc1 */


static void
write_mbc1_trace (
                  const GBCu16 addr,
                  const GBCu8  data
                  )
{
  
  write_mbc1 ( addr, data );
  if ( addr >= 0x2000 && addr < 0x8000 )
    _mapper_changed ( _udata );
  
} /* end write_mbc1_trace */


static void
mbc1_init_ram (void)
{

  int i;
  GBCu8 *mem;

  
  if ( _state.mapper == GBC_MBC1_RAM )
    {
      init_static_ram ();
      mem= &(_ram[0][0]);
    }
  else mem= _get_external_ram ( _state.s.mbc1.ram_2KB ? 0x800 :
        			_state.s.mbc1.nbanks_ram*RAM_BANK_SIZE,
        			_udata );
  for ( i= 0; i < _state.s.mbc1.nbanks_ram; ++i, mem+= RAM_BANK_SIZE )
    _state.s.mbc1.ram[i]= mem;
  
} /* end mbc1_init_ram */


static GBC_Error
mbc1_init (void)
{
  
  int ram_size;
  
  
  /* Fixa la memòria RAM */
  if ( _state.mapper == GBC_MBC1 )
    {
      GBC_mapper_read_ram= read_ram_empty;
      GBC_mapper_write_ram= write_ram_empty;
    }
  else
    {
      ram_size= GBC_rom_get_ram_size ( _state.rom );
      if ( ram_size != 2 && ram_size != 8 && ram_size != 32)
        return GBC_WRONGRAMSIZE;
      if ( ram_size == 2 )
        {
          _state.s.mbc1.nbanks_ram= 1;
          _state.s.mbc1.ram_2KB= GBC_TRUE;
        }
      else
        {
          _state.s.mbc1.nbanks_ram= ram_size>>3;
          _state.s.mbc1.ram_2KB= GBC_FALSE;
        }
      mbc1_init_ram ();
      GBC_mapper_read_ram= read_ram_mbc1;
      GBC_mapper_write_ram= write_ram_mbc1;
      _state.s.mbc1.cram= _state.s.mbc1.ram[0];
      _state.s.mbc1.ram_enabled= GBC_FALSE;
    }
  
  /* Fixa la ROM. */
  if ( _state.rom->nbanks < 2 || _state.rom->nbanks > 128 )
    return GBC_WRONGROMSIZE;
  GBC_mapper_read= read_mbc1;
  GBC_mapper_write= _mapper_changed==NULL ? write_mbc1 : write_mbc1_trace;
  _state.s.mbc1.rom_num= 1;
  _state.s.mbc1.rom0= &(_state.rom->banks[0][0]);
  _state.s.mbc1.rom1= &(_state.rom->banks[1][0]);
  _state.s.mbc1.mode0= GBC_TRUE;
  
  GBC_mapper_get_bank1= get_bank1_mbc1;
  GBC_mapper_clock= mapper_clock_empty;
  
  return GBC_NOERROR;
  
} /* end mbc1_init */


static int
mbc1_save_state (
        	 FILE *f
        	 )
{

  int i;
  GBCu8 *aux;
  size_t ret;
  
  
  if ( _state.mapper == GBC_MBC1 )
    {
      SAVE ( _state.s.mbc1 );
    }
  else
    {
      aux= _state.s.mbc1.cram;
      if ( _state.s.mbc1.cram == _state.s.mbc1.ram[0] )
        _state.s.mbc1.cram= (void *) 0;
      else if ( _state.s.mbc1.cram == _state.s.mbc1.ram[1] )
        _state.s.mbc1.cram= (void *) 1;
      else if ( _state.s.mbc1.cram == _state.s.mbc1.ram[2] )
        _state.s.mbc1.cram= (void *) 2;
      else
        _state.s.mbc1.cram= (void *) 3;
      ret= fwrite ( &_state.s.mbc1, sizeof(_state.s.mbc1), 1, f );
      _state.s.mbc1.cram= aux;
      if ( ret != 1 ) return -1;
      for ( i= 0; i < _state.s.mbc1.nbanks_ram; ++i )
        if ( fwrite ( _state.s.mbc1.ram[i], RAM_BANK_SIZE, 1, f ) != 1 )
          return -1;
    }
  
  return 0;
  
} /* end mbc1_save_state */


static int
mbc1_load_state (
        	 FILE *f
        	 )
{

  int ram_size, i;

  
  LOAD ( _state.s.mbc1 );
  if ( _state.mapper != GBC_MBC1 )
    {
      ram_size= GBC_rom_get_ram_size ( _state.rom );
      if ( ram_size == 2 )
        {
          CHECK ( _state.s.mbc1.nbanks_ram == 1 );
          CHECK ( _state.s.mbc1.ram_2KB == GBC_TRUE );
        }
      else
        {
          CHECK ( _state.s.mbc1.nbanks_ram == (ram_size>>3) );
          CHECK ( _state.s.mbc1.ram_2KB == GBC_FALSE );
        }
      mbc1_init_ram ();
      for ( i= 0; i < _state.s.mbc1.nbanks_ram; ++i )
        if ( fread ( _state.s.mbc1.ram[i], RAM_BANK_SIZE, 1, f ) != 1 )
          return -1;
      CHECK ( (ptrdiff_t) _state.s.mbc1.cram >= 0 &&
              (ptrdiff_t) _state.s.mbc1.cram < _state.s.mbc1.nbanks_ram );
      _state.s.mbc1.cram= _state.s.mbc1.ram[(ptrdiff_t) _state.s.mbc1.cram];
    }
  CHECK ( _state.s.mbc1.rom_num >= 0 );
  _state.s.mbc1.rom0= &(_state.rom->banks[0][0]);
  _state.s.mbc1.rom1=
    &(_state.rom->banks[_state.s.mbc1.rom_num%_state.rom->nbanks][0]);
  
  return 0;
  
} /* end mbc1_load_state */




/********/
/* MBC2 */
/********/

static int
get_bank1_mbc2 (void)
{
  return _state.s.mbc2.rom_num;
} // end get_bank1_mbc2


// NOTA!!! La RAM són bytes de 4 bits.
static GBCu8
read_ram_mbc2 (
               const GBCu16 addr
               )
{
  
  if ( !_state.s.mbc2.ram_enabled ) return 0xFF;
  if ( addr >= 0x200 ) return 0xFF;

  return _state.s.mbc2.ram[addr]&0x0F;
  
} // end read_ream_mbc2


static void
write_ram_mbc2 (
                const GBCu16 addr,
                const GBCu8  data
                )
{
  
  if ( !_state.s.mbc2.ram_enabled ) return;
  if ( addr >= 0x200 ) return;
  _state.s.mbc2.ram[addr]= data&0x0F;
  
} // end write_ram_mbc2


static GBCu8
read_mbc2 (
           const GBCu16 addr
           )
{
  
  if ( addr < 0x4000 ) return _state.s.mbc2.rom0[addr];
  else                 return _state.s.mbc2.rom1[addr&0x3FFF];
  
} // end read_mbc2


static void
write_mbc2 (
            const GBCu16 addr,
            const GBCu8  data
            )
{
  
  // RAMG.
  if ( addr < 0x2000 )
    {
      if ( !(addr&0x0100) )
        _state.s.mbc2.ram_enabled= ((data&0x0A)==0xA);
    }
  
  // ROM Bank Number.
  else if ( addr < 0x4000 )
    {
      if ( addr&0x0100 )
        {
          _state.s.mbc2.rom_num= data&0xF;
          _state.s.mbc2.rom1=
            &(_state.rom->banks[_state.s.mbc2.rom_num%_state.rom->nbanks][0]);
        }
    }
  
} // end write_mbc2


static void
write_mbc2_trace (
                  const GBCu16 addr,
                  const GBCu8  data
                  )
{
  
  write_mbc2 ( addr, data );
  if ( addr >= 0x2000 && addr < 0x8000 )
    _mapper_changed ( _udata );
  
} // end write_mbc2_trace


static void
mbc2_init_ram (void)
{

  GBCu8 *mem;

  
  if ( _state.mapper == GBC_MBC2 )
    {
      init_static_ram ();
      mem= &(_ram[0][0]);
    }
  else mem= _get_external_ram ( 512, _udata );
  _state.s.mbc2.ram= mem;
  
} // end mbc2_init_ram


static GBC_Error
mbc2_init (void)
{
  
  // Inicialitza ram (en realitat no és ram)
  mbc2_init_ram ();
  GBC_mapper_read_ram= read_ram_mbc2;
  GBC_mapper_write_ram= write_ram_mbc2;
  _state.s.mbc2.ram_enabled= GBC_FALSE;
  
  // Fixa la ROM.
  if ( _state.rom->nbanks < 2 || _state.rom->nbanks > 16 )
    return GBC_WRONGROMSIZE;
  GBC_mapper_read= read_mbc2;
  GBC_mapper_write= _mapper_changed==NULL ? write_mbc2 : write_mbc2_trace;
  _state.s.mbc2.rom_num= 1;
  _state.s.mbc2.rom0= &(_state.rom->banks[0][0]);
  _state.s.mbc2.rom1= &(_state.rom->banks[1][0]);
  
  GBC_mapper_get_bank1= get_bank1_mbc2;
  GBC_mapper_clock= mapper_clock_empty;
  
  return GBC_NOERROR;
  
} // end mbc2_init


static int
mbc2_save_state (
        	 FILE *f
        	 )
{

  SAVE ( _state.s.mbc2 );
  if ( fwrite ( _state.s.mbc2.ram, 512, 1, f ) != 1 )
    return -1;
  
  return 0;
  
} // end mbc2_save_state


static int
mbc2_load_state (
        	 FILE *f
        	 )
{

  LOAD ( _state.s.mbc2 );
  mbc2_init_ram ();
  if ( fread ( _state.s.mbc2.ram, 512, 1, f ) != 1 )
    return -1;
  CHECK ( _state.s.mbc2.rom_num >= 0 );
  _state.s.mbc2.rom0= &(_state.rom->banks[0][0]);
  _state.s.mbc2.rom1=
    &(_state.rom->banks[_state.s.mbc2.rom_num%_state.rom->nbanks][0]);
  
  return 0;
  
} // end mbc2_load_state




/********/
/* MBC3 */
/********/

static void
mbc3_update_counters (void)
{
  
  clock_t aux, cc;
  int ss;
  
  
  if ( !_state.s.mbc3.timer_enabled ) return;
  aux= clock ();
  assert ( aux != (clock_t) -1 );
  assert ( aux >= _state.s.mbc3.cc );
  cc= (aux-_state.s.mbc3.cc) + _state.s.mbc3.remaincc;
  _state.s.mbc3.cc= aux;
  _state.s.mbc3.remaincc= cc%CLOCKS_PER_SEC;
  ss= cc/CLOCKS_PER_SEC;
  ss+=
    _state.s.mbc3.counters.ss +
    _state.s.mbc3.counters.mm*60 +
    _state.s.mbc3.counters.hh*3600 +
    _state.s.mbc3.counters.dd*3600*24;
  _state.s.mbc3.counters.dd= ss/(3600*24); ss%= 3600*24;
  _state.s.mbc3.counters.hh= ss/3600; ss%= 3600;
  _state.s.mbc3.counters.mm= ss/60; ss%= 60;
  _state.s.mbc3.counters.ss= ss;
  if ( _state.s.mbc3.counters.dd >= 512 )
    {
      _state.s.mbc3.counters.dd%= 512;
      _state.s.mbc3.counters.carry= GBC_TRUE;
    }
  
} /* end mbc3_update_counters */


static int
get_bank1_mbc3 (void)
{
  return _state.s.mbc3.rom_num;
} /* end get_bank1_mbc3 */


static GBCu8
read_mbc3 (
           const GBCu16 addr
           )
{
  
  if ( addr < 0x4000 ) return _state.s.mbc3.rom0[addr];
  else                 return _state.s.mbc3.rom1[addr&0x3FFF];
  
} /* end read_mbc3 */


static void
write_mbc3 (
            const GBCu16 addr,
            const GBCu8  data
            )
{
  
  GBC_Bool aux;
  
  
  /* REG0 (Protect RAM and counters). */
  if ( addr < 0x2000 )
    _state.s.mbc3.ram_enabled= (data==0x0A);
  
  /* REG1 (ROM bank). */
  else if ( addr < 0x4000 )
    {
      _state.s.mbc3.rom_num= data&0x7F;
      _state.s.mbc3.rom1=
        &(_state.rom->banks[_state.s.mbc3.rom_num%_state.rom->nbanks][0]);
    }
  
  /* REG2 (RAM bank i counters) */
  else if ( addr < 0x6000 )
    {
      if ( data < 0x04 )
        {
          _state.s.mbc3.ram_mode= MBC3_MODE_RAM;
          _state.s.mbc3.cram= _state.s.mbc3.ram[data];
        }
      else if ( data < 0x08 ) _state.s.mbc3.ram_mode= MBC3_MODE_NONE;
      else if ( data < 0x0D )
        {
          switch ( data )
            {
            case 0x08: _state.s.mbc3.ram_mode= MBC3_MODE_RTC_S; break;
            case 0x09: _state.s.mbc3.ram_mode= MBC3_MODE_RTC_M; break;
            case 0x0A: _state.s.mbc3.ram_mode= MBC3_MODE_RTC_H; break;
            case 0x0B: _state.s.mbc3.ram_mode= MBC3_MODE_RTC_DL; break;
            case 0x0C: _state.s.mbc3.ram_mode= MBC3_MODE_RTC_DH; break;
            default: break;
            }
        }
      else _state.s.mbc3.ram_mode= MBC3_MODE_NONE;
    }
  
  /* REG3 (Latch counters) */
  else if ( addr < 0x8000 )
    {
      aux= ((data&0x1)==0x1);
      if ( aux && !_state.s.mbc3.latch_flag )
        {
          mbc3_update_counters ();
          _state.s.mbc3.latch= _state.s.mbc3.counters;
        }
      _state.s.mbc3.latch_flag= aux;
    }
  
} /* end write_mbc3 */


static void
write_mbc3_trace (
        	  const GBCu16 addr,
        	  const GBCu8  data
        	  )
{
  
  write_mbc3 ( addr, data );
  if ( addr >= 0x2000 && addr < 0x4000 )
    _mapper_changed ( _udata );
  
} /* end write_mbc3_trace */


static GBCu8
read_ram_mbc3 (
               const GBCu16 addr
               )
{
  
  if ( !_state.s.mbc3.ram_enabled ) return 0x00;
  switch ( _state.s.mbc3.ram_mode )
    {
    case MBC3_MODE_RAM: return _state.s.mbc3.cram[addr];
    case MBC3_MODE_RTC_S: return (GBCu8) _state.s.mbc3.latch.ss;
    case MBC3_MODE_RTC_M: return (GBCu8) _state.s.mbc3.latch.mm;
    case MBC3_MODE_RTC_H: return (GBCu8) _state.s.mbc3.latch.hh;
    case MBC3_MODE_RTC_DL: return (GBCu8) (_state.s.mbc3.latch.dd&0xFF);
    case MBC3_MODE_RTC_DH:
      return 
        (_state.s.mbc3.latch.carry ? 0x80 : 0x00) |
        ((_state.s.mbc3.timer_enabled) ? 0x00 : 0x40) |
        ((GBCu8) ((_state.s.mbc3.latch.dd&0x100)>>8));
    default: return 0x00;
    }
  
} /* end read_ram_mbc3 */


static void
write_ram_mbc3 (
        	const GBCu16 addr,
        	const GBCu8  data
        	)
{
  
  GBC_Bool aux;
  
  
  if ( !_state.s.mbc3.ram_enabled ) return;
  switch ( _state.s.mbc3.ram_mode )
    {
    case MBC3_MODE_RAM: _state.s.mbc3.cram[addr]= data; break;
    case MBC3_MODE_RTC_S:
      _state.s.mbc3.counters.ss= (data&0x3F)%60;
      break;
    case MBC3_MODE_RTC_M:
      _state.s.mbc3.counters.mm= (data&0x3F)%60;
      break;
    case MBC3_MODE_RTC_H:
      _state.s.mbc3.counters.hh= (data&0x1F)%24;
      break;
    case MBC3_MODE_RTC_DL:
      _state.s.mbc3.counters.dd&= 0x100;
      _state.s.mbc3.counters.dd|= data;
      break;
    case MBC3_MODE_RTC_DH:
      aux= ((0x80&data)!=0x80); /* 1 - HALT */
      if ( aux != _state.s.mbc3.timer_enabled )
        {
          if ( aux ) /* Reseteja cicles quan s'inicia. */
            {
              _state.s.mbc3.cc= clock ();
              _state.s.mbc3.remaincc= 0;
            }
          else mbc3_update_counters (); /* Abans de parar actualitza. */
        }
      _state.s.mbc3.timer_enabled= aux;
      _state.s.mbc3.counters.dd&= 0xFF;
      _state.s.mbc3.counters.dd|= (((int) (data&0x01))<<8);
      _state.s.mbc3.counters.carry= ((data&0x80)==0x80);
      break;
    default: break;
    }
  
} /* end write_ram_mbc3 */


static void
mbc3_init_ram (void)
{

  int i;
  GBCu8 *mem;


  if ( _state.mapper == GBC_MBC3_RAM )
    {
      init_static_ram ();
      mem= &(_ram[0][0]);
    }
  else mem= _get_external_ram ( 4*RAM_BANK_SIZE, _udata );
  for ( i= 0; i < 4; ++i, mem+= RAM_BANK_SIZE )
    _state.s.mbc3.ram[i]= mem;
  
} /* end mbc3_init_ram */


static GBC_Error
mbc3_init (void)
{
  
  int ram_size;
  
  
  /* Fixa la memòria RAM */
  if ( _state.mapper == GBC_MBC3 || _state.mapper == GBC_MBC3_TIMER_BATTERY )
    {
      GBC_mapper_read_ram= read_ram_empty;
      GBC_mapper_write_ram= write_ram_empty;
    }
  else
    {
      ram_size= GBC_rom_get_ram_size ( _state.rom );
      if ( ram_size != 32 )
        return GBC_WRONGRAMSIZE;
      mbc3_init_ram ();
      _state.s.mbc3.ram_mode= MBC3_MODE_RAM;
      GBC_mapper_read_ram= read_ram_mbc3;
      GBC_mapper_write_ram= write_ram_mbc3;
      _state.s.mbc3.cram= _state.s.mbc3.ram[0];
      _state.s.mbc3.ram_enabled= GBC_FALSE;
    }
  
  /* Fixa la ROM. */
  if ( _state.rom->nbanks < 4 || _state.rom->nbanks > 128 )
    return GBC_WRONGROMSIZE;
  GBC_mapper_read= read_mbc3;
  GBC_mapper_write= _mapper_changed==NULL ? write_mbc3 : write_mbc3_trace;
  _state.s.mbc3.rom_num= 1;
  _state.s.mbc3.rom0= &(_state.rom->banks[0][0]);
  _state.s.mbc3.rom1= &(_state.rom->banks[1][0]);
  
  GBC_mapper_get_bank1= get_bank1_mbc3;
  GBC_mapper_clock= mapper_clock_empty;
  
  /* Temporitzador. */
  _state.s.mbc3.latch_flag= GBC_FALSE;
  _state.s.mbc3.counters.ss= 0;
  _state.s.mbc3.counters.mm= 0;
  _state.s.mbc3.counters.hh= 0;
  _state.s.mbc3.counters.dd= 0;
  _state.s.mbc3.counters.carry= GBC_FALSE;
  _state.s.mbc3.latch.ss= 0;
  _state.s.mbc3.latch.mm= 0;
  _state.s.mbc3.latch.hh= 0;
  _state.s.mbc3.latch.dd= 0;
  _state.s.mbc3.latch.carry= GBC_FALSE;
  _state.s.mbc3.cc= 0;
  _state.s.mbc3.remaincc= 0;
  _state.s.mbc3.timer_enabled= GBC_FALSE;
  
  return GBC_NOERROR;
  
} /* end mbc3_init */


static int
mbc3_save_state (
        	 FILE *f
        	 )
{

  int i;
  GBCu8 *aux;
  size_t ret;
  
  
  if ( _state.mapper == GBC_MBC3 || _state.mapper == GBC_MBC3_TIMER_BATTERY )
    {
      SAVE ( _state.s.mbc3 );
    }
  else
    {
      aux= _state.s.mbc3.cram;
      if ( _state.s.mbc3.cram == _state.s.mbc3.ram[0] )
        _state.s.mbc3.cram= (void *) 0;
      else if ( _state.s.mbc3.cram == _state.s.mbc3.ram[1] )
        _state.s.mbc3.cram= (void *) 1;
      else if ( _state.s.mbc3.cram == _state.s.mbc3.ram[2] )
        _state.s.mbc3.cram= (void *) 2;
      else
        _state.s.mbc3.cram= (void *) 3;
      ret= fwrite ( &_state.s.mbc3, sizeof(_state.s.mbc3), 1, f );
      _state.s.mbc3.cram= aux;
      if ( ret != 1 ) return -1;
      for ( i= 0; i < 4; ++i )
        if ( fwrite ( _state.s.mbc3.ram[i], RAM_BANK_SIZE, 1, f ) != 1 )
          return -1;
    }
  
  return 0;
  
} /* end mbc3_save_state */


static int
mbc3_load_state (
        	 FILE *f
        	 )
{

  int i;
  
  
  LOAD ( _state.s.mbc3 );
  if ( _state.mapper != GBC_MBC3 && _state.mapper != GBC_MBC3_TIMER_BATTERY )
    {
      mbc3_init_ram ();
      for ( i= 0; i < 4; ++i )
        if ( fread ( _state.s.mbc3.ram[i], RAM_BANK_SIZE, 1, f ) != 1 )
          return -1;
      CHECK ( (ptrdiff_t) _state.s.mbc3.cram >= 0 &&
              (ptrdiff_t) _state.s.mbc3.cram < 4 );
      _state.s.mbc3.cram= _state.s.mbc3.ram[(ptrdiff_t) _state.s.mbc3.cram];
    }
  CHECK ( _state.s.mbc3.rom_num >= 0 );
  _state.s.mbc3.rom0= &(_state.rom->banks[0][0]);
  _state.s.mbc3.rom1=
    &(_state.rom->banks[_state.s.mbc3.rom_num%_state.rom->nbanks][0]);
  CHECK ( _state.s.mbc3.cc >= 0 );
  CHECK ( _state.s.mbc3.remaincc >= 0 );
  
  return 0;
  
} /* end mbc3_load_state */




/********/
/* MBC5 */
/********/

static int
get_bank1_mbc5 (void)
{
  return _state.s.mbc5.rom_num;
} /* end get_bank1_mbc5 */


static GBCu8
read_ram_mbc5 (
               const GBCu16 addr
               )
{
  
  if ( !_state.s.mbc5.ram_enabled ) return 0x00;
  return _state.s.mbc5.cram[addr];
  
} /* end read_ream_mbc5 */


static void
write_ram_mbc5 (
        	const GBCu16 addr,
        	const GBCu8  data
        	)
{
  
  if ( !_state.s.mbc5.ram_enabled ) return;
  _state.s.mbc5.cram[addr]= data;
  
} /* end write_ram_mbc5 */


static GBCu8
read_mbc5 (
           const GBCu16 addr
           )
{
  
  if ( addr < 0x4000 ) return _state.s.mbc5.rom0[addr];
  else                 return _state.s.mbc5.rom1[addr&0x3FFF];
  
} /* end read_mbc5 */


static void
write_mbc5 (
            const GBCu16 addr,
            const GBCu8  data
            )
{
  
  /* RAMG. */
  if ( addr < 0x2000 )
    _state.s.mbc5.ram_enabled= (data==0x0A);
  
  /* ROMB0. */
  else if ( addr < 0x3000 )
    {
      _state.s.mbc5.rom_num&= 0x100;
      _state.s.mbc5.rom_num|= data;
      _state.s.mbc5.rom1=
        &(_state.rom->banks[_state.s.mbc5.rom_num%_state.rom->nbanks][0]);
    }
  
  /* ROMB1. */
  else if ( addr < 0x4000 )
    {
      _state.s.mbc5.rom_num&= 0xFF;
      _state.s.mbc5.rom_num|= ((GBCu16) (data&0x1))<<8;
      _state.s.mbc5.rom1=
        &(_state.rom->banks[_state.s.mbc5.rom_num%_state.rom->nbanks][0]);
    }
  
  /* RAMB */
  else if ( addr < 0x6000 )
    {
      if ( _state.s.mbc5.rumble && _state.s.mbc5.cc > RBL_MIN_CICLES )
        {
          if ( data&0x08 ) ++_state.s.mbc5.rumble_level;
          if ( ++_state.s.mbc5.rumble_nframes == 3 )
            {
              if ( _state.s.mbc5.rumble_state != _state.s.mbc5.rumble_level )
        	{
        	  _state.s.mbc5.rumble_state= _state.s.mbc5.rumble_level;
        	  _update_rumble ( _state.s.mbc5.rumble_state, _udata );
        	}
              _state.s.mbc5.rumble_level= _state.s.mbc5.rumble_nframes= 0;
            }
        }
      _state.s.mbc5.cram=
        _state.s.mbc5.ram[(data&0x3)%_state.s.mbc5.nbanks_ram];
    }
  
} /* end write_mbc5 */


static void
write_mbc5_trace (
        	  const GBCu16 addr,
        	  const GBCu8  data
        	  )
{
  
  write_mbc5 ( addr, data );
  if ( addr >= 0x2000 && addr < 0x4000 )
    _mapper_changed ( _udata );
  
} /* end write_mbc5_trace */


static void
mbc5_mapper_clock (
        	   const int cc
        	   )
{
  
  _state.s.mbc5.cc+= cc;
  if ( _state.s.mbc5.cc > RBL_MAX_CICLES && _state.s.mbc5.rumble_state )
    {
      _update_rumble ( 0, _udata );
      _state.s.mbc5.rumble_state= _state.s.mbc5.rumble_level=
        _state.s.mbc5.rumble_nframes= 0;
    }
  
} /* end mbc5_mapper_clock */


static void
mbc5_init_ram (void)
{

  int i;
  GBCu8 *mem;


  if ( _state.mapper == GBC_MBC5_RAM ||
       _state.mapper == GBC_MBC5_RUMBLE_RAM )
    {
      init_static_ram ();
      mem= &(_ram[0][0]);
    }
  else mem= _get_external_ram ( _state.s.mbc5.nbanks_ram*RAM_BANK_SIZE,
        			_udata );
  for ( i= 0; i < _state.s.mbc5.nbanks_ram; ++i, mem+= RAM_BANK_SIZE )
    _state.s.mbc5.ram[i]= mem;
  
} /* end mbc5_init_ram */


static GBC_Error
mbc5_init (void)
{
  
  int ram_size;

  
  /* Fixa la memòria RAM */
  if ( _state.mapper == GBC_MBC5 || _state.mapper == GBC_MBC5_RUMBLE )
    {
      GBC_mapper_read_ram= read_ram_empty;
      GBC_mapper_write_ram= write_ram_empty;
    }
  else
    {
      ram_size= GBC_rom_get_ram_size ( _state.rom );
      if ( ram_size < 8 || ram_size > 32 )
        return GBC_WRONGRAMSIZE;
      _state.s.mbc5.nbanks_ram= ram_size>>3;
      mbc5_init_ram ();
      GBC_mapper_read_ram= read_ram_mbc5;
      GBC_mapper_write_ram= write_ram_mbc5;
      _state.s.mbc5.cram= _state.s.mbc5.ram[0];
      _state.s.mbc5.ram_enabled= GBC_FALSE;
    }
  
  /* Fixa la ROM. */
  if ( _state.rom->nbanks < 2 || _state.rom->nbanks > 512 )
    return GBC_WRONGROMSIZE;
  GBC_mapper_read= read_mbc5;
  GBC_mapper_write= _mapper_changed==NULL ? write_mbc5 : write_mbc5_trace;
  _state.s.mbc5.rom_num= 1;
  _state.s.mbc5.rom0= &(_state.rom->banks[0][0]);
  _state.s.mbc5.rom1= &(_state.rom->banks[1][0]);
  
  GBC_mapper_get_bank1= get_bank1_mbc5;
  
  /* Rumble. */
  _state.s.mbc5.rumble=
    _state.mapper==GBC_MBC5_RUMBLE ||
    _state.mapper==GBC_MBC5_RUMBLE_RAM ||
    _state.mapper==GBC_MBC5_RUMBLE_RAM_BATTERY;
  if ( _state.s.mbc5.rumble )
    {
      _state.s.mbc5.cc= 0;
      GBC_mapper_clock= mbc5_mapper_clock;
      _state.s.mbc5.rumble_state= 0;
      _state.s.mbc5.rumble_level= 0;
      _state.s.mbc5.rumble_nframes= 0;
    }
  else GBC_mapper_clock= mapper_clock_empty;
  
  return GBC_NOERROR;
  
} /* end mbc5_init */


static int
mbc5_save_state (
        	 FILE *f
        	 )
{

  int i;
  GBCu8 *aux;
  size_t ret;
  
  
  if ( _state.mapper == GBC_MBC5 || _state.mapper == GBC_MBC5_RUMBLE )
    {
      SAVE ( _state.s.mbc5 );
    }
  else
    {
      aux= _state.s.mbc5.cram;
      if ( _state.s.mbc5.cram == _state.s.mbc5.ram[0] )
        _state.s.mbc5.cram= (void *) 0;
      else if ( _state.s.mbc5.cram == _state.s.mbc5.ram[1] )
        _state.s.mbc5.cram= (void *) 1;
      else if ( _state.s.mbc5.cram == _state.s.mbc5.ram[2] )
        _state.s.mbc5.cram= (void *) 2;
      else
        _state.s.mbc5.cram= (void *) 3;
      ret= fwrite ( &_state.s.mbc5, sizeof(_state.s.mbc5), 1, f );
      _state.s.mbc5.cram= aux;
      if ( ret != 1 ) return -1;
      for ( i= 0; i < _state.s.mbc5.nbanks_ram; ++i )
        if ( fwrite ( _state.s.mbc5.ram[i], RAM_BANK_SIZE, 1, f ) != 1 )
          return -1;
    }
  
  return 0;
  
} /* end mbc5_save_state */


static int
mbc5_load_state (
        	 FILE *f
        	 )
{

  int ram_size, i;

  
  LOAD ( _state.s.mbc5 );
  if ( _state.mapper != GBC_MBC5 && _state.mapper != GBC_MBC5_RUMBLE )
    {
      ram_size= GBC_rom_get_ram_size ( _state.rom );
      CHECK ( _state.s.mbc5.nbanks_ram == (ram_size>>3) );
      mbc5_init_ram ();
      for ( i= 0; i < _state.s.mbc5.nbanks_ram; ++i )
        if ( fread ( _state.s.mbc5.ram[i], RAM_BANK_SIZE, 1, f ) != 1 )
          return -1;
      CHECK ( (ptrdiff_t) _state.s.mbc5.cram >= 0 &&
              (ptrdiff_t) _state.s.mbc5.cram < _state.s.mbc5.nbanks_ram );
      _state.s.mbc5.cram= _state.s.mbc5.ram[(ptrdiff_t) _state.s.mbc5.cram];
    }
  CHECK ( _state.s.mbc5.rom_num >= 0 );
  _state.s.mbc5.rom0= &(_state.rom->banks[0][0]);
  _state.s.mbc5.rom1=
    &(_state.rom->banks[_state.s.mbc5.rom_num%_state.rom->nbanks][0]);
  if ( _state.s.mbc5.rumble )
    {
      CHECK ( _state.s.mbc5.cc >= 0 );
    }
  
  return 0;
  
} /* end mbc5_load_state */




/****************/
/* ESTAT PÚBLIC */
/****************/

void (*GBC_mapper_clock) (const int);
int (*GBC_mapper_get_bank1) (void);
GBCu8 (*GBC_mapper_read) (const GBCu16);
GBCu8 (*GBC_mapper_read_ram) (const GBCu16);
void (*GBC_mapper_write) (const GBCu16,const GBCu8);
void (*GBC_mapper_write_ram) (const GBCu16,const GBCu8);




/**********************/
/* FUNCIONS PÚBLIQUES */
/**********************/

GBC_Error
GBC_mapper_init (
        	 const GBC_Rom      *rom,
        	 const GBC_Bool      check_rom,
        	 GBC_GetExternalRAM *get_external_ram,
        	 GBC_UpdateRumble   *update_rumble,
        	 GBC_MapperChanged  *mapper_changed,
        	 void               *udata
        	 )
{
  
  _mapper_changed= mapper_changed;
  _update_rumble= update_rumble;
  _get_external_ram= get_external_ram;
  _udata= udata;
  
  _state.rom= rom;
  if ( check_rom )
    {
      if ( !GBC_rom_check_nintendo_logo ( rom ) )
        return GBC_WRONGLOGO;
      if ( !GBC_rom_check_checksum ( rom ) )
        return GBC_WRONGCHKS;
    }

  return GBC_mapper_init_state ();
  
} /* end GBC_mapper_init */


GBC_Error
GBC_mapper_init_state (void)
{
  
  _state.mapper= GBC_rom_get_mapper ( _state.rom );
  switch ( _state.mapper )
    {
      
    case GBC_UNKMAPPER: return GBC_EUNKMAPPER;
      
      // ROM.
    case GBC_ROM:
      return rom_init ();
      break;
      
      // MBC1.
    case GBC_MBC1:
    case GBC_MBC1_RAM:
    case GBC_MBC1_RAM_BATTERY:
      return mbc1_init ();
      break;

      // MBC2.
    case GBC_MBC2:
    case GBC_MBC2_BATTERY:
      return mbc2_init ();
      break;
      
      // MBC3.
    case GBC_MBC3_TIMER_BATTERY:
    case GBC_MBC3_TIMER_RAM_BATTERY:
    case GBC_MBC3:
    case GBC_MBC3_RAM:
    case GBC_MBC3_RAM_BATTERY:
      return mbc3_init ();
      break;
      
      // MBC5.
    case GBC_MBC5:
    case GBC_MBC5_RAM:
    case GBC_MBC5_RAM_BATTERY:
    case GBC_MBC5_RUMBLE:
    case GBC_MBC5_RUMBLE_RAM:
    case GBC_MBC5_RUMBLE_RAM_BATTERY:
      return mbc5_init ();
      break;
      
    default: 
      fprintf(stderr,"No s'ha implementat el mapper: '%s'\n",
              GBC_rom_mapper2str ( _state.mapper ) );
      exit ( EXIT_FAILURE );
      
    }
  
  return GBC_NOERROR;
  
} // end GBC_mapper_init_state


int
GBC_mapper_save_state (
        	       FILE *f
        	       )
{

  int ret;
  
  
  SAVE ( _state.rom->nbanks );
  SAVE ( _state.mapper );
  switch ( _state.mapper )
    {
      
      // MBC1.
    case GBC_MBC1:
    case GBC_MBC1_RAM:
    case GBC_MBC1_RAM_BATTERY:
      ret= mbc1_save_state ( f );
      if ( ret != 0 ) return ret;
      break;

      // MBC2.
    case GBC_MBC2:
    case GBC_MBC2_BATTERY:
      ret= mbc2_save_state ( f );
      if ( ret != 0 ) return ret;
      break;
      
      // MBC3.
    case GBC_MBC3_TIMER_BATTERY:
    case GBC_MBC3_TIMER_RAM_BATTERY:
    case GBC_MBC3:
    case GBC_MBC3_RAM:
    case GBC_MBC3_RAM_BATTERY:
      ret= mbc3_save_state ( f );
      if ( ret != 0 ) return ret;
      break;
      
      // MBC5.
    case GBC_MBC5:
    case GBC_MBC5_RAM:
    case GBC_MBC5_RAM_BATTERY:
    case GBC_MBC5_RUMBLE:
    case GBC_MBC5_RUMBLE_RAM:
    case GBC_MBC5_RUMBLE_RAM_BATTERY:
      ret= mbc5_save_state ( f );
      if ( ret != 0 ) return ret;
      break;
      
    case GBC_ROM:
    case GBC_UNKMAPPER:
    default: break;
    }
  
  return 0;
  
} // end GBC_mapper_save_state


int
GBC_mapper_load_state (
        	       FILE *f
        	       )
{
  
  GBC_Rom rom_fk;
  GBC_Mapper mapper;
  int ret;
  

  LOAD ( rom_fk.nbanks );
  CHECK ( rom_fk.nbanks == _state.rom->nbanks );
  LOAD ( mapper );
  CHECK ( _state.mapper == mapper );
  switch ( _state.mapper )
    {
      
      // MBC1.
    case GBC_MBC1:
    case GBC_MBC1_RAM:
    case GBC_MBC1_RAM_BATTERY:
      ret= mbc1_load_state ( f );
      if ( ret != 0 ) return ret;
      break;

      // MBC2.
    case GBC_MBC2:
    case GBC_MBC2_BATTERY:
      ret= mbc2_load_state ( f );
      if ( ret != 0 ) return ret;
      break;
      
      // MBC3.
    case GBC_MBC3_TIMER_BATTERY:
    case GBC_MBC3_TIMER_RAM_BATTERY:
    case GBC_MBC3:
    case GBC_MBC3_RAM:
    case GBC_MBC3_RAM_BATTERY:
      ret= mbc3_load_state ( f );
      if ( ret != 0 ) return ret;
      break;
      
      // MBC5.
    case GBC_MBC5:
    case GBC_MBC5_RAM:
    case GBC_MBC5_RAM_BATTERY:
    case GBC_MBC5_RUMBLE:
    case GBC_MBC5_RUMBLE_RAM:
    case GBC_MBC5_RUMBLE_RAM_BATTERY:
      ret= mbc5_load_state ( f );
      if ( ret != 0 ) return ret;
      break;
      
    case GBC_ROM:
    case GBC_UNKMAPPER:
    default: break;
    }
  
  return 0;
  
} // end GBC_mapper_load_state
