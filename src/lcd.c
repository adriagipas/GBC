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
 *  lcd.c - Implementació del mòdul 'LCD'.
 *
 *  NOTES:
 * - No tinc clar si les interrupcions STAT es produeixen en el moment
 *   del canvi, o sempre que s'està en l'estat indicat. De moment vaig
 *   a fer que es produeix en el moment del canvi d'estat.
 * - No entenc en 'pandocs' la part de 'Writing will reset the
 *   counter.' referint-se al registres LY. Ho vaig a ignorar.
 * - El codi inicial del ZELDA clarament escriu en VRAM mentres s'està
 *   renderitzant. He decidit permetre tots els accessos a vídeo
 *   mentres es renderitza.
 * - Supossare que al deshabilitar la pantalla es torna al principi.
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


/* Màxim i mínim. */
#define MAX(A,B) (((A)>(B)) ? (A) : (B))
#define MIN(A,B) (((A)<(B)) ? (A) : (B))

/* Número màxim d'sprites per línia. */
#define NMAX_SPRITES 10

/* Coincidence flag. */
#define CFLAG 0x04

/* Temps. */
/* Els modes son variables, agafe el temps en el que el mode 0 està
   més rato. */
#define CICLESPERLINE 456
#define CICLESTOM3 77
#define CICLESTOM0 246
#define CICLESPERFRAME 70224

/* Grandària banc. */
#define BANK_SIZE 8192

/* Grandària OAM. */
#define OAM_SIZE 160


/* MACROS DE 'render_line_bg_color'. */
#define VFLIP 0x40
#define HFLIP 0x20
#define BGPRIOR 0x80

#define GET_NEXT_NT_MONO_AUX        		\
  addr= addr_row|addr_col;                      \
  NT= _vram[0][addr]

#define GET_NEXT_NT_MONO        		\
  GET_NEXT_NT_MONO_AUX;        			\
  if ( (++addr_col) == 32 ) addr_col= 0

#define GET_NEXT_NT_MONO_WIN        		\
  GET_NEXT_NT_MONO_AUX;        			\
  ++addr_col

#define GET_NEXT_NT_COLOR_AUX        		\
  addr= addr_row|addr_col;                      \
  NT= _vram[0][addr];        			\
  ATTR= _vram[1][addr];        			\
  tram= &(_vram[(ATTR&0x08)>>3][0])

#define GET_NEXT_NT_COLOR        		\
  GET_NEXT_NT_COLOR_AUX;        		\
  if ( (++addr_col) == 32 ) addr_col= 0

#define GET_NEXT_NT_COLOR_WIN        		\
  GET_NEXT_NT_COLOR_AUX;        		\
  ++addr_col

#define CALC_ADDR_PAT_MONO_AUX        		\
  addr_pat=        				\
    _control.bgwin_tile_data ?        		\
    (((GBCu16) NT)<<4) :        		\
    (GBCu16) ((0x100+((GBCs8) NT))<<4);        	\
  addr_pat|= sel_bp

#define CALC_ADDR_PAT_MONO        		\
  CALC_ADDR_PAT_MONO_AUX

#define CALC_ADDR_PAT_MONO_WIN        		\
  CALC_ADDR_PAT_MONO_AUX

#define CALC_ADDR_PAT_COLOR_AUX        		\
  addr_pat=        				\
    _control.bgwin_tile_data ?        		\
    (((GBCu16) NT)<<4) :        		\
    (GBCu16) ((0x100+((GBCs8) NT))<<4);        	\
  addr_pat|= (ATTR&VFLIP)?sel_bp_flip:sel_bp

#define CALC_ADDR_PAT_COLOR        		\
  CALC_ADDR_PAT_COLOR_AUX;        		\
  sel_pal^= 1;        				\
  pal[sel_pal]= &(_cpal.bg.v[ATTR&0x7][0])

#define CALC_ADDR_PAT_COLOR_WIN        		\
  CALC_ADDR_PAT_COLOR_AUX;        		\
  pal= &(_cpal.bg.v[ATTR&0x7][0]);        	\
  prio= ((ATTR&BGPRIOR)!=0)

#define GET_NEXT_BP_COLOR        				   \
  if ( ATTR&HFLIP )        					   \
    {        							   \
      bp0|= reverse_byte ( tram[addr_pat] );        		   \
      bp1|= reverse_byte ( tram[addr_pat|1] );        		   \
    }        							   \
  else        							   \
    {        							   \
      bp0|= tram[addr_pat];        				   \
      bp1|= tram[addr_pat|1];        				   \
    }        							   \
  bpal|= sel_pal ? 0xFF : 0x00;        				   \
  bprio|= ATTR&BGPRIOR ? 0xFF : 0x00

#define GET_NEXT_BP_MONO        				   \
  bp0|= tram[addr_pat];        					   \
  bp1|= tram[addr_pat|1]

#define GET_NEXT_BP_COLOR_WIN        				   \
  do {        							   \
    if ( ATTR&HFLIP )        					   \
      {        							   \
        bp0= reverse_byte ( tram[addr_pat] );			   \
        bp1= reverse_byte ( tram[addr_pat|1] );			   \
      }        							   \
    else        						   \
      {        							   \
        bp0= tram[addr_pat];					   \
        bp1= tram[addr_pat|1];					   \
      }        							   \
  } while(0)

#define GET_NEXT_BP_MONO_WIN        				   \
  do {        							   \
    bp0= tram[addr_pat];        				   \
    bp1= tram[addr_pat|1];        				   \
  } while(0)

#define RENDER_LINE_BG_COLOR_INIT                                  \
  sel_pal= 0;        						   \
  GET_NEXT_NT_COLOR;        					   \
  CALC_ADDR_PAT_COLOR;        					   \
  if ( ATTR&HFLIP )        					   \
    {        							   \
      bp0= ((GBCu16) reverse_byte ( tram[addr_pat] ))<<8;           \
      bp1= ((GBCu16) reverse_byte ( tram[addr_pat|1] ))<<8;           \
    }        							   \
  else        							   \
    {        							   \
      bp0= ((GBCu16) tram[addr_pat])<<8;        		   \
      bp1= ((GBCu16) tram[addr_pat|1])<<8;        		   \
    }        							   \
  bpal= sel_pal ? 0xFF00 : 0x0000;        			   \
  bprio= ATTR&BGPRIOR ? 0xFF00 : 0x0000

#define RENDER_LINE_BG_MONO_INIT        			   \
  GET_NEXT_NT_MONO;        					   \
  CALC_ADDR_PAT_MONO;        					   \
  bp0= ((GBCu16) tram[addr_pat])<<8;        			   \
  bp1= ((GBCu16) tram[addr_pat|1])<<8




/*********/
/* TIPUS */
/*********/

typedef struct
{
  
  int      v[8][4];
  int      p; /* Paleta. */
  int      c; /* Color. */
  int      high; /* Posició. */
  GBC_Bool auto_increment;
  
} cpal_t;




/*********/
/* ESTAT */
/*********/

/* Mode. */
static GBC_Bool _cgb_mode;
static GBC_Bool _pal_lock;

/* Callbacks. */
static GBC_Warning *_warning;
static GBC_UpdateScreen *_update_screen;
static void *_udata;

