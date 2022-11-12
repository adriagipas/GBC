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
 * along with adriagipas/GBC.  If not, see <https://www.gnu.org/licenses/>.
 */
/*
 *  cpu.c - Implementació del processador de la GameBoy Color.
 *
 *  NOTA: Els registres que són més grans del que toca han de tindre
 *  els bits sobrants sempre a 0.
 *  NOTA: Esta implementació està basada en la del Z80.
 *  NOTA: L'ordre dels flags en el registre F és incorrecte, estic
 *  gastant l'ordre del Z80, però no passa res per què eixe registre
 *  sols té siginficat intern. De fet hauria sigut més òptim tindre
 *  cada flag en una variable.
 *  NOTA: F sols importa quan es fa un POP AF o un PUSH AF. Per a no
 *  modificar tot el codi, tindré un F2 especial per a fer la
 *  transformació. Els 4 bits baixets de F teoricament sempre són 0!!!
 *  NOTA: No vaig a implementar EI com en el Z80, es a dir, després
 *  d'executar EI ja es pot interrompre. De fet si RETI habilita és
 *  com un EI serà per algo.
 *  NOTA: No se quan tarda una interrupció!!! Me ho he inventat!!! 16
 *  cicles.
 *
 */


#include "GBC.h"




/**********/
/* MACROS */
/**********/

#define SAVE(VAR)                                               \
  if ( fwrite ( &(VAR), sizeof(VAR), 1, f ) != 1 ) return -1

#define LOAD(VAR)                                               \
  if ( fread ( &(VAR), sizeof(VAR), 1, f ) != 1 ) return -1


#define VBINT 0x01
#define LSINT 0x02
#define TIINT 0x04
#define SEINT 0x08
#define JOINT 0x10


#define UPDATE_IAUX _regs.IAUX= (_regs.IE&_regs.IF)&0x1F


#define ZFLAG 0x40
#define HFLAG 0x10
#define NFLAG 0x02
#define CFLAG 0x01


#define R16(HI,LO) ((((GBCu16) _regs.HI)<<8)|_regs.LO)
#define GET_NN(ADDR)        				\
  ADDR= GBC_mem_read ( _regs.PC++ );        		\
  ADDR|= ((GBCu16) GBC_mem_read ( _regs.PC++ ))<<8
#define RESET_FLAGS(MASK) _regs.F&= ((MASK)^0xff)
#define INCR16(HI,LO) if ( ++_regs.LO == 0x00 ) ++_regs.HI
#define DECR16(HI,LO) if ( --_regs.LO == 0xff ) --_regs.HI
#define C1(VAL) ((GBCu8)(~(VAL)))
#define AD_A_SETFLAGS(VAL,VARU8)        			\
  _regs.F|= ((_regs.A&0x100)>>8) /* CFLAG és 0x01 */ |        	\
    (((VARU8^(VAL))^_regs.A)&HFLAG); /* HFLAG és 0x10 */        \
  if ( (_regs.A&= 0xff) == 0 ) _regs.F|= ZFLAG
#define SB_A_SETFLAGS(VAL,VARU8)        				\
  _regs.F|= (((~_regs.A)&0x100)>>8) /* CFLAG és 0x01 */ |        	\
    ((~((VARU8^(VAL))^_regs.A))&HFLAG) /* HFLAG és 0x10 */ |        	\
    NFLAG;        							\
  if ( (_regs.A&= 0xff) == 0 ) _regs.F|= ZFLAG
#define ADD_A_VAL(VAL,VARU8)        					\
  VARU8= (GBCu8) _regs.A;        					\
  _regs.A+= (VAL);        				                \
  RESET_FLAGS ( ZFLAG|HFLAG|NFLAG|CFLAG );        		\
  AD_A_SETFLAGS(VAL,VARU8)
#define SUB_A_VAL(VAL,VARU8)        					\
  VARU8= (GBCu8) _regs.A;        					\
  _regs.A+= C1(VAL);        				                \
  ++_regs.A;        					                \
  RESET_FLAGS ( ZFLAG|HFLAG|CFLAG );        	       	\
  SB_A_SETFLAGS ( C1(VAL), VARU8 )
#define ADC_A_VAL(VAL,VARU8)        					\
  VARU8= (GBCu8) _regs.A;        					\
  _regs.A+= (VAL) + (GBCu16) (_regs.F&CFLAG);        			\
  RESET_FLAGS ( ZFLAG|HFLAG|NFLAG|CFLAG );        		\
  AD_A_SETFLAGS ( VAL, VARU8 )
#define SBC_A_VAL(VAL,VARU8)        					\
  VARU8= (GBCu8) _regs.A;        					\
  _regs.A+= C1(VAL) + (GBCu16) ((~_regs.F)&CFLAG);                        \
  RESET_FLAGS ( ZFLAG|HFLAG|CFLAG );        		\
  SB_A_SETFLAGS ( C1(VAL), VARU8 )
#define LOP_A_VAL(OP,VAL)        					\
  _regs.A OP ## = (VAL);        					\
  RESET_FLAGS ( ZFLAG|HFLAG|NFLAG|CFLAG );        		\
  _regs.F|= (_regs.A?0:ZFLAG)
#define AND_A_VAL(VAL) LOP_A_VAL ( &, VAL ) | HFLAG
#define OR_A_VAL(VAL) LOP_A_VAL ( |, VAL )
#define XOR_A_VAL(VAL) LOP_A_VAL ( ^, VAL )
#define CP_A_VAL(VAL,VARU16)        					\
  VARU16= _regs.A + C1(VAL);        					\
  ++VARU16;        							\
  RESET_FLAGS ( ZFLAG|HFLAG|CFLAG );        		\
  _regs.F|=        							\
    NFLAG | ((~((_regs.A^C1(VAL))^VARU16))&HFLAG) |        		\
    (((~VARU16)&0x100)>>8) | ((VARU16&0xff)?0:ZFLAG)
#define INCDEC_VARU8(VARU8,AUXU8,OP,PVVAL)        	  \
  AUXU8= VARU8;        					  \
  OP VARU8;        					  \
  RESET_FLAGS ( ZFLAG|HFLAG|NFLAG );          \
  _regs.F|= (VARU8?0:ZFLAG)
#define INC_VARU8(VARU8,AUXU8)        		  \
  INCDEC_VARU8 ( VARU8, AUXU8, ++, 0x7f ) |          \
  (((0x00^AUXU8)^VARU8)&HFLAG)
#define DEC_VARU8(VARU8,AUXU8)        				\
  INCDEC_VARU8 ( VARU8, AUXU8, --, 0x80 ) |        		\
  ((~((HFLAG^AUXU8)^VARU8))&HFLAG) |        			\
  NFLAG
#define ADD_RU16_RU16(AUX32,A,B)        	\
  AUX32= A+B;        				\
  RESET_FLAGS ( HFLAG|CFLAG|NFLAG );        	\
  _regs.F|=        				\
    ((((A^B)^AUX32)&0x1000)>>8) |        	\
    ((AUX32&0x10000)>>16)
#define ROTATE_SET_FLAGS(VAR,C)        					\
  RESET_FLAGS ( ZFLAG|HFLAG|NFLAG|CFLAG );        		\
  _regs.F|= ((VAR)?0x00:ZFLAG) | (C)
#define RLC_VARU8(AUX,VAR)        			\
  AUX= (VAR)>>7;        				\
  (VAR)= ((VAR)<<1)|AUX;        		        \
  ROTATE_SET_FLAGS ( VAR, AUX )
#define RL_VARU8(AUX,VAR)               \
  AUX= (VAR)>>7;        	       \
  (VAR) = ((VAR)<<1)|(_regs.F&CFLAG);  \
  ROTATE_SET_FLAGS ( VAR, AUX )
#define RRC_VARU8(AUX,VAR)     \
  AUX= (VAR)&0x1;               \
  (VAR)= ((VAR)>>1)|(AUX<<7);  \
  ROTATE_SET_FLAGS ( VAR, AUX )
#define RR_VARU8(AUX,VAR)        	  \
  AUX= (VAR)&0x1;        		  \
  (VAR)= ((VAR)>>1)|((_regs.F&CFLAG)<<7); \
  ROTATE_SET_FLAGS ( VAR, AUX )
#define SHIFT_SET_FLAGS(VAR)        		\
  _regs.F|= ((VAR)?0x00:ZFLAG)
#define SLA_VARU8(VAR)        				\
  RESET_FLAGS ( ZFLAG|HFLAG|NFLAG|CFLAG ); \
  _regs.F|= (VAR)>>7;                                   \
  (VAR)<<= 1;                                           \
  SHIFT_SET_FLAGS ( VAR )
#define SRA_VARU8(VAR)        				\
  RESET_FLAGS ( ZFLAG|HFLAG|NFLAG|CFLAG ); \
  _regs.F|= (VAR)&0x1;        				\
  (VAR)= ((VAR)&0x80)|((VAR)>>1);                       \
  SHIFT_SET_FLAGS ( VAR )
#define SRL_VARU8(VAR)        				\
  RESET_FLAGS ( ZFLAG|HFLAG|NFLAG|CFLAG ); \
  _regs.F|= (VAR)&0x1;        				\
  (VAR)>>= 1;                                           \
  SHIFT_SET_FLAGS ( VAR )
#define SWAP_VARU8(VAR)        					\
  RESET_FLAGS ( ZFLAG|HFLAG|NFLAG|CFLAG );        		\
  (VAR)= ((VAR)<<4) | ((VAR)>>4);        			\
  _regs.F|= ((VAR)?0x00:ZFLAG)
#define BRANCH _regs.PC+= ((GBCs8) GBC_mem_read ( _regs.PC ))+1
#define PUSH_PC        					\
  GBC_mem_write ( --_regs.SP, (GBCu8) (_regs.PC>>8) );        \
  GBC_mem_write ( --_regs.SP, (GBCu8) (_regs.PC&0xff) )


#define LD_R_R return 4
#define LD_R1_R2(R1,R2) _regs.R1= _regs.R2; return 4
#define LD_R_A(R) _regs.R= (GBCu8) _regs.A; return 4
#define LD_R_N(R) _regs.R= GBC_mem_read ( _regs.PC++ ); return 8
#define LD_R_pHL_AUX(R) _regs.R= GBC_mem_read ( R16 ( H, L ) )
#define LD_R_pHL(R) LD_R_pHL_AUX(R); return 8
#define LD_pHL_R_AUX(R) GBC_mem_write ( R16 ( H, L ), (GBCu8) _regs.R )
#define LD_pHL_R(R) LD_pHL_R_AUX(R); return 8
#define LD_A_pR16(HI,LO) _regs.A= GBC_mem_read ( R16 ( HI, LO ) ); return 8
#define LD_pR16_A(HI,LO)        				\
  GBC_mem_write ( R16 ( HI, LO ), (GBCu8) _regs.A ); return 8

#define LD_DD_NN(HI,LO)        		 \
  _regs.LO= GBC_mem_read ( _regs.PC++ ); \
  _regs.HI= GBC_mem_read ( _regs.PC++ ); \
  return 12
#define LD_RU16_NN_NORET(RU16)        				\
  _regs.RU16= GBC_mem_read ( _regs.PC++ );        		\
  _regs.RU16|= ((GBCu16) GBC_mem_read ( _regs.PC++ ))<<8
