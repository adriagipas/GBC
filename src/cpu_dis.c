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
 *  cpu_dis.c - Implementació de les instruccions de desassemblatge de
 *              codi.
 *
 *  NOTA: Implementació basad en la del Z80.
 *
 */


#include <stddef.h>
#include <stdlib.h>

#include "GBC.h"




/**********/
/* MACROS */
/**********/

#define UNK { GBC_UNK, GBC_NONE, GBC_NONE }




/*************/
/* CONSTANTS */
/*************/

static const GBC_InstId _insts_cb[256]=
  {
    /* 0x0 */  { GBC_RLC, GBC_B, GBC_NONE },
    /* 0x1 */  { GBC_RLC, GBC_C, GBC_NONE },
    /* 0x2 */  { GBC_RLC, GBC_D, GBC_NONE },
    /* 0x3 */  { GBC_RLC, GBC_E, GBC_NONE },
    /* 0x4 */  { GBC_RLC, GBC_H, GBC_NONE },
    /* 0x5 */  { GBC_RLC, GBC_L, GBC_NONE },
    /* 0x6 */  { GBC_RLC, GBC_pHL, GBC_NONE },
    /* 0x7 */  { GBC_RLC, GBC_A, GBC_NONE },
    /* 0x8 */  { GBC_RRC, GBC_B, GBC_NONE },
    /* 0x9 */  { GBC_RRC, GBC_C, GBC_NONE },
    /* 0xa */  { GBC_RRC, GBC_D, GBC_NONE },
    /* 0xb */  { GBC_RRC, GBC_E, GBC_NONE },
    /* 0xc */  { GBC_RRC, GBC_H, GBC_NONE },
    /* 0xd */  { GBC_RRC, GBC_L, GBC_NONE },
    /* 0xe */  { GBC_RRC, GBC_pHL, GBC_NONE },
    /* 0xf */  { GBC_RRC, GBC_A, GBC_NONE },
    /* 0x10 */ { GBC_RL, GBC_B, GBC_NONE },
    /* 0x11 */ { GBC_RL, GBC_C, GBC_NONE },
    /* 0x12 */ { GBC_RL, GBC_D, GBC_NONE },
    /* 0x13 */ { GBC_RL, GBC_E, GBC_NONE },
    /* 0x14 */ { GBC_RL, GBC_H, GBC_NONE },
    /* 0x15 */ { GBC_RL, GBC_L, GBC_NONE },
    /* 0x16 */ { GBC_RL, GBC_pHL, GBC_NONE },
    /* 0x17 */ { GBC_RL, GBC_A, GBC_NONE },
    /* 0x18 */ { GBC_RR, GBC_B, GBC_NONE },
    /* 0x19 */ { GBC_RR, GBC_C, GBC_NONE },
    /* 0x1a */ { GBC_RR, GBC_D, GBC_NONE },
    /* 0x1b */ { GBC_RR, GBC_E, GBC_NONE },
    /* 0x1c */ { GBC_RR, GBC_H, GBC_NONE },
    /* 0x1d */ { GBC_RR, GBC_L, GBC_NONE },
    /* 0x1e */ { GBC_RR, GBC_pHL, GBC_NONE },
    /* 0x1f */ { GBC_RR, GBC_A, GBC_NONE },
    /* 0x20 */ { GBC_SLA, GBC_B, GBC_NONE },
    /* 0x21 */ { GBC_SLA, GBC_C, GBC_NONE },
    /* 0x22 */ { GBC_SLA, GBC_D, GBC_NONE },
    /* 0x23 */ { GBC_SLA, GBC_E, GBC_NONE },
    /* 0x24 */ { GBC_SLA, GBC_H, GBC_NONE },
    /* 0x25 */ { GBC_SLA, GBC_L, GBC_NONE },
    /* 0x26 */ { GBC_SLA, GBC_pHL, GBC_NONE },
    /* 0x27 */ { GBC_SLA, GBC_A, GBC_NONE },
    /* 0x28 */ { GBC_SRA, GBC_B, GBC_NONE },
    /* 0x29 */ { GBC_SRA, GBC_C, GBC_NONE },
    /* 0x2a */ { GBC_SRA, GBC_D, GBC_NONE },
    /* 0x2b */ { GBC_SRA, GBC_E, GBC_NONE },
    /* 0x2c */ { GBC_SRA, GBC_H, GBC_NONE },
    /* 0x2d */ { GBC_SRA, GBC_L, GBC_NONE },
    /* 0x2e */ { GBC_SRA, GBC_pHL, GBC_NONE },
    /* 0x2f */ { GBC_SRA, GBC_A, GBC_NONE },
    /* 0x30 */ { GBC_SWAP, GBC_B, GBC_NONE },
    /* 0x31 */ { GBC_SWAP, GBC_C, GBC_NONE },
    /* 0x32 */ { GBC_SWAP, GBC_D, GBC_NONE },
    /* 0x33 */ { GBC_SWAP, GBC_E, GBC_NONE },
    /* 0x34 */ { GBC_SWAP, GBC_H, GBC_NONE },
    /* 0x35 */ { GBC_SWAP, GBC_L, GBC_NONE },
    /* 0x36 */ { GBC_SWAP, GBC_pHL, GBC_NONE },
    /* 0x37 */ { GBC_SWAP, GBC_A, GBC_NONE },
    /* 0x38 */ { GBC_SRL, GBC_B, GBC_NONE },
    /* 0x39 */ { GBC_SRL, GBC_C, GBC_NONE },
    /* 0x3a */ { GBC_SRL, GBC_D, GBC_NONE },
    /* 0x3b */ { GBC_SRL, GBC_E, GBC_NONE },
    /* 0x3c */ { GBC_SRL, GBC_H, GBC_NONE },
    /* 0x3d */ { GBC_SRL, GBC_L, GBC_NONE },
    /* 0x3e */ { GBC_SRL, GBC_pHL, GBC_NONE },
    /* 0x3f */ { GBC_SRL, GBC_A, GBC_NONE },
    /* 0x40 */ { GBC_BIT, GBC_B0, GBC_B },
    /* 0x41 */ { GBC_BIT, GBC_B0, GBC_C },
    /* 0x42 */ { GBC_BIT, GBC_B0, GBC_D },
    /* 0x43 */ { GBC_BIT, GBC_B0, GBC_E },
    /* 0x44 */ { GBC_BIT, GBC_B0, GBC_H },
    /* 0x45 */ { GBC_BIT, GBC_B0, GBC_L },
    /* 0x46 */ { GBC_BIT, GBC_B0, GBC_pHL },
    /* 0x47 */ { GBC_BIT, GBC_B0, GBC_A },
    /* 0x48 */ { GBC_BIT, GBC_B1, GBC_B },
    /* 0x49 */ { GBC_BIT, GBC_B1, GBC_C },
    /* 0x4a */ { GBC_BIT, GBC_B1, GBC_D },
    /* 0x4b */ { GBC_BIT, GBC_B1, GBC_E },
    /* 0x4c */ { GBC_BIT, GBC_B1, GBC_H },
    /* 0x4d */ { GBC_BIT, GBC_B1, GBC_L },
    /* 0x4e */ { GBC_BIT, GBC_B1, GBC_pHL },
    /* 0x4f */ { GBC_BIT, GBC_B1, GBC_A },
    /* 0x50 */ { GBC_BIT, GBC_B2, GBC_B },
    /* 0x51 */ { GBC_BIT, GBC_B2, GBC_C },
    /* 0x52 */ { GBC_BIT, GBC_B2, GBC_D },
    /* 0x53 */ { GBC_BIT, GBC_B2, GBC_E },
    /* 0x54 */ { GBC_BIT, GBC_B2, GBC_H },
    /* 0x55 */ { GBC_BIT, GBC_B2, GBC_L },
    /* 0x56 */ { GBC_BIT, GBC_B2, GBC_pHL },
    /* 0x57 */ { GBC_BIT, GBC_B2, GBC_A },
    /* 0x58 */ { GBC_BIT, GBC_B3, GBC_B },
    /* 0x59 */ { GBC_BIT, GBC_B3, GBC_C },
    /* 0x5a */ { GBC_BIT, GBC_B3, GBC_D },
    /* 0x5b */ { GBC_BIT, GBC_B3, GBC_E },
    /* 0x5c */ { GBC_BIT, GBC_B3, GBC_H },
    /* 0x5d */ { GBC_BIT, GBC_B3, GBC_L },
    /* 0x5e */ { GBC_BIT, GBC_B3, GBC_pHL },
    /* 0x5f */ { GBC_BIT, GBC_B3, GBC_A },
    /* 0x60 */ { GBC_BIT, GBC_B4, GBC_B },
    /* 0x61 */ { GBC_BIT, GBC_B4, GBC_C },
    /* 0x62 */ { GBC_BIT, GBC_B4, GBC_D },
    /* 0x63 */ { GBC_BIT, GBC_B4, GBC_E },
    /* 0x64 */ { GBC_BIT, GBC_B4, GBC_H },
    /* 0x65 */ { GBC_BIT, GBC_B4, GBC_L },
    /* 0x66 */ { GBC_BIT, GBC_B4, GBC_pHL },
    /* 0x67 */ { GBC_BIT, GBC_B4, GBC_A },
    /* 0x68 */ { GBC_BIT, GBC_B5, GBC_B },
    /* 0x69 */ { GBC_BIT, GBC_B5, GBC_C },
    /* 0x6a */ { GBC_BIT, GBC_B5, GBC_D },
    /* 0x6b */ { GBC_BIT, GBC_B5, GBC_E },
    /* 0x6c */ { GBC_BIT, GBC_B5, GBC_H },
    /* 0x6d */ { GBC_BIT, GBC_B5, GBC_L },
    /* 0x6e */ { GBC_BIT, GBC_B5, GBC_pHL },
    /* 0x6f */ { GBC_BIT, GBC_B5, GBC_A },
    /* 0x70 */ { GBC_BIT, GBC_B6, GBC_B },
    /* 0x71 */ { GBC_BIT, GBC_B6, GBC_C },
    /* 0x72 */ { GBC_BIT, GBC_B6, GBC_D },
    /* 0x73 */ { GBC_BIT, GBC_B6, GBC_E },
    /* 0x74 */ { GBC_BIT, GBC_B6, GBC_H },
    /* 0x75 */ { GBC_BIT, GBC_B6, GBC_L },
    /* 0x76 */ { GBC_BIT, GBC_B6, GBC_pHL },
    /* 0x77 */ { GBC_BIT, GBC_B6, GBC_A },
    /* 0x78 */ { GBC_BIT, GBC_B7, GBC_B },
    /* 0x79 */ { GBC_BIT, GBC_B7, GBC_C },
    /* 0x7a */ { GBC_BIT, GBC_B7, GBC_D },
    /* 0x7b */ { GBC_BIT, GBC_B7, GBC_E },
    /* 0x7c */ { GBC_BIT, GBC_B7, GBC_H },
    /* 0x7d */ { GBC_BIT, GBC_B7, GBC_L },
    /* 0x7e */ { GBC_BIT, GBC_B7, GBC_pHL },
    /* 0x7f */ { GBC_BIT, GBC_B7, GBC_A },
    /* 0x80 */ { GBC_RES, GBC_B0, GBC_B },
    /* 0x81 */ { GBC_RES, GBC_B0, GBC_C },
    /* 0x82 */ { GBC_RES, GBC_B0, GBC_D },
    /* 0x83 */ { GBC_RES, GBC_B0, GBC_E },
    /* 0x84 */ { GBC_RES, GBC_B0, GBC_H },
    /* 0x85 */ { GBC_RES, GBC_B0, GBC_L },
    /* 0x86 */ { GBC_RES, GBC_B0, GBC_pHL },
    /* 0x87 */ { GBC_RES, GBC_B0, GBC_A },
    /* 0x88 */ { GBC_RES, GBC_B1, GBC_B },
    /* 0x89 */ { GBC_RES, GBC_B1, GBC_C },
    /* 0x8a */ { GBC_RES, GBC_B1, GBC_D },
    /* 0x8b */ { GBC_RES, GBC_B1, GBC_E },
    /* 0x8c */ { GBC_RES, GBC_B1, GBC_H },
    /* 0x8d */ { GBC_RES, GBC_B1, GBC_L },
    /* 0x8e */ { GBC_RES, GBC_B1, GBC_pHL },
    /* 0x8f */ { GBC_RES, GBC_B1, GBC_A },
    /* 0x90 */ { GBC_RES, GBC_B2, GBC_B },
    /* 0x91 */ { GBC_RES, GBC_B2, GBC_C },
    /* 0x92 */ { GBC_RES, GBC_B2, GBC_D },
    /* 0x93 */ { GBC_RES, GBC_B2, GBC_E },
    /* 0x94 */ { GBC_RES, GBC_B2, GBC_H },
    /* 0x95 */ { GBC_RES, GBC_B2, GBC_L },
    /* 0x96 */ { GBC_RES, GBC_B2, GBC_pHL },
    /* 0x97 */ { GBC_RES, GBC_B2, GBC_A },
    /* 0x98 */ { GBC_RES, GBC_B3, GBC_B },
    /* 0x99 */ { GBC_RES, GBC_B3, GBC_C },
    /* 0x9a */ { GBC_RES, GBC_B3, GBC_D },
    /* 0x9b */ { GBC_RES, GBC_B3, GBC_E },
    /* 0x9c */ { GBC_RES, GBC_B3, GBC_H },
    /* 0x9d */ { GBC_RES, GBC_B3, GBC_L },
    /* 0x9e */ { GBC_RES, GBC_B3, GBC_pHL },
    /* 0x9f */ { GBC_RES, GBC_B3, GBC_A },
    /* 0xa0 */ { GBC_RES, GBC_B4, GBC_B },
    /* 0xa1 */ { GBC_RES, GBC_B4, GBC_C },
    /* 0xa2 */ { GBC_RES, GBC_B4, GBC_D },
    /* 0xa3 */ { GBC_RES, GBC_B4, GBC_E },
    /* 0xa4 */ { GBC_RES, GBC_B4, GBC_H },
    /* 0xa5 */ { GBC_RES, GBC_B4, GBC_L },
    /* 0xa6 */ { GBC_RES, GBC_B4, GBC_pHL },
    /* 0xa7 */ { GBC_RES, GBC_B4, GBC_A },
    /* 0xa8 */ { GBC_RES, GBC_B5, GBC_B },
    /* 0xa9 */ { GBC_RES, GBC_B5, GBC_C },
    /* 0xaa */ { GBC_RES, GBC_B5, GBC_D },
    /* 0xab */ { GBC_RES, GBC_B5, GBC_E },
    /* 0xac */ { GBC_RES, GBC_B5, GBC_H },
    /* 0xad */ { GBC_RES, GBC_B5, GBC_L },
    /* 0xae */ { GBC_RES, GBC_B5, GBC_pHL },
    /* 0xaf */ { GBC_RES, GBC_B5, GBC_A },
    /* 0xb0 */ { GBC_RES, GBC_B6, GBC_B },
    /* 0xb1 */ { GBC_RES, GBC_B6, GBC_C },
    /* 0xb2 */ { GBC_RES, GBC_B6, GBC_D },
    /* 0xb3 */ { GBC_RES, GBC_B6, GBC_E },
    /* 0xb4 */ { GBC_RES, GBC_B6, GBC_H },
    /* 0xb5 */ { GBC_RES, GBC_B6, GBC_L },
    /* 0xb6 */ { GBC_RES, GBC_B6, GBC_pHL },
    /* 0xb7 */ { GBC_RES, GBC_B6, GBC_A },
    /* 0xb8 */ { GBC_RES, GBC_B7, GBC_B },
    /* 0xb9 */ { GBC_RES, GBC_B7, GBC_C },
    /* 0xba */ { GBC_RES, GBC_B7, GBC_D },
    /* 0xbb */ { GBC_RES, GBC_B7, GBC_E },
    /* 0xbc */ { GBC_RES, GBC_B7, GBC_H },
    /* 0xbd */ { GBC_RES, GBC_B7, GBC_L },
    /* 0xbe */ { GBC_RES, GBC_B7, GBC_pHL },
    /* 0xbf */ { GBC_RES, GBC_B7, GBC_A },
    /* 0xc0 */ { GBC_SET, GBC_B0, GBC_B },
    /* 0xc1 */ { GBC_SET, GBC_B0, GBC_C },
    /* 0xc2 */ { GBC_SET, GBC_B0, GBC_D },
    /* 0xc3 */ { GBC_SET, GBC_B0, GBC_E },
    /* 0xc4 */ { GBC_SET, GBC_B0, GBC_H },
    /* 0xc5 */ { GBC_SET, GBC_B0, GBC_L },
    /* 0xc6 */ { GBC_SET, GBC_B0, GBC_pHL },
    /* 0xc7 */ { GBC_SET, GBC_B0, GBC_A },
    /* 0xc8 */ { GBC_SET, GBC_B1, GBC_B },
    /* 0xc9 */ { GBC_SET, GBC_B1, GBC_C },
    /* 0xca */ { GBC_SET, GBC_B1, GBC_D },
    /* 0xcb */ { GBC_SET, GBC_B1, GBC_E },
    /* 0xcc */ { GBC_SET, GBC_B1, GBC_H },
    /* 0xcd */ { GBC_SET, GBC_B1, GBC_L },
    /* 0xce */ { GBC_SET, GBC_B1, GBC_pHL },
    /* 0xcf */ { GBC_SET, GBC_B1, GBC_A },
    /* 0xd0 */ { GBC_SET, GBC_B2, GBC_B },
    /* 0xd1 */ { GBC_SET, GBC_B2, GBC_C },
    /* 0xd2 */ { GBC_SET, GBC_B2, GBC_D },
    /* 0xd3 */ { GBC_SET, GBC_B2, GBC_E },
    /* 0xd4 */ { GBC_SET, GBC_B2, GBC_H },
    /* 0xd5 */ { GBC_SET, GBC_B2, GBC_L },
    /* 0xd6 */ { GBC_SET, GBC_B2, GBC_pHL },
    /* 0xd7 */ { GBC_SET, GBC_B2, GBC_A },
    /* 0xd8 */ { GBC_SET, GBC_B3, GBC_B },
    /* 0xd9 */ { GBC_SET, GBC_B3, GBC_C },
    /* 0xda */ { GBC_SET, GBC_B3, GBC_D },
    /* 0xdb */ { GBC_SET, GBC_B3, GBC_E },
    /* 0xdc */ { GBC_SET, GBC_B3, GBC_H },
    /* 0xdd */ { GBC_SET, GBC_B3, GBC_L },
    /* 0xde */ { GBC_SET, GBC_B3, GBC_pHL },
    /* 0xdf */ { GBC_SET, GBC_B3, GBC_A },
    /* 0xe0 */ { GBC_SET, GBC_B4, GBC_B },
    /* 0xe1 */ { GBC_SET, GBC_B4, GBC_C },
    /* 0xe2 */ { GBC_SET, GBC_B4, GBC_D },
    /* 0xe3 */ { GBC_SET, GBC_B4, GBC_E },
    /* 0xe4 */ { GBC_SET, GBC_B4, GBC_H },
    /* 0xe5 */ { GBC_SET, GBC_B4, GBC_L },
    /* 0xe6 */ { GBC_SET, GBC_B4, GBC_pHL },
    /* 0xe7 */ { GBC_SET, GBC_B4, GBC_A },
    /* 0xe8 */ { GBC_SET, GBC_B5, GBC_B },
    /* 0xe9 */ { GBC_SET, GBC_B5, GBC_C },
    /* 0xea */ { GBC_SET, GBC_B5, GBC_D },
    /* 0xeb */ { GBC_SET, GBC_B5, GBC_E },
    /* 0xec */ { GBC_SET, GBC_B5, GBC_H },
    /* 0xed */ { GBC_SET, GBC_B5, GBC_L },
    /* 0xee */ { GBC_SET, GBC_B5, GBC_pHL },
    /* 0xef */ { GBC_SET, GBC_B5, GBC_A },
    /* 0xf0 */ { GBC_SET, GBC_B6, GBC_B },
    /* 0xf1 */ { GBC_SET, GBC_B6, GBC_C },
    /* 0xf2 */ { GBC_SET, GBC_B6, GBC_D },
    /* 0xf3 */ { GBC_SET, GBC_B6, GBC_E },
    /* 0xf4 */ { GBC_SET, GBC_B6, GBC_H },
    /* 0xf5 */ { GBC_SET, GBC_B6, GBC_L },
    /* 0xf6 */ { GBC_SET, GBC_B6, GBC_pHL },
    /* 0xf7 */ { GBC_SET, GBC_B6, GBC_A },
    /* 0xf8 */ { GBC_SET, GBC_B7, GBC_B },
    /* 0xf9 */ { GBC_SET, GBC_B7, GBC_C },
    /* 0xfa */ { GBC_SET, GBC_B7, GBC_D },
    /* 0xfb */ { GBC_SET, GBC_B7, GBC_E },
    /* 0xfc */ { GBC_SET, GBC_B7, GBC_H },
    /* 0xfd */ { GBC_SET, GBC_B7, GBC_L },
    /* 0xfe */ { GBC_SET, GBC_B7, GBC_pHL },
    /* 0xff */ { GBC_SET, GBC_B7, GBC_A }
  };