/* Registre de control. */
static struct
{
  
  GBCu8    data;               /* Valor del registre. */
  GBC_Bool enabled;            /* Dispositiu activat. */
  GBCu16   win_tile_map;       /* Adreça del mapa de tiles utilitzat
        			  per la finestra. */
  GBC_Bool b5;                 /* Bit5. */
  GBC_Bool win_enabled;        /* Finestra activada. */
  GBC_Bool bgwin_tile_data;    /* TRUE -> 8000-8FFF (Unsigned). FALSE
        			  -> 8800-97FF (Signed). */
  GBC_Bool bg_tile_map;        /* Adreça del mapa de tiles utilitzat
        			  per al fons. */
  GBC_Bool obj_size16;         /* A cert indica que els sprites són de
        			  8x16. */
  GBC_Bool obj_enabled;        /* Sprites actius. */
  GBC_Bool bg_enabled;         /* Fons actiu. */
  GBC_Bool obj_has_prio;       /* Els sprites sempre tenen prioritat
        			  sobre el fons i la finestra. */
  
} _control;

/* Registre d'estat. */
static struct
{
  
  GBCu8    hdata;           /* Conté la part que no varia del registre
        		       ([7-3]). */
  GBC_Bool intC_enabled;    /* Interrupció coincidència. */
  GBC_Bool int2_enabled;    /* Interrupció mode 2. */
  GBC_Bool int1_enabled;    /* Interrupció mode 1. */
  GBC_Bool int0_enabled;    /* Interrupció mode 0. */
  GBCu8    mode;            /* Mode actual. */
  
} _status;

/* Timing. */
static struct
{
  
  int cc;           /* Cicles acumulats. */
  int cctoVBInt;    /* Cicles per a la següent interrupció
        	       VBlank. Coincideix amb la interrupció mode
        	       1. */
  int cctoCInt;     /* Cicles fins a la següent interrupció per
        	       coincidència. */
  int ccto2Int;     /* Cicles fins a la següent interrupció mode 2. */
  int ccto0Int;     /* Cicles fins a la següent interrupció mode 0. */
  int extracc;      /* Cicles extra. */
  
} _timing;

/* Registres de posició. */
static struct
{
  
  GBCu8 SCY,SCX;
  int LY,LX;
  int LYC;
  GBCu8 WY,WX;
  
} _pos;

/* Memòria. */
static GBCu8 _vram[2][BANK_SIZE];
static GBCu8 *_cvram;
static GBCu8 _vram_selected;

/* OAM. */
static GBCu8 _oam[OAM_SIZE];

/* DMA. */
static struct
{
  
  GBCu16   src;
  GBCu16   dst;
  GBCu8    length;
  GBC_Bool active;
  
} _dma;

/* Paleta monocroma. */
static struct
{
  
  GBCu8 bg[4];
  GBCu8 ob0[4];
  GBCu8 ob1[4];
  
} _mpal;

/* Paleta de colors. */
static struct
{
  
  cpal_t bg;
  cpal_t ob;
  
} _cpal;

/* Estat renderitzat. */
static struct
{
  
  int  fb[23040 /*160x144*/];    /* Frame buffer. */
  int  lines;                    /* Número de línies reals
        			    renderitzades. */
  int *p;                        /* Apunta al següent píxel a
        			    renderitzar. */
  int  line_bg[160];             /* Línia amb el fons dibuixat. */
  int  line_obj[160];            /* Línia amb els sprites. -1 indica
        			    transparent. */
  signed char prio_bg[160];      /* La prioritat segons el fons. 0 ->
        			    El que diguen els sprites. 1 ->
        			    Prioritat fons. -1 -> Color fons
        			    transparent. */
  signed char prio_obj[160];     /* La prioritat segons els sprites. 0
        			    -> prioritat sprite. El valor sols
        			    es fixa quan el color no és
        			    transparent. */
  
} _render;

/* Indica si està parat. */
static GBC_Bool _stop;




/*********************/
/* FUNCIONS PRIVADES */
/*********************/


static void
update_cctoCInt (void)
{
  
  if ( _pos.LY < _pos.LYC )
    _timing.cctoCInt= (_pos.LYC-_pos.LY)*CICLESPERLINE - _pos.LX;
  else
    _timing.cctoCInt= (_pos.LYC+154-_pos.LY)*CICLESPERLINE - _pos.LX;
  
} /* end update_cctoCInt */


static void
vram_dma_hblank_block (void)
{
  
  int i;
  
  
  if ( (_dma.src >= 0x0000 && _dma.src < 0x8000) ||
       (_dma.src >= 0xA000 && _dma.src < 0xE000) )
    for ( i= 0; i < 0x10; ++i, ++_dma.dst, ++_dma.src )
      _cvram[_dma.dst]= GBC_mem_read ( _dma.src );
  else { _dma.dst+= 0x10; _dma.src+= 10; }
  
} /* vram_dma_hblank_block */


static GBCu8
reverse_byte (
              GBCu8 byte
              )
{
  
  int i;
  GBCu8 ret;
  
  
  ret= 0;
  for ( i= 0; i < 8; ++i )
    {
      ret<<= 1;
      if ( byte&0x1 ) ret|= 0x1;
      byte>>= 1;
    }
  
  return ret;
  
} /* end reverse_byte */


static void
render_line_bg_mono (void)
{
  
  int x, i, j, row, sel_bp, color;
  const int *pal;
  GBCu8 NT;
  GBCu16 addr, addr_row, addr_col, addr_pat, desp0, desp1,
    bp0, bp1;
  const GBCu8 *tram;
  
  
  /* RAM tiles i paleta. */
  tram= &(_vram[0][0]);
  pal= &(_cpal.bg.v[0][0]);
  
  /* Açò no pot passar en mode color. */
  if ( !_control.bg_enabled)
    {
      for ( x= 0; x < 160; ++x )
        {
          _render.line_bg[x]= pal[_mpal.bg[0]];
          _render.prio_bg[x]= -1;
        }
      return;
    }
  
  /* Calcula fila (i adreça base) del 'map tile'. */
  row= _render.lines + _pos.SCY;
  if ( row >= 256 ) row-= 256;
  addr_row= _control.bg_tile_map | ((row&0xF8)<<2);
  
  /* Selector de bitmap dins d'un patró. */
  sel_bp= (row&0x7)<<1;
  
  /* Columna inicial. */
  addr_col= (GBCu16) (_pos.SCX>>3);
  
  /* Desplaçaments. */
  desp0= 15 - (_pos.SCX&0x7);
  desp1= desp0 - 1;
  
  /* Renderitza. */
  RENDER_LINE_BG_MONO_INIT;
  for ( x= i= 0; i < 20; ++i )
    {
      GET_NEXT_NT_MONO;
      CALC_ADDR_PAT_MONO;
      GET_NEXT_BP_MONO;
      for ( j= 0; j < 8; ++j, ++x )
        {
          color= ((bp0>>desp0)&0x01) | ((bp1>>desp1)&0x02);
          _render.line_bg[x]= pal[_mpal.bg[color]];
          _render.prio_bg[x]= color==0 ? -1 : 0;
          bp0<<= 1; bp1<<= 1;
        }
    }
  
} /* end render_line_bg_mono */