#define LD_pNN_RU16(RU16)        		     \
  GBCu16 addr;        				     \
  GET_NN ( addr );        			     \
  GBC_mem_write ( addr, (GBCu8) (_regs.RU16&0xff) ); \
  GBC_mem_write ( addr+1, (GBCu8) (_regs.RU16>>8) ); \
  return 20
#define PUSH_QQ(HI,LO)        			     \
  GBC_mem_write ( --_regs.SP, (GBCu8) _regs.HI);     \
  GBC_mem_write ( --_regs.SP, _regs.LO );             \
  return 16
#define POP_QQ_NORET(HI,LO)        	 \
  _regs.LO= GBC_mem_read ( _regs.SP++ ); \
  _regs.HI= GBC_mem_read ( _regs.SP++ )
#define POP_QQ(HI,LO)        		 \
  POP_QQ_NORET ( HI, LO );        	 \
  return 12


#define OPVAR_A_R(R,OP)        			\
  GBCu8 aux;        				\
  OP ## _A_VAL ( _regs.R, aux );        	\
  return 4
#define OPVAR_A_A(OP)        			\
  GBCu8 aux;        				\
  OP ## _A_VAL ( aux, aux );        		\
  return 4
#define OPVAR_A_N(OP)        			\
  GBCu8 aux, val;        			\
  val= GBC_mem_read ( _regs.PC++ );        	\
  OP ## _A_VAL ( val, aux );        		\
  return 8
#define OPVAR_A_pHL(OP)        			\
  GBCu8 aux, val;        			\
  val= GBC_mem_read ( R16 ( H, L ) );        	\
  OP ## _A_VAL ( val, aux );        		\
  return 8
#define OP_A_R(R,OP)        			\
  OP ## _A_VAL ( (GBCu8) _regs.R );        	\
  return 4
#define OP_A_N(OP)        			\
  GBCu8 val;        				\
  val= GBC_mem_read ( _regs.PC++ );        	\
  OP ## _A_VAL ( val );        			\
  return 8
#define OP_A_pHL(OP)        			\
  GBCu8 val;        				\
  val= GBC_mem_read ( R16 ( H, L ) );        	\
  OP ## _A_VAL ( val );        			\
  return 8
#define CP_A_R(R)        			\
  GBCu16 aux;        				\
  CP_A_VAL ( (GBCu8) _regs.R, aux );        	\
  return 4
#define CP_A_N        				\
  GBCu8 val;        				\
  GBCu16 aux;        				\
  val= GBC_mem_read ( _regs.PC++ );        	\
  CP_A_VAL ( val, aux );        		\
  return 8
#define CP_A_pHL        			\
  GBCu8 val;        				\
  GBCu16 aux;        				\
  val= GBC_mem_read ( R16 ( H, L ) );        	\
  CP_A_VAL ( val, aux );        		\
  return 8
#define INCDEC_R(R,OP)        			\
  GBCu8 aux;        				\
  OP ## _VARU8 ( _regs.R, aux );        	\
  return 4
#define INCDEC_A(OP)        			\
  GBCu8 val, aux;        			\
  val= (GBCu8) _regs.A;        			\
  OP ## _VARU8 ( val, aux );                        \
  _regs.A= val;        		                \
  return 4
#define INCDEC_pHL(OP)        			\
  GBCu8 val, aux;        			\
  GBCu16 addr;        				\
  addr= R16 ( H, L );        			\
  val= GBC_mem_read ( addr );        		\
  OP ## _VARU8 ( val, aux );        		\
  GBC_mem_write ( addr, val );        		\
  return 12


#define OP_HL_SS_NORET(OP,HI,LO)        	\
  GBCu32 aux;        				\
  GBCu16 a, b;        				\
  a= R16 ( H, L );        			\
  b= R16 ( HI, LO );        			\
  OP ## _RU16_RU16 ( aux, a, b );        	\
  _regs.H= (aux>>8)&0xff;        		\
  _regs.L= aux&0xff
#define OP_HL_RU16_NORET(OP,R)        		\
  GBCu32 aux;        				\
  GBCu16 a;        				\
  a= R16 ( H, L );        			\
  OP ## _RU16_RU16 ( aux, a, _regs.R );        	\
  _regs.H= (aux>>8)&0xff;        		\
  _regs.L= aux&0xff
#define INCDEC_SS(OP,HI,LO)        		\
  OP ## R16 ( HI, LO );        			\
  return 8
#define SPdd_DEFAUX32()        						\
  GBCu32 aux;        							\
  GBCu16 b;        							\
  b= ((GBCu16) ((GBCs16) ((GBCs8) GBC_mem_read ( _regs.PC++ ))));        \
  aux= _regs.SP+b;        						\
  RESET_FLAGS ( HFLAG|CFLAG|ZFLAG|NFLAG );        			\
  _regs.F|=        							\
    (((_regs.SP^b)^aux)&HFLAG) |        				\
    ((((_regs.SP^b)^aux)&0x100)>>8)


#define ROT_R(OP,R)        	 \
  GBCu8 aux;        		 \
  OP ## _VARU8 ( aux, _regs.R ); \
  return 8
#define ROT_A(OP)             \
  GBCu8 aux, var;             \
  var= (GBCu8) _regs.A;             \
  OP ## _VARU8 ( aux, var ); \
  _regs.A= var;              \
  return 8
#define ROT_pHL(OP)        	 \
  GBCu16 addr;        		 \
  GBCu8 aux, var;        	 \
  addr= R16 ( H, L );        	 \
  var= GBC_mem_read ( addr );    \
  OP ## _VARU8 ( aux, var );         \
  GBC_mem_write ( addr, var );   \
  return 16
#define SHI_R(OP,R)        	 \
  OP ## _VARU8 ( _regs.R );         \
  return 8
#define SHI_A(OP)             \
  GBCu8 var;        	     \
  var= (GBCu8) _regs.A;             \
  OP ## _VARU8 ( var );      \
  _regs.A= var;              \
  return 8
#define SHI_pHL(OP)        	 \
  GBCu16 addr;        		 \
  GBCu8 var;        		 \
  addr= R16 ( H, L );        	 \
  var= GBC_mem_read ( addr );    \
  OP ## _VARU8 ( var );        	 \
  GBC_mem_write ( addr, var );   \
  return 16
#define SWAP_R(R)        	 \
  SWAP_VARU8 ( _regs.R );         \
  return 8
#define SWAP_A()        	\
  GBCu8 var;        		\
  var= (GBCu8) _regs.A;        	\
  SWAP_VARU8 ( var );        	\
  _regs.A= var;        		\
  return 8
#define SWAP_pHL()             \
  GBCu16 addr;        	     \
  GBCu8 var;        	     \
  addr= R16 ( H, L );             \
  var= GBC_mem_read ( addr );    \
  SWAP_VARU8 ( var );        \
  GBC_mem_write ( addr, var );   \
  return 16

#define BIT_VARU8_NORET(VAR,MASK)        	\
  RESET_FLAGS ( ZFLAG|NFLAG );        		\
  _regs.F|= HFLAG|(((VAR)&(MASK))?0x00:ZFLAG)
#define BIT_R(R,MASK)        			\
  BIT_VARU8_NORET ( _regs.R, MASK );        	\
  return 8
#define BIT_pHL(MASK)        	      \
  GBCu8 val;        		      \
  val= GBC_mem_read ( R16 ( H, L ) ); \
  BIT_VARU8_NORET ( val, MASK );      \
  return 12
#define SET_R(R,MASK) _regs.R|= (MASK); return 8
#define SET_pHL(MASK)        			  \
  GBCu16 addr;        				  \
  addr= R16 ( H, L );        			  \
  GBC_mem_write ( addr, GBC_mem_read ( addr ) | (MASK) ); \
  return 16
#define RES_R(R,MASK) _regs.R&= (MASK); return 8
#define RES_pHL(MASK)        			  \
  GBCu16 addr;        				  \
  addr= R16 ( H, L );        			  \
  GBC_mem_write ( addr, GBC_mem_read ( addr ) & (MASK) ); \
  return 16


#define JP_GET_ADDR() \
  GBCu16 addr;              \
  GET_NN ( addr )
#define JP()           \
  JP_GET_ADDR ();  \
  _regs.PC= addr;  \
  return 16
#define JP_COND(COND)        			\
  JP_GET_ADDR ();        			\
  if ( (COND) ) { _regs.PC= addr; return 16; }        \
  else return 12
#define JR_COND(COND)        	       \
  if ( (COND) ) { BRANCH; return 12; } \
  else { ++_regs.PC; return 8; }


#define CALL_NORET(VARU16)        			\
  GET_NN ( VARU16 );        				\
  PUSH_PC;        					\
  _regs.PC= VARU16
#define CALL_COND(COND)        			   \
  GBCu16 aux;        				   \
  if ( (COND) ) { CALL_NORET ( aux ); return 24; } \
  else { _regs.PC+= 2; return 12; }
#define RET_NORET        				\
  _regs.PC= GBC_mem_read ( _regs.SP++ );        		\
  _regs.PC|= ((GBCu16) GBC_mem_read ( _regs.SP++ ))<<8
#define RET_COND(COND)        			\
  if ( (COND) ) { RET_NORET; return 20; }        \
  else return 8
#define RST_P(P) PUSH_PC; _regs.PC= P; _regs.unhalted= GBC_TRUE; return 16




/*********/
/* ESTAT */
/*********/

/* L'acumulador tenen més bits dels que neecessita. */
static struct
{
  
  GBCu16 SP;
  GBCu16 PC;
  GBCu16 A;
  GBCu8  F, F2;
  GBCu8  B;
  GBCu8  C;
  GBCu8  D;
  GBCu8  E;
  GBCu8  H;
  GBCu8  L;
  GBCu8  IE, IF, IAUX;
  GBC_Bool IME;
  GBC_Bool halted;
  GBC_Bool unhalted;
  
} _regs;


/* Opcode de la instrucció que s'està executant. */
static GBCu8 _opcode;
static GBCu8 _opcode2;


/* Informació de l'usuari. */
static GBC_Warning *_warning;
static void *_udata;


/* Mode. */
static int _cgb_mode;


/* Velocitat. */
static struct
{
  
  GBCu8    current;
  GBC_Bool prepare;
  
} _speed;




/****************/
/* INSTRUCCIONS */
/****************/

static int
unk (void)
{
  _warning ( _udata, "l'opcode '0x%02x' és desconegut", _opcode );
  return 0;
}


/* 8-BIT LOAD GROUP */

