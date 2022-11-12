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
 *  GBC.h - Simulador de la 'GameBoy Color' escrit en ANSI C.
 *
 */

#ifndef __GBC_H__
#define __GBC_H__

#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>


/*********/
/* TIPUS */
/*********/

#if (CHAR_BIT != 8) || (USHRT_MAX != 65535U) || (UINT_MAX != 4294967295U)
#error Arquitectura no suportada
#endif

/* Tipus booleà. */
typedef enum
  {
    GBC_FALSE= 0,
    GBC_TRUE
  } GBC_Bool;

/* Tipus sencers. */
typedef signed char GBCs8;
typedef unsigned char GBCu8;
typedef signed short GBCs16;
typedef unsigned short GBCu16;
typedef unsigned int GBCu32;

/* Funció per a metre avísos. */
typedef void 
(GBC_Warning) (
               void       *udata,
               const char *format,
               ...
               );

typedef enum
  {
    GBC_NOERROR= 0,      /* No hi ha cap error. */
    GBC_EUNKMAPPER,      /* El mapper de la ROM és desconegut. */
    GBC_WRONGLOGO,       /* La ROM no ha passat el test del logotip. */
    GBC_WRONGCHKS,       /* La ROM no ha passat el test del checksum. */
    GBC_WRONGRAMSIZE,    /* La grandària de la RAM no està suportada. */
    GBC_WRONGROMSIZE     /* La grandària de la ROM no està suportada. */
  } GBC_Error;


/*******/
/* ROM */
/*******/
/* Definició de tipus i funcions relacionades amb la ROM. */

/* Grandària d'un 'bank' de rom (16K). */
#define GBC_BANK_SIZE 16384

/* 'Bank'. */
typedef GBCu8 GBC_Bank[GBC_BANK_SIZE];

/* Valor nul per al tipus 'Bank'. */
#define GBC_BANK_NULL ((GBC_Bank *) NULL)

/* Estructura per a guardar una ROM. */
typedef struct
{
  
  int       nbanks;    /* Número de 'banks's. */
  GBC_Bank *banks;     /* 'Bank's. */
  
} GBC_Rom;

/* Indica el tipus de mapper. */
typedef enum
  {
    GBC_UNKMAPPER,
    GBC_ROM,
    GBC_MBC1,
    GBC_MBC1_RAM,
    GBC_MBC1_RAM_BATTERY,
    GBC_MBC2,
    GBC_MBC2_BATTERY,
    GBC_ROM_RAM,
    GBC_ROM_RAM_BATTERY,
    GBC_MMM01,
    GBC_MMM01_RAM,
    GBC_MMM01_RAM_BATTERY,
    GBC_MBC3_TIMER_BATTERY,
    GBC_MBC3_TIMER_RAM_BATTERY,
    GBC_MBC3,
    GBC_MBC3_RAM,
    GBC_MBC3_RAM_BATTERY,
    GBC_MBC4,
    GBC_MBC4_RAM,
    GBC_MBC4_RAM_BATTERY,
    GBC_MBC5,
    GBC_MBC5_RAM,
    GBC_MBC5_RAM_BATTERY,
    GBC_MBC5_RUMBLE,
    GBC_MBC5_RUMBLE_RAM,
    GBC_MBC5_RUMBLE_RAM_BATTERY,
    GBC_POCKET_CAMERA,
    GBC_BANDAI_TAMA5,
    GBC_HUC3,
    GBC_HUC1_RAM_BATTERY
  } GBC_Mapper;

/* Estructura per a emmagatzemar la informació de la capçalera d'una
 * ROM de manera accessible.
 */
typedef struct
{
  
  char        title[17];          /* Títol. És difícil distinguir el
        			     títol del codi del fabricant, per
        			     eixe motiu a vegades inclou també
        			     el codi. És una cadena de
        			     caràcters. */
  char        manufacturer[5];    /* Codi del fabricant, pot estar
        			     buit. És una cadena de
        			     caràcters. */
  enum
    {
      GBC_ROM_ONLY_GBC=0,
      GBC_ROM_GBC,
      GBC_ROM_GB
    }         cgb_flag;           /* Indica si és un joc sols per a
        			     GBC, per a GB amb suport per a
        			     GBC, o sols GB. */
  GBCu8       old_license;        /* Byte que indica codi de la
        			     llicència. Un 0x33 vol dir que
        			     s'està gastant 'new_license'. */
  char        new_license[3];     /* Cadena de caràcters amb la
        			     llicència. Si 'old_license!=0x33'
        			     aleshores esta cadena està
        			     buida. */
  GBC_Bool    sgb_flag;           /* Si està a cert indica que suporta
        			     funcions de Super GameBoy. */
  const char *mapper;             /* Mapper. */
  int         rom_size;           /* Medit en número de troços de
        			     16K. Un -1 indica que no es
        			     sap. Aquest valor és el que diu
        			     tindre la ROM. */
  int         ram_size;           /* Grandària de la RAM externa medit
        			     en KB. -1 indica que és una
        			     grandària desconeguda. */
  GBC_Bool    japanese_rom;       /* ROM destinada al mercar
        			     japonés. */
  GBCu8       version;            /* Versió del joc. */
  GBCu8       checksum;           /* Checksum de la capçalera. Aquest
        			     checksum s'ha de cumplir per a
        			     què la GameBoy Color execute la
        			     ROM. */
  GBCu16     global_checksum;     /* Checksum global. */
  
} GBC_RomHeader;