static const GBC_InstId _insts[256]=
  {
    /* 0x00 */ { GBC_NOP, GBC_NONE, GBC_NONE },
    /* 0x01 */ { GBC_LD, GBC_BC, GBC_WORD },
    /* 0x02 */ { GBC_LD, GBC_pBC, GBC_A },
    /* 0x03 */ { GBC_INC, GBC_BC, GBC_NONE },
    /* 0x04 */ { GBC_INC, GBC_B, GBC_NONE },
    /* 0x05 */ { GBC_DEC, GBC_B, GBC_NONE },
    /* 0x06 */ { GBC_LD, GBC_B, GBC_BYTE },
    /* 0x07 */ { GBC_RLCA, GBC_NONE, GBC_NONE },
    /* 0x08 */ { GBC_LD, GBC_ADDR, GBC_SP },
    /* 0x09 */ { GBC_ADD, GBC_HL, GBC_BC },
    /* 0x0a */ { GBC_LD, GBC_A, GBC_pBC },
    /* 0x0b */ { GBC_DEC, GBC_BC, GBC_NONE },
    /* 0x0c */ { GBC_INC, GBC_C, GBC_NONE },
    /* 0x0d */ { GBC_DEC, GBC_C, GBC_NONE },
    /* 0x0e */ { GBC_LD, GBC_C, GBC_BYTE },
    /* 0x0f */ { GBC_RRCA, GBC_NONE, GBC_NONE },
    /* 0x10 */ { GBC_STOP, GBC_NONE, GBC_NONE },
    /* 0x11 */ { GBC_LD, GBC_DE, GBC_WORD },
    /* 0x12 */ { GBC_LD, GBC_pDE, GBC_A },
    /* 0x13 */ { GBC_INC, GBC_DE, GBC_NONE },
    /* 0x14 */ { GBC_INC, GBC_D, GBC_NONE },
    /* 0x15 */ { GBC_DEC, GBC_D, GBC_NONE },
    /* 0x16 */ { GBC_LD, GBC_D, GBC_BYTE },
    /* 0x17 */ { GBC_RLA, GBC_NONE, GBC_NONE },
    /* 0x18 */ { GBC_JR, GBC_BRANCH, GBC_NONE },
    /* 0x19 */ { GBC_ADD, GBC_HL, GBC_DE },
    /* 0x1a */ { GBC_LD, GBC_A, GBC_pDE },
    /* 0x1b */ { GBC_DEC, GBC_DE, GBC_NONE },
    /* 0x1c */ { GBC_INC, GBC_E, GBC_NONE },
    /* 0x1d */ { GBC_DEC, GBC_E, GBC_NONE },
    /* 0x1e */ { GBC_LD, GBC_E, GBC_BYTE },
    /* 0x1f */ { GBC_RRA, GBC_NONE, GBC_NONE },
    /* 0x20 */ { GBC_JR, GBC_F_NZ, GBC_BRANCH },
    /* 0x21 */ { GBC_LD, GBC_HL, GBC_WORD },
    /* 0x22 */ { GBC_LDI, GBC_pHL, GBC_A },
    /* 0x23 */ { GBC_INC, GBC_HL, GBC_NONE },
    /* 0x24 */ { GBC_INC, GBC_H, GBC_NONE },
    /* 0x25 */ { GBC_DEC, GBC_H, GBC_NONE },
    /* 0x26 */ { GBC_LD, GBC_H, GBC_BYTE },
    /* 0x27 */ { GBC_DAA, GBC_NONE, GBC_NONE },
    /* 0x28 */ { GBC_JR, GBC_F_Z, GBC_BRANCH },
    /* 0x29 */ { GBC_ADD, GBC_HL, GBC_HL },
    /* 0x2a */ { GBC_LDI, GBC_A, GBC_pHL },
    /* 0x2b */ { GBC_DEC, GBC_HL, GBC_NONE },
    /* 0x2c */ { GBC_INC, GBC_L, GBC_NONE },
    /* 0x2d */ { GBC_DEC, GBC_L, GBC_NONE },
    /* 0x2e */ { GBC_LD, GBC_L, GBC_BYTE },
    /* 0x2f */ { GBC_CPL, GBC_NONE, GBC_NONE },
    /* 0x30 */ { GBC_JR, GBC_F_NC, GBC_BRANCH },
    /* 0x31 */ { GBC_LD, GBC_SP, GBC_WORD },
    /* 0x32 */ { GBC_LDD, GBC_pHL, GBC_A },
    /* 0x33 */ { GBC_INC, GBC_SP, GBC_NONE },
    /* 0x34 */ { GBC_INC, GBC_pHL, GBC_NONE },
    /* 0x35 */ { GBC_DEC, GBC_pHL, GBC_NONE },
    /* 0x36 */ { GBC_LD, GBC_pHL, GBC_BYTE },
    /* 0x37 */ { GBC_SCF, GBC_NONE, GBC_NONE },
    /* 0x38 */ { GBC_JR, GBC_F_C, GBC_BRANCH },
    /* 0x39 */ { GBC_ADD, GBC_HL, GBC_SP },
    /* 0x3a */ { GBC_LDD, GBC_A, GBC_pHL },
    /* 0x3b */ { GBC_DEC, GBC_SP, GBC_NONE },
    /* 0x3c */ { GBC_INC, GBC_A, GBC_NONE },
    /* 0x3d */ { GBC_DEC, GBC_A, GBC_NONE },
    /* 0x3e */ { GBC_LD, GBC_A, GBC_BYTE },
    /* 0x3f */ { GBC_CCF, GBC_NONE, GBC_NONE },
    /* 0x40 */ { GBC_LD, GBC_B, GBC_B },
    /* 0x41 */ { GBC_LD, GBC_B, GBC_C },
    /* 0x42 */ { GBC_LD, GBC_B, GBC_D },
    /* 0x43 */ { GBC_LD, GBC_B, GBC_E },
    /* 0x44 */ { GBC_LD, GBC_B, GBC_H },
    /* 0x45 */ { GBC_LD, GBC_B, GBC_L },
    /* 0x46 */ { GBC_LD, GBC_B, GBC_pHL },
    /* 0x47 */ { GBC_LD, GBC_B, GBC_A },
    /* 0x48 */ { GBC_LD, GBC_C, GBC_B },
    /* 0x49 */ { GBC_LD, GBC_C, GBC_C },
    /* 0x4a */ { GBC_LD, GBC_C, GBC_D },
    /* 0x4b */ { GBC_LD, GBC_C, GBC_E },
    /* 0x4c */ { GBC_LD, GBC_C, GBC_H },
    /* 0x4d */ { GBC_LD, GBC_C, GBC_L },
    /* 0x4e */ { GBC_LD, GBC_C, GBC_pHL },
    /* 0x4f */ { GBC_LD, GBC_C, GBC_A },
    /* 0x50 */ { GBC_LD, GBC_D, GBC_B },
    /* 0x51 */ { GBC_LD, GBC_D, GBC_C },
    /* 0x52 */ { GBC_LD, GBC_D, GBC_D },
    /* 0x53 */ { GBC_LD, GBC_D, GBC_E },
    /* 0x54 */ { GBC_LD, GBC_D, GBC_H },
    /* 0x55 */ { GBC_LD, GBC_D, GBC_L },
    /* 0x56 */ { GBC_LD, GBC_D, GBC_pHL },
    /* 0x57 */ { GBC_LD, GBC_D, GBC_A },
    /* 0x58 */ { GBC_LD, GBC_E, GBC_B },
    /* 0x59 */ { GBC_LD, GBC_E, GBC_C },
    /* 0x5a */ { GBC_LD, GBC_E, GBC_D },
    /* 0x5b */ { GBC_LD, GBC_E, GBC_E },
    /* 0x5c */ { GBC_LD, GBC_E, GBC_H },
    /* 0x5d */ { GBC_LD, GBC_E, GBC_L },
    /* 0x5e */ { GBC_LD, GBC_E, GBC_pHL },
    /* 0x5f */ { GBC_LD, GBC_E, GBC_A },
    /* 0x60 */ { GBC_LD, GBC_H, GBC_B },
    /* 0x61 */ { GBC_LD, GBC_H, GBC_C },
    /* 0x62 */ { GBC_LD, GBC_H, GBC_D },
    /* 0x63 */ { GBC_LD, GBC_H, GBC_E },
    /* 0x64 */ { GBC_LD, GBC_H, GBC_H },
    /* 0x65 */ { GBC_LD, GBC_H, GBC_L },
    /* 0x66 */ { GBC_LD, GBC_H, GBC_pHL },
    /* 0x67 */ { GBC_LD, GBC_H, GBC_A },
    /* 0x68 */ { GBC_LD, GBC_L, GBC_B },
    /* 0x69 */ { GBC_LD, GBC_L, GBC_C },
    /* 0x6a */ { GBC_LD, GBC_L, GBC_D },
    /* 0x6b */ { GBC_LD, GBC_L, GBC_E },
    /* 0x6c */ { GBC_LD, GBC_L, GBC_H },
    /* 0x6d */ { GBC_LD, GBC_L, GBC_L },
    /* 0x6e */ { GBC_LD, GBC_L, GBC_pHL },
    /* 0x6f */ { GBC_LD, GBC_L, GBC_A },
    /* 0x70 */ { GBC_LD, GBC_pHL, GBC_B },
    /* 0x71 */ { GBC_LD, GBC_pHL, GBC_C },
    /* 0x72 */ { GBC_LD, GBC_pHL, GBC_D },
    /* 0x73 */ { GBC_LD, GBC_pHL, GBC_E },
    /* 0x74 */ { GBC_LD, GBC_pHL, GBC_H },
    /* 0x75 */ { GBC_LD, GBC_pHL, GBC_L },
    /* 0x76 */ { GBC_HALT, GBC_NONE, GBC_NONE },
    /* 0x77 */ { GBC_LD, GBC_pHL, GBC_A },
    /* 0x78 */ { GBC_LD, GBC_A, GBC_B },
    /* 0x79 */ { GBC_LD, GBC_A, GBC_C },
    /* 0x7a */ { GBC_LD, GBC_A, GBC_D },
    /* 0x7b */ { GBC_LD, GBC_A, GBC_E },
    /* 0x7c */ { GBC_LD, GBC_A, GBC_H },
    /* 0x7d */ { GBC_LD, GBC_A, GBC_L },
    /* 0x7e */ { GBC_LD, GBC_A, GBC_pHL },
    /* 0x7f */ { GBC_LD, GBC_A, GBC_A },
    /* 0x80 */ { GBC_ADD, GBC_A, GBC_B },
    /* 0x81 */ { GBC_ADD, GBC_A, GBC_C },
    /* 0x82 */ { GBC_ADD, GBC_A, GBC_D },
    /* 0x83 */ { GBC_ADD, GBC_A, GBC_E },
    /* 0x84 */ { GBC_ADD, GBC_A, GBC_H },
    /* 0x85 */ { GBC_ADD, GBC_A, GBC_L },
    /* 0x86 */ { GBC_ADD, GBC_A, GBC_pHL },
    /* 0x87 */ { GBC_ADD, GBC_A, GBC_A },
    /* 0x88 */ { GBC_ADC, GBC_A, GBC_B },
    /* 0x89 */ { GBC_ADC, GBC_A, GBC_C },
    /* 0x8a */ { GBC_ADC, GBC_A, GBC_D },
    /* 0x8b */ { GBC_ADC, GBC_A, GBC_E },
    /* 0x8c */ { GBC_ADC, GBC_A, GBC_H },
    /* 0x8d */ { GBC_ADC, GBC_A, GBC_L },
    /* 0x8e */ { GBC_ADC, GBC_A, GBC_pHL },
    /* 0x8f */ { GBC_ADC, GBC_A, GBC_A },
    /* 0x90 */ { GBC_SUB, GBC_A, GBC_B },
    /* 0x91 */ { GBC_SUB, GBC_A, GBC_C },
    /* 0x92 */ { GBC_SUB, GBC_A, GBC_D },
    /* 0x93 */ { GBC_SUB, GBC_A, GBC_E },
    /* 0x94 */ { GBC_SUB, GBC_A, GBC_H },
    /* 0x95 */ { GBC_SUB, GBC_A, GBC_L },
    /* 0x96 */ { GBC_SUB, GBC_A, GBC_pHL },
    /* 0x97 */ { GBC_SUB, GBC_A, GBC_A },
    /* 0x98 */ { GBC_SBC, GBC_A, GBC_B },
    /* 0x99 */ { GBC_SBC, GBC_A, GBC_C },
    /* 0x9a */ { GBC_SBC, GBC_A, GBC_D },
    /* 0x9b */ { GBC_SBC, GBC_A, GBC_E },
    /* 0x9c */ { GBC_SBC, GBC_A, GBC_H },
    /* 0x9d */ { GBC_SBC, GBC_A, GBC_L },
    /* 0x9e */ { GBC_SBC, GBC_A, GBC_pHL },
    /* 0x9f */ { GBC_SBC, GBC_A, GBC_A },
    /* 0xa0 */ { GBC_AND, GBC_A, GBC_B },
    /* 0xa1 */ { GBC_AND, GBC_A, GBC_C },
    /* 0xa2 */ { GBC_AND, GBC_A, GBC_D },
    /* 0xa3 */ { GBC_AND, GBC_A, GBC_E },
    /* 0xa4 */ { GBC_AND, GBC_A, GBC_H },
    /* 0xa5 */ { GBC_AND, GBC_A, GBC_L },
    /* 0xa6 */ { GBC_AND, GBC_A, GBC_pHL },
    /* 0xa7 */ { GBC_AND, GBC_A, GBC_A },
    /* 0xa8 */ { GBC_XOR, GBC_A, GBC_B },
    /* 0xa9 */ { GBC_XOR, GBC_A, GBC_C },
    /* 0xaa */ { GBC_XOR, GBC_A, GBC_D },
    /* 0xab */ { GBC_XOR, GBC_A, GBC_E },
    /* 0xac */ { GBC_XOR, GBC_A, GBC_H },
    /* 0xad */ { GBC_XOR, GBC_A, GBC_L },
    /* 0xae */ { GBC_XOR, GBC_A, GBC_pHL },
    /* 0xaf */ { GBC_XOR, GBC_A, GBC_A },
    /* 0xb0 */ { GBC_OR, GBC_A, GBC_B },
    /* 0xb1 */ { GBC_OR, GBC_A, GBC_C },
    /* 0xb2 */ { GBC_OR, GBC_A, GBC_D },
    /* 0xb3 */ { GBC_OR, GBC_A, GBC_E },
    /* 0xb4 */ { GBC_OR, GBC_A, GBC_H },
    /* 0xb5 */ { GBC_OR, GBC_A, GBC_L },
    /* 0xb6 */ { GBC_OR, GBC_A, GBC_pHL },
    /* 0xb7 */ { GBC_OR, GBC_A, GBC_A },
    /* 0xb8 */ { GBC_CP, GBC_A, GBC_B },
    /* 0xb9 */ { GBC_CP, GBC_A, GBC_C },
    /* 0xba */ { GBC_CP, GBC_A, GBC_D },
    /* 0xbb */ { GBC_CP, GBC_A, GBC_E },
    /* 0xbc */ { GBC_CP, GBC_A, GBC_H },
    /* 0xbd */ { GBC_CP, GBC_A, GBC_L },
    /* 0xbe */ { GBC_CP, GBC_A, GBC_pHL },
    /* 0xbf */ { GBC_CP, GBC_A, GBC_A },
    /* 0xc0 */ { GBC_RET, GBC_F_NZ, GBC_NONE },
    /* 0xc1 */ { GBC_POP, GBC_BC, GBC_NONE },
    /* 0xc2 */ { GBC_JP, GBC_F_NZ, GBC_WORD },
    /* 0xc3 */ { GBC_JP, GBC_WORD, GBC_NONE },
    /* 0xc4 */ { GBC_CALL, GBC_F_NZ, GBC_WORD },
    /* 0xc5 */ { GBC_PUSH, GBC_BC, GBC_NONE },
    /* 0xc6 */ { GBC_ADD, GBC_A, GBC_BYTE },
    /* 0xc7 */ { GBC_RST00, GBC_NONE, GBC_NONE },
    /* 0xc8 */ { GBC_RET, GBC_F_Z, GBC_NONE },
    /* 0xc9 */ { GBC_RET, GBC_NONE, GBC_NONE },
    /* 0xca */ { GBC_JP, GBC_F_Z, GBC_WORD },
    /* 0xcb */ UNK,
    /* 0xcc */ { GBC_CALL, GBC_F_Z, GBC_WORD },
    /* 0xcd */ { GBC_CALL, GBC_WORD, GBC_NONE },
    /* 0xce */ { GBC_ADC, GBC_A, GBC_BYTE },
    /* 0xcf */ { GBC_RST08, GBC_NONE, GBC_NONE },
    /* 0xd0 */ { GBC_RET, GBC_F_NC, GBC_NONE },
    /* 0xd1 */ { GBC_POP, GBC_DE, GBC_NONE },
    /* 0xd2 */ { GBC_JP, GBC_F_NC, GBC_WORD },
    /* 0xd3 */ UNK,
    /* 0xd4 */ { GBC_CALL, GBC_F_NC, GBC_WORD },
    /* 0xd5 */ { GBC_PUSH, GBC_DE, GBC_NONE },
    /* 0xd6 */ { GBC_SUB, GBC_A, GBC_BYTE },
    /* 0xd7 */ { GBC_RST10, GBC_NONE, GBC_NONE },
    /* 0xd8 */ { GBC_RET, GBC_F_C, GBC_NONE },
    /* 0xd9 */ { GBC_RETI, GBC_NONE, GBC_NONE },
    /* 0xda */ { GBC_JP, GBC_F_C, GBC_WORD },
    /* 0xdb */ UNK,
    /* 0xdc */ { GBC_CALL, GBC_F_C, GBC_WORD },
    /* 0xdd */ UNK,
    /* 0xde */ { GBC_SBC, GBC_A, GBC_BYTE },
    /* 0xdf */ { GBC_RST18, GBC_NONE, GBC_NONE },
    /* 0xe0 */ { GBC_LD, GBC_pFF00n, GBC_A },
    /* 0xe1 */ { GBC_POP, GBC_HL, GBC_NONE },
    /* 0xe2 */ { GBC_LD, GBC_pFF00C, GBC_A },
    /* 0xe3 */ UNK,
    /* 0xe4 */ UNK,
    /* 0xe5 */ { GBC_PUSH, GBC_HL, GBC_NONE },
    /* 0xe6 */ { GBC_AND, GBC_A, GBC_BYTE },
    /* 0xe7 */ { GBC_RST20, GBC_NONE, GBC_NONE },
    /* 0xe8 */ { GBC_ADD, GBC_SP, GBC_DESP },
    /* 0xe9 */ { GBC_JP, GBC_pHL, GBC_NONE },
    /* 0xea */ { GBC_LD, GBC_ADDR, GBC_A },
    /* 0xeb */ UNK,
    /* 0xec */ UNK,
    /* 0xed */ UNK,
    /* 0xee */ { GBC_XOR, GBC_A, GBC_BYTE },
    /* 0xef */ { GBC_RST28, GBC_NONE, GBC_NONE },
    /* 0xf0 */ { GBC_LD, GBC_A, GBC_pFF00n },
    /* 0xf1 */ { GBC_POP, GBC_AF, GBC_NONE },
    /* 0xf2 */ { GBC_LD, GBC_A, GBC_pFF00C },
    /* 0xf3 */ { GBC_DI, GBC_NONE, GBC_NONE },
    /* 0xf4 */ UNK,
    /* 0xf5 */ { GBC_PUSH, GBC_AF, GBC_NONE },
    /* 0xf6 */ { GBC_OR, GBC_A, GBC_BYTE },
    /* 0xf7 */ { GBC_RST30, GBC_NONE, GBC_NONE },
    /* 0xf8 */ { GBC_LD, GBC_HL, GBC_SPdd },
    /* 0xf9 */ { GBC_LD, GBC_SP, GBC_HL },
    /* 0xfa */ { GBC_LD, GBC_A, GBC_ADDR },
    /* 0xfb */ { GBC_EI, GBC_NONE, GBC_NONE },
    /* 0xfc */ UNK,
    /* 0xfd */ UNK,
    /* 0xfe */ { GBC_CP, GBC_A, GBC_BYTE },
    /* 0xff */ { GBC_RST38, GBC_NONE, GBC_NONE }
  };