static int ld_B_B (void) { LD_R_R; }
static int ld_B_C (void) { LD_R1_R2 ( B, C ); }
static int ld_B_D (void) { LD_R1_R2 ( B, D ); }
static int ld_B_E (void) { LD_R1_R2 ( B, E ); }
static int ld_B_H (void) { LD_R1_R2 ( B, H ); }
static int ld_B_L (void) { LD_R1_R2 ( B, L ); }
static int ld_B_A (void) { LD_R_A ( B ); }
static int ld_C_B (void) { LD_R1_R2 ( C, B ); }
static int ld_C_C (void) { LD_R_R; }
static int ld_C_D (void) { LD_R1_R2 ( C, D ); }
static int ld_C_E (void) { LD_R1_R2 ( C, E ); }
static int ld_C_H (void) { LD_R1_R2 ( C, H ); }
static int ld_C_L (void) { LD_R1_R2 ( C, L ); }
static int ld_C_A (void) { LD_R_A ( C ); }
static int ld_D_B (void) { LD_R1_R2 ( D, B ); }
static int ld_D_C (void) { LD_R1_R2 ( D, C ); }
static int ld_D_D (void) { LD_R_R; }
static int ld_D_E (void) { LD_R1_R2 ( D, E ); }
static int ld_D_H (void) { LD_R1_R2 ( D, H ); }
static int ld_D_L (void) { LD_R1_R2 ( D, L ); }
static int ld_D_A (void) { LD_R_A ( D ); }
static int ld_E_B (void) { LD_R1_R2 ( E, B ); }
static int ld_E_C (void) { LD_R1_R2 ( E, C ); }
static int ld_E_D (void) { LD_R1_R2 ( E, D ); }
static int ld_E_E (void) { LD_R_R; }
static int ld_E_H (void) { LD_R1_R2 ( E, H ); }
static int ld_E_L (void) { LD_R1_R2 ( E, L ); }
static int ld_E_A (void) { LD_R_A ( E ); }
static int ld_H_B (void) { LD_R1_R2 ( H, B ); }
static int ld_H_C (void) { LD_R1_R2 ( H, C ); }
static int ld_H_D (void) { LD_R1_R2 ( H, D ); }
static int ld_H_E (void) { LD_R1_R2 ( H, E ); }
static int ld_H_H (void) { LD_R_R; }
static int ld_H_L (void) { LD_R1_R2 ( H, L ); }
static int ld_H_A (void) { LD_R_A ( H ); }
static int ld_L_B (void) { LD_R1_R2 ( L, B ); }
static int ld_L_C (void) { LD_R1_R2 ( L, C ); }
static int ld_L_D (void) { LD_R1_R2 ( L, D ); }
static int ld_L_E (void) { LD_R1_R2 ( L, E ); }
static int ld_L_H (void) { LD_R1_R2 ( L, H ); }
static int ld_L_L (void) { LD_R_R; }
static int ld_L_A (void) { LD_R_A ( L ); }
static int ld_A_B (void) { LD_R1_R2 ( A, B ); }
static int ld_A_C (void) { LD_R1_R2 ( A, C ); }
static int ld_A_D (void) { LD_R1_R2 ( A, D ); }
static int ld_A_E (void) { LD_R1_R2 ( A, E ); }
static int ld_A_H (void) { LD_R1_R2 ( A, H ); }
static int ld_A_L (void) { LD_R1_R2 ( A, L ); }
static int ld_A_A (void) { LD_R_R; }
static int ld_B_n (void) { LD_R_N ( B ); }
static int ld_C_n (void) { LD_R_N ( C ); }
static int ld_D_n (void) { LD_R_N ( D ); }
static int ld_E_n (void) { LD_R_N ( E ); }
static int ld_H_n (void) { LD_R_N ( H ); }
static int ld_L_n (void) { LD_R_N ( L ); }
static int ld_A_n (void) { LD_R_N ( A ); }
static int ld_B_pHL (void) { LD_R_pHL ( B ); }
static int ld_C_pHL (void) { LD_R_pHL ( C ); }
static int ld_D_pHL (void) { LD_R_pHL ( D ); }
static int ld_E_pHL (void) { LD_R_pHL ( E ); }
static int ld_H_pHL (void) { LD_R_pHL ( H ); }
static int ld_L_pHL (void) { LD_R_pHL ( L ); }
static int ld_A_pHL (void) { LD_R_pHL ( A ); }
static int ld_pHL_B (void) { LD_pHL_R ( B ); }
static int ld_pHL_C (void) { LD_pHL_R ( C ); }
static int ld_pHL_D (void) { LD_pHL_R ( D ); }
static int ld_pHL_E (void) { LD_pHL_R ( E ); }
static int ld_pHL_H (void) { LD_pHL_R ( H ); }
static int ld_pHL_L (void) { LD_pHL_R ( L ); }
static int ld_pHL_A (void) { LD_pHL_R ( A ); }
static int ld_pHL_n (void)
{
  GBC_mem_write ( R16 ( H, L ), GBC_mem_read ( _regs.PC++ ) );
  return 12;
}
static int ld_A_pBC (void) { LD_A_pR16 ( B, C ); }
static int ld_A_pDE (void) { LD_A_pR16 ( D, E ); }
static int ld_A_pnn (void)
{
  GBCu16 addr;
  GET_NN ( addr );
  _regs.A= GBC_mem_read ( addr );
  return 16;
}
static int ld_pBC_A (void) { LD_pR16_A ( B, C ); }
static int ld_pDE_A (void) { LD_pR16_A ( D, E ); }
static int ld_pnn_A (void)
{
  GBCu16 addr;
  GET_NN ( addr );
  GBC_mem_write ( addr, (GBCu8) _regs.A );
  return 16;
}
static int ldi_pHL_A (void) { LD_pHL_R_AUX ( A ); INCR16 ( H, L ); return 8; }
static int ldi_A_pHL (void) { LD_R_pHL_AUX ( A ); INCR16 ( H, L ); return 8; }
static int ldd_pHL_A (void) { LD_pHL_R_AUX ( A ); DECR16 ( H, L ); return 8; }
static int ldd_A_pHL (void) { LD_R_pHL_AUX ( A ); DECR16 ( H, L ); return 8; }
static int ld_A_pFF00n (void) {
  _regs.A= GBC_mem_read ( 0xFF00 | GBC_mem_read ( _regs.PC++ ) );
  return 12;
}
static int ld_pFF00n_A (void) {
  GBC_mem_write ( 0xFF00 | GBC_mem_read ( _regs.PC++ ), (GBCu8) _regs.A );
  return 12;
}
static int ld_A_pFF00C (void) {
  _regs.A= GBC_mem_read ( 0xFF00 | _regs.C );
  return 8;
}
static int ld_pFF00C_A (void) {
  GBC_mem_write ( 0xFF00 | _regs.C, (GBCu8) _regs.A );
  return 8;
}


/* 16-BIT LOAD GROUP */

static int ld_BC_nn (void) { LD_DD_NN ( B, C ); }
static int ld_DE_nn (void) { LD_DD_NN ( D, E ); }
static int ld_HL_nn (void) { LD_DD_NN ( H, L ); }
static int ld_SP_nn (void) { LD_RU16_NN_NORET ( SP ); return 12; }
static int ld_pnn_SP (void) { LD_pNN_RU16 ( SP ); }
static int ld_SP_HL (void) { _regs.SP= R16 ( H, L ); return 8; }
static int push_BC (void) { PUSH_QQ ( B, C ); }
static int push_DE (void) { PUSH_QQ ( D, E ); }
static int push_HL (void) { PUSH_QQ ( H, L ); }
static int push_AF (void) {
  _regs.F2=
    ((_regs.F&ZFLAG)?0x80:0x00) |
    ((_regs.F&NFLAG)?0x40:0x00) |
    ((_regs.F&HFLAG)?0x20:0x00) |
    ((_regs.F&CFLAG)?0x10:0x00);
  PUSH_QQ ( A, F2 );
}
static int pop_BC (void) { POP_QQ ( B, C ); }
static int pop_DE (void) { POP_QQ ( D, E ); }
static int pop_HL (void) { POP_QQ ( H, L ); }
static int pop_AF (void) {
  POP_QQ_NORET ( A, F2 );
  _regs.F=
    ((_regs.F2&0x80)?ZFLAG:0x00) |
    ((_regs.F2&0x40)?NFLAG:0x00) |
    ((_regs.F2&0x20)?HFLAG:0x00) |
    ((_regs.F2&0x10)?CFLAG:0x00);
  return 12;
}


/* 8-BIT ARITHMETIC GROUP */

static int add_A_B (void) { OPVAR_A_R ( B, ADD ); }
static int add_A_C (void) { OPVAR_A_R ( C, ADD ); }
static int add_A_D (void) { OPVAR_A_R ( D, ADD ); }
static int add_A_E (void) { OPVAR_A_R ( E, ADD ); }
static int add_A_H (void) { OPVAR_A_R ( H, ADD ); }
static int add_A_L (void) { OPVAR_A_R ( L, ADD ); }
static int add_A_A (void) { OPVAR_A_A ( ADD ); }
static int add_A_n (void) { OPVAR_A_N ( ADD ); }
static int add_A_pHL (void) { OPVAR_A_pHL ( ADD ); }
static int adc_A_B (void) { OPVAR_A_R ( B, ADC ); }
static int adc_A_C (void) { OPVAR_A_R ( C, ADC ); }
static int adc_A_D (void) { OPVAR_A_R ( D, ADC ); }
static int adc_A_E (void) { OPVAR_A_R ( E, ADC ); }
static int adc_A_H (void) { OPVAR_A_R ( H, ADC ); }
static int adc_A_L (void) { OPVAR_A_R ( L, ADC ); }
static int adc_A_A (void) { OPVAR_A_A ( ADC ); }
static int adc_A_n (void) { OPVAR_A_N ( ADC ); }
static int adc_A_pHL (void) { OPVAR_A_pHL ( ADC ); }
static int sub_A_B (void) { OPVAR_A_R ( B, SUB ); }
static int sub_A_C (void) { OPVAR_A_R ( C, SUB ); }
static int sub_A_D (void) { OPVAR_A_R ( D, SUB ); }
static int sub_A_E (void) { OPVAR_A_R ( E, SUB ); }
static int sub_A_H (void) { OPVAR_A_R ( H, SUB ); }
static int sub_A_L (void) { OPVAR_A_R ( L, SUB ); }
static int sub_A_A (void) { OPVAR_A_A ( SUB ); }
static int sub_A_n (void) { OPVAR_A_N ( SUB ); }
static int sub_A_pHL (void) { OPVAR_A_pHL ( SUB ); }
static int sbc_A_B (void) { OPVAR_A_R ( B, SBC ); }
static int sbc_A_C (void) { OPVAR_A_R ( C, SBC ); }
static int sbc_A_D (void) { OPVAR_A_R ( D, SBC ); }
static int sbc_A_E (void) { OPVAR_A_R ( E, SBC ); }
static int sbc_A_H (void) { OPVAR_A_R ( H, SBC ); }
static int sbc_A_L (void) { OPVAR_A_R ( L, SBC ); }
static int sbc_A_A (void) { OPVAR_A_A ( SBC ); }
static int sbc_A_n (void) { OPVAR_A_N ( SBC ); }
static int sbc_A_pHL (void) { OPVAR_A_pHL ( SBC ); }
static int and_A_B (void) { OP_A_R ( B, AND ); }
static int and_A_C (void) { OP_A_R ( C, AND ); }
static int and_A_D (void) { OP_A_R ( D, AND ); }
static int and_A_E (void) { OP_A_R ( E, AND ); }
static int and_A_H (void) { OP_A_R ( H, AND ); }
static int and_A_L (void) { OP_A_R ( L, AND ); }
static int and_A_A (void) { OP_A_R ( A, AND ); }
static int and_A_n (void) { OP_A_N ( AND ); }
static int and_A_pHL (void) { OP_A_pHL ( AND ); }
static int or_A_B (void) { OP_A_R ( B, OR ); }
static int or_A_C (void) { OP_A_R ( C, OR ); }
static int or_A_D (void) { OP_A_R ( D, OR ); }
static int or_A_E (void) { OP_A_R ( E, OR ); }
static int or_A_H (void) { OP_A_R ( H, OR ); }
static int or_A_L (void) { OP_A_R ( L, OR ); }
static int or_A_A (void) { OP_A_R ( A, OR ); }
static int or_A_n (void) { OP_A_N ( OR ); }
static int or_A_pHL (void) { OP_A_pHL ( OR ); }
static int xor_A_B (void) { OP_A_R ( B, XOR ); }
static int xor_A_C (void) { OP_A_R ( C, XOR ); }
static int xor_A_D (void) { OP_A_R ( D, XOR ); }
static int xor_A_E (void) { OP_A_R ( E, XOR ); }
static int xor_A_H (void) { OP_A_R ( H, XOR ); }
static int xor_A_L (void) { OP_A_R ( L, XOR ); }
static int xor_A_A (void) { OP_A_R ( A, XOR ); }
static int xor_A_n (void) { OP_A_N ( XOR ); }
static int xor_A_pHL (void) { OP_A_pHL ( XOR ); }
static int cp_A_B (void) { CP_A_R ( B ); }
static int cp_A_C (void) { CP_A_R ( C ); }
static int cp_A_D (void) { CP_A_R ( D ); }
static int cp_A_E (void) { CP_A_R ( E ); }
static int cp_A_H (void) { CP_A_R ( H ); }
static int cp_A_L (void) { CP_A_R ( L ); }
static int cp_A_A (void) { CP_A_R ( A ); }
static int cp_A_n (void) { CP_A_N; }
static int cp_A_pHL (void) { CP_A_pHL; }
static int inc_B (void) { INCDEC_R ( B, INC ); }
static int inc_C (void) { INCDEC_R ( C, INC ); }
static int inc_D (void) { INCDEC_R ( D, INC ); }
static int inc_E (void) { INCDEC_R ( E, INC ); }
static int inc_H (void) { INCDEC_R ( H, INC ); }
static int inc_L (void) { INCDEC_R ( L, INC ); }
static int inc_A (void) { INCDEC_A ( INC ); }
static int inc_pHL (void) { INCDEC_pHL ( INC ); }
static int dec_B (void) { INCDEC_R ( B, DEC ); }
static int dec_C (void) { INCDEC_R ( C, DEC ); }
static int dec_D (void) { INCDEC_R ( D, DEC ); }
static int dec_E (void) { INCDEC_R ( E, DEC ); }
static int dec_H (void) { INCDEC_R ( H, DEC ); }
static int dec_L (void) { INCDEC_R ( L, DEC ); }
static int dec_A (void) { INCDEC_A ( DEC ); }
static int dec_pHL (void) { INCDEC_pHL ( DEC ); }