/* Reserva memòria per als 'bank's d'una variable 'GBC_Rom'. Si la
 * variable continua a NULL s'ha pdrouït un error.
 */
#define GBC_rom_alloc(ROM)                                               \
  ((ROM).banks= (GBC_Bank *) malloc ( sizeof(GBC_Bank)*(ROM).nbanks ))

/* Comprova que el checksum de la capçalera és correcte. Una GameBoy
 * Color no executa una ROM si este requisit no es complix.
 */
GBC_Bool
GBC_rom_check_checksum (
        		const GBC_Rom *rom
        		);

/* Comprova que el checksum global és correcte. */
GBC_Bool
GBC_rom_check_global_checksum (
        		       const GBC_Rom *rom
        		       );

/* Comprova si la rom conté el logotip de la Nintendo (realment sols
 * els 24 primers bytes). Una GameBoy Color no executa una ROM si este
 * requisit no es complix.
 */
GBC_Bool
GBC_rom_check_nintendo_logo (
        		     const GBC_Rom *rom
        		     );

/* Si s'ha reservat memòria en la ROM la llibera. */
#define GBC_rom_free(ROM)                           \
  do {                                             \
  if ( (ROM).banks != NULL ) free ( (ROM).banks ); \
  } while(0)

/* Obté la capçalera d'una ROM. */
void
GBC_rom_get_header (
        	    const GBC_Rom *rom,
        	    GBC_RomHeader *header
        	    );

/* Torna el mapper utilitzat en la ROM. */
GBC_Mapper
GBC_rom_get_mapper (
        	    const GBC_Rom *rom
        	    );

/* Torna la grandària de la RAM externa medida en KB. */
int
GBC_rom_get_ram_size (
        	      const GBC_Rom *rom
        	      );

/* Torna una cadena de caràcters amb el nom del mapper. */
const char *
GBC_rom_mapper2str (
        	    const GBC_Mapper mapper
        	    );


/**********/
/* MAPPER */
/**********/
/* Mòdul que gestiona la memòria dels cartutxos. */

/* Tipus de la funció utilitzada per a indicar l'estat del
 * rumble. Sols es crida per a canviar el nivell de rumble. 0 és
 * parat, 3 màxim nivell.
 */
typedef void (GBC_UpdateRumble) (
        			 const int  level,
        			 void      *udata
        			 );

/* Tipus de la funció utilitzada per a demanar la memòria RAM
 * externa. Es supossa que esta memòria es estàtica.
 */
typedef GBCu8 * (GBC_GetExternalRAM) (
        			      const size_t  nbytes,
        			      void         *udata
        			      );

/* Tipus de la funció que es cridada cada vegada que la disposició de
 * la memòria canvia.
 */
typedef void (GBC_MapperChanged) (
        			  void *udata
        			  );

/* Procesa cicles de la UCP. */
extern void
(*GBC_mapper_clock) (const int cc);

/* Torna el número de banc mapejat en el banc 1. */
extern int
(*GBC_mapper_get_bank1) (void);

/* Inicialitza el mapper. */
GBC_Error
GBC_mapper_init (
        	 const GBC_Rom      *rom,
        	 const GBC_Bool      check_rom,
        	 GBC_GetExternalRAM *get_external_ram,
        	 GBC_UpdateRumble   *update_rumble,
        	 GBC_MapperChanged  *mapper_changed,    /* Pot ser NULL. */
        	 void               *udata
        	 );

/* Com init, però sense fixar altra vegada els callback i la rom. */
GBC_Error
GBC_mapper_init_state (void);

/* Llig un byte de la ROM. L'adreça ha d'estar en el rang
 * [0000-7FFF].
 */
extern GBCu8
(*GBC_mapper_read) (
        	    const GBCu16 addr    /* Adreça. */
        	    );

/* Llig un byte de la RAM. L'adreça ha d'estar en el rang
 * [0000-1FFF].
 */
extern GBCu8
(*GBC_mapper_read_ram) (
        		const GBCu16 addr    /* Adreça. */
        		);

/* Escriu un byte en la ROM. L'adreça ha d'estar en el rang
 * [0000-7FFF].
 */
extern void
(*GBC_mapper_write) (
        	     const GBCu16 addr,    /* Adreça. */
        	     const GBCu8  data     /* Dades. */
        	     );

/* Escriu un byte en la RAM. L'adreça ha d'estar en el rang
 * [0000-1FFF].
 */
extern void
(*GBC_mapper_write_ram) (
        		 const GBCu16 addr,    /* Adreça. */
        		 const GBCu8  data     /* Dades. */
        		 );

int
GBC_mapper_save_state (
        	       FILE *f
        	       );

int
GBC_mapper_load_state (
        	       FILE *f
        	       );


/*******/
/* MEM */
/*******/
/* Mòdul que simula el mapa de memòria. */

/* Tipus d'accessos a memòria. */
typedef enum
  {
    GBC_READ,
    GBC_WRITE
  } GBC_MemAccessType;

/* Tipus de la funció per a fer una traça dels accessos a
 * memòria. Cada vegada que es produeix un accés a memòria es crida.
 */
typedef void (GBC_MemAccess) (
        		      const GBC_MemAccessType type, /* Tipus. */
        		      const GBCu16            addr, /* Adreça
        						       dins de
        						       la
        						       memòria,
        						       es a
        						       dir en
        						       el rang
        						       [0000:1FFF]. */
        		      const GBCu8             data, /* Dades
        						       transmitides. */
        		      void                   *udata
                             );