/*********************/
/* FUNCIONS PRIVADES */
/*********************/

static GBCu16
get_extra_byte (
        	GBCu16         addr,
        	GBC_InstExtra *extra,
        	GBC_Inst      *inst
        	)
{
  
  inst->bytes[inst->nbytes++]= extra->byte= GBC_mem_read ( addr++ );
  
  return addr;
  
} /* end get_extra_byte */


static GBCu16
get_extra_desp (
                GBCu16         addr,
                GBC_InstExtra *extra,
                GBC_Inst      *inst
                )
{
  
  inst->bytes[inst->nbytes]= GBC_mem_read ( addr++ );
  extra->desp= (GBCs8) (inst->bytes[inst->nbytes++]);
  
  return addr;
  
} /* end get_extra_desp */


static GBCu16
get_extra_addr_word (
        	     GBCu16         addr,
        	     GBC_InstExtra *extra,
        	     GBC_Inst      *inst
        	     )
{
  
  extra->addr_word= inst->bytes[inst->nbytes++]= GBC_mem_read ( addr++ );
  inst->bytes[inst->nbytes]= GBC_mem_read ( addr++ );
  extra->addr_word|= ((GBCu16) inst->bytes[inst->nbytes++])<<8;
  
  return addr;
  
} /* end get_extra_addr_word */