static void
render_line_win_mono (void)
{
  
  int x, i, row, sel_bp, color;
  const int *pal;
  GBCu8 NT, bp0, bp1;
  GBCu16 addr, addr_row, addr_col, addr_pat;
  const GBCu8 *tram;
  
  
  /* Si no està activa no fa res. */
  if ( !_control.win_enabled) return;
  
  /* RAM tiles i paleta. */
  tram= &(_vram[0][0]);
  pal= &(_cpal.bg.v[0][0]);
  
  /* Calcula fila (i adreça base) del 'map tile'. */
  row= _render.lines - _pos.WY;
  if ( row < 0 ) return;
  addr_row= _control.win_tile_map | ((row&0xF8)<<2);
  
  /* Selector de bitmap dins d'un patró. */
  sel_bp= (row&0x7)<<1;
  
  /* Columna inicial. */
  addr_col= 0x0000;
  
  /* Renderitza primer tile (Pot apareixer fora de la pantalla). */
  x= _pos.WX-7;
  GET_NEXT_NT_MONO_WIN;
  CALC_ADDR_PAT_MONO_WIN;
  GET_NEXT_BP_MONO_WIN;
  for ( i= 0; x < 0; ++x, ++i ) { bp0<<= 1; bp1<<= 1; }
  for ( ; i < 8 && x < 160; ++x, ++i )
    {
      color= ((bp0>>7)&0x01) | ((bp1>>6)&0x02);
      _render.line_bg[x]= pal[_mpal.bg[color]];
      _render.prio_bg[x]= color==0 ? -1 : 0;
      bp0<<= 1; bp1<<= 1;
    }
  if ( x == 160 ) return;
  
  /* Renderitza resta. */
  do {
    GET_NEXT_NT_MONO_WIN;
    CALC_ADDR_PAT_MONO_WIN;
    GET_NEXT_BP_MONO_WIN;
    for ( i= 0; i < 8 && x < 160; ++x, ++i )
      {
        color= ((bp0>>7)&0x01) | ((bp1>>6)&0x02);
        _render.line_bg[x]= pal[_mpal.bg[color]];
        _render.prio_bg[x]= color==0 ? -1 : 0;
        bp0<<= 1; bp1<<= 1;
      }
  } while ( x < 160 );
  
} /* end render_line_win_mono */


static void
clear_line_obj (void)
{
  
  int i;
  
  
  for ( i= 0; i < 160; ++i )
    _render.line_obj[i]= -1;
  
} /* end clear_line_obj */


static void
render_line_obj_mono (void)
{
  
  /* NOTA: Com no es diu res en ninguna part de la documentació sobre
     els timings, i com la coordinada vertical està desplaçada 16
     píxels (cosa que hem despista) faré l'avaluació i renderitza al
     mateix temps. */
  
  struct
  {
    const GBCu8 *obj;
    int          row;
  } buffer[NMAX_SPRITES];
  int n, N, row, max_row, x, begin, end, color;
  const GBCu8 *p, *tram;
  GBCu8 NT, ATTR, bp0, bp1, *mpal;
  GBCu16 addr_pat;
  const int *pal;
  signed char prio;
  
  
  /* Si no està activat. */
  if ( !_control.obj_enabled )
    {
      clear_line_obj ();
      return;
    }
  
  /* AVALUACIÓ. */
  /* ATENCIÓ!!!! En mode B/N la prioritat no es basa en l'ordre. */
  /* ATENCIÓ!!!! Però de moment passe!!!!. */
  max_row= _control.obj_size16 ? 16 : 8;
  N= 0;
  for ( n= 0, p= &(_oam[0]); n < 40; ++n, p+= 4 )
    {
      row= _render.lines + 16 - *p;
      if ( row >= 0 && row < max_row )
        {
          buffer[N].obj= p;
          buffer[N].row= row;
          if ( ++N == NMAX_SPRITES ) break;
        }
    }
  
  /* VRAM bank i paletes. */
  tram= &(_vram[0][0]);
  
  /* PINTA. */
  clear_line_obj ();
  for ( n= N-1; n >= 0; --n )
    {
      
      p= buffer[n].obj;
      row= buffer[n].row;
      
      /* NT, ATTR i tram. */
      NT= p[2];
      ATTR= p[3];
      
      /* Calcula addr_pat. */
      if ( _control.obj_size16 ) NT&= 0xFE;
      if ( ATTR&0x40 /* Y-FLIP */)
        row= max_row - row - 1;
      addr_pat= (((GBCu16) NT)<<4) | (row<<1);
      
      /* Prepara renderitzat. */
      if ( ATTR&0x20 /* X-FLIP */ )
        {
          bp0= reverse_byte ( tram[addr_pat] );
          bp1= reverse_byte ( tram[addr_pat|1] );
        }
      else
        {
          bp0= tram[addr_pat];
          bp1= tram[addr_pat|1];
        }
      end= p[1]; begin= end - 8;
      if ( end == 0 ) continue;
      end= MIN ( end, 160 );
      if ( ATTR&0x10 )
        {
          pal= &(_cpal.ob.v[1][0]);
          mpal= &(_mpal.ob1[0]);
        }
      else
        {
          pal= &(_cpal.ob.v[0][0]);
          mpal= &(_mpal.ob0[0]);
        }
      prio= (ATTR>>7);
      
      /* Renderitza. */
      for ( x= begin; x < 0; ++x ) { bp0<<= 1; bp1<<= 1; }
      for ( ; x < end; ++x )
        {
          color= ((bp0>>7)&0x01) | ((bp1>>6)&0x02);
          if ( color != 0 )
            {
              _render.line_obj[x]= pal[mpal[color]];
              _render.prio_obj[x]= prio;
            }
          bp0<<= 1; bp1<<= 1;
        }
      
    }
  
} /* end render_line_obj_mono */


static void
render_line_bg_color (void)
{
  
  int x, i, j, row, sel_bp, sel_bp_flip, sel_pal, color;
  const int *pal[2];
  GBCu8 NT, ATTR;
  GBCu16 addr, addr_row, addr_col, addr_pat, desp0, desp1,
    bp0, bp1, bpal, bprio;
  const GBCu8 *tram;
  
  
  /* Açò no pot passar en mode color. */
  /* if ( !_control.bg_enabled) {} */
  
  /* Calcula fila (i adreça base) del 'map tile'. */
  row= _render.lines + _pos.SCY;
  if ( row >= 256 ) row-= 256;
  addr_row= _control.bg_tile_map | ((row&0xF8)<<2);
  
  /* Selector de bitmap dins d'un patró. */
  sel_bp= (row&0x7)<<1;
  sel_bp_flip= (7<<1)-sel_bp;
  
  /* Columna inicial. */
  addr_col= (GBCu16) (_pos.SCX>>3);
  
  /* Desplaçaments. */
  desp0= 15 - (_pos.SCX&0x7);
  desp1= desp0 - 1;
  
  /* Renderitza. */
  RENDER_LINE_BG_COLOR_INIT;
  for ( x= i= 0; i < 20; ++i )
    {
      GET_NEXT_NT_COLOR;
      CALC_ADDR_PAT_COLOR;
      GET_NEXT_BP_COLOR;
      for ( j= 0; j < 8; ++j, ++x )
        {
          color= ((bp0>>desp0)&0x01) | ((bp1>>desp1)&0x02);
          _render.line_bg[x]= pal[(bpal>>desp0)&0x1][color];
          _render.prio_bg[x]= color==0 ? -1 : ((bprio>>desp0)&0x01);
          bp0<<= 1; bp1<<= 1;
          bpal<<= 1; bprio<<= 1;
        }
    }
  
} /* end render_line_bg_color */