/* Inicialitza el mapa de memòria. Per defecte en mode CGB. */
void
GBC_mem_init (
              const GBCu8     bios[0x900],    /* Pot ser NULL, els
        					 valors [0x100,0x1FF]
        					 no s'utilitzen. */
              GBC_MemAccess  *mem_access,     /* Pot ser NULL. */
              void           *udata
              );

/* Com init, però sense fixar altra vegada els callback i la bios. */
void
GBC_mem_init_state (void);

/* Indica si la BIOS està mapejada o no. */
GBC_Bool
GBC_mem_is_bios_mapped (void);

/* Llig un byte de l'adreça especificada. */
GBCu8
GBC_mem_read (
              const GBCu16 addr    /* Adreça. */
              );

/* Activa/Desactiva el mode traça en el mòdul de memòria. */
void
GBC_mem_set_mode_trace (
        		const GBC_Bool val
        		);

/* Escriu un byte en l'adreça especificada. */
void
GBC_mem_write (
               const GBCu16 addr,    /* Adreça. */
               const GBCu8  data     /* Dades. */
               );

int
GBC_mem_save_state (
        	    FILE *f
        	    );

int
GBC_mem_load_state (
        	    FILE *f
        	    );


/*******/
/* CPU */
/*******/
/* Mòdul que simula el processador. */

/* Mnemonics. */
typedef enum
  {
    GBC_UNK= 0,
    GBC_LD,
    GBC_PUSH,
    GBC_POP,
    GBC_LDI,
    GBC_LDD,
    GBC_ADD,
    GBC_ADC,
    GBC_SUB,
    GBC_SBC,
    GBC_AND,
    GBC_OR,
    GBC_XOR,
    GBC_CP,
    GBC_INC,
    GBC_DEC,
    GBC_DAA,
    GBC_CPL,
    GBC_CCF,
    GBC_SCF,
    GBC_NOP,
    GBC_HALT,
    GBC_DI,
    GBC_EI,
    GBC_RLCA,
    GBC_RLA,
    GBC_RRCA,
    GBC_RRA,
    GBC_RLC,
    GBC_RL,
    GBC_RRC,
    GBC_RR,
    GBC_SLA,
    GBC_SRA,
    GBC_SRL,
    GBC_RLD,
    GBC_RRD,
    GBC_BIT,
    GBC_SET,
    GBC_RES,
    GBC_JP,
    GBC_JR,
    GBC_CALL,
    GBC_RET,
    GBC_RETI,
    GBC_RST00,
    GBC_RST08,
    GBC_RST10,
    GBC_RST18,
    GBC_RST20,
    GBC_RST28,
    GBC_RST30,
    GBC_RST38,
    GBC_STOP,
    GBC_SWAP
  } GBC_Mnemonic;

/* Tipus d'operador. */
typedef enum
  {
    
    GBC_NONE= 0,
    
    /* Registres de 8 bits. */
    GBC_A,
    GBC_B,
    GBC_C,
    GBC_D,
    GBC_E,
    GBC_H,
    GBC_L,
    
    GBC_BYTE,
    
    GBC_DESP,
    
    GBC_SPdd,
    
    /* Adreces contingudes en registres de 16 bits (HL),(BC)... */
    GBC_pHL,
    GBC_pBC,
    GBC_pDE,
    
    GBC_ADDR,
    
    /* Registres de 16 bits. */
    GBC_BC,
    GBC_DE,
    GBC_HL,
    GBC_SP,
    GBC_AF,
    
    /* Bits. */
    GBC_B0,
    GBC_B1,
    GBC_B2,
    GBC_B3,
    GBC_B4,
    GBC_B5,
    GBC_B6,
    GBC_B7,
    
    GBC_WORD,
    
    /* Condicions. */
    GBC_F_NZ,
    GBC_F_Z,
    GBC_F_NC,
    GBC_F_C,
    
    /* Displacement. */
    GBC_BRANCH,
    
    /* Registres de 8 bits entre paréntesis. */
    GBC_pB,
    GBC_pC,
    GBC_pD,
    GBC_pE,
    GBC_pH,
    GBC_pL,
    GBC_pA,
    
    GBC_pBYTE,
    
    /* Accés a ports. */
    GBC_pFF00n,
    GBC_pFF00C
    
  } GBC_OpType;

/* Identifica a una instrucció. */
typedef struct
{
  
  GBC_Mnemonic name;        /* Nom. */
  GBC_OpType   op1;         /* Primer operant. */
  GBC_OpType   op2;         /* Segon operant. */
  
} GBC_InstId;

/* Dades relacionades amb els operadors. */
typedef union
{
  
  GBCu8  byte;           /* Byte. */
  GBCs8  desp;           /* Desplaçament. */
  GBCu16 addr_word;      /* Adreça o paraula. */
  struct
  {
    GBCs8  desp;
    GBCu16 addr;
  }      branch;         /* Bot. */
  
} GBC_InstExtra;

/* Estructura per a desar tota la informació relativa a una
 * instrucció.
 */
typedef struct
{
  
  GBC_InstId    id;          /* Identificador d'instrucció. */
  GBC_InstExtra e1;          /* Dades extra operador1. */
  GBC_InstExtra e2;          /* Dades extra operador2. */
  GBCu8         bytes[4];    /* Bytes */
  GBCu8         nbytes;      /* Número de bytes. */
  
} GBC_Inst;