/* GENERAL-PURPOSE ARITHMETIC AND CPU CONTROL GROUPS */

static int daa (void)
{
  GBCu8 aux, cflag;
  if ( _regs.F&NFLAG ) /* SUB SBC DEC NEG */
    {
      if ( _regs.F&HFLAG )
        {
          if ( _regs.F&CFLAG ) {aux= 0x9A;cflag=CFLAG;} /* -0x66 */
          else                 {aux= 0xFA;cflag=0x00;} /* -0x06. */
        }
      else
        {
          if ( _regs.F&CFLAG ) {aux= 0xA0;cflag=CFLAG;} /* -0x60 */
          else                 {aux= 0x00;cflag=0x00;} /* -0x00. */
        }
    }
  else /* ADD, ADC i INC. */
    {
      if ( (_regs.F&HFLAG) || (_regs.A&0xF)>9 )
        {
          if ( (_regs.F&CFLAG) || _regs.A>0x99 ) {aux= 0x66;cflag=CFLAG;}
          else                                   {aux= 0x06;cflag=0x00;}
        }
      else
        {
          if ( (_regs.F&CFLAG) || _regs.A>0x99 ) {aux= 0x60;cflag=CFLAG;}
          else                                   {aux= 0x00;cflag=0x00;}
        }
      
    }
  _regs.A+= aux;
  RESET_FLAGS ( ZFLAG|HFLAG|CFLAG );
  _regs.F|= ((_regs.A&= 0xff)?0:ZFLAG) | cflag;
  return 4;
}
static int cpl (void)
{
  _regs.A= (GBCu8) (~_regs.A);
  _regs.F|= HFLAG|NFLAG;
  return 4;
}
static int ccf (void) {
  RESET_FLAGS ( NFLAG|HFLAG );
  _regs.F^= CFLAG;
  return 4;
}
static int scf (void)
{
  RESET_FLAGS ( HFLAG|NFLAG );
  _regs.F|= CFLAG;
  return 4;
}
static int nop (void) { return 4; }
static int halt (void)
{
  if ( _regs.halted && _regs.unhalted )
    {
      _regs.halted= GBC_FALSE;
      _regs.unhalted= GBC_FALSE;
    }
  else
    {
      if ( !_regs.halted )
        {
          _regs.halted= GBC_TRUE;
          _regs.unhalted= GBC_FALSE;
        }
      --_regs.PC;
    }
  return 4;
}
static int stop (void)
{
  if ( _speed.prepare )
    {
      _speed.prepare= GBC_FALSE;
      _speed.current^= 0x80;
      GBC_main_switch_speed ();
      return 0;
    }
  if ( _regs.halted && _regs.unhalted )
    {
      _regs.halted= GBC_FALSE;
      _regs.unhalted= GBC_FALSE;
      GBC_lcd_stop ( GBC_FALSE );
      GBC_apu_stop ( GBC_FALSE );
    }
  else
    {
      if ( !_regs.halted )
        {
          _regs.halted= GBC_TRUE;
          _regs.unhalted= GBC_FALSE;
          GBC_lcd_stop ( GBC_TRUE );
          GBC_apu_stop ( GBC_TRUE );
        }
      --_regs.PC;
    }
  return 0; /* Realment medix 2 bytes, però es pot emular com un NOP,
               per tant el nop següent té el cost de l'actual. */
}
static int di (void) { _regs.IME= GBC_FALSE; return 4; }
static int ei (void) { _regs.IME= GBC_TRUE; return 4; }


/* 16-BIT ARITHMETIC GROUP */

static int add_HL_BC (void) { OP_HL_SS_NORET ( ADD, B, C ); return 8; }
static int add_HL_DE (void) { OP_HL_SS_NORET ( ADD, D, E ); return 8; }
static int add_HL_HL (void) { OP_HL_SS_NORET ( ADD, H, L ); return 8; }
static int add_HL_SP (void) { OP_HL_RU16_NORET ( ADD, SP ); return 8; }
static int inc_BC (void) { INCDEC_SS ( INC, B, C ); }
static int inc_DE (void) { INCDEC_SS ( INC, D, E ); }
static int inc_HL (void) { INCDEC_SS ( INC, H, L ); }
static int inc_SP (void) { ++_regs.SP; return 8; }
static int dec_BC (void) { INCDEC_SS ( DEC, B, C ); }
static int dec_DE (void) { INCDEC_SS ( DEC, D, E ); }
static int dec_HL (void) { INCDEC_SS ( DEC, H, L ); }
static int dec_SP (void) { --_regs.SP; return 8; }
static int add_SP_dd (void) {
  SPdd_DEFAUX32();
  _regs.SP= aux&0xFFFF;
  return 16;
}
static int ld_HL_SPdd (void) {
  SPdd_DEFAUX32();
  _regs.H= (GBCu8) ((aux>>8)&0xFF);
  _regs.L= (GBCu8) (aux&0xFF);
  return 12;
}


/* ROTATE AND SHIFT GROUP */

static int rlca (void)
{
  GBCu8 aux;
  aux= _regs.A>>7;
  _regs.F= aux;
  _regs.A= ((_regs.A<<1)|aux)&0xff;
  return 4;
}
static int rla (void)
{
  _regs.A<<= 1;
  _regs.A|= _regs.F&CFLAG;
  _regs.F= _regs.A>>8;
  _regs.A&= 0xff;
  return 4;
}
static int rrca (void)
{
  GBCu8 aux;
  aux= _regs.A&0x1;
  _regs.F= aux;
  _regs.A= (_regs.A>>1)|(aux<<7);
  return 4;
}
static int rra (void)
{
  GBCu8 aux;
  aux= _regs.A&0x1;
  _regs.A= (_regs.A>>1)|((_regs.F&CFLAG)<<7);
  _regs.F= aux;
  return 4;
}
static int rlc_B (void) { ROT_R ( RLC, B ); }
static int rlc_C (void) { ROT_R ( RLC, C ); }
static int rlc_D (void) { ROT_R ( RLC, D ); }
static int rlc_E (void) { ROT_R ( RLC, E ); }
static int rlc_H (void) { ROT_R ( RLC, H ); }
static int rlc_L (void) { ROT_R ( RLC, L ); }
static int rlc_A (void) { ROT_A ( RLC ); }
static int rlc_pHL (void) { ROT_pHL ( RLC ); }
static int rl_B (void) { ROT_R ( RL, B ); }
static int rl_C (void) { ROT_R ( RL, C ); }
static int rl_D (void) { ROT_R ( RL, D ); }
static int rl_E (void) { ROT_R ( RL, E ); }
static int rl_H (void) { ROT_R ( RL, H ); }
static int rl_L (void) { ROT_R ( RL, L ); }
static int rl_A (void) { ROT_A ( RL ); }
static int rl_pHL (void) { ROT_pHL ( RL ); }
static int rrc_B (void) { ROT_R ( RRC, B ); }
static int rrc_C (void) { ROT_R ( RRC, C ); }
static int rrc_D (void) { ROT_R ( RRC, D ); }
static int rrc_E (void) { ROT_R ( RRC, E ); }
static int rrc_H (void) { ROT_R ( RRC, H ); }
static int rrc_L (void) { ROT_R ( RRC, L ); }
static int rrc_A (void) { ROT_A ( RRC ); }
static int rrc_pHL (void) { ROT_pHL ( RRC ); }
static int rr_B (void) { ROT_R ( RR, B ); }
static int rr_C (void) { ROT_R ( RR, C ); }
static int rr_D (void) { ROT_R ( RR, D ); }
static int rr_E (void) { ROT_R ( RR, E ); }
static int rr_H (void) { ROT_R ( RR, H ); }
static int rr_L (void) { ROT_R ( RR, L ); }
static int rr_A (void) { ROT_A ( RR ); }
static int rr_pHL (void) { ROT_pHL ( RR ); }
static int sla_B (void) { SHI_R ( SLA, B ); }
static int sla_C (void) { SHI_R ( SLA, C ); }
static int sla_D (void) { SHI_R ( SLA, D ); }
static int sla_E (void) { SHI_R ( SLA, E ); }
static int sla_H (void) { SHI_R ( SLA, H ); }
static int sla_L (void) { SHI_R ( SLA, L ); }
static int sla_A (void) { SHI_A ( SLA ); }
static int sla_pHL (void) { SHI_pHL ( SLA ); }
static int sra_B (void) { SHI_R ( SRA, B ); }
static int sra_C (void) { SHI_R ( SRA, C ); }
static int sra_D (void) { SHI_R ( SRA, D ); }
static int sra_E (void) { SHI_R ( SRA, E ); }
static int sra_H (void) { SHI_R ( SRA, H ); }
static int sra_L (void) { SHI_R ( SRA, L ); }
static int sra_A (void) { SHI_A ( SRA ); }
static int sra_pHL (void) { SHI_pHL ( SRA ); }
static int srl_B (void) { SHI_R ( SRL, B ); }
static int srl_C (void) { SHI_R ( SRL, C ); }
static int srl_D (void) { SHI_R ( SRL, D ); }
static int srl_E (void) { SHI_R ( SRL, E ); }
static int srl_H (void) { SHI_R ( SRL, H ); }
static int srl_L (void) { SHI_R ( SRL, L ); }
static int srl_A (void) { SHI_A ( SRL ); }
static int srl_pHL (void) { SHI_pHL ( SRL ); }
static int swap_B (void) { SWAP_R ( B ); }
static int swap_C (void) { SWAP_R ( C ); }
static int swap_D (void) { SWAP_R ( D ); }
static int swap_E (void) { SWAP_R ( E ); }
static int swap_H (void) { SWAP_R ( H ); }
static int swap_L (void) { SWAP_R ( L ); }
static int swap_A (void) { SWAP_A (); }
static int swap_pHL (void) { SWAP_pHL (); }