static void
render_line_win_color (void)
{
  
  int x, i, row, sel_bp, sel_bp_flip, color;
  const int *pal;
  GBCu8 NT, ATTR, bp0, bp1;
  GBCu16 addr, addr_row, addr_col, addr_pat;
  const GBCu8 *tram;
  signed char prio;
  
  
  /* Si no està activa no fa res. */
  if ( !_control.win_enabled) return;
  
  /* Calcula fila (i adreça base) del 'map tile'. */
  row= _render.lines - _pos.WY;
  if ( row < 0 ) return;
  addr_row= _control.win_tile_map | ((row&0xF8)<<2);
  
  /* Selector de bitmap dins d'un patró. */
  sel_bp= (row&0x7)<<1;
  sel_bp_flip= (7<<1)-sel_bp;
  
  /* Columna inicial. */
  addr_col= 0x0000;
  
  /* Renderitza primer tile (Pot apareixer fora de la pantalla). */
  x= _pos.WX-7;
  GET_NEXT_NT_COLOR_WIN;
  CALC_ADDR_PAT_COLOR_WIN;
  GET_NEXT_BP_COLOR_WIN;
  for ( i= 0; x < 0; ++x, ++i ) { bp0<<= 1; bp1<<= 1; }
  for ( ; i < 8 && x < 160; ++x, ++i )
    {
      color= ((bp0>>7)&0x01) | ((bp1>>6)&0x02);
      _render.line_bg[x]= pal[color];
      _render.prio_bg[x]= color==0 ? -1 : prio;
      bp0<<= 1; bp1<<= 1;
    }
  if ( x == 160 ) return;
  
  /* Renderitza resta. */
  do {
    GET_NEXT_NT_COLOR_WIN;
    CALC_ADDR_PAT_COLOR_WIN;
    GET_NEXT_BP_COLOR_WIN;
    for ( i= 0; i < 8 && x < 160; ++x, ++i )
      {
        color= ((bp0>>7)&0x01) | ((bp1>>6)&0x02);
        _render.line_bg[x]= pal[color];
        _render.prio_bg[x]= color==0 ? -1 : prio;
        bp0<<= 1; bp1<<= 1;
      }
  } while ( x < 160 );
  
} /* end render_line_win_color */


static void
render_line_obj_color (void)
{
  
  /* NOTA: Com no es diu res en ninguna part de la documentació sobre
     els timings, i com la coordinada vertical està desplaçada 16
     píxels (cosa que hem despista) faré l'avaluació i renderitza al
     mateix temps. */
  
  struct
  {
    const GBCu8 *obj;
    int          row;
  } buffer[NMAX_SPRITES];
  int n, N, row, max_row, x, begin, end, color;
  const GBCu8 *p, *tram;
  GBCu8 NT, ATTR, bp0, bp1;
  GBCu16 addr_pat;
  const int *pal;
  signed char prio;
  
  
  /* Si no està activat. */
  if ( !_control.obj_enabled )
    {
      clear_line_obj ();
      return;
    }
  
  /* AVALUACIÓ. */
  /* ATENCIÓ!!!! En mode B/N la prioritat no es basa en l'ordre. */
  max_row= _control.obj_size16 ? 16 : 8;
  N= 0;
  for ( n= 0, p= &(_oam[0]); n < 40; ++n, p+= 4 )
    {
      row= _render.lines + 16 - *p;
      if ( row >= 0 && row < max_row )
        {
          buffer[N].obj= p;
          buffer[N].row= row;
          if ( ++N == NMAX_SPRITES ) break;
        }
    }
  
  /* PINTA. */
  clear_line_obj ();
  for ( n= N-1; n >= 0; --n )
    {
      
      p= buffer[n].obj;
      row= buffer[n].row;
      
      /* NT, ATTR i tram. */
      NT= p[2];
      ATTR= p[3];
      tram= &(_vram[(ATTR&0x08)>>3][0]);
      
      /* Calcula addr_pat. */
      if ( _control.obj_size16 ) NT&= 0xFE;
      if ( ATTR&0x40 /* Y-FLIP */)
        row= max_row - row - 1;
      addr_pat= (((GBCu16) NT)<<4) | (row<<1);
      
      /* Prepara renderitzat. */
      if ( ATTR&0x20 /* X-FLIP */ )
        {
          bp0= reverse_byte ( tram[addr_pat] );
          bp1= reverse_byte ( tram[addr_pat|1] );
        }
      else
        {
          bp0= tram[addr_pat];
          bp1= tram[addr_pat|1];
        }
      end= p[1]; begin= end - 8;
      if ( end == 0 ) continue;
      end= MIN ( end, 160 );
      pal= &(_cpal.ob.v[ATTR&0x7][0]);
      prio= (ATTR>>7);
      
      /* Renderitza. */
      for ( x= begin; x < 0; ++x ) { bp0<<= 1; bp1<<= 1; }
      for ( ; x < end; ++x )
        {
          color= ((bp0>>7)&0x01) | ((bp1>>6)&0x02);
          if ( color != 0 )
            {
              _render.line_obj[x]= pal[color];
              _render.prio_obj[x]= prio;
            }
          bp0<<= 1; bp1<<= 1;
        }
      
    }
  
} /* end render_line_obj_color */


static void
render_line (void)
{
  
  int x, color_obj, bgprio;
  
  
  if ( !_control.enabled )
    for ( x= 0; x < 160; ++x ) *(_render.p++)= 0x7FFF;
  else
    {
      if ( _cgb_mode )
        {
          render_line_bg_color ();
          render_line_win_color ();
          render_line_obj_color ();
        }
      else
        {
          render_line_bg_mono ();
          render_line_win_mono ();
          render_line_obj_mono ();
        }
      for ( x= 0; x < 160; ++x )
        {
          color_obj= _render.line_obj[x];
          bgprio= _render.prio_bg[x];
          *(_render.p++)=
            color_obj != -1 && (bgprio == -1 ||
        			_control.obj_has_prio ||
        			(!bgprio && !_render.prio_obj[x])) ?
            color_obj : _render.line_bg[x];
        }
    }
  ++_render.lines;
  
} /* end render_line */


static void
render_lines (
              const int lines
              )
{
  
  int i;
  
  
  for ( i= 0; i < lines; ++i )
    render_line ();
  
} /* end render_lines */


