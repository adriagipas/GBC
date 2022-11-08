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
 * along with Foobar.  If not, see <https://www.gnu.org/licenses/>.
 */
/*
 *  mem.c - Implementació del mòdul de memòria.
 *
 */


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


#define RAM_PAGE_SIZE 4096

#define HRAM_SIZE 127




/*********/
/* ESTAT */
/*********/

/* BIOS. */
static const GBCu8 *_bios;
static GBC_Bool _bios_mapped;

/* RAM. */
static GBCu8 _ram[8][RAM_PAGE_SIZE];
static GBCu8 *_ram0;
static GBCu8 *_ram1;
static GBCu8 _svbk;

/* HRAM. */
static GBCu8 _hram[HRAM_SIZE];

/* Funcions per a llegir. */
static GBCu8 (*_mem_read) (const GBCu16 addr);
static void (*_mem_write) (const GBCu16 addr,const GBCu8 data);

/* Callback. */
static GBC_MemAccess *_mem_access;
static void *_udata;




/*********************/
/* FUNCIONS PRIVADES */
/*********************/

#include <stdio.h>
static GBCu8
mem_read (
          const GBCu16 addr
          )
{
  
  /* ROM+BIOS. */
  if ( addr < 0x900 )
    {
      if ( _bios_mapped && (addr < 0x100 || addr >= 0x200) )
        return _bios[addr];
      else return GBC_mapper_read ( addr );
    }
  
  /* ROM. */
  else if ( addr < 0x8000 ) return GBC_mapper_read ( addr );
  
  /* VRAM. */
  else if ( addr < 0xA000 ) return GBC_lcd_vram_read ( addr&0x1FFF );
  
  /* External RAM. */
  else if ( addr < 0xC000 ) return GBC_mapper_read_ram ( addr&0x1FFF );
  
  /* RAM. */
  else if ( addr < 0xFE00 )
    {
      if ( addr&0x1000 ) return _ram1[addr&0xFFF];
      else               return _ram0[addr&0xFFF];
    }
  
  /* OAM. */
  else if ( addr < 0xFEA0 ) return GBC_lcd_oam_read ( addr&0xFF );
  
  /* Not usable. */
  else if ( addr < 0xFF00 ) return 0x00;
  
  /* I/O Ports. */
  else if ( addr < 0xFF80 )
    {
      switch ( addr&0xFF )
        {
          
        case 0x00: /* JOYP. */
          return GBC_joypad_read ();
          
        case 0x04: /* DIV. */
          return GBC_timers_divider_read ();
          
        case 0x05: /* TIMA. */
          return GBC_timers_timer_counter_read ();
          
        case 0x06: /* TMA. */
          return GBC_timers_timer_modulo_read ();
          
        case 0x07: /* TAC. */
          return GBC_timers_timer_control_read ();
          
        case 0x0F: /* Interrupt Flag. */
          return GBC_cpu_read_IF ();
        
        case 0x10: /* NR10. */
          return GBC_apu_ch1_sweep_read ();
          
        case 0x11: /* NR11. */
          return GBC_apu_ch1_get_wave_pattern_duty ();
          
        case 0x12: /* NR12. */
          return GBC_apu_ch1_volume_envelope_read ();
        
        case 0x13: /* NR13. */
          return 0xFF;
          
        case 0x14: /* NR14. */
          return GBC_apu_ch1_get_lc_status ();
        
        case 0x15: /* NR20. */
          return 0xFF;
          
        case 0x16: /* NR21. */
          return GBC_apu_ch2_get_wave_pattern_duty ();
          
        case 0x17: /* NR22. */
          return GBC_apu_ch2_volume_envelope_read ();
          
        case 0x18: /* NR23. */
          return 0xFF;
          
        case 0x19: /* NR24. */
          return GBC_apu_ch2_get_lc_status ();
        
        case 0x1A: /* NR30. */
          return GBC_apu_ch3_sound_on_off_read ();
        
        case 0x1B: /* NR31. */
          return 0xFF;
          
        case 0x1C: /* NR32. */  
          return GBC_apu_ch3_output_level_read ();
          
        case 0x1D: /* NR33. */
          return 0xFF;
          
        case 0x1E: /* NR34. */  
          return GBC_apu_ch3_get_lc_status ();
          
        case 0x1F: /* NR40. */
          return 0xFF;
          
        case 0x20: /* NR41. */
          return 0xFF;
          
        case 0x21: /* NR42. */
          return GBC_apu_ch4_volume_envelope_read ();
          
        case 0x22: /* NR43. */
          return GBC_apu_ch4_polynomial_counter_read ();
        
        case 0x23: /* NR44. */
          return GBC_apu_ch4_get_lc_status ();
          
        case 0x24: /* NR50. */
          return GBC_apu_vin_read ();
          
        case 0x25: /* NR51. */
          return GBC_apu_select_out_read ();
          
        case 0x26: /* NR52. */
          return GBC_apu_get_status ();
          
          /* Wave Pattern RAM. */
        case 0x30: return GBC_apu_ch3_ram_read ( 0 );
        case 0x31: return GBC_apu_ch3_ram_read ( 1 );
        case 0x32: return GBC_apu_ch3_ram_read ( 2 );
        case 0x33: return GBC_apu_ch3_ram_read ( 3 );
        case 0x34: return GBC_apu_ch3_ram_read ( 4 );
        case 0x35: return GBC_apu_ch3_ram_read ( 5 );
        case 0x36: return GBC_apu_ch3_ram_read ( 6 );
        case 0x37: return GBC_apu_ch3_ram_read ( 7 );
        case 0x38: return GBC_apu_ch3_ram_read ( 8 );
        case 0x39: return GBC_apu_ch3_ram_read ( 9 );
        case 0x3A: return GBC_apu_ch3_ram_read ( 10 );
        case 0x3B: return GBC_apu_ch3_ram_read ( 11 );
        case 0x3C: return GBC_apu_ch3_ram_read ( 12 );
        case 0x3D: return GBC_apu_ch3_ram_read ( 13 );
        case 0x3E: return GBC_apu_ch3_ram_read ( 14 );
        case 0x3F: return GBC_apu_ch3_ram_read ( 15 );
          
        case 0x40: /* LCDC. */
          return GBC_lcd_control_read ();
          
        case 0x41: /* STAT. */
          return GBC_lcd_status_read ();
          
        case 0x42: /* SCY. */
          return GBC_lcd_scy_read ();
          
        case 0x43: /* SCX. */
          return GBC_lcd_scx_read ();
          
        case 0x44: /* LY. */
          return GBC_lcd_ly_read ();
          
        case 0x45: /* LYC. */
          return GBC_lcd_lyc_read ();
          
        case 0x47: /* BGP. */
          return GBC_lcd_mpal_bg_get ();
          
        case 0x48: /* OBP0. */
          return GBC_lcd_mpal_ob0_get ();
          
        case 0x49: /* OBP1. */
          return GBC_lcd_mpal_ob1_get ();
          
        case 0x4A: /* WY. */
          return GBC_lcd_wy_read ();
          
        case 0x4B: /* WX. */
          return GBC_lcd_wx_read ();
          
        case 0x4D: /* KEY1. */
          return GBC_cpu_speed_query ();
          
        case 0x4F: /* VBK. */
          return GBC_lcd_get_vram_bank ();
          
        case 0x55: /* HDMA5. */
          return GBC_lcd_vram_dma_status ();
          
        case 0x69: /* BGPD. */
          return GBC_lcd_cpal_bg_read_data ();
          
        case 0x6B: /* OBPD. */
          return GBC_lcd_cpal_ob_read_data ();
          
        case 0x70: /* SVBK. */
          return _svbk;
          
        default:
          printf ( "Read I/O port FF%02x\n", addr&0xFF );
          return 0xFF;
          
        }
    }
  
  /* HRAM. */
  else if ( addr < 0xFFFF ) return _hram[addr&0x7F];
  
  /* Interrupt Enable Register. */
  else return GBC_cpu_read_IE ();
  
} /* end mem_read */


