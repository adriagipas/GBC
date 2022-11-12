/*
 * Copyright 2011,2022 Adrià Giménez Pastor.
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
 *  rom.c - Implementa funcions relacionades amb la ROM.
 *
 */


#include <ctype.h>
#include <stddef.h>
#include <stdlib.h>

#include "GBC.h"




/**********************/
/* FUNCIONS PÚBLIQUES */
/**********************/

GBC_Bool
GBC_rom_check_checksum (
        		const GBC_Rom *rom
        		)
{
  
  int aux, i;
  const GBCu8 *p;
  
  
  p= &(rom->banks[0][0]);
  aux= 0;
  for ( i= 0x134; i <= 0x14C; ++i )
    aux+= p[i]+1;
  
  return (((GBCu8) ((-aux)&0xFF)) == rom->banks[0][0x14D]);
  
} /* end GBC_rom_check_checksum */


GBC_Bool
GBC_rom_check_global_checksum (
        		       const GBC_Rom *rom
        		       )
{
  
  int aux, i;
  const GBCu8 *p, *end;
  GBCu16 checksum;
  
  
  p= &(rom->banks[0][0]);
  end= p+rom->nbanks*GBC_BANK_SIZE;
  aux= 0;
  for ( i= 0; i < 0x14E; ++i )
    aux+= *(p++);
  checksum= ((GBCu16) *(p++))<<8;
  checksum|= *(p++);
  for ( ; p != end; ++p ) aux+= *p;
  
  return ((GBCu16) (aux&0xFFFF)) == checksum;
  
} /* end GBC_rom_check_global_checksum */


GBC_Bool
GBC_rom_check_nintendo_logo (
        		     const GBC_Rom *rom
        		     )
{
  
  /* La GBC sols comprova els 24 primers.*/
  static const GBCu8 logo[48]=
    {
      0xCE, 0xED, 0x66, 0x66, 0xCC, 0x0D, 0x00, 0x0B,
      0x03, 0x73, 0x00, 0x83, 0x00, 0x0C, 0x00, 0x0D,
      0x00, 0x08, 0x11, 0x1F, 0x88, 0x89, 0x00, 0x0E,
      0xDC, 0xCC, 0x6E, 0xE6, 0xDD, 0xDD, 0xD9, 0x99,
      0xBB, 0xBB, 0x67, 0x63, 0x6E, 0x0E, 0xEC, 0xCC,
      0xDD, 0xDC, 0x99, 0x9F, 0xBB, 0xB9, 0x33, 0x3E
    };
  int i;
  const GBCu8 *p;
  
  
  for ( i= 0, p= &(rom->banks[0][0x104]); i < 24; ++i )
    if ( logo[i] != p[i] ) return GBC_FALSE;
  
  return GBC_TRUE;
  
} /* end GBC_rom_check_nintendo_logo */


void
GBC_rom_get_header (
        	    const GBC_Rom *rom,
        	    GBC_RomHeader *header
        	    )
{
  
  const GBCu8 *p, *bank0;
  GBCu8 aux, i;
  
  
  bank0= &(rom->banks[0][0]);
  
  /* Flag CGB, títol i codi fabricant. */
  /* Açò és un poc heurístic. */
  aux= rom->banks[0][0x143];
  p= &(bank0[0x134]);
  if ( aux&0x80 )
    {
      if ( aux&0xC0 ) header->cgb_flag= GBC_ROM_ONLY_GBC;
      else            header->cgb_flag= GBC_ROM_GBC;
      for ( i= 0; i < 15 && isascii ( p[i] ) &&
              (isupper ( p[i] ) || isspace ( p[i] )); ++i )
        header->title[i]= p[i];
      header->title[i]= '\0';
      if ( i <= 11 || i == 15 )
        {
          p= &(bank0[0x13F]);
          for ( i= 0; i < 4 && isascii ( p[i] ) && isupper ( p[i] ); ++i )
            header->manufacturer[i]= p[i];
          if ( i != 4 ) header->manufacturer[0]= '\0';
          else          header->manufacturer[i]= '\0';
        }
      else header->manufacturer[0]= '\0';
    }
  else /* Asumiré que tots els cartutxos de GB són antics. */
    {
      header->cgb_flag= GBC_ROM_GB;
      header->manufacturer[0]= '\0';
      for ( i= 0; i < 16 && isascii ( p[i] ) && isupper ( p[i] ); ++i )
        header->title[i]= p[i];
      header->title[i]= '\0';
    }
  
  /* Codi de la llicència. */
  header->old_license= bank0[0x14B];
  if ( header->old_license == 0x33 )
    {
      header->new_license[0]= bank0[0x144];
      header->new_license[1]= bank0[0x145];
      header->new_license[2]= '\0';
    }
  else header->new_license[0]= '\0';
  
  /* Flag SGB. */
  header->sgb_flag= (bank0[0x146]==0x03);
  
  /* Mapper. */
  header->mapper= GBC_rom_mapper2str ( GBC_rom_get_mapper ( rom ) );
  
  /* Rom size. */
  aux= bank0[0x148];
  if ( aux < 0x8 ) header->rom_size= 2<<aux;
  else
    {
      switch ( aux )
        {
        case 0x52: header->rom_size= 72; break;
        case 0x53: header->rom_size= 80; break;
        case 0x54: header->rom_size= 96; break;
        default: header->rom_size= -1; break;
        }
    }
  
  /* Ram size. */
  header->ram_size= GBC_rom_get_ram_size ( rom );
  
  /* Japanse. */
  header->japanese_rom= (bank0[0x14A]==0x00);
  
  /* Version. */
  header->version= bank0[0x14C];
  
  /* Header Checksum. */
  header->checksum= bank0[0x14D];
  
  /* Global Checksum. */
  header->global_checksum= (((GBCu16) bank0[0x14E])<<8) | bank0[0x14F];
  
} /* end GBC_rom_get_header */