/* BIT SET, RESET, AND TEST GROUP */

static int bit_0_B (void) { BIT_R ( B, 0x01 ); }
static int bit_0_C (void) { BIT_R ( C, 0x01 ); }
static int bit_0_D (void) { BIT_R ( D, 0x01 ); }
static int bit_0_E (void) { BIT_R ( E, 0x01 ); }
static int bit_0_H (void) { BIT_R ( H, 0x01 ); }
static int bit_0_L (void) { BIT_R ( L, 0x01 ); }
static int bit_0_A (void) { BIT_R ( A, 0x01 ); }
static int bit_1_B (void) { BIT_R ( B, 0x02 ); }
static int bit_1_C (void) { BIT_R ( C, 0x02 ); }
static int bit_1_D (void) { BIT_R ( D, 0x02 ); }
static int bit_1_E (void) { BIT_R ( E, 0x02 ); }
static int bit_1_H (void) { BIT_R ( H, 0x02 ); }
static int bit_1_L (void) { BIT_R ( L, 0x02 ); }
static int bit_1_A (void) { BIT_R ( A, 0x02 ); }
static int bit_2_B (void) { BIT_R ( B, 0x04 ); }
static int bit_2_C (void) { BIT_R ( C, 0x04 ); }
static int bit_2_D (void) { BIT_R ( D, 0x04 ); }
static int bit_2_E (void) { BIT_R ( E, 0x04 ); }
static int bit_2_H (void) { BIT_R ( H, 0x04 ); }
static int bit_2_L (void) { BIT_R ( L, 0x04 ); }
static int bit_2_A (void) { BIT_R ( A, 0x04 ); }
static int bit_3_B (void) { BIT_R ( B, 0x08 ); }
static int bit_3_C (void) { BIT_R ( C, 0x08 ); }
static int bit_3_D (void) { BIT_R ( D, 0x08 ); }
static int bit_3_E (void) { BIT_R ( E, 0x08 ); }
static int bit_3_H (void) { BIT_R ( H, 0x08 ); }
static int bit_3_L (void) { BIT_R ( L, 0x08 ); }
static int bit_3_A (void) { BIT_R ( A, 0x08 ); }
static int bit_4_B (void) { BIT_R ( B, 0x10 ); }
static int bit_4_C (void) { BIT_R ( C, 0x10 ); }
static int bit_4_D (void) { BIT_R ( D, 0x10 ); }
static int bit_4_E (void) { BIT_R ( E, 0x10 ); }
static int bit_4_H (void) { BIT_R ( H, 0x10 ); }
static int bit_4_L (void) { BIT_R ( L, 0x10 ); }
static int bit_4_A (void) { BIT_R ( A, 0x10 ); }
static int bit_5_B (void) { BIT_R ( B, 0x20 ); }
static int bit_5_C (void) { BIT_R ( C, 0x20 ); }
static int bit_5_D (void) { BIT_R ( D, 0x20 ); }
static int bit_5_E (void) { BIT_R ( E, 0x20 ); }
static int bit_5_H (void) { BIT_R ( H, 0x20 ); }
static int bit_5_L (void) { BIT_R ( L, 0x20 ); }
static int bit_5_A (void) { BIT_R ( A, 0x20 ); }
static int bit_6_B (void) { BIT_R ( B, 0x40 ); }
static int bit_6_C (void) { BIT_R ( C, 0x40 ); }
static int bit_6_D (void) { BIT_R ( D, 0x40 ); }
static int bit_6_E (void) { BIT_R ( E, 0x40 ); }
static int bit_6_H (void) { BIT_R ( H, 0x40 ); }
static int bit_6_L (void) { BIT_R ( L, 0x40 ); }
static int bit_6_A (void) { BIT_R ( A, 0x40 ); }
static int bit_7_B (void) { BIT_R ( B, 0x80 ); }
static int bit_7_C (void) { BIT_R ( C, 0x80 ); }
static int bit_7_D (void) { BIT_R ( D, 0x80 ); }
static int bit_7_E (void) { BIT_R ( E, 0x80 ); }
static int bit_7_H (void) { BIT_R ( H, 0x80 ); }
static int bit_7_L (void) { BIT_R ( L, 0x80 ); }
static int bit_7_A (void) { BIT_R ( A, 0x80 ); }
static int bit_0_pHL (void) { BIT_pHL ( 0x01 ); }
static int bit_1_pHL (void) { BIT_pHL ( 0x02 ); }
static int bit_2_pHL (void) { BIT_pHL ( 0x04 ); }
static int bit_3_pHL (void) { BIT_pHL ( 0x08 ); }
static int bit_4_pHL (void) { BIT_pHL ( 0x10 ); }
static int bit_5_pHL (void) { BIT_pHL ( 0x20 ); }
static int bit_6_pHL (void) { BIT_pHL ( 0x40 ); }
static int bit_7_pHL (void) { BIT_pHL ( 0x80 ); }
static int set_0_B (void) { SET_R ( B, 0x01 ); }
static int set_0_C (void) { SET_R ( C, 0x01 ); }
static int set_0_D (void) { SET_R ( D, 0x01 ); }
static int set_0_E (void) { SET_R ( E, 0x01 ); }
static int set_0_H (void) { SET_R ( H, 0x01 ); }
static int set_0_L (void) { SET_R ( L, 0x01 ); }
static int set_0_A (void) { SET_R ( A, 0x01 ); }
static int set_1_B (void) { SET_R ( B, 0x02 ); }
static int set_1_C (void) { SET_R ( C, 0x02 ); }
static int set_1_D (void) { SET_R ( D, 0x02 ); }
static int set_1_E (void) { SET_R ( E, 0x02 ); }
static int set_1_H (void) { SET_R ( H, 0x02 ); }
static int set_1_L (void) { SET_R ( L, 0x02 ); }
static int set_1_A (void) { SET_R ( A, 0x02 ); }
static int set_2_B (void) { SET_R ( B, 0x04 ); }
static int set_2_C (void) { SET_R ( C, 0x04 ); }
static int set_2_D (void) { SET_R ( D, 0x04 ); }
static int set_2_E (void) { SET_R ( E, 0x04 ); }
static int set_2_H (void) { SET_R ( H, 0x04 ); }
static int set_2_L (void) { SET_R ( L, 0x04 ); }
static int set_2_A (void) { SET_R ( A, 0x04 ); }
static int set_3_B (void) { SET_R ( B, 0x08 ); }
static int set_3_C (void) { SET_R ( C, 0x08 ); }
static int set_3_D (void) { SET_R ( D, 0x08 ); }
static int set_3_E (void) { SET_R ( E, 0x08 ); }
static int set_3_H (void) { SET_R ( H, 0x08 ); }
static int set_3_L (void) { SET_R ( L, 0x08 ); }
static int set_3_A (void) { SET_R ( A, 0x08 ); }
static int set_4_B (void) { SET_R ( B, 0x10 ); }
static int set_4_C (void) { SET_R ( C, 0x10 ); }
static int set_4_D (void) { SET_R ( D, 0x10 ); }
static int set_4_E (void) { SET_R ( E, 0x10 ); }
static int set_4_H (void) { SET_R ( H, 0x10 ); }
static int set_4_L (void) { SET_R ( L, 0x10 ); }
static int set_4_A (void) { SET_R ( A, 0x10 ); }
static int set_5_B (void) { SET_R ( B, 0x20 ); }
static int set_5_C (void) { SET_R ( C, 0x20 ); }
static int set_5_D (void) { SET_R ( D, 0x20 ); }
static int set_5_E (void) { SET_R ( E, 0x20 ); }
static int set_5_H (void) { SET_R ( H, 0x20 ); }
static int set_5_L (void) { SET_R ( L, 0x20 ); }
static int set_5_A (void) { SET_R ( A, 0x20 ); }
static int set_6_B (void) { SET_R ( B, 0x40 ); }
static int set_6_C (void) { SET_R ( C, 0x40 ); }
static int set_6_D (void) { SET_R ( D, 0x40 ); }
static int set_6_E (void) { SET_R ( E, 0x40 ); }
static int set_6_H (void) { SET_R ( H, 0x40 ); }
static int set_6_L (void) { SET_R ( L, 0x40 ); }
static int set_6_A (void) { SET_R ( A, 0x40 ); }
static int set_7_B (void) { SET_R ( B, 0x80 ); }
static int set_7_C (void) { SET_R ( C, 0x80 ); }
static int set_7_D (void) { SET_R ( D, 0x80 ); }
static int set_7_E (void) { SET_R ( E, 0x80 ); }
static int set_7_H (void) { SET_R ( H, 0x80 ); }
static int set_7_L (void) { SET_R ( L, 0x80 ); }
static int set_7_A (void) { SET_R ( A, 0x80 ); }
static int set_0_pHL (void) { SET_pHL ( 0x01 ); }
static int set_1_pHL (void) { SET_pHL ( 0x02 ); }
static int set_2_pHL (void) { SET_pHL ( 0x04 ); }
static int set_3_pHL (void) { SET_pHL ( 0x08 ); }
static int set_4_pHL (void) { SET_pHL ( 0x10 ); }
static int set_5_pHL (void) { SET_pHL ( 0x20 ); }
static int set_6_pHL (void) { SET_pHL ( 0x40 ); }
static int set_7_pHL (void) { SET_pHL ( 0x80 ); }
static int res_0_B (void) { RES_R ( B, 0xfe ); }
static int res_0_C (void) { RES_R ( C, 0xfe ); }
static int res_0_D (void) { RES_R ( D, 0xfe ); }
static int res_0_E (void) { RES_R ( E, 0xfe ); }
static int res_0_H (void) { RES_R ( H, 0xfe ); }
static int res_0_L (void) { RES_R ( L, 0xfe ); }
static int res_0_A (void) { RES_R ( A, 0xfe ); }
static int res_1_B (void) { RES_R ( B, 0xfd ); }
static int res_1_C (void) { RES_R ( C, 0xfd ); }
static int res_1_D (void) { RES_R ( D, 0xfd ); }
static int res_1_E (void) { RES_R ( E, 0xfd ); }
static int res_1_H (void) { RES_R ( H, 0xfd ); }
static int res_1_L (void) { RES_R ( L, 0xfd ); }
static int res_1_A (void) { RES_R ( A, 0xfd ); }
static int res_2_B (void) { RES_R ( B, 0xfb ); }
static int res_2_C (void) { RES_R ( C, 0xfb ); }
static int res_2_D (void) { RES_R ( D, 0xfb ); }
static int res_2_E (void) { RES_R ( E, 0xfb ); }
static int res_2_H (void) { RES_R ( H, 0xfb ); }
static int res_2_L (void) { RES_R ( L, 0xfb ); }
static int res_2_A (void) { RES_R ( A, 0xfb ); }
static int res_3_B (void) { RES_R ( B, 0xf7 ); }
static int res_3_C (void) { RES_R ( C, 0xf7 ); }
static int res_3_D (void) { RES_R ( D, 0xf7 ); }
static int res_3_E (void) { RES_R ( E, 0xf7 ); }
static int res_3_H (void) { RES_R ( H, 0xf7 ); }
static int res_3_L (void) { RES_R ( L, 0xf7 ); }
static int res_3_A (void) { RES_R ( A, 0xf7 ); }
static int res_4_B (void) { RES_R ( B, 0xef ); }
static int res_4_C (void) { RES_R ( C, 0xef ); }
static int res_4_D (void) { RES_R ( D, 0xef ); }
static int res_4_E (void) { RES_R ( E, 0xef ); }
static int res_4_H (void) { RES_R ( H, 0xef ); }
static int res_4_L (void) { RES_R ( L, 0xef ); }
static int res_4_A (void) { RES_R ( A, 0xef ); }
static int res_5_B (void) { RES_R ( B, 0xdf ); }
static int res_5_C (void) { RES_R ( C, 0xdf ); }
static int res_5_D (void) { RES_R ( D, 0xdf ); }
static int res_5_E (void) { RES_R ( E, 0xdf ); }
static int res_5_H (void) { RES_R ( H, 0xdf ); }
static int res_5_L (void) { RES_R ( L, 0xdf ); }
static int res_5_A (void) { RES_R ( A, 0xdf ); }
static int res_6_B (void) { RES_R ( B, 0xbf ); }
static int res_6_C (void) { RES_R ( C, 0xbf ); }
static int res_6_D (void) { RES_R ( D, 0xbf ); }
static int res_6_E (void) { RES_R ( E, 0xbf ); }
static int res_6_H (void) { RES_R ( H, 0xbf ); }
static int res_6_L (void) { RES_R ( L, 0xbf ); }
static int res_6_A (void) { RES_R ( A, 0xbf ); }
static int res_7_B (void) { RES_R ( B, 0x7f ); }
static int res_7_C (void) { RES_R ( C, 0x7f ); }
static int res_7_D (void) { RES_R ( D, 0x7f ); }
static int res_7_E (void) { RES_R ( E, 0x7f ); }
static int res_7_H (void) { RES_R ( H, 0x7f ); }
static int res_7_L (void) { RES_R ( L, 0x7f ); }
static int res_7_A (void) { RES_R ( A, 0x7f ); }
static int res_0_pHL (void) { RES_pHL ( 0xfe ); }
static int res_1_pHL (void) { RES_pHL ( 0xfd ); }
static int res_2_pHL (void) { RES_pHL ( 0xfb ); }
static int res_3_pHL (void) { RES_pHL ( 0xf7 ); }
static int res_4_pHL (void) { RES_pHL ( 0xef ); }
static int res_5_pHL (void) { RES_pHL ( 0xdf ); }
static int res_6_pHL (void) { RES_pHL ( 0xbf ); }
static int res_7_pHL (void) { RES_pHL ( 0x7f ); }