static GBCu8
mem_read_trace (
        	const GBCu16 addr
        	)
{
  
  GBCu8 data;
  
  
  data= mem_read ( addr );
  _mem_access ( GBC_READ, addr, data, _udata );
  
  return data;
  
} /* end mem_read_trace */


static void
mem_write (
           const GBCu16 addr,
           const GBCu8  data
           )
{
  
  int aux;
  
  
  /* ROM. */
  if ( addr < 0x8000 ) GBC_mapper_write ( addr, data );
  
  /* VRAM. */
  else if ( addr < 0xA000 ) GBC_lcd_vram_write ( addr&0x1FFF, data );
  
  /* External RAM. */
  else if ( addr < 0xC000 ) GBC_mapper_write_ram ( addr&0x1FFF, data );
  
  /* RAM. */
  else if ( addr < 0xFE00 )
    {
      if ( addr&0x1000 ) _ram1[addr&0xFFF]= data;
      else               _ram0[addr&0xFFF]= data;
    }
  
  /* OAM. */
  else if ( addr < 0xFEA0 ) GBC_lcd_oam_write ( addr&0xFF, data );
  
  /* Not usable. */
  else if ( addr < 0xFF00 ) return;
  
  /* I/O Ports. */
  else if ( addr < 0xFF80 )
    {
      switch ( addr&0xFF )
        {
          
        case 0x00: /* JOYP. */
          GBC_joypad_write ( data );
          break;
          
        case 0x04: /* DIV. */
          GBC_timers_divider_write ( data );
          break;
          
        case 0x05: /* TIMA. */
          GBC_timers_timer_counter_write ( data );
          break;
          
        case 0x06: /* TMA. */
          GBC_timers_timer_modulo_write ( data );
          break;
          
        case 0x07: /* TAC. */
          GBC_timers_timer_control_write ( data );
          break;
          
        case 0x0F: /* Interrupt Flag. */
          GBC_cpu_write_IF ( data );
          break;
        
        case 0x10: /* NR10. */
          GBC_apu_ch1_sweep_write ( data );
          break;
        
        case 0x11: /* NR11. */
          GBC_apu_ch1_set_length_wave_pattern_dutty ( data );
          break;
          
        case 0x12: /* NR12. */
          GBC_apu_ch1_volume_envelope_write ( data );
          break;
          
        case 0x13: /* NR13. */
          GBC_apu_ch1_freq_lo ( data );
          break;
          
        case 0x14: /* NR14. */
          GBC_apu_ch1_freq_hi ( data );
          break;
          
        case 0x15: /* NR20. */
          break;
          
        case 0x16: /* NR21. */
          GBC_apu_ch2_set_length_wave_pattern_dutty ( data );
          break;
          
        case 0x17: /* NR22. */
          GBC_apu_ch2_volume_envelope_write ( data );
          break;
          
        case 0x18: /* NR23. */
          GBC_apu_ch2_freq_lo ( data );
          break;
          
        case 0x19: /* NR24. */
          GBC_apu_ch2_freq_hi ( data );
          break;
         
        case 0x1A: /* NR30. */
          GBC_apu_ch3_sound_on_off_write ( data );
          break;
          
        case 0x1B: /* NR31. */ 
          GBC_apu_ch3_set_length ( data );
          break;
          
        case 0x1C: /* NR32. */
          GBC_apu_ch3_output_level_write ( data );
          break;
          
        case 0x1D: /* NR33. */
          GBC_apu_ch3_freq_lo ( data );
          break;
          
        case 0x1E: /* NR34. */
          GBC_apu_ch3_freq_hi ( data );
          break;
          
        case 0x1F: /* NR40. */
          break;
          
        case 0x20: /* NR41. */
          GBC_apu_ch4_set_length ( data );
          break;
          
        case 0x21: /* NR42. */
          GBC_apu_ch4_volume_envelope_write ( data );
          break;
          
        case 0x22: /* NR43. */
          GBC_apu_ch4_polynomial_counter_write ( data );
          break;
         
        case 0x23: /* NR44. */
          GBC_apu_ch4_init ( data );
          break;
          
        case 0x24: /* NR50. */
          GBC_apu_vin_write ( data );
          break;
          
        case 0x25: /* NR51. */	  
          GBC_apu_select_out_write ( data );
          break;
          
        case 0x26: /* NR52. */
          GBC_apu_turn_on ( data );
          break;
          
          /* Wave Pattern RAM. */
        case 0x30: GBC_apu_ch3_ram_write ( data, 0 ); break;
        case 0x31: GBC_apu_ch3_ram_write ( data, 1 ); break;
        case 0x32: GBC_apu_ch3_ram_write ( data, 2 ); break;
        case 0x33: GBC_apu_ch3_ram_write ( data, 3 ); break;
        case 0x34: GBC_apu_ch3_ram_write ( data, 4 ); break;
        case 0x35: GBC_apu_ch3_ram_write ( data, 5 ); break;
        case 0x36: GBC_apu_ch3_ram_write ( data, 6 ); break;
        case 0x37: GBC_apu_ch3_ram_write ( data, 7 ); break;
        case 0x38: GBC_apu_ch3_ram_write ( data, 8 ); break;
        case 0x39: GBC_apu_ch3_ram_write ( data, 9 ); break;
        case 0x3A: GBC_apu_ch3_ram_write ( data, 10 ); break;
        case 0x3B: GBC_apu_ch3_ram_write ( data, 11 ); break;
        case 0x3C: GBC_apu_ch3_ram_write ( data, 12 ); break;
        case 0x3D: GBC_apu_ch3_ram_write ( data, 13 ); break;
        case 0x3E: GBC_apu_ch3_ram_write ( data, 14 ); break;
        case 0x3F: GBC_apu_ch3_ram_write ( data, 15 ); break;
          
        case 0x40: /* LCDC. */
          GBC_lcd_control_write ( data );
          break;
          
        case 0x41: /* STAT. */
          GBC_lcd_status_write ( data );
          break;
          
        case 0x42: /* SCY. */
          GBC_lcd_scy_write ( data );
          break;
          
        case 0x43: /* SCX. */
          GBC_lcd_scx_write ( data );
          break;
          
        case 0x45: /* LYC. */
          GBC_lcd_lyc_write ( data );
          break;
          
        case 0x46: /* DMA. */
          GBC_lcd_oam_dma ( data );
          break;
          
        case 0x47: /* BGP. */
          GBC_lcd_mpal_bg_set ( data );
          break;
          
        case 0x48: /* OBP0. */
          GBC_lcd_mpal_ob0_set ( data );
          break;
          
        case 0x49: /* OBP1. */
          GBC_lcd_mpal_ob1_set ( data );
          break;
          
        case 0x4A: /* WY. */
          GBC_lcd_wy_write ( data );
          break;
          
        case 0x4B: /* WX. */
          GBC_lcd_wx_write ( data );
          break;
          
        case 0x4C: /* LCDMODE */
          GBC_cpu_set_cgb_mode ( (data&0x80)!=0 );
          GBC_lcd_set_cgb_mode ( (data&0x80)!=0 );
          break;
          
        case 0x4D: /* KEY1. */
          GBC_cpu_speed_prepare ( data );
          break;
          
        case 0x4F: /* VBK. */
          GBC_lcd_select_vram_bank ( data );
          break;
        
        case 0x50: /* BLCK */
          if ( data == 0x11 ) _bios_mapped= GBC_FALSE;
          break;
          
        case 0x51: /* HDMA1. */
          GBC_lcd_vram_dma_src_high ( data );
          break;
          
        case 0x52: /* HDMA2. */
          GBC_lcd_vram_dma_src_low ( data );
          break;
          
        case 0x53: /* HDMA3. */
          GBC_lcd_vram_dma_dst_high ( data );
          break;
          
        case 0x54: /* HDMA4. */
          GBC_lcd_vram_dma_dst_low ( data );
          break;
          
        case 0x55: /* HDMA5. */
          GBC_lcd_vram_dma_init ( data );
          break;
          
        case 0x68: /* BGPI. */
          GBC_lcd_cpal_bg_index ( data );
          break;
          
        case 0x69: /* BGPD. */
          GBC_lcd_cpal_bg_write_data ( data );
          break;
          
        case 0x6A: /* OBPI. */
          GBC_lcd_cpal_ob_index ( data );
          break;
          
        case 0x6B: /* OBPD. */
          GBC_lcd_cpal_ob_write_data ( data );
          break;
          
        case 0x6C: /* PAL_LOCK */
          GBC_lcd_pal_lock ( data );
          break;
          
        case 0x70: /* SVBK. */
          /* Mirant el codi de la BIOS he aplegat a la conclusió de
             que aquesta opció sempre té que estar activa. */
          _svbk= data;
          aux= data&0x7;
          if ( aux == 0 ) aux= 1;
          _ram1= &(_ram[aux][0]);
          break;
          
        default:
          printf ( "Write I/O port FF%02x\n", addr&0xFF );
          
        }
    }
  
  /* HRAM. */
  else if ( addr < 0xFFFF ) _hram[addr&0x7F]= data;
  
  /* Interrupt Enable Register. */
  else GBC_cpu_write_IE ( data );
  
} /* end mem_write */