/* Tipus de pas d'execució. */
typedef struct
{
  
  enum {
    GBC_STEP_INST,
    GBC_STEP_VBINT,
    GBC_STEP_LSINT,
    GBC_STEP_TIINT,
    GBC_STEP_SEINT,
    GBC_STEP_JOINT
  } type;    /* Tipus. */
  union
  {
    GBC_Inst inst;    /* Instrucció decodificada. */
  } val;    /* Valor. */
} GBC_Step;

/* Descodifica la instrucció de l'adreça indicada. */
GBCu16
GBC_cpu_decode (
        	GBCu16    addr,
        	GBC_Inst *inst
        	);

/* Descodifica en STEP el següent pas i torna l'adreça de la
 * següent instrucció en memòria.
 */
GBCu16
GBC_cpu_decode_next_step (
        		  GBC_Step *step
        		  );

/* Inicialitza el mòdul. */
void
GBC_cpu_init (
              GBC_Warning *warning,     /* Funció per als avisos. */
              void        *udata        /* Dades de l'usuari. */
              );

/* Com init, però sense fixar altra vegada els callback. */
void
GBC_cpu_init_state (void);

/* Fica la UCP en l'estat després de la seqüència d'encés. */
void
GBC_cpu_power_up (void);

/* Torna el contingut del registre IE. IE és el registre que habilitat
 * i deshabilita interrupcions.
 */
GBCu8
GBC_cpu_read_IE (void);

/* Torna el contingut del registre IF. IF és el registre amb les
 * peticions d'interrupció.
 */
GBCu8
GBC_cpu_read_IF (void);

/* Petició d'interrupció V-Blank. */
void
GBC_cpu_request_vblank_int (void);

/* Petició d'interrupció LCD STAT. */
void
GBC_cpu_request_lcdstat_int (void);

/* Petició d'interrupció Timer. */
void
GBC_cpu_request_timer_int (void);

/* Petició d'interrupció Serial. */
void
GBC_cpu_request_serial_int (void);

/* Petició d'interrupció Joypad. */
void
GBC_cpu_request_joypad_int (void);

/* Executa la següent instrucció o interrupció. Torna el número de
 * cicles.
 */
int
GBC_cpu_run (void);

/* Actica/Desactiva el mode CGB. */
void
GBC_cpu_set_cgb_mode (
        	      const GBC_Bool enabled
        	      );

/* Prepara la UCP per a modificar la velocitat. */
void
GBC_cpu_speed_prepare (
        	       const GBCu8 data
        	       );

/* Consulta la velocitat i si està llesta per a ser modificada. */
GBCu8
GBC_cpu_speed_query (void);

/* Escriu el registre IE. */
void
GBC_cpu_write_IE (
        	  GBCu8 data    /* Dades */
        	  );

/* Escriu el registre IF. */
void
GBC_cpu_write_IF (
        	  GBCu8 data    /* Dades */
        	  );

int
GBC_cpu_save_state (
        	    FILE *f
        	    );

int
GBC_cpu_load_state (
        	    FILE *f
        	    );


/**********/
/* TIMERS */
/**********/
/* Mòdul que implementa el temporitzador i el divisor. */

/* Procesa cicles de la UCP (rellotge). */
void
GBC_timers_clock (
        	  const int cc
        	  );

/* Torna el valor del divisor. */
GBCu8
GBC_timers_divider_read (void);

/* Escriu en el registre del divisor. */
void
GBC_timers_divider_write (
        		  GBCu8 data
        		  );

/* Inicialitza el mòdul. */
void
GBC_timers_init (void);

/* Llig el contingut del control del temporitzador. */
GBCu8
GBC_timers_timer_control_read (void);

/* Control del temporitzador. */
void
GBC_timers_timer_control_write (
        			GBCu8 data
        			);

/* Llig el comptador del temporitzador. */
GBCu8
GBC_timers_timer_counter_read (void);

/* Escriu en el comptador del temporitzador. */
void
GBC_timers_timer_counter_write (
        			GBCu8 data
        			);

/* Llig el mòdul del temporitzador. */
GBCu8
GBC_timers_timer_modulo_read (void);

/* Escriu en el mòdul del temporitzador. */
void
GBC_timers_timer_modulo_write (
        		       GBCu8 data
        		       );

int
GBC_timers_save_state (
        	       FILE *f
        	       );

int
GBC_timers_load_state (
        	       FILE *f
        	       );


/**********/
/* JOYPAD */
/**********/
/* Mòdul que implementa el mando. */

/* Tipus per a indicar amb l'operador lògic OR els botons actualment
 * actius.
 */
typedef enum
  {
    GBC_RIGHT= 0x01,
    GBC_LEFT= 0x02,
    GBC_UP= 0x04,
    GBC_DOWN= 0x08,
    GBC_BUTTON_A= 0x10,
    GBC_BUTTON_B= 0x20,
    GBC_SELECT= 0x40,
    GBC_START= 0x80
  } GBC_Button;

/* Tipus d'una funció que obté l'estat dels botons. Un botó està
 * apretat si el seu bit corresponent (GBC_Button) està actiu.
 */
typedef int (GBC_CheckButtons) (
        			void *udata
        			);

/* Inicialitza el mòdul. */
void
GBC_joypad_init (
        	 GBC_CheckButtons *check_buttons,
                 void             *udata
        	 );

/* Com init, però sense fixar altra vegada els callback. */
void
GBC_joypad_init_state (void);

/* Indica al mòdul que una tecla s'ha apretat. */
void
GBC_joypad_key_pressed (
        		GBC_Bool button_pressed,
        		GBC_Bool direction_pressed
        		);

/* Llig l'estat actual del mando. */
GBCu8
GBC_joypad_read (void);