/* JUMP GROUP */

static int jp (void) { JP (); }
static int jp_NZ (void) { JP_COND ( !(_regs.F&ZFLAG) ); }
static int jp_Z (void) { JP_COND ( _regs.F&ZFLAG ); }
static int jp_NC (void) { JP_COND ( !(_regs.F&CFLAG) ); }
static int jp_C (void) { JP_COND ( _regs.F&CFLAG ); }
static int jr (void) { BRANCH; return 12; }
static int jr_C (void) { JR_COND ( _regs.F&CFLAG ) }
static int jr_NC (void) { JR_COND ( !(_regs.F&CFLAG) ) }
static int jr_Z (void) { JR_COND ( _regs.F&ZFLAG ) }
static int jr_NZ (void) { JR_COND ( !(_regs.F&ZFLAG) ) }
static int jp_HL (void) { _regs.PC= R16 ( H, L ); return 4; }


/* CALL AND RETURN GROUP */

static int call (void) { GBCu16 aux; CALL_NORET ( aux ); return 24; }
static int call_NZ (void) { CALL_COND ( !(_regs.F&ZFLAG) ); }
static int call_Z (void) { CALL_COND ( _regs.F&ZFLAG ); }
static int call_NC (void) { CALL_COND ( !(_regs.F&CFLAG) ); }
static int call_C (void) { CALL_COND ( _regs.F&CFLAG ); }
static int ret (void) { RET_NORET; return 16; }
static int ret_NZ (void) { RET_COND ( !(_regs.F&ZFLAG) ); }
static int ret_Z (void) { RET_COND ( _regs.F&ZFLAG ); }
static int ret_NC (void) { RET_COND ( !(_regs.F&CFLAG) ); }
static int ret_C (void) { RET_COND ( _regs.F&CFLAG ); }
static int reti (void) { RET_NORET; _regs.IME= GBC_TRUE; return 16; }
static int rst_00 (void) { RST_P ( 0x00 ); }
static int rst_08 (void) { RST_P ( 0x08 ); }
static int rst_10 (void) { RST_P ( 0x10 ); }
static int rst_18 (void) { RST_P ( 0x18 ); }
static int rst_20 (void) { RST_P ( 0x20 ); }
static int rst_28 (void) { RST_P ( 0x28 ); }
static int rst_30 (void) { RST_P ( 0x30 ); }
static int rst_38 (void) { RST_P ( 0x38 ); }


static int (*const _insts_cb[256]) (void)=
{
  /* 0x0 */ rlc_B,
  /* 0x1 */ rlc_C,
  /* 0x2 */ rlc_D,
  /* 0x3 */ rlc_E,
  /* 0x4 */ rlc_H,
  /* 0x5 */ rlc_L,
  /* 0x6 */ rlc_pHL,
  /* 0x7 */ rlc_A,
  /* 0x8 */ rrc_B,
  /* 0x9 */ rrc_C,
  /* 0xa */ rrc_D,
  /* 0xb */ rrc_E,
  /* 0xc */ rrc_H,
  /* 0xd */ rrc_L,
  /* 0xe */ rrc_pHL,
  /* 0xf */ rrc_A,
  /* 0x10 */ rl_B,
  /* 0x11 */ rl_C,
  /* 0x12 */ rl_D,
  /* 0x13 */ rl_E,
  /* 0x14 */ rl_H,
  /* 0x15 */ rl_L,
  /* 0x16 */ rl_pHL,
  /* 0x17 */ rl_A,
  /* 0x18 */ rr_B,
  /* 0x19 */ rr_C,
  /* 0x1a */ rr_D,
  /* 0x1b */ rr_E,
  /* 0x1c */ rr_H,
  /* 0x1d */ rr_L,
  /* 0x1e */ rr_pHL,
  /* 0x1f */ rr_A,
  /* 0x20 */ sla_B,
  /* 0x21 */ sla_C,
  /* 0x22 */ sla_D,
  /* 0x23 */ sla_E,
  /* 0x24 */ sla_H,
  /* 0x25 */ sla_L,
  /* 0x26 */ sla_pHL,
  /* 0x27 */ sla_A,
  /* 0x28 */ sra_B,
  /* 0x29 */ sra_C,
  /* 0x2a */ sra_D,
  /* 0x2b */ sra_E,
  /* 0x2c */ sra_H,
  /* 0x2d */ sra_L,
  /* 0x2e */ sra_pHL,
  /* 0x2f */ sra_A,
  /* 0x30 */ swap_B,
  /* 0x31 */ swap_C,
  /* 0x32 */ swap_D,
  /* 0x33 */ swap_E,
  /* 0x34 */ swap_H,
  /* 0x35 */ swap_L,
  /* 0x36 */ swap_pHL,
  /* 0x37 */ swap_A,
  /* 0x38 */ srl_B,
  /* 0x39 */ srl_C,
  /* 0x3a */ srl_D,
  /* 0x3b */ srl_E,
  /* 0x3c */ srl_H,
  /* 0x3d */ srl_L,
  /* 0x3e */ srl_pHL,
  /* 0x3f */ srl_A,
  /* 0x40 */ bit_0_B,
  /* 0x41 */ bit_0_C,
  /* 0x42 */ bit_0_D,
  /* 0x43 */ bit_0_E,
  /* 0x44 */ bit_0_H,
  /* 0x45 */ bit_0_L,
  /* 0x46 */ bit_0_pHL,
  /* 0x47 */ bit_0_A,
  /* 0x48 */ bit_1_B,
  /* 0x49 */ bit_1_C,
  /* 0x4a */ bit_1_D,
  /* 0x4b */ bit_1_E,
  /* 0x4c */ bit_1_H,
  /* 0x4d */ bit_1_L,
  /* 0x4e */ bit_1_pHL,
  /* 0x4f */ bit_1_A,
  /* 0x50 */ bit_2_B,
  /* 0x51 */ bit_2_C,
  /* 0x52 */ bit_2_D,
  /* 0x53 */ bit_2_E,
  /* 0x54 */ bit_2_H,
  /* 0x55 */ bit_2_L,
  /* 0x56 */ bit_2_pHL,
  /* 0x57 */ bit_2_A,
  /* 0x58 */ bit_3_B,
  /* 0x59 */ bit_3_C,
  /* 0x5a */ bit_3_D,
  /* 0x5b */ bit_3_E,
  /* 0x5c */ bit_3_H,
  /* 0x5d */ bit_3_L,
  /* 0x5e */ bit_3_pHL,
  /* 0x5f */ bit_3_A,
  /* 0x60 */ bit_4_B,
  /* 0x61 */ bit_4_C,
  /* 0x62 */ bit_4_D,
  /* 0x63 */ bit_4_E,
  /* 0x64 */ bit_4_H,
  /* 0x65 */ bit_4_L,
  /* 0x66 */ bit_4_pHL,
  /* 0x67 */ bit_4_A,
  /* 0x68 */ bit_5_B,
  /* 0x69 */ bit_5_C,
  /* 0x6a */ bit_5_D,
  /* 0x6b */ bit_5_E,
  /* 0x6c */ bit_5_H,
  /* 0x6d */ bit_5_L,
  /* 0x6e */ bit_5_pHL,
  /* 0x6f */ bit_5_A,
  /* 0x70 */ bit_6_B,
  /* 0x71 */ bit_6_C,
  /* 0x72 */ bit_6_D,
  /* 0x73 */ bit_6_E,
  /* 0x74 */ bit_6_H,
  /* 0x75 */ bit_6_L,
  /* 0x76 */ bit_6_pHL,
  /* 0x77 */ bit_6_A,
  /* 0x78 */ bit_7_B,
  /* 0x79 */ bit_7_C,
  /* 0x7a */ bit_7_D,
  /* 0x7b */ bit_7_E,
  /* 0x7c */ bit_7_H,
  /* 0x7d */ bit_7_L,
  /* 0x7e */ bit_7_pHL,
  /* 0x7f */ bit_7_A,
  /* 0x80 */ res_0_B,
  /* 0x81 */ res_0_C,
  /* 0x82 */ res_0_D,
  /* 0x83 */ res_0_E,
  /* 0x84 */ res_0_H,
  /* 0x85 */ res_0_L,
  /* 0x86 */ res_0_pHL,
  /* 0x87 */ res_0_A,
  /* 0x88 */ res_1_B,
  /* 0x89 */ res_1_C,
  /* 0x8a */ res_1_D,
  /* 0x8b */ res_1_E,
  /* 0x8c */ res_1_H,
  /* 0x8d */ res_1_L,
  /* 0x8e */ res_1_pHL,
  /* 0x8f */ res_1_A,
  /* 0x90 */ res_2_B,
  /* 0x91 */ res_2_C,
  /* 0x92 */ res_2_D,
  /* 0x93 */ res_2_E,
  /* 0x94 */ res_2_H,
  /* 0x95 */ res_2_L,
  /* 0x96 */ res_2_pHL,
  /* 0x97 */ res_2_A,
  /* 0x98 */ res_3_B,
  /* 0x99 */ res_3_C,
  /* 0x9a */ res_3_D,
  /* 0x9b */ res_3_E,
  /* 0x9c */ res_3_H,
  /* 0x9d */ res_3_L,
  /* 0x9e */ res_3_pHL,
  /* 0x9f */ res_3_A,
  /* 0xa0 */ res_4_B,
  /* 0xa1 */ res_4_C,
  /* 0xa2 */ res_4_D,
  /* 0xa3 */ res_4_E,
  /* 0xa4 */ res_4_H,
  /* 0xa5 */ res_4_L,
  /* 0xa6 */ res_4_pHL,
  /* 0xa7 */ res_4_A,
  /* 0xa8 */ res_5_B,
  /* 0xa9 */ res_5_C,
  /* 0xaa */ res_5_D,
  /* 0xab */ res_5_E,
  /* 0xac */ res_5_H,
  /* 0xad */ res_5_L,
  /* 0xae */ res_5_pHL,
  /* 0xaf */ res_5_A,
  /* 0xb0 */ res_6_B,
  /* 0xb1 */ res_6_C,
  /* 0xb2 */ res_6_D,
  /* 0xb3 */ res_6_E,
  /* 0xb4 */ res_6_H,
  /* 0xb5 */ res_6_L,
  /* 0xb6 */ res_6_pHL,
  /* 0xb7 */ res_6_A,
  /* 0xb8 */ res_7_B,
  /* 0xb9 */ res_7_C,
  /* 0xba */ res_7_D,
  /* 0xbb */ res_7_E,
  /* 0xbc */ res_7_H,
  /* 0xbd */ res_7_L,
  /* 0xbe */ res_7_pHL,
  /* 0xbf */ res_7_A,
  /* 0xc0 */ set_0_B,
  /* 0xc1 */ set_0_C,
  /* 0xc2 */ set_0_D,
  /* 0xc3 */ set_0_E,
  /* 0xc4 */ set_0_H,
  /* 0xc5 */ set_0_L,
  /* 0xc6 */ set_0_pHL,
  /* 0xc7 */ set_0_A,
  /* 0xc8 */ set_1_B,
  /* 0xc9 */ set_1_C,
  /* 0xca */ set_1_D,
  /* 0xcb */ set_1_E,
  /* 0xcc */ set_1_H,
  /* 0xcd */ set_1_L,
  /* 0xce */ set_1_pHL,
  /* 0xcf */ set_1_A,
  /* 0xd0 */ set_2_B,
  /* 0xd1 */ set_2_C,
  /* 0xd2 */ set_2_D,
  /* 0xd3 */ set_2_E,
  /* 0xd4 */ set_2_H,
  /* 0xd5 */ set_2_L,
  /* 0xd6 */ set_2_pHL,
  /* 0xd7 */ set_2_A,
  /* 0xd8 */ set_3_B,
  /* 0xd9 */ set_3_C,
  /* 0xda */ set_3_D,
  /* 0xdb */ set_3_E,
  /* 0xdc */ set_3_H,
  /* 0xdd */ set_3_L,
  /* 0xde */ set_3_pHL,
  /* 0xdf */ set_3_A,
  /* 0xe0 */ set_4_B,
  /* 0xe1 */ set_4_C,
  /* 0xe2 */ set_4_D,
  /* 0xe3 */ set_4_E,
  /* 0xe4 */ set_4_H,
  /* 0xe5 */ set_4_L,
  /* 0xe6 */ set_4_pHL,
  /* 0xe7 */ set_4_A,
  /* 0xe8 */ set_5_B,
  /* 0xe9 */ set_5_C,
  /* 0xea */ set_5_D,
  /* 0xeb */ set_5_E,
  /* 0xec */ set_5_H,
  /* 0xed */ set_5_L,
  /* 0xee */ set_5_pHL,
  /* 0xef */ set_5_A,
  /* 0xf0 */ set_6_B,
  /* 0xf1 */ set_6_C,
  /* 0xf2 */ set_6_D,
  /* 0xf3 */ set_6_E,
  /* 0xf4 */ set_6_H,
  /* 0xf5 */ set_6_L,
  /* 0xf6 */ set_6_pHL,
  /* 0xf7 */ set_6_A,
  /* 0xf8 */ set_7_B,
  /* 0xf9 */ set_7_C,
  /* 0xfa */ set_7_D,
  /* 0xfb */ set_7_E,
  /* 0xfc */ set_7_H,
  /* 0xfd */ set_7_L,
  /* 0xfe */ set_7_pHL,
  /* 0xff */ set_7_A
};