static GBCu16
get_extra_branch (
        	  GBCu16         addr,
        	  GBC_InstExtra *extra,
        	  GBC_Inst      *inst
        	  )
{
  
  inst->bytes[inst->nbytes]= GBC_mem_read ( addr++ );
  extra->branch.desp= (GBCs8) (inst->bytes[inst->nbytes++]);
  extra->branch.addr= addr + extra->branch.desp;
  
  return addr;
  
} /* end get_extra_branch */


static GBCu16
get_extra_op (
              GBCu16            addr,
              const GBC_OpType  op,
              GBC_InstExtra    *extra,
              GBC_Inst         *inst
              )
{
  
  switch ( op )
    {
    case GBC_pFF00n:
    case GBC_pBYTE:
    case GBC_BYTE: return get_extra_byte ( addr, extra, inst );
    case GBC_DESP:
    case GBC_SPdd: return get_extra_desp ( addr, extra, inst );
    case GBC_ADDR:
    case GBC_WORD: return get_extra_addr_word ( addr, extra, inst );
    case GBC_BRANCH: return get_extra_branch ( addr, extra, inst );
    default: break;
    }
  
  return addr;
  
} /* end get_extra_op */


static GBCu16
get_extra (
           GBCu16    addr,
           GBC_Inst *inst
           )
{
  
  addr= get_extra_op ( addr, inst->id.op1, &(inst->e1), inst );
  return get_extra_op ( addr, inst->id.op2, &(inst->e2), inst );
  
} /* end get_extra */


static GBCu16
decode_cb (
           GBCu16    addr,
           GBC_Inst *inst
           )
{
  
  GBCu8 opcode;
  
  
  opcode= inst->bytes[1]= GBC_mem_read ( addr++ );
  inst->nbytes= 2;
  inst->id= _insts_cb[opcode];
  
  return get_extra ( addr, inst );
  
} /* end decode_cb */




/**********************/
/* FUNCIONS PÚBLIQUES */
/**********************/

GBCu16
GBC_cpu_decode (
        	GBCu16    addr,
        	GBC_Inst *inst
        	)
{
  
  GBCu8 opcode;
  
  
  opcode= inst->bytes[0]= GBC_mem_read ( addr++ );
  switch ( opcode )
    {
    case 0xcb: return decode_cb ( addr, inst );
    default:
      inst->nbytes= 1;
      inst->id= _insts[opcode];
      return get_extra ( addr, inst );
    }
  
} /* end GBC_cpu_decode */