/* Escriu en el registre del mando. */
void
GBC_joypad_write (
        	  GBCu8 data
        	  );

int
GBC_joypad_save_state (
        	       FILE *f
        	       );

int
GBC_joypad_load_state (
        	       FILE *f
        	       );


/*******/
/* LCD */
/*******/
/* Mòdul que implementa la pantalla de la GameBoy Color. */

/* Tipus de la funció que actualitza la pantalla real. FB és el buffer
 * amb una imatge de 160x144, on cada valor és un sencer entre
 * [0,32767] amb format BBBBBGGGGGRRRRR.
 */
typedef void (GBC_UpdateScreen) (
        			 const int  fb[23040 /*160x144*/],
        			 void      *udata
        			 );

/* Processa cicles de UCP. A vegades aquest dispositiu para el
 * processador, per eixe motiu torna els cicles extra que s'ha
 * processat mentre el processador estava parat.
 */
int
GBC_lcd_clock (
               const int cc    /* Cicles a processar. */
               );

/* Torna el contingut del registre de control. */
GBCu8
GBC_lcd_control_read (void);

/* Escriu en el registre de control. */
void
GBC_lcd_control_write (
        	       const GBCu8 data
        	       );

/* Fixa l'índex de la paleta (color) del fons. */
void
GBC_lcd_cpal_bg_index (
        	       const GBCu8 data
        	       );

/* Fixa l'índex de la paleta (color) dels sprites. */
void
GBC_lcd_cpal_ob_index (
        	       const GBCu8 data
        	       );

/* Llig dades de la paleta (color) del fons. */
GBCu8
GBC_lcd_cpal_bg_read_data (void);

/* Llig dades de la paleta (color) dels sprites. */
GBCu8
GBC_lcd_cpal_ob_read_data (void);

/* Fixa dades en la paleta (color) del fons. */
void
GBC_lcd_cpal_bg_write_data (
        		    const GBCu8 data
        		    );

/* Fixa dades en la paleta (color) dels sprites. */
void
GBC_lcd_cpal_ob_write_data (
        		    const GBCu8 data
        		    );

/* Torna les paletes de color actuals. */
void
GBC_lcd_get_cpal (
        	  int bg[8][4],    /* Guarda la paleta del fons. */
        	  int ob[8][4]     /* Guarda la paleta dels sprites. */
        	  );

/* Torna un punter a la memòria de vídeo. La grandària és 8192*2. */
const GBCu8 *
GBC_lcd_get_vram (void);

/* Torna el banc de memòria de vídeo actual. Sols funciona en mode
 * CGB.
 */
GBCu8
GBC_lcd_get_vram_bank (void);

/* Inicialitza el mòdul. */
void
GBC_lcd_init (
              GBC_UpdateScreen *update_screen,    /* Per a actualitzar
        					     la pantalla. */
              GBC_Warning      *warning,          /* Per a mostrar
        					     avisos. */
              void             *udata             /* Dades de
        					     l'usuari. */
              );

/* Com init, però sense fixar altra vegada els callback. */
void
GBC_lcd_init_state (void);

/* Inicialitza la paleta de colors a la paleta gris per al mode
 * DMG.
 */
void
GBC_lcd_init_gray_pal (void);

/* Torna el contingut del registre LY. */
GBCu8
GBC_lcd_ly_read (void);

/* Torna el contingut del registre LYC. */
GBCu8
GBC_lcd_lyc_read (void);

/* Escriu en el registre LYC. */
void
GBC_lcd_lyc_write (
        	   const GBCu8 data
        	   );

/* Torna la paleta (monocroma) del fons. */
GBCu8
GBC_lcd_mpal_bg_get (void);

/* Fixa la paleta (monocroma) del fons. */
void
GBC_lcd_mpal_bg_set (
        	     const GBCu8 data
        	     );

/* Torna la paleta (monocroma) dels sprites 0. */
GBCu8
GBC_lcd_mpal_ob0_get (void);

/* Fixa la paleta (monocroma) dels sprites 0. */
void
GBC_lcd_mpal_ob0_set (
        	      const GBCu8 data
        	      );

/* Torna la paleta (monocroma) dels sprites 1. */
GBCu8
GBC_lcd_mpal_ob1_get (void);

/* Fixa la paleta (monocroma) dels sprites 1. */
void
GBC_lcd_mpal_ob1_set (
        	      const GBCu8 data
        	      );

/* Inicialitza la transferència DMA a la OAM. Teòricament sols es pot
 * accedir a la HRAM mentre esta operació s'està executant i tarda
 * aproximadament 160 (80 double) microsegons, però assumiré que el
 * programador sap gastar-ho, i per tant s'executa la operació sense
 * esperar ni comprovar si es pot accedir a la OAM.
 */
void
GBC_lcd_oam_dma (
        	 const GBCu8 data
        	 );

/* LLig de l'adreça indicada d'OAM. L'adreça ha d'estar en el rang
 * [0000-009F].
 */
GBCu8
GBC_lcd_oam_read (
        	  const GBCu16 addr
        	  );

/* Escriu en l'adreça indicada d'OAM. L'adreça ha d'estar en el rang
 * [0000-009F].
 */
void
GBC_lcd_oam_write (
        	   const GBCu16 addr,
        	   const GBCu8  data
        	   );

/* Registre per a bloquejar/desbloquejar (0/1) la paleta de colors en
 * mode DMG.
 */
void
GBC_lcd_pal_lock (
        	  const GBCu8 data
        	  );