static int cb (void)
{
  _opcode2= GBC_mem_read ( _regs.PC++ );
  return _insts_cb[_opcode2] ();
}


static int (*const _insts[256]) (void)=
{
  /* 0x00 */ nop,
  /* 0x01 */ ld_BC_nn,
  /* 0x02 */ ld_pBC_A,
  /* 0x03 */ inc_BC,
  /* 0x04 */ inc_B,
  /* 0x05 */ dec_B,
  /* 0x06 */ ld_B_n,
  /* 0x07 */ rlca,
  /* 0x08 */ ld_pnn_SP,
  /* 0x09 */ add_HL_BC,
  /* 0x0a */ ld_A_pBC,
  /* 0x0b */ dec_BC,
  /* 0x0c */ inc_C,
  /* 0x0d */ dec_C,
  /* 0x0e */ ld_C_n,
  /* 0x0f */ rrca,
  /* 0x10 */ stop,
  /* 0x11 */ ld_DE_nn,
  /* 0x12 */ ld_pDE_A,
  /* 0x13 */ inc_DE,
  /* 0x14 */ inc_D,
  /* 0x15 */ dec_D,
  /* 0x16 */ ld_D_n,
  /* 0x17 */ rla,
  /* 0x18 */ jr,
  /* 0x19 */ add_HL_DE,
  /* 0x1a */ ld_A_pDE,
  /* 0x1b */ dec_DE,
  /* 0x1c */ inc_E,
  /* 0x1d */ dec_E,
  /* 0x1e */ ld_E_n,
  /* 0x1f */ rra,
  /* 0x20 */ jr_NZ,
  /* 0x21 */ ld_HL_nn,
  /* 0x22 */ ldi_pHL_A,
  /* 0x23 */ inc_HL,
  /* 0x24 */ inc_H,
  /* 0x25 */ dec_H,
  /* 0x26 */ ld_H_n,
  /* 0x27 */ daa,
  /* 0x28 */ jr_Z,
  /* 0x29 */ add_HL_HL,
  /* 0x2a */ ldi_A_pHL,
  /* 0x2b */ dec_HL,
  /* 0x2c */ inc_L,
  /* 0x2d */ dec_L,
  /* 0x2e */ ld_L_n,
  /* 0x2f */ cpl,
  /* 0x30 */ jr_NC,
  /* 0x31 */ ld_SP_nn,
  /* 0x32 */ ldd_pHL_A,
  /* 0x33 */ inc_SP,
  /* 0x34 */ inc_pHL,
  /* 0x35 */ dec_pHL,
  /* 0x36 */ ld_pHL_n,
  /* 0x37 */ scf,
  /* 0x38 */ jr_C,
  /* 0x39 */ add_HL_SP,
  /* 0x3a */ ldd_A_pHL,
  /* 0x3b */ dec_SP,
  /* 0x3c */ inc_A,
  /* 0x3d */ dec_A,
  /* 0x3e */ ld_A_n,
  /* 0x3f */ ccf,
  /* 0x40 */ ld_B_B,
  /* 0x41 */ ld_B_C,
  /* 0x42 */ ld_B_D,
  /* 0x43 */ ld_B_E,
  /* 0x44 */ ld_B_H,
  /* 0x45 */ ld_B_L,
  /* 0x46 */ ld_B_pHL,
  /* 0x47 */ ld_B_A,
  /* 0x48 */ ld_C_B,
  /* 0x49 */ ld_C_C,
  /* 0x4a */ ld_C_D,
  /* 0x4b */ ld_C_E,
  /* 0x4c */ ld_C_H,
  /* 0x4d */ ld_C_L,
  /* 0x4e */ ld_C_pHL,
  /* 0x4f */ ld_C_A,
  /* 0x50 */ ld_D_B,
  /* 0x51 */ ld_D_C,
  /* 0x52 */ ld_D_D,
  /* 0x53 */ ld_D_E,
  /* 0x54 */ ld_D_H,
  /* 0x55 */ ld_D_L,
  /* 0x56 */ ld_D_pHL,
  /* 0x57 */ ld_D_A,
  /* 0x58 */ ld_E_B,
  /* 0x59 */ ld_E_C,
  /* 0x5a */ ld_E_D,
  /* 0x5b */ ld_E_E,
  /* 0x5c */ ld_E_H,
  /* 0x5d */ ld_E_L,
  /* 0x5e */ ld_E_pHL,
  /* 0x5f */ ld_E_A,
  /* 0x60 */ ld_H_B,
  /* 0x61 */ ld_H_C,
  /* 0x62 */ ld_H_D,
  /* 0x63 */ ld_H_E,
  /* 0x64 */ ld_H_H,
  /* 0x65 */ ld_H_L,
  /* 0x66 */ ld_H_pHL,
  /* 0x67 */ ld_H_A,
  /* 0x68 */ ld_L_B,
  /* 0x69 */ ld_L_C,
  /* 0x6a */ ld_L_D,
  /* 0x6b */ ld_L_E,
  /* 0x6c */ ld_L_H,
  /* 0x6d */ ld_L_L,
  /* 0x6e */ ld_L_pHL,
  /* 0x6f */ ld_L_A,
  /* 0x70 */ ld_pHL_B,
  /* 0x71 */ ld_pHL_C,
  /* 0x72 */ ld_pHL_D,
  /* 0x73 */ ld_pHL_E,
  /* 0x74 */ ld_pHL_H,
  /* 0x75 */ ld_pHL_L,
  /* 0x76 */ halt,
  /* 0x77 */ ld_pHL_A,
  /* 0x78 */ ld_A_B,
  /* 0x79 */ ld_A_C,
  /* 0x7a */ ld_A_D,
  /* 0x7b */ ld_A_E,
  /* 0x7c */ ld_A_H,
  /* 0x7d */ ld_A_L,
  /* 0x7e */ ld_A_pHL,
  /* 0x7f */ ld_A_A,
  /* 0x80 */ add_A_B,
  /* 0x81 */ add_A_C,
  /* 0x82 */ add_A_D,
  /* 0x83 */ add_A_E,
  /* 0x84 */ add_A_H,
  /* 0x85 */ add_A_L,
  /* 0x86 */ add_A_pHL,
  /* 0x87 */ add_A_A,
  /* 0x88 */ adc_A_B,
  /* 0x89 */ adc_A_C,
  /* 0x8a */ adc_A_D,
  /* 0x8b */ adc_A_E,
  /* 0x8c */ adc_A_H,
  /* 0x8d */ adc_A_L,
  /* 0x8e */ adc_A_pHL,
  /* 0x8f */ adc_A_A,
  /* 0x90 */ sub_A_B,
  /* 0x91 */ sub_A_C,
  /* 0x92 */ sub_A_D,
  /* 0x93 */ sub_A_E,
  /* 0x94 */ sub_A_H,
  /* 0x95 */ sub_A_L,
  /* 0x96 */ sub_A_pHL,
  /* 0x97 */ sub_A_A,
  /* 0x98 */ sbc_A_B,
  /* 0x99 */ sbc_A_C,
  /* 0x9a */ sbc_A_D,
  /* 0x9b */ sbc_A_E,
  /* 0x9c */ sbc_A_H,
  /* 0x9d */ sbc_A_L,
  /* 0x9e */ sbc_A_pHL,
  /* 0x9f */ sbc_A_A,
  /* 0xa0 */ and_A_B,
  /* 0xa1 */ and_A_C,
  /* 0xa2 */ and_A_D,
  /* 0xa3 */ and_A_E,
  /* 0xa4 */ and_A_H,
  /* 0xa5 */ and_A_L,
  /* 0xa6 */ and_A_pHL,
  /* 0xa7 */ and_A_A,
  /* 0xa8 */ xor_A_B,
  /* 0xa9 */ xor_A_C,
  /* 0xaa */ xor_A_D,
  /* 0xab */ xor_A_E,
  /* 0xac */ xor_A_H,
  /* 0xad */ xor_A_L,
  /* 0xae */ xor_A_pHL,
  /* 0xaf */ xor_A_A,
  /* 0xb0 */ or_A_B,
  /* 0xb1 */ or_A_C,
  /* 0xb2 */ or_A_D,
  /* 0xb3 */ or_A_E,
  /* 0xb4 */ or_A_H,
  /* 0xb5 */ or_A_L,
  /* 0xb6 */ or_A_pHL,
  /* 0xb7 */ or_A_A,
  /* 0xb8 */ cp_A_B,
  /* 0xb9 */ cp_A_C,
  /* 0xba */ cp_A_D,
  /* 0xbb */ cp_A_E,
  /* 0xbc */ cp_A_H,
  /* 0xbd */ cp_A_L,
  /* 0xbe */ cp_A_pHL,
  /* 0xbf */ cp_A_A,
  /* 0xc0 */ ret_NZ,
  /* 0xc1 */ pop_BC,
  /* 0xc2 */ jp_NZ,
  /* 0xc3 */ jp,
  /* 0xc4 */ call_NZ,
  /* 0xc5 */ push_BC,
  /* 0xc6 */ add_A_n,
  /* 0xc7 */ rst_00,
  /* 0xc8 */ ret_Z,
  /* 0xc9 */ ret,
  /* 0xca */ jp_Z,
  /* 0xcb */ cb,
  /* 0xcc */ call_Z,
  /* 0xcd */ call,
  /* 0xce */ adc_A_n,
  /* 0xcf */ rst_08,
  /* 0xd0 */ ret_NC,
  /* 0xd1 */ pop_DE,
  /* 0xd2 */ jp_NC,
  /* 0xd3 */ unk,
  /* 0xd4 */ call_NC,
  /* 0xd5 */ push_DE,
  /* 0xd6 */ sub_A_n,
  /* 0xd7 */ rst_10,
  /* 0xd8 */ ret_C,
  /* 0xd9 */ reti,
  /* 0xda */ jp_C,
  /* 0xdb */ unk,
  /* 0xdc */ call_C,
  /* 0xdd */ unk,
  /* 0xde */ sbc_A_n,
  /* 0xdf */ rst_18,
  /* 0xe0 */ ld_pFF00n_A,
  /* 0xe1 */ pop_HL,
  /* 0xe2 */ ld_pFF00C_A,
  /* 0xe3 */ unk,
  /* 0xe4 */ unk,
  /* 0xe5 */ push_HL,
  /* 0xe6 */ and_A_n,
  /* 0xe7 */ rst_20,
  /* 0xe8 */ add_SP_dd,
  /* 0xe9 */ jp_HL,
  /* 0xea */ ld_pnn_A,
  /* 0xeb */ unk,
  /* 0xec */ unk,
  /* 0xed */ unk,
  /* 0xee */ xor_A_n,
  /* 0xef */ rst_28,
  /* 0xf0 */ ld_A_pFF00n,
  /* 0xf1 */ pop_AF,
  /* 0xf2 */ ld_A_pFF00C,
  /* 0xf3 */ di,
  /* 0xf4 */ unk,
  /* 0xf5 */ push_AF,
  /* 0xf6 */ or_A_n,
  /* 0xf7 */ rst_30,
  /* 0xf8 */ ld_HL_SPdd,
  /* 0xf9 */ ld_SP_HL,
  /* 0xfa */ ld_A_pnn,
  /* 0xfb */ ei,
  /* 0xfc */ unk,
  /* 0xfd */ unk,
  /* 0xfe */ cp_A_n,
  /* 0xff */ rst_38
};