static void
run (
     const int Yb,
     const int Xb,
     const int Ye,
     const int Xe
     )
{
  
  int lines;
  
  
  if ( Yb < 144 )
    {
      if ( Ye < 144 )
        {
          lines= Ye - Yb + (Xe>=CICLESTOM0) - (Xb>=CICLESTOM0);
          render_lines ( lines );
        }
      else
        {
          lines= 144 - Yb - (Xb>=CICLESTOM0);
          render_lines ( lines );
        }
    }
  if ( _render.lines == 144 )
    {
      _update_screen ( _render.fb, _udata );
      _render.p= &(_render.fb[0]);
      _render.lines= 0;
    }
  
} /* end run */


static void
clock (void)
{
  
  int newY, newX;
  
  
  if ( _stop || !_control.enabled )
    {
      _timing.cc= 0;
      return;
    }
  
  /* Actualitza comptadors. */
  _timing.ccto0Int-= _timing.cc;
  if ( _timing.ccto0Int <= 0 && _dma.active )
    {
      _timing.cc+= 8;
      _timing.extracc+= 8;
      _dma.dst&= 0x1FFF;
      vram_dma_hblank_block ();
      if ( --_dma.length == 0xFF )
        {
          _dma.length= 0x7F;
          _dma.active= GBC_FALSE;
        }
    }
  _timing.cctoVBInt-= _timing.cc;
  _timing.cctoCInt-= _timing.cc;
  _timing.ccto2Int-= _timing.cc;
  
  /* Calcula nous valors i executa. */
  newY= _pos.LY + _timing.cc/CICLESPERLINE;
  newX= _pos.LX + _timing.cc%CICLESPERLINE;
  if ( newX >= CICLESPERLINE ) { ++newY; newX-= CICLESPERLINE; }
  _timing.cc= 0;
  while ( newY >= 154 )
    {
      run ( _pos.LY, _pos.LX, 154, 0 );
      newY-= 154;
      _pos.LY= _pos.LX= 0;
    }
  run ( _pos.LY, _pos.LX, newY, newX );
  _pos.LY= newY;
  _pos.LX= newX;
  
  /* Actualiza el mode. */
  if ( newY > 143 )             _status.mode= 1;
  else if ( newX > CICLESTOM0 ) _status.mode= 0;
  else if ( newX > CICLESTOM3 ) _status.mode= 3;
  else                          _status.mode= 2;
  
  /* Recalcula els comptadors que tenen que ser recalculats i si és el
     cas demana interrupció. */
  if ( _timing.cctoVBInt <= 0 )
    {
      GBC_cpu_request_vblank_int ();
      if ( _status.int1_enabled )
        GBC_cpu_request_lcdstat_int ();
      if ( newY < 144 )
        _timing.cctoVBInt= (144-newY)*CICLESPERLINE - newX;
      else
        _timing.cctoVBInt= (144+154-newY)*CICLESPERLINE - newX;
    }
  if ( _timing.cctoCInt <= 0 )
    {
      if ( _status.intC_enabled && _pos.LYC < 154 )
        GBC_cpu_request_lcdstat_int ();
      update_cctoCInt ();
    }
  if ( _timing.ccto2Int <= 0 )
    {
      if ( _status.int2_enabled )
        GBC_cpu_request_lcdstat_int ();
      /* S'entra en el mode 2 al principi de cada línia que no és del
         VBLANK. Per tant si ja estic en la última línia visible o més
         allà, aleshores són els cicles que falten per a la línia
         0. */
      if ( newY < 143 )
        _timing.ccto2Int= CICLESPERLINE - newX;
      else
        _timing.ccto2Int= (154-newY)*CICLESPERLINE - newX;
    }
  if ( _timing.ccto0Int <= 0 )
    {
      if ( _status.int0_enabled )
        GBC_cpu_request_lcdstat_int ();
      if ( newY < 143 || (newY == 143 && newX < CICLESTOM0) )
        _timing.ccto0Int=
          (newX<CICLESTOM0)?CICLESTOM0-newX:CICLESPERLINE+CICLESTOM0-newX;
      else
        _timing.ccto0Int= (154-newY)*CICLESPERLINE+CICLESTOM0-newX;
    }
  
} /* end clock */


static GBCu8
mpal_get (
          const GBCu8 pal[4]
          )
{
  
  clock ();
  
  return pal[0] | (pal[1]<<2) | (pal[2]<<4) | (pal[3]<<6);
  
} /* end mpal_get */


static void
mpal_set (
          const GBCu8 data,
          GBCu8       pal[4]
          )
{
  
  clock ();
  pal[0]= data&0x3;
  pal[1]= (data>>2)&0x3;
  pal[2]= (data>>4)&0x3;
  pal[3]= data>>6;
  
} /* end mpal_set */


static void
init_cpal (
           cpal_t *pal
           )
{
  
  int *p, i;
  
  
  for ( p= &(pal->v[0][0]), i= 0; i < 32; ++i )
    p[i]= 0x7FFF;
  pal->p= 0;
  pal->c= 0;
  pal->high= 0;
  pal->auto_increment= GBC_FALSE;
  
} /* end init_cpal */


static void
cpal_index (
            const GBCu8  data,
            cpal_t      *pal
            )
{
  
  if ( !_cgb_mode && _pal_lock ) return;
  clock ();
  pal->auto_increment= ((data&0x80)!=0);
  pal->high= (data&0x1);
  pal->c= (data>>1)&0x3;
  pal->p= (data>>3)&0x7;
  
} /* end cpal_index */


static void
cpal_write_data (
        	 const GBCu8  data,
        	 cpal_t      *pal
        	 )
{
  
  if ( !_cgb_mode && _pal_lock ) return;
  clock ();
  if ( pal->high )
    {
      pal->v[pal->p][pal->c]&= 0xFF;
      pal->v[pal->p][pal->c]|= ((GBCu16) (data&0x7F))<<8;
    }
  else
    {
      pal->v[pal->p][pal->c]&= 0x7F00;
      pal->v[pal->p][pal->c]|= data;
    }
  if ( pal->auto_increment )
    {
      if ( (pal->high^= 1) == 0 )
        if ( ++pal->c == 4 )
          {
            pal->c= 0;
            if ( ++pal->p == 8 ) pal->p= 0;
          }
    }
  
} /* end cpal_write_data */


static GBCu8
cpal_read_data (
        	const cpal_t *pal
        	)
{
  
  if ( !_cgb_mode && _pal_lock ) return 0xFF;
  clock ();
  /*if ( _status.mode == 3 ) return 0xFF;*/
  if ( pal->high ) return (GBCu8) ((pal->v[pal->p][pal->c]&0x7F00)>>8);
  else             return (GBCu8) (pal->v[pal->p][pal->c]&0xFF);
  
} /* end cpal_read_data */


static void
clear_cc_num_int (void)
{
  
  _timing.cctoVBInt= 144*CICLESPERLINE;
  _timing.ccto2Int= 0;
  _timing.ccto0Int= CICLESTOM0;
  
} /* end clear_cc_num_int */




/**********************/
/* FUNCIONS PÚBLIQUES */
/**********************/