/* Torna el contingut del registre SCX. */
GBCu8
GBC_lcd_scx_read (void);

/* Escriu en el registre SCX. */
void
GBC_lcd_scx_write (
        	   const GBCu8 data
        	   );

/* Torna el contingut del registre SCY. */
GBCu8
GBC_lcd_scy_read (void);

/* Escriu en el registre SCY. */
void
GBC_lcd_scy_write (
        	   const GBCu8 data
        	   );

/* Selecciona el banc de memòria de vídeo. Sols funciona en mode
 * CGB.
 */
void
GBC_lcd_select_vram_bank (
        		  const GBCu8 data
        		  );

/* Activa/Desactiva el mode CGB. Açò s'ha de fer amb coneixement, es a
 * dir, quan no s'està gastant ninguna característica especial del
 * mode CGB.
 */
void
GBC_lcd_set_cgb_mode (
        	      const GBC_Bool enabled
        	      );

/* Torna el contingut del registre d'estat. */
GBCu8
GBC_lcd_status_read (void);

/* Escriu en el registre d'estat. */
void
GBC_lcd_status_write (
        	      const GBCu8 data
        	      );

/* Para el dispositiu gràfic. Quan està parat la pantalla es torna
 * blanca i no genera interrupcions ni processa res.
 */
void
GBC_lcd_stop (
              const GBC_Bool state
              );

/* Fixa la part alta de l'adreça destí per al DMA. */
void
GBC_lcd_vram_dma_dst_high (
        		   const GBCu8 data
        		   );

/* Fixa la part baixa de l'adreça destí per al DMA. */
void
GBC_lcd_vram_dma_dst_low (
        		  const GBCu8 data
        		  );

/* Inicialitza la transferència DMA. En el cas de General Porpouse DMA
 * sols es pot accedir a la VRAM d'acord al tipus de període, però
 * assumiré que es fa un bon ús. En el cas del H-Blank DMA assumiré que
 * si comencem en meitat d'un període H-BLANK, realment la
 * transferència s'inicia en el següent període.
 */
void
GBC_lcd_vram_dma_init (
        	       const GBCu8 data
        	       );

/* Fixa la part alta de l'adreça origen per al DMA. */
void
GBC_lcd_vram_dma_src_high (
        		   const GBCu8 data
        		   );

/* Fixa la part baixa de l'adreça origen per al DMA. */
void
GBC_lcd_vram_dma_src_low (
        		  const GBCu8 data
        		  );

/* Torna l'estat del DMA. */
GBCu8
GBC_lcd_vram_dma_status (void);

/* LLig de l'adreça indicada. L'adreça ha d'estar en el rang
 * [0000-1FFF].
 */
GBCu8
GBC_lcd_vram_read (
        	   const GBCu16 addr
        	   );

/* Escriu en l'adreça indicada. L'adreça ha d'estar en el rang
 * [0000-1FFF].
 */
void
GBC_lcd_vram_write (
        	    const GBCu16 addr,
        	    const GBCu8  data
        	    );

/* Torna el contingut del registre WX. */
GBCu8
GBC_lcd_wx_read (void);

/* Escriu en el registre WX. */
void
GBC_lcd_wx_write (
        	  const GBCu8 data
        	  );

/* Torna el contingut del registre WY. */
GBCu8
GBC_lcd_wy_read (void);

/* Escriu en el registre WY. */
void
GBC_lcd_wy_write (
        	  const GBCu8 data
        	  );

int
GBC_lcd_save_state (
        	    FILE *f
        	    );

int
GBC_lcd_load_state (
        	    FILE *f
        	    );


/*******/
/* APU */
/*******/
/* Mòdul que implementa el xip de so. */

/* Número de mostres per segon que genera el xip. */
#define GBC_APU_SAMPLES_PER_SEC 1048576

/* Número de mostres que té cadascun dels buffers que genera el xip de
 * sò. Es poc més d'una centèsima de segon.
 */
#define GBC_APU_BUFFER_SIZE 10486

/* Tipus de la funció que actualitza es crida per a reproduir so. Es
 * proporcionen el canal de l'esquerra i el de la dreta. Cada mostra
 * està codificada en un valor en el rang [0,1].
 */
typedef void (GBC_PlaySound) (
        		      const double  left[GBC_APU_BUFFER_SIZE],
        		      const double  right[GBC_APU_BUFFER_SIZE],
        		      void         *udata
        		      );

/* Registre amb els 3 bits superiors, inicialització i algo més per al
 * canal 1.
 */
void
GBC_apu_ch1_freq_hi (
        	     GBCu8 data
        	     );

/* Registre amb els 8 bits més baixos de la freqüència per al canal
 * 1.
 */
void
GBC_apu_ch1_freq_lo (
        	     GBCu8 data
        	     );

/* Torna l'estat del 'length counter' del canal 1. */
GBCu8
GBC_apu_ch1_get_lc_status (void);

/* Torna 'Wave pattern duty' del canal 1. */
GBCu8
GBC_apu_ch1_get_wave_pattern_duty (void);

/* Fixa el 'wave pattern duty' i la longitut del canal 1. */
void
GBC_apu_ch1_set_length_wave_pattern_dutty (
        				   GBCu8 data
        				   );

/* Llig el contingut del registre de sweep del canal 1. */
GBCu8
GBC_apu_ch1_sweep_read (void);

/* Escriu en el registre de sweep del canal 1. */
void
GBC_apu_ch1_sweep_write (
        		 GBCu8 data
        		 );