GBC_Mapper
GBC_rom_get_mapper (
        	    const GBC_Rom *rom
        	    )
{
  
  switch ( rom->banks[0][0x147] )
    {
    case 0x00: return GBC_ROM;
    case 0x01: return GBC_MBC1;
    case 0x02: return GBC_MBC1_RAM;
    case 0x03: return GBC_MBC1_RAM_BATTERY;
    case 0x05: return GBC_MBC2;
    case 0x06: return GBC_MBC2_BATTERY;
    case 0x08: return GBC_ROM_RAM;
    case 0x09: return GBC_ROM_RAM_BATTERY;
    case 0x0B: return GBC_MMM01;
    case 0x0C: return GBC_MMM01_RAM;
    case 0x0D: return GBC_MMM01_RAM_BATTERY;
    case 0x0F: return GBC_MBC3_TIMER_BATTERY;
    case 0x10: return GBC_MBC3_TIMER_RAM_BATTERY;
    case 0x11: return GBC_MBC3;
    case 0x12: return GBC_MBC3_RAM;
    case 0x13: return GBC_MBC3_RAM_BATTERY;
    case 0x15: return GBC_MBC4;
    case 0x16: return GBC_MBC4_RAM;
    case 0x17: return GBC_MBC4_RAM_BATTERY;
    case 0x19: return GBC_MBC5;
    case 0x1A: return GBC_MBC5_RAM;
    case 0x1B: return GBC_MBC5_RAM_BATTERY;
    case 0x1C: return GBC_MBC5_RUMBLE;
    case 0x1D: return GBC_MBC5_RUMBLE_RAM;
    case 0x1E: return GBC_MBC5_RUMBLE_RAM_BATTERY;
    case 0xFC: return GBC_POCKET_CAMERA;
    case 0xFD: return GBC_BANDAI_TAMA5;
    case 0xFE: return GBC_HUC3;
    case 0xFF: return GBC_HUC1_RAM_BATTERY;
    default: return GBC_UNKMAPPER;
    }
  
} /* end GBC_rom_get_mapper */


int
GBC_rom_get_ram_size (
        	      const GBC_Rom *rom
        	      )
{
  
  GBCu8 aux;
  
  
  aux= rom->banks[0][0x149];
  switch ( aux )
    {
    case 0x00: return 0;
    case 0x01: return 2;
    case 0x02: return 8;
    case 0x03: return 32;
    default: return -1;
    }
  
} /* end GBC_rom_get_ram_size */


const char *
GBC_rom_mapper2str (
        	    const GBC_Mapper mapper
        	    )
{
  
  switch ( mapper )
    {
    case GBC_UNKMAPPER: return "Unknown";
    case GBC_ROM: return "ROM";
    case GBC_MBC1: return "MBC1";
    case GBC_MBC1_RAM: return "MBC1+RAM";
    case GBC_MBC1_RAM_BATTERY: return "MBC1+RAM+BATTERY";
    case GBC_MBC2: return "MBC2";
    case GBC_MBC2_BATTERY: return "MBC2+BATTERY";
    case GBC_ROM_RAM: return "ROM+RAM";
    case GBC_ROM_RAM_BATTERY: return "ROM+RAM+BATTERY";
    case GBC_MMM01: return "MMM01";
    case GBC_MMM01_RAM: return "MMM01+RAM";
    case GBC_MMM01_RAM_BATTERY: return "MMM01+RAM+BATTERY";
    case GBC_MBC3_TIMER_BATTERY: return "MBC3+TIMER+BATTERY";
    case GBC_MBC3_TIMER_RAM_BATTERY: return "MBC3+TIMER+RAM+BATTERY";
    case GBC_MBC3: return "MBC3";
    case GBC_MBC3_RAM: return "MBC3+RAM";
    case GBC_MBC3_RAM_BATTERY: return "MBC3+RAM+BATTERY";
    case GBC_MBC4: return "MBC4";
    case GBC_MBC4_RAM: return "MBC4+RAM";
    case GBC_MBC4_RAM_BATTERY: return "MBC4+RAM+BATTERY";
    case GBC_MBC5: return "MBC5";
    case GBC_MBC5_RAM: return "MBC5+RAM";
    case GBC_MBC5_RAM_BATTERY: return "MBC5+RAM+BATTERY";
    case GBC_MBC5_RUMBLE: return "MBC5+RUMBLE";
    case GBC_MBC5_RUMBLE_RAM: return "MBC5+RUMBLE+RAM";
    case GBC_MBC5_RUMBLE_RAM_BATTERY: return "MBC5+RUMBLE+RAM+BATTERY";
    case GBC_POCKET_CAMERA: return "POCKET CAMERA";
    case GBC_BANDAI_TAMA5: return "BANDAI TAMA5";
    case GBC_HUC3: return "HuC3";
    case GBC_HUC1_RAM_BATTERY: return "HuC1+RAM+BATTERY";
    default: return NULL;
    }
  
} /* end GBC_rom_mapper2str */