int
GBC_lcd_clock (
               const int cc
               )
{
  
  int ret;
  
  
  _timing.cc+= cc;
  if ( _timing.cc >= _timing.cctoVBInt ||
       (_timing.cc >= _timing.cctoCInt && _status.intC_enabled) ||
       (_timing.cc >= _timing.ccto0Int &&
        (_status.int0_enabled || _dma.active )) ||
       (_timing.cc >= _timing.ccto2Int && _status.int2_enabled) )
    clock ();
  ret= _timing.extracc;
  _timing.extracc= 0;
  
  return ret;
  
} /* end GBC_lcd_clock */


GBCu8
GBC_lcd_control_read (void)
{
  return _control.data;
} /* end GBC_lcd_control_read */


void
GBC_lcd_control_write (
        	       const GBCu8 data
        	       )
{
  
  GBC_Bool aux;
  
  
  clock ();
  
  _control.data= data;
  aux= _control.enabled;
  _control.enabled= ((data&0x80)!=0);
  if ( aux != _control.enabled && !_control.enabled )
    {
      /*if ( _status.mode != 1 )
        _warning ( _udata, "El LCD s'ha desactivat fora del període V-Blank" );
      */
      _timing.cc= 0;
      clear_cc_num_int ();
      _pos.LY= _pos.LX= 0;
      update_cctoCInt ();
      _render.p= &(_render.fb[0]);
      _render.lines= 0;
      _status.mode= 0;
      /* ACÍ PUC GENERAR UNA PANTALLA EN NEGRE. */
    }
  _control.win_tile_map= (data&0x40) ? 0x1C00 : 0x1800;
  _control.b5= _control.win_enabled= ((data&0x20)!=0);
  _control.bgwin_tile_data= ((data&0x10)!=0);
  _control.bg_tile_map= (data&0x08) ? 0x1C00 : 0x1800;
  _control.obj_size16= ((data&0x04)!=0);
  _control.obj_enabled= ((data&0x02)!=0);
  if ( _cgb_mode ) _control.obj_has_prio= ((data&0x1)==0);
  else
    {
      if ( data&0x01 ) _control.bg_enabled= GBC_TRUE;
      else
        {
          _control.bg_enabled= GBC_FALSE;
          _control.win_enabled= GBC_FALSE; /* Sobreescriu el bit 5. */
        }
    }
  
} /* end GBC_lcd_control_write */


void
GBC_lcd_cpal_bg_index (
        	       const GBCu8 data
        	       )
{
  cpal_index ( data, &_cpal.bg );
} /* end GBC_lcd_cpal_bg_index */


void
GBC_lcd_cpal_ob_index (
        	       const GBCu8 data
        	       )
{
  cpal_index ( data, &_cpal.ob );
} /* end GBC_lcd_cpal_ob_index */


GBCu8
GBC_lcd_cpal_bg_read_data (void)
{
  return cpal_read_data ( &_cpal.bg );
} /* end GBC_lcd_cpal_bg_read_data */


GBCu8
GBC_lcd_cpal_ob_read_data (void)
{
  return cpal_read_data ( &_cpal.ob );
} /* end GBC_lcd_cpal_ob_read_data */


void
GBC_lcd_cpal_bg_write_data (
        		    const GBCu8 data
        		    )
{
  cpal_write_data ( data, &_cpal.bg );
} /* end GBC_lcd_cpal_bg_write_data */


void
GBC_lcd_cpal_ob_write_data (
        		    const GBCu8 data
        		    )
{
  cpal_write_data ( data, &_cpal.ob );
} /* end GBC_lcd_cpal_ob_write_data */


void
GBC_lcd_get_cpal (
        	  int bg[8][4],
        	  int ob[8][4]
        	  )
{
  
  int i, j;
  
  
  for ( i= 0; i < 8; ++i )
    for ( j= 0; j < 4; ++j )
      {
        bg[i][j]= _cpal.bg.v[i][j];
        ob[i][j]= _cpal.ob.v[i][j];
      }
  
} /* end GBC_lcd_get_cpal */


const GBCu8 *
GBC_lcd_get_vram (void)
{
  return &(_vram[0][0]);
} /* end GBC_lcd_get_vram */


GBCu8
GBC_lcd_get_vram_bank (void)
{
  
  if ( !_cgb_mode ) return 0xFF;
  return _vram_selected;
  
} /* end GBC_lcd_get_vram_bank */


void
GBC_lcd_init (
              GBC_UpdateScreen *update_screen,
              GBC_Warning      *warning,
              void             *udata
              )
{
  
  _update_screen= update_screen;
  _warning= warning;
  _udata= udata;
  
  GBC_lcd_init_state ();
  
} /* end GBC_lcd_init */


void
GBC_lcd_init_state (void)
{
  
  /* Mode. */
  _cgb_mode= GBC_TRUE;
  _pal_lock= GBC_TRUE;
  
  /* Registre de control. */
  _control.data= 0x00;
  _control.enabled= GBC_FALSE;
  _control.win_tile_map= 0x1800;
  _control.b5= _control.win_enabled= GBC_FALSE;
  _control.bgwin_tile_data= GBC_FALSE;
  _control.bg_tile_map= 0x1800;
  _control.obj_size16= GBC_FALSE;
  _control.obj_enabled= GBC_FALSE;
  /*if ( _cgb_mode )
    {*/
      _control.bg_enabled= GBC_TRUE;
      _control.obj_has_prio= GBC_TRUE;
      /*}
  else
    {
      _control.bg_enabled= GBC_FALSE;
      _control.obj_has_prio= GBC_FALSE;
      }*/
  
  /* Registre d'estat. */
  _status.hdata= 0x00;
  _status.intC_enabled= GBC_FALSE;
  _status.int2_enabled= GBC_FALSE;
  _status.int1_enabled= GBC_FALSE;
  _status.int0_enabled= GBC_FALSE;
  _status.mode= 0; /* Dispositiu desactivat!!!!. */
  
  /* Tming. */
  _timing.cc= 0;
  clear_cc_num_int ();
  _timing.cctoCInt= 0;
  
  /* Registres de posició. */
  _pos.SCY= _pos.SCX= 0;
  _pos.LY= _pos.LX= 0;
  _pos.LYC= 0;
  _pos.WY= 0;
  _pos.WX= 7;
  
  /* Memòria. */
  memset ( _vram[0], 0, BANK_SIZE*2 );
  _cvram= &(_vram[0][0]);
  _vram_selected= 0x00;
  
  /* OAM. */
  memset ( _oam, 0, OAM_SIZE );
  
  /* DMA. */
  _dma.src= 0x0000;
  _dma.dst= 0x8000;
  _dma.length= 0x00;
  _dma.active= GBC_FALSE;
  
  /* Paleta monocroma. */
  memset ( _mpal.bg, 3, 4 );
  memset ( _mpal.ob0, 3, 4 );
  memset ( _mpal.ob1, 3, 4 );
  
  /* Paleta de color. */
  init_cpal ( &_cpal.bg );
  init_cpal ( &_cpal.ob );
  
  /* Renderitzat. */
  memset ( _render.fb, 0, 23040*sizeof(int) );
  memset ( _render.line_bg, 0, 160*sizeof(int) );
  memset ( _render.line_obj, 0, 160*sizeof(int) );
  memset ( _render.prio_bg, 0, 160 );
  memset ( _render.prio_obj, 0, 160 );
  _render.p= &(_render.fb[0]);
  _render.lines= 0;
  
  /* Estat parat. */
  _stop= GBC_FALSE;
  
} /* end GBC_lcd_init_state */