/* Llig el contingut del registre de volum del canal 1. */
GBCu8
GBC_apu_ch1_volume_envelope_read (void);

/* Escriu en el registre de volum del canal 1. */
void
GBC_apu_ch1_volume_envelope_write (
        			   GBCu8 data
        			   );

/* Registre amb els 3 bits superiors, inicialització i algo més per al
 * canal 2.
 */
void
GBC_apu_ch2_freq_hi (
        	     GBCu8 data
        	     );

/* Registre amb els 8 bits més baixos de la freqüència per al canal
 * 2.
 */
void
GBC_apu_ch2_freq_lo (
        	     GBCu8 data
        	     );

/* Torna l'estat del 'length counter' del canal 2. */
GBCu8
GBC_apu_ch2_get_lc_status (void);

/* Torna 'Wave pattern duty' del canal 2. */
GBCu8
GBC_apu_ch2_get_wave_pattern_duty (void);

/* Fixa el 'wave pattern duty' i la longitut del canal 2. */
void
GBC_apu_ch2_set_length_wave_pattern_dutty (
        				   GBCu8 data
        				   );

/* Llig el contingut del registre de volum del canal 2. */
GBCu8
GBC_apu_ch2_volume_envelope_read (void);

/* Escriu en el registre de volum del canal 2. */
void
GBC_apu_ch2_volume_envelope_write (
        			   GBCu8 data
        			   );

/* Registre amb els 3 bits superiors, inicialització i algo més per al
 * canal 3.
 */
void
GBC_apu_ch3_freq_hi (
        	     GBCu8 data
        	     );

/* Registre amb els 8 bits més baixos de la freqüència per al canal
 * 3.
 */
void
GBC_apu_ch3_freq_lo (
        	     GBCu8 data
        	     );

/* Torna l'estat del 'length counter' del canal 3. */
GBCu8
GBC_apu_ch3_get_lc_status (void);

/* Llig el volum del canal 3. */
GBCu8
GBC_apu_ch3_output_level_read (void);

/* Escriu el volum del canal 3. */
void
GBC_apu_ch3_output_level_write (
        			GBCu8 data
        			);

/* Llig de la memòria interna del canal 3. */
GBCu8
GBC_apu_ch3_ram_read (
        	      int pos
        	      );

/* Escriu en la memòria interna del canal 3. */
void
GBC_apu_ch3_ram_write (
        	       GBCu8 data,
        	       int   pos
        	       );

/* Fixa la longitut del canal 3. */
void
GBC_apu_ch3_set_length (
        		GBCu8 data
        		);

/* Indica si el canal 3 està actiu o no. */
GBCu8
GBC_apu_ch3_sound_on_off_read (void);

/* Canvia el Master Channel Control Switch del canal 3. */
void
GBC_apu_ch3_sound_on_off_write (
        			GBCu8 data
        			);

/* Torna l'estat del 'length counter' del canal 4. */
GBCu8
GBC_apu_ch4_get_lc_status (void);

/* Inicialitza el canal 4. */
void
GBC_apu_ch4_init (
        	  GBCu8 data
        	  );

/* Llig el contingut del polynomial counter del canal 4. */
GBCu8
GBC_apu_ch4_polynomial_counter_read (void);

/* Escriu en el contingut del polynomial counter del canal 4. */
void
GBC_apu_ch4_polynomial_counter_write (
        			      GBCu8 data
        			      );

/* Fixa la longitut del canal 4. */
void
GBC_apu_ch4_set_length (
        		GBCu8 data
        		);

/* Llig el contingut del registre de volum del canal 4. */
GBCu8
GBC_apu_ch4_volume_envelope_read (void);

/* Escriu en el registre de volum del canal 4. */
void
GBC_apu_ch4_volume_envelope_write (
        			   GBCu8 data
        			   );

/* Alimenta el dispositiu amb cicles de rellotge de la UCP. */
void
GBC_apu_clock (
               const int cc
               );

/* Torna l'estat. */
GBCu8
GBC_apu_get_status (void);

/* Inicialitza el mòdul. */
void
GBC_apu_init (
              GBC_PlaySound *play_sound,
              void          *udata
              );

/* Com init, però sense fixar altra vegada els callback. */
void
GBC_apu_init_state (void);

/* Fixa els valors després del procés d'encès. Aquesta funció s'ha de
 * cridar després de 'GBC_apu_init'.
 */
void
GBC_apu_power_up (void);

/* Llig el contingut del registre de selecció de canals. */
GBCu8
GBC_apu_select_out_read (void);

/* Escriu en el registre de selecció de canals. */
void
GBC_apu_select_out_write (
        		  GBCu8 data
        		  );

/* Para el dispositiu de so. Quan està parat es sent res. Per
 * conveniència el simulador continuarà generant samples a la mateixa
 * freqüència.
 */
void
GBC_apu_stop (
              const GBC_Bool state
              );

/* Encen o apaga el dispositiu de so. */
void
GBC_apu_turn_on (
        	 GBCu8 data
        	 );

/* Llig el contingut del registre de Vin. */
GBCu8
GBC_apu_vin_read (void);

/* Escriu en el registre de Vin. */
void
GBC_apu_vin_write (
        	   GBCu8 data
        	   );

int
GBC_apu_save_state (
        	    FILE *f
        	    );

int
GBC_apu_load_state (
        	    FILE *f
        	    );


/********/
/* MAIN */
/********/
/* Mòdul que controla el simulador. */

/* Número aproxima de cicles per segon. */
#define GBC_CICLES_PER_SEC 4194304