/* Sols es pot executar si hi ha alguna interrupció activa. */
static int
interruption (void)
{
   
  PUSH_PC;
  _regs.IME= GBC_FALSE;
  if ( _regs.IAUX&VBINT )
    {
      _regs.IF&= ~VBINT;
      _regs.PC= 0x40;
    }
  else if ( _regs.IAUX&LSINT )
    {
      _regs.IF&= ~LSINT;
      _regs.PC= 0x48;
    }
  else if ( _regs.IAUX&TIINT )
    {
      _regs.IF&= ~TIINT;
      _regs.PC= 0x50;
    }
  else if ( _regs.IAUX&SEINT )
    {
      _regs.IF&= ~SEINT;
      _regs.PC= 0x58;
    }
  else /* _regs.IAUX&JOINT */
    {
      _regs.IF&= ~JOINT;
      _regs.PC= 0x60;
    }
  UPDATE_IAUX;
  
  return 16;
  
} /* end interruption */




/**********************/
/* FUNCIONS PÚBLIQUES */
/**********************/

GBCu16
GBC_cpu_decode_next_step (
        		  GBC_Step *step
        		  )
{
  
  if ( _regs.IME && _regs.IAUX )
    {
      if ( _regs.IAUX&VBINT )
        {
          step->type= GBC_STEP_VBINT;
          return 0x40;
        }
      else if ( _regs.IAUX&LSINT )
        {
          step->type= GBC_STEP_LSINT;
          return 0x48;
        }
      else if ( _regs.IAUX&TIINT )
        {
          step->type= GBC_STEP_TIINT;
          return 0x50;
        }
      else if ( _regs.IAUX&SEINT )
        {
          step->type= GBC_STEP_SEINT;
          return 0x58;
        }
      else /* _regs.IAUX&JOINT */
        {
          step->type= GBC_STEP_JOINT;
          return 0x60;
        }
    }
  step->type= GBC_STEP_INST;
  return GBC_cpu_decode ( _regs.PC, &(step->val.inst) );
  
} /* end GBC_cpu_decode_next_step */


void
GBC_cpu_init (
              GBC_Warning *warning,
              void        *udata
              )
{
  
  _warning= warning;
  _udata= udata;
  GBC_cpu_init_state ();
  
} /* end GBC_cpu_init */


void
GBC_cpu_init_state (void)
{
  
  _cgb_mode= GBC_TRUE;
  _opcode= _opcode2= 0;
  
  /* Registres. */
  _regs.A= 0;
  _regs.F= _regs.F2= 0;
  _regs.B= 0;
  _regs.C= 0;
  _regs.D= 0;
  _regs.E= 0;
  _regs.H= 0;
  _regs.L= 0;
  _regs.SP= 0;
  _regs.PC= 0;
  _regs.IE= _regs.IF= _regs.IAUX= 0;
  _regs.IME= GBC_FALSE;
  _regs.halted= GBC_FALSE;
  _regs.unhalted= GBC_FALSE;
  
  /* Velocitat. */
  _speed.current= 0x00;
  _speed.prepare= GBC_FALSE;
  
} /* end GBC_cpu_init_state */


void
GBC_cpu_power_up (void)
{
  
  _regs.A= 0x11; _regs.F= 0xB0;
  _regs.B= 0x00; _regs.C= 0x13;
  _regs.D= 0x00; _regs.E= 0xD8;
  _regs.H= 0x01; _regs.L= 0x4D;
  _regs.SP= 0xFFFE;
  _regs.PC= 0x0100;
  
} /* end GBC_cpu_power_up */


GBCu8
GBC_cpu_read_IE (void)
{
  return _regs.IE;
} /* end GBC_cpu_read_IE */


GBCu8
GBC_cpu_read_IF (void)
{
  return _regs.IF;
} /* end GBC_cpu_read_IF */


void
GBC_cpu_request_vblank_int (void)
{
  
  _regs.unhalted= GBC_TRUE;
  _regs.IF|= VBINT;
  UPDATE_IAUX;
  
} /* end GBC_cpu_request_vblank_int */


void
GBC_cpu_request_lcdstat_int (void)
{
  
  _regs.unhalted= GBC_TRUE;
  _regs.IF|= LSINT;
  UPDATE_IAUX;
  
} /* end GBC_cpu_request_lcdstat_int */


void
GBC_cpu_request_timer_int (void)
{
  
  _regs.unhalted= GBC_TRUE;
  _regs.IF|= TIINT;
  UPDATE_IAUX;
  
} /* end GBC_cpu_request_timer_int */


void
GBC_cpu_request_serial_int (void)
{
  
  _regs.unhalted= GBC_TRUE;
  _regs.IF|= SEINT;
  UPDATE_IAUX;
  
} /* end GBC_cpu_request_serial_int */


void
GBC_cpu_request_joypad_int (void)
{
  
  _regs.unhalted= GBC_TRUE;
  _regs.IF|= JOINT;
  UPDATE_IAUX;
  
} /* end GBC_cpu_request_joypad_int */


int
GBC_cpu_run (void)
{
  
  if ( _regs.IME && _regs.IAUX )
    return interruption ();
  _opcode= GBC_mem_read ( _regs.PC++ );
  return _insts[_opcode] ();
  
} /* end GBC_cpu_run */


void
GBC_cpu_set_cgb_mode (
        	      const GBC_Bool enabled
        	      )
{
  _cgb_mode= enabled;
} /* end GBC_cpu_set_cgb_mode */


void
GBC_cpu_speed_prepare (
        	       const GBCu8 data
        	       )
{
  
  if ( !_cgb_mode ) return;
  _speed.prepare= ((data&0x1)!=0);
  
} /* GBC_cpu_speed_prepare */


GBCu8
GBC_cpu_speed_query (void)
{
  
  if ( !_cgb_mode ) return 0xFF;
  
  return _speed.current | (_speed.prepare ? 0x01 : 0x00);
  
} /* end GBC_cpu_speed_query */


void
GBC_cpu_write_IE (
        	  GBCu8 data
        	  )
{
  
  _regs.IE= data;
  UPDATE_IAUX;
  
} /* end GBC_cpu_write_IE */


void
GBC_cpu_write_IF (
        	  GBCu8 data    /* Dades */
        	  )
{
  
  _regs.IF= data;
  UPDATE_IAUX;
  
} /* end GBC_cpu_write_IF */


int
GBC_cpu_save_state (
        	    FILE *f
        	    )
{

  SAVE ( _regs );
  SAVE ( _cgb_mode );
  SAVE ( _speed );

  return 0;
  
} /* end GBC_cpu_save_state */


int
GBC_cpu_load_state (
        	    FILE *f
        	    )
{

  LOAD ( _regs );
  LOAD ( _cgb_mode );
  LOAD ( _speed );

  return 0;
  
} /* end GBC_cpu_load_state */