static void
mem_write_trace (
        	 const GBCu16 addr,
        	 const GBCu8  data
        	 )
{
  
  mem_write ( addr, data );
  _mem_access ( GBC_WRITE, addr, data, _udata );
  
} /* end mem_write_trace */




/**********************/
/* FUNCIONS PÚBLIQUES */
/**********************/

void
GBC_mem_init (
              const GBCu8     bios[0x900],
              GBC_MemAccess  *mem_access,
              void           *udata
              )
{
  
  _mem_access= mem_access;
  _udata= udata;
  _bios= bios;

  GBC_mem_init_state ();
  
  /* Funcions. */
  _mem_read= mem_read;
  _mem_write= mem_write;
  
} /* end GBC_mem_init */


void
GBC_mem_init_state (void)
{
  
  _bios_mapped= (_bios!=NULL);
  
  /* RAM. */
  memset ( _ram[0], 0, 8*RAM_PAGE_SIZE );
  _ram0= &(_ram[0][0]);
  _ram1= &(_ram[1][0]);
  _svbk= 0x00;
  
  /* HRAM. */
  memset ( _hram, 0, HRAM_SIZE );
  
} /* end GBC_mem_init_state */


GBC_Bool
GBC_mem_is_bios_mapped (void)
{
  return _bios_mapped;
} /* end GBC_mem_is_bios_mapped */