/* Tipus de funció amb la que el 'frontend' indica a la llibreria si
 * s'ha produït una senyal de parada, s'ha apretat un botó o la
 * creueta de direcció. A més esta funció pot ser emprada per el
 * frontend per a tractar els events pendents.
 */
typedef void (GBC_CheckSignals) (
        			 GBC_Bool *stop,
        			 GBC_Bool *button_pressed,
        			 GBC_Bool *direction_pressed,
        			 void     *udata
        			 );

/* Tipus de funció per a saber quin a sigut l'últim pas d'execució de
 * la UCP.
 */
typedef void (GBC_CPUStep) (
        		    const GBC_Step *step,        /* Punter a
        						    pas
        						    d'execuió. */
        		    const GBCu16    nextaddr,    /* Següent
        						    adreça de
        						    memòria. */
        		    void           *udata
        		    );

/* No tots els camps tenen que ser distint de NULL. */
typedef struct
{
  
  GBC_MemAccess     *mem_access;         /* Es crida cada vegada que
        				    es produïx un accés a
        				    memòria. */
  GBC_MapperChanged *mapper_changed;    /* Es crida cada vegada que el
        				   segon banc de memòria de la
        				   ROM es modifica. */
  GBC_CPUStep       *cpu_step;          /* Es crida en cada pas de la
        				   UCP. */
  
} GBC_TraceCallbacks;

/* Conté la informació necessària per a comunicar-se amb el
 * 'frontend'.
 */
typedef struct
{
  
  GBC_Warning              *warning;             /* Funció per a
        					    mostrar avisos. */
  GBC_GetExternalRAM       *get_external_ram;    /* Per a obtindre la
        					    memòria RAM
        					    externa. */
  GBC_UpdateScreen         *update_screen;       /* Actualitza la
        					    pantalla. */
  GBC_CheckSignals         *check;               /* Comprova si ha de
        					    parar i events
        					    externs. Pot ser
        					    NULL, en eixe cas
        					    el simulador
        					    s'executarà fins
        					    que es cride a
        					    'GBC_stop'. */
  GBC_CheckButtons         *check_buttons;       /* Comprova l'estat
        					    dels botons. */
  GBC_PlaySound            *play_sound;          /* Reprodueix so. */
  GBC_UpdateRumble         *update_rumble;       /* Actualitza el
        					    valor del
        					    rumble. */
  const GBC_TraceCallbacks *trace;               /* Pot ser NULL si no
        					    es van a gastar
        					    les funcions per a
        					    fer una traça. */
  
} GBC_Frontend;

/* Canvia la velocitat. */
void
GBC_main_switch_speed (void);

/* Inicialitza la llibreria, s'ha de cridar cada vegada que s'inserte
 * una nova rom. Torna GBC_NOERROR si tot ha anat bé.
 */
GBC_Error
GBC_init (
          const GBCu8         bios[0x900],    /* BIOS. Pot ser
        					 NULL. Els valors
        					 [0x100-0x1FF] no
        					 s'utilitzen. */
          const GBC_Rom      *rom,            /* ROM. */
          const GBC_Frontend *frontend,       /* Frontend. */
          void               *udata           /* Dades proporcionades
        					 per l'usuari que són
        					 pasades al
        					 'frontend'. */
         );

/* Executa un cicle de la GameBoy Color. Aquesta funció executa una
 * iteració de 'GBC_loop' i torna els cicles de UCP emprats. Si
 * CHECKSIGNALS en el frontend no és NULL aleshores cada cert temps al
 * cridar a GBC_iter es fa una comprovació de CHECKSIGNALS.  La funció
 * CHECKSIGNALS del frontend es crida amb una freqüència suficient per
 * a que el frontend tracte els seus events. La senyal stop de
 * CHECKSIGNALS és llegit en STOP si es crida a CHECKSIGNALS.
 */
int
GBC_iter (
          GBC_Bool *stop
          );

/* Envia la senyal de que s'ha apretat un botó o la creuta
 * direccional.
 */
void
GBC_key_pressed (
        	 GBC_Bool button_pressed,
        	 GBC_Bool direction_pressed
        	 );

/* Carrega l'estat de 'f'. Torna 0 si tot ha anat bé. S'espera que el
 * fitxer siga un fitxer d'estat vàlid de GameBoy Color per a la ROM
 * (BIOS si s'ha desat durant la bios) actual. Si es produeix un error
 * de lectura o es compromet la integritat del simulador, aleshores es
 * reiniciarà el simulador.
 */
int
GBC_load_state (
        	FILE *f
        	);

/* Executa la GameBoy Color. Aquesta funció es bloqueja fins que llig
 * una senyal de parada mitjançant CHECKSIGNALS o mitjançant GBC_stop,
 * si es para es por tornar a cridar i continuarà on s'havia
 * quedat. La funció CHECKSIGNALS del frontend es crida amb una
 * freqüència suficient per a que el frontend tracte els seus events.
 */
void
GBC_loop (void);

/* Escriu en 'f' l'estat de la màquina. Torna 0 si tot ha anat bé, -1
 * en cas contrari.
 */
int
GBC_save_state (
        	FILE *f
        	);

/* Para a 'GBC_loop'. */
void
GBC_stop (void);

/* Executa els següent pas de UCP en mode traça. Tots aquelles
 * funcions de 'callback' que no són nul·les es cridaran si és el
 * cas. Torna el clocks de rellotge executats en l'últim pas.
 */
int
GBC_trace (void);

#endif /* __GBC_H__ */