void
GBC_lcd_init_gray_pal (void)
{
  
  /* BG/WIN. */
  _cpal.bg.v[0][0]= 32767;
  _cpal.bg.v[0][1]= 21140;
  _cpal.bg.v[0][2]= 10570;
  _cpal.bg.v[0][3]= 0;
  
  /* OBJ0. */
  _cpal.ob.v[0][0]= 32767;
  _cpal.ob.v[0][1]= 21140;
  _cpal.ob.v[0][2]= 10570;
  _cpal.ob.v[0][3]= 0;
  
  /* OBJ1. */
  _cpal.ob.v[1][0]= 32767;
  _cpal.ob.v[1][1]= 21140;
  _cpal.ob.v[1][2]= 10570;
  _cpal.ob.v[1][3]= 0;
  
} /* end GBC_lcd_init_gray_pal */


GBCu8
GBC_lcd_ly_read (void)
{
  
  clock ();
  return (GBCu8) ((GBCs8) _pos.LY);
  
} /* end GBC_lcd_ly_read */


GBCu8
GBC_lcd_lyc_read (void)
{
  return (GBCu8) ((GBCs8) _pos.LYC);
} /* end GBC_lcd_lyc_read */


void
GBC_lcd_lyc_write (
        	   const GBCu8 data
        	   )
{
  
  clock ();
  _pos.LYC= (int) data;
  update_cctoCInt ();
  
} /* end GBC_lcd_lyc_write */


GBCu8
GBC_lcd_mpal_bg_get (void)
{
  return mpal_get ( _mpal.bg );
} /* end GBC_lcd_mpal_bg_get */


void
GBC_lcd_mpal_bg_set (
        	     const GBCu8 data
        	     )
{
  mpal_set ( data, _mpal.bg );
} /* end GBC_lcd_mpal_bg_set */


GBCu8
GBC_lcd_mpal_ob0_get (void)
{
  return mpal_get ( _mpal.ob0 );
} /* end GBC_lcd_mpal_ob0_get */


void
GBC_lcd_mpal_ob0_set (
        	      const GBCu8 data
        	      )
{
  mpal_set ( data, _mpal.ob0 );
} /* end GBC_lcd_mpal_ob0_set */


GBCu8
GBC_lcd_mpal_ob1_get (void)
{
  return mpal_get ( _mpal.ob1 );
} /* end GBC_lcd_mpal_ob1_set */


void
GBC_lcd_mpal_ob1_set (
        	      const GBCu8 data
        	      )
{
  mpal_set ( data, _mpal.ob1 );
} /* end GBC_lcd_mpal_ob1_set */


void
GBC_lcd_oam_dma (
        	 const GBCu8 data
        	 )
{
  
  GBCu16 addr, i;
  
  
  clock ();
  for ( i= 0, addr= ((GBCu16)data)<<8; i < 0xA0; ++i, ++addr )
    _oam[i]= GBC_mem_read ( addr );
  
} /* end GBC_lcd_oam_dma */


GBCu8
GBC_lcd_oam_read (
        	  const GBCu16 addr
        	  )
{
  
  clock ();
  /*if ( _status.mode&0x2 ) return 0xFF;*/
  
  return _oam[addr];
  
} /* end GBC_lcd_oam_read */


void
GBC_lcd_oam_write (
        	   const GBCu16 addr,
        	   const GBCu8  data
        	   )
{
  
  clock ();
  /*if ( _status.mode&0x2 ) return;*/
  _oam[addr]= data;
  
} /* end GBC_lcd_oam_write */


void
GBC_lcd_pal_lock (
        	  const GBCu8 data
        	  )
{
  
  clock ();
  _pal_lock= ((data&0x1)==0);
  
} /* end GBC_lcd_pal_lock */


GBCu8
GBC_lcd_scx_read (void)
{
  return _pos.SCX;
} /* end GBC_lcd_scx_read */


void
GBC_lcd_scx_write (
        	   const GBCu8 data
        	   )
{
  
  clock ();
  _pos.SCX= data;
  
} /* end GBC_lcd_scx_write */


GBCu8
GBC_lcd_scy_read (void)
{
  return _pos.SCY;
} /* end GBC_lcd_scy_read */


void
GBC_lcd_scy_write (
        	   const GBCu8 data
        	   )
{
  
  clock ();
  _pos.SCY= data;
  
} /* end GBC_lcd_scy_write */


void
GBC_lcd_select_vram_bank (
        		  const GBCu8 data
        		  )
{
  
  if ( !_cgb_mode ) return;
  clock ();
  _vram_selected= data;
  _cvram= &(_vram[_vram_selected&0x1][0]);
  
} /* end GBC_lcd_select_vram_bank */


void
GBC_lcd_set_cgb_mode (
        	      const GBC_Bool enabled
        	      )
{
  
  clock ();
  
  /* CGB -> DMG */
  if ( _cgb_mode && !enabled )
    {
      if ( _control.obj_has_prio /*b1==0*/ )
        _control.bg_enabled= GBC_TRUE;
      else
        {
          _control.bg_enabled= GBC_FALSE;
          _control.win_enabled= GBC_FALSE;
        }
    }
  /* DMG -> CGB */
  else if ( !_cgb_mode && enabled )
    {
      _control.win_enabled= _control.b5;
      _control.obj_has_prio= !_control.bg_enabled;
    }
  _cgb_mode= enabled;
  
} /* end GBC_lcd_set_cgb_mode */


GBCu8
GBC_lcd_status_read (void)
{
  
  clock ();
  
  return _status.hdata | ((_pos.LY==_pos.LYC) ? CFLAG : 0x00) | _status.mode;
  
} /* end GBC_lcd_status_read */


void
GBC_lcd_status_write (
        	      const GBCu8 data
        	      )
{
  
  clock ();
  
  _status.hdata= data&0xF8;
  _status.intC_enabled= ((data&0x40)!=0);
  _status.int2_enabled= ((data&0x20)!=0);
  _status.int1_enabled= ((data&0x10)!=0);
  _status.int0_enabled= ((data&0x08)!=0);
  
} /* end GBC_lcd_status_write */


void
GBC_lcd_stop (
              const GBC_Bool state
              )
{
  
  /* Processa els clocks pendents i para. Si ja estava parat clock()
     buidarà els cicles acumulats en aquest periode. */
  clock ();
  _stop= state;
  if ( state )
    {
      memset ( _render.fb, 0, 160*144*sizeof(int) );
      _update_screen ( _render.fb, _udata );
    }
  
} /* end GBC_lcd_stop */


void
GBC_lcd_vram_dma_dst_high (
        		   const GBCu8 data
        		   )
{
  if ( !_cgb_mode ) return;
  clock ();
  _dma.dst&= 0xFF;
  _dma.dst|= ((GBCu16) (data&0x1F))<<8;
  
} /* end GBC_lcd_vram_dma_dst_high */