GBCu8
GBC_mem_read (
              const GBCu16 addr
              )
{
  return _mem_read ( addr );
} /* end GBC_mem_read */


void
GBC_mem_set_mode_trace (
        		const GBC_Bool val
        		)
{
  
  if ( _mem_access == NULL ) return;
  if ( val )
    {
      _mem_read= mem_read_trace;
      _mem_write= mem_write_trace;
    }
  else
    {
      _mem_read= mem_read;
      _mem_write= mem_write;
    }
  
} /* end GBC_mem_set_mode_trace */


void
GBC_mem_write (
               const GBCu16 addr,
               const GBCu8  data
               )
{
  _mem_write ( addr, data );
} /* end GBC_mem_write */


int
GBC_mem_save_state (
        	    FILE *f
        	    )
{

  int p;


  SAVE ( _bios_mapped );
  SAVE ( _ram );
  if ( _ram1 == _ram[0] ) p= 0;
  else if ( _ram1 == _ram[1] ) p= 1;
  else if ( _ram1 == _ram[2] ) p= 2;
  else if ( _ram1 == _ram[3] ) p= 3;
  else if ( _ram1 == _ram[4] ) p= 4;
  else if ( _ram1 == _ram[5] ) p= 5;
  else if ( _ram1 == _ram[6] ) p= 6;
  else p= 7;
  SAVE ( p );
  SAVE ( _svbk );
  SAVE ( _hram );
  
  return 0;
  
} /* end GBC_mem_save_state */


int
GBC_mem_load_state (
        	    FILE *f
        	    )
{

  int p;

  
  LOAD ( _bios_mapped );
  CHECK ( !_bios_mapped || _bios != NULL );
  LOAD ( _ram );
  LOAD ( p );
  CHECK ( p >= 0 && p < 8 );
  _ram0= &(_ram[0][0]);
  _ram1= &(_ram[p][0]);
  LOAD ( _svbk );
  LOAD ( _hram );

  return 0;
  
} /* end GBC_mem_load_state */