void
GBC_lcd_vram_dma_dst_low (
        		  const GBCu8 data
        		  )
{
  
  if ( !_cgb_mode ) return;
  clock ();
  _dma.dst&= 0xFF00;
  _dma.dst|= data&0xF0;
  
} /* end GBC_lcd_vram_dma_dst_low */


void
GBC_lcd_vram_dma_init (
        	       const GBCu8 data
        	       )
{
  
  int cc;
  
  
  if ( !_cgb_mode ) return;
  clock ();
  
  /* NOTA: Açò no està del tot clar. */
  /* Si hi ha una transferència activa sols es pot desactivar. */
  if ( _dma.active )
    {
      if ( !(data&0x80) ) _dma.active= GBC_FALSE;
    }
  else
    {
      _dma.length= data&0x7F;
      if ( data&0x80 ) /* H-Blank. */
        _dma.active= GBC_TRUE;
      else /* General Purpose. */
        {
          cc= (_dma.length+1)*8;
          _timing.cc+= cc;
          _timing.extracc+= cc;
          _dma.dst&= 0x1FFF;
          for ( ; _dma.length != 0xFF; --_dma.length )
            {
              vram_dma_hblank_block ();
              _dma.dst&= 0x1FFF;
            }
          _dma.length= 0x7F;
          clock ();
        }
    }
  
} /* end GBC_lcd_vram_dma_init */


void
GBC_lcd_vram_dma_src_high (
        		   const GBCu8 data
        		   )
{
  if ( !_cgb_mode ) return;
  clock ();
  _dma.src&= 0xFF;
  _dma.src|= ((GBCu16) data)<<8;
  
} /* end GBC_lcd_vram_dma_src_high */


void
GBC_lcd_vram_dma_src_low (
        		  const GBCu8 data
        		  )
{
  
  if ( !_cgb_mode ) return;
  clock ();
  _dma.src&= 0xFF00;
  _dma.src|= data&0xF0;
  
} /* end GBC_lcd_vram_dma_src_low */


GBCu8
GBC_lcd_vram_dma_status (void)
{
  
  if ( !_cgb_mode ) return 0xFF;
  clock ();
  
  return _dma.length | (_dma.active ? 0x00 : 0x80);
  
} /* end GBC_lcd_vram_dma_status */


GBCu8
GBC_lcd_vram_read (
        	   const GBCu16 addr
        	   )
{
  
  clock ();
  /*if ( _status.mode == 3 ) return 0xFF;*/
  
  return _cvram[addr];
  
} /* end GBC_lcd_vram_read */


void
GBC_lcd_vram_write (
        	    const GBCu16 addr,
        	    const GBCu8  data
        	    )
{
  
  clock ();
  /*if ( _status.mode == 3 ) return;*/
  _cvram[addr]= data;
  
} /* end GBC_lcd_vram_write */


GBCu8
GBC_lcd_wx_read (void)
{
  return _pos.WX;
} /* end GBC_lcd_wx_read */


void
GBC_lcd_wx_write (
        	  const GBCu8 data
        	  )
{
  
  clock ();
  _pos.WX= data;
  
} /* end GBC_lcd_wx_write */


GBCu8
GBC_lcd_wy_read (void)
{
  return _pos.WY;
} /* end GBC_lcd_wy_read */


void
GBC_lcd_wy_write (
        	  const GBCu8 data
        	  )
{
  
  clock ();
  _pos.WY= data;
  
} /* end GBC_lcd_wy_write */


int
GBC_lcd_save_state (
        	    FILE *f
        	    )
{

  int *aux;
  size_t ret;
  
  
  SAVE ( _cgb_mode );
  SAVE ( _pal_lock );
  SAVE ( _control );
  SAVE ( _status );
  SAVE ( _timing );
  SAVE ( _pos );
  SAVE ( _vram );
  /* _cvram es pot obtindre de _vram i _vram_selected. */
  SAVE ( _vram_selected );
  SAVE ( _oam );
  SAVE ( _dma );
  SAVE ( _mpal );
  SAVE ( _cpal );
  aux= _render.p;
  _render.p= (void *) (_render.p-&(_render.fb[0]));
  ret= fwrite ( &_render, sizeof(_render), 1, f );
  _render.p= aux;
  if ( ret != 1 ) return -1;
  SAVE ( _stop );

  return 0;
  
} /* end GBC_lcd_save_state */


int
GBC_lcd_load_state (
        	    FILE *f
        	    )
{

  int i, j;

  
  LOAD ( _cgb_mode );
  LOAD ( _pal_lock );
  LOAD ( _control );
  CHECK ( _control.win_tile_map==0x1C00 || _control.win_tile_map==0x1800 );
  LOAD ( _status );
  LOAD ( _timing );
  CHECK ( _timing.cc >= 0 && _timing.extracc >= 0 );
  LOAD ( _pos );
  CHECK ( _pos.LY >= 0 && _pos.LY < 154 );
  CHECK ( _pos.LX >= 0 && _pos.LX < CICLESPERLINE );
  LOAD ( _vram );
  LOAD ( _vram_selected );
  _cvram= &(_vram[_vram_selected&0x1][0]);
  LOAD ( _oam );
  LOAD ( _dma );
  CHECK ( (_dma.src&0xFFF0) == _dma.src );
  CHECK ( (_dma.dst&0xFFF0) == _dma.dst );
  LOAD ( _mpal );
  for ( i= 0; i < 4; ++i )
    {
      CHECK ( (_mpal.bg[i]&0x3) == _mpal.bg[i] );
      CHECK ( (_mpal.ob0[i]&0x3) == _mpal.ob0[i] );
      CHECK ( (_mpal.ob1[i]&0x3) == _mpal.ob1[i] );
    }
  LOAD ( _cpal );
  for ( i= 0; i < 8; ++i )
    for ( j= 0; j < 4; ++j )
      {
        CHECK ( (_cpal.bg.v[i][j]&0x7FFF) == _cpal.bg.v[i][j] );
        CHECK ( (_cpal.ob.v[i][j]&0x7FFF) == _cpal.ob.v[i][j] );
      }
  CHECK ( _cpal.bg.p >= 0 && _cpal.bg.p < 8 );
  CHECK ( _cpal.bg.c >= 0 && _cpal.bg.c < 4 );
  CHECK ( _cpal.bg.high == 0 || _cpal.bg.high == 1 );
  CHECK ( _cpal.ob.p >= 0 && _cpal.ob.p < 8 );
  CHECK ( _cpal.ob.c >= 0 && _cpal.ob.c < 4 );
  CHECK ( _cpal.ob.high == 0 || _cpal.ob.high == 1 );

  /* En render es fa un tractament especial del punter.  */
  LOAD ( _render );
  _render.p= &(_render.fb[0]) + (ptrdiff_t) _render.p;
  CHECK ( ((&(_render.fb[0])) - _render.p) <= 160*144 );
  CHECK ( (&(_render.fb[0]) + _render.lines*160) == _render.p );
  /* NOTA: els buffer de _render s'omplin cada vegada per a dibuixar
     una línia. */
  for ( i= 0; i < 160*144; ++i )
    if ( _render.fb[i] < 0 || _render.fb[i] > 32767 )
      return -1;
  
  LOAD ( _stop );
  
  return 0;
  
} /* end GBC_lcd_load_state */
