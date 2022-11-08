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
 * along with Foobar.  If not, see <https://www.gnu.org/licenses/>.
 */
/*
 *  gbcmodule.c - Mòdul que implementa una GameBoy Color en Python.
 *
 */


#include <Python.h>
#include <SDL/SDL.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "GBC.h"




/**********/
/* MACROS */
/**********/

#undef RECORD_AUDIO

#define WIDTH 160
#define HEIGHT 144


#define CHECK_INITIALIZED                                               \
  do {                                                                  \
    if ( !_initialized )                                                \
      {                                                                 \
        PyErr_SetString ( GBCError, "Module must be initialized" );        \
        return NULL;                                                    \
      }                                                                 \
  } while(0)


#define CHECK_ROM                                                       \
  do {                                                                  \
    if ( _rom.banks ==NULL )                                            \
      {                                                                 \
        PyErr_SetString ( GBCError, "There is no ROM inserted"           \
                          " into the simulator" );                      \
        return NULL;                                                    \
      }                                                                 \
  } while(0)

#define NBUFF 4




/*********/
/* TIPUS */
/*********/

enum { FALSE= 0, TRUE };

typedef struct
{
  
  double       *v;
  volatile int  full;
  
} buffer_t;




/*********/
/* ESTAT */
/*********/

#ifdef RECORD_AUDIO
static FILE *_audio_out;
#endif

/* Error. */
static PyObject *GBCError;

/* Inicialitzat. */
static char _initialized;

/* BIOS. */
static struct
{
  GBCu8 data[0x900];
  int   active;
} _bios;

/* Rom. */
static GBC_Rom _rom;

/* Tracer. */
static struct
{
  PyObject *obj;
  int       has_cpu_step;
  int       has_mapper_changed;
  int       has_mem_access;
} _tracer;

/* Pantalla. */
static struct
{
  
  int          width;
  int          height;
  SDL_Surface *surface;
  GLuint       textureid;
  uint32_t     data[WIDTH*HEIGHT];
  
} _screen;

/* Paleta de colors. */
static uint32_t _palette[32768];

/* RAM externa. */
static struct
{
  
  GBCu8  *ram;
  size_t  size;
  
} _eram;

/* Control. */
static int _control;

/* Estat so. */
static struct
{
  
  buffer_t buffers[NBUFF];
  int      buff_in;
  int      buff_out;
  char     silence;
  int      pos;
  int      size;
  int      nsamples;
  double   ratio;
  double   pos2;
  
} _audio;




/*********************/
/* FUNCIONS PRIVADES */
/*********************/

static int
has_method (
            PyObject   *obj,
            const char *name
            )
{
  
  PyObject *aux;
  int ret;
  
  if ( !PyObject_HasAttrString ( obj, name ) ) return 0;
  aux= PyObject_GetAttrString ( obj, name );
  ret= PyMethod_Check ( aux );
  Py_DECREF ( aux );
  
  return ret;
  
} /* end has_method */


static void
init_GL (void)
{
  
  /* Configuració per a 2D. */
  glMatrixMode( GL_PROJECTION );
  glLoadIdentity();
  glViewport ( 0, 0, WIDTH, HEIGHT );
  glOrtho( 0, WIDTH, HEIGHT, 0, -1, 1 );
  glMatrixMode( GL_MODELVIEW );
  glLoadIdentity();
  glDisable ( GL_DEPTH_TEST );
  glDisable ( GL_LIGHTING );
  glDisable ( GL_BLEND );
  glDisable ( GL_DITHER );
  
  /* Crea textura. */
  glEnable ( GL_TEXTURE_RECTANGLE_NV );
  glGenTextures ( 1, &_screen.textureid );
  glBindTexture ( GL_TEXTURE_RECTANGLE_NV, _screen.textureid );
  glTexParameteri( GL_TEXTURE_RECTANGLE_NV, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
  
} /* end init_GL */


static void
init_palette (void)
{
  
  uint32_t color;
  double frac;
  int i;
  
  
  frac= 255.0/31.0;
  color= 0;
  for ( i= 0; i < 32768; ++i )
    {
      ((uint8_t *) &color)[0]= (uint8_t) ((i&0x1F)*frac+0.5);
      ((uint8_t *) &color)[1]= (uint8_t) (((i>>5)&0x1F)*frac+0.5);
      ((uint8_t *) &color)[2]= (uint8_t) (((i>>10)&0x1F)*frac+0.5);
      _palette[i]= color;
    }
  
} /* end init_palette */


static void
screen_update (void)
{
  
  glClear ( GL_COLOR_BUFFER_BIT );
  glTexImage2D( GL_TEXTURE_RECTANGLE_NV, 0, GL_RGBA,
                WIDTH, HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                _screen.data );
  glBegin ( GL_QUADS );
  glTexCoord2i ( 0, HEIGHT );
  glVertex2i ( 0, HEIGHT );
  glTexCoord2i ( WIDTH, HEIGHT );
  glVertex2i ( WIDTH, HEIGHT );
  glTexCoord2i ( WIDTH, 0 );
  glVertex2i ( WIDTH, 0 );
  glTexCoord2i ( 0, 0 );
  glVertex2i ( 0, 0 );
  glEnd ();
  
  SDL_GL_SwapBuffers ();
  
} /* end scree_update */


static void
screen_clear (void)
{
  
  memset ( _screen.data, 0, sizeof(uint32_t)*WIDTH*HEIGHT );
  screen_update ();
  
} /* end screen_clear */


static void
audio_callback (
                void  *userdata,
                Uint8 *stream,
                int    len
                )
{
  
  int i;
  const double *buffer;
  
  
  assert ( _audio.size == len );
  if ( _audio.buffers[_audio.buff_out].full )
    {
      buffer= _audio.buffers[_audio.buff_out].v;
      for ( i= 0; i < len; ++i )
        stream[i]= 127 + (Uint8) ((128*buffer[i]) + 0.5);
      _audio.buffers[_audio.buff_out].full= 0;
      _audio.buff_out= (_audio.buff_out+1)%NBUFF;
    }
  else
    for ( i= 0; i < len; ++i )
      stream[i]= _audio.silence;

#ifdef RECORD_AUDIO
  fwrite ( stream, 1, len, _audio_out );
#endif
  
} /* end audio_callback */


/* Torna 0 si tot ha anat bé. */
static const char *
init_audio (void)
{
  
  SDL_AudioSpec desired, obtained;
  int n;
  double *mem;
  
  
  /* Únic camp de l'estat que s'inicialitza abans. */
  _audio.buff_out= _audio.buff_in= 0;
  for ( n= 0; n < NBUFF; ++n ) _audio.buffers[n].full= 0;
  
  /* Inicialitza. */
  desired.freq= 44100;
  desired.format= AUDIO_U8;
  desired.channels= 2;
  desired.samples= 2048;
  desired.size= 4096;
  desired.callback= audio_callback;
  desired.userdata= NULL;
  if ( SDL_OpenAudio ( &desired, &obtained ) == -1 )
    return SDL_GetError ();
  if ( obtained.format != desired.format )
    {
      fprintf ( stderr, "Força format audio\n" );
      SDL_CloseAudio ();
      if ( SDL_OpenAudio ( &desired, NULL ) == -1 )
        return SDL_GetError ();
      obtained= desired;
    }
  
  /* Inicialitza estat. */
  mem= (double *) malloc ( sizeof(double)*obtained.size*NBUFF );
  for ( n= 0; n < NBUFF; ++n, mem+= obtained.size )
    _audio.buffers[n].v= (double *) mem;
  _audio.silence= (char) obtained.silence;
  _audio.pos= 0;
  _audio.size= obtained.size;
  if ( obtained.freq >= GBC_APU_SAMPLES_PER_SEC )
    {
      SDL_CloseAudio ();
      return "Freqüència massa gran";
    }
  _audio.ratio= GBC_APU_SAMPLES_PER_SEC / (double) obtained.freq;
  _audio.pos2= 0.0;
  
#ifdef RECORD_AUDIO
  _audio_out= fopen ( "audio_out.raw", "wb" );
  if ( _audio_out == NULL )
    {
      fprintf ( stderr, "No he pogut crear audio_out.raw" );
      exit ( EXIT_FAILURE );
    }
#endif

  return NULL;
  
} /* end init_audio */


static void
close_audio (void)
{
  
#ifdef RECORD_AUDIO
  fclose ( _audio_out );
#endif
  SDL_CloseAudio ();
  free ( _audio.buffers[0].v );
  
} /* end close_audio */




/************/
/* FRONTEND */
/************/

static void
warning (
         void       *udata,
         const char *format,
         ...
         )
{
  
  va_list ap;
  
  
  va_start ( ap, format );
  fprintf ( stderr, "Warning: " );
  vfprintf ( stderr, format, ap );
  putc ( '\n', stderr );
  va_end ( ap );
  
} /* end warning */


static GBCu8 *
get_external_ram (
        	  const size_t  nbytes,
        	  void         *udata
        	  )
{
  
  if ( _eram.ram == NULL )
    {
      _eram.ram= (GBCu8 *) calloc ( nbytes, 1 );
      if ( _eram.ram == NULL ) goto error;
      _eram.size= nbytes;
    }
  else if ( _eram.size < nbytes )
    {
      _eram.ram= (GBCu8 *) realloc ( _eram.ram, nbytes );
      if ( _eram.ram == NULL ) goto error;
      _eram.size= nbytes;
    }
  
  return _eram.ram;
  
 error:
  fprintf ( stderr, "GBC: Error FATA! No s'ha"
            " pogut reservar memòria!\n" );
  exit ( EXIT_FAILURE );
  
} /* end get_external_ram */


static void
update_screen (
               const int  fb[23040],
               void      *udata
               )
{
  
  int i;
  
  
  for ( i= 0; i < 23040; ++i )
    _screen.data[i]= _palette[fb[i]];
  screen_update ();
  
} /* end update_screen */


static void
check_signals (
               GBC_Bool *stop,
               GBC_Bool *button_pressed,
               GBC_Bool *direction_pressed,
               void     *udata
               )
{
  
  SDL_Event event;
  
  
  *stop= *button_pressed= *direction_pressed= GBC_FALSE;
  while ( SDL_PollEvent ( &event ) )
    switch ( event.type )
      {
      case SDL_ACTIVEEVENT:
        if ( event.active.state&SDL_APPINPUTFOCUS &&
             !event.active.gain )
          _control= 0x00;
        break;
      case SDL_VIDEOEXPOSE: screen_update (); break;
      case SDL_KEYDOWN:
        if ( event.key.keysym.mod&KMOD_CTRL )
          {
            switch ( event.key.keysym.sym )
              {
              case SDLK_q: *stop= GBC_TRUE; break;
              default: break;
              }
          }
        else
          {
            switch ( event.key.keysym.sym )
              {
              case SDLK_RETURN: _control|= GBC_SELECT; break;
              case SDLK_SPACE: _control|= GBC_START; break;
              case SDLK_w: _control|= GBC_UP; break;
              case SDLK_s: _control|= GBC_DOWN; break;
              case SDLK_a: _control|= GBC_LEFT; break;
              case SDLK_d: _control|= GBC_RIGHT; break;
              case SDLK_o: _control|= GBC_BUTTON_A; break;
              case SDLK_p: _control|= GBC_BUTTON_B; break;
              default: break;
              }
            *button_pressed= ((_control&0xF0)!=0);
            *direction_pressed= ((_control&0xF)!=0);
          }
        break;
      case SDL_KEYUP:
        switch ( event.key.keysym.sym )
          {
          case SDLK_RETURN: _control&= ~GBC_SELECT; break;
          case SDLK_SPACE: _control&= ~GBC_START; break;
          case SDLK_w: _control&= ~GBC_UP; break;
          case SDLK_s: _control&= ~GBC_DOWN; break;
          case SDLK_a: _control&= ~GBC_LEFT; break;
          case SDLK_d: _control&= ~GBC_RIGHT; break;
          case SDLK_o: _control&= ~GBC_BUTTON_A; break;
          case SDLK_p: _control&= ~GBC_BUTTON_B; break;
          default: break;
          }
        break;
      default: break;
      }
  
} /* end check_signals */


static int
check_buttons (
               void *udata
               )
{
  return _control;
} /* end check_buttons */


static void
play_sound (
            const double  left[GBC_APU_BUFFER_SIZE],
            const double  right[GBC_APU_BUFFER_SIZE],
            void         *udata
            )
{
  int nofull, j;
  double *buffer;
  
  
  for (;;)
    {
      
      while ( _audio.buffers[_audio.buff_in].full ) SDL_Delay ( 1 );
      buffer= _audio.buffers[_audio.buff_in].v;
      
      j= (int) (_audio.pos2 + 0.5);
      while ( (nofull= (_audio.pos != _audio.size)) &&
              j < GBC_APU_BUFFER_SIZE )
        {
          buffer[_audio.pos++]= left[j];
          buffer[_audio.pos++]= right[j];
          _audio.pos2+= _audio.ratio;
          j= (int) (_audio.pos2 + 0.5);
        }
      if ( !nofull )
        {
          _audio.pos= 0;
          _audio.buffers[_audio.buff_in].full= 1;
          _audio.buff_in= (_audio.buff_in+1)%NBUFF;
        }
      if ( j >= GBC_APU_BUFFER_SIZE )
        {
          _audio.pos2-= GBC_APU_BUFFER_SIZE;
          break;
        }
      
    }
  
} /* end play_sound */


static void
update_rumble (
               const int  level,
               void      *udata
               )
{
} /* end update_rumble */


static void
mem_access (
            const GBC_MemAccessType  type,
            const GBCu16             addr,
            const GBCu8              data,
            void                    *udata
            )
{
  
  PyObject *ret;
  
  
  if ( _tracer.obj == NULL ||
       !_tracer.has_mem_access ||
       PyErr_Occurred () != NULL ) return;
  ret= PyObject_CallMethod ( _tracer.obj, "mem_access",
                             "iHB", type, addr, data );
  Py_XDECREF ( ret );
  
} /* end mem_access */


static void
mapper_changed (
                void *udata
                )
{
  
  PyObject *ret;
  
  
  if ( _tracer.obj == NULL ||
       !_tracer.has_mapper_changed ||
       PyErr_Occurred () != NULL ) return;
  ret= PyObject_CallMethod ( _tracer.obj, "mapper_changed", "" );
  Py_XDECREF ( ret );
  
} /* end mapper_changed */


static void
cpu_step (
          const GBC_Step *step,
          const GBCu16    nextaddr,
          void           *udata
          )
{
  
  PyObject *ret;
  
  
  if ( _tracer.obj == NULL ||
       !_tracer.has_cpu_step ||
       PyErr_Occurred () != NULL ) return;
  switch ( step->type )
    {
    case GBC_STEP_INST:
      ret= PyObject_CallMethod ( _tracer.obj, "cpu_step",
                                 "iHiiiy#(BbH(bH))(BbH(bH))",
                                 GBC_STEP_INST,
                                 nextaddr,
                                 step->val.inst.id.name,
                                 step->val.inst.id.op1,
                                 step->val.inst.id.op2,
                                 step->val.inst.bytes,
                                 step->val.inst.nbytes,
                                 step->val.inst.e1.byte,
                                 step->val.inst.e1.desp,
                                 step->val.inst.e1.addr_word,
                                 step->val.inst.e1.branch.desp,
                                 step->val.inst.e1.branch.addr,
                                 step->val.inst.e2.byte,
                                 step->val.inst.e2.desp,
                                 step->val.inst.e2.addr_word,
                                 step->val.inst.e2.branch.desp,
                                 step->val.inst.e2.branch.addr );
      break;
    case GBC_STEP_VBINT:
      ret= PyObject_CallMethod ( _tracer.obj, "cpu_step", "iH",
                                 GBC_STEP_VBINT, nextaddr );
      break;
    case GBC_STEP_LSINT:
      ret= PyObject_CallMethod ( _tracer.obj, "cpu_step", "iH",
                                 GBC_STEP_LSINT, nextaddr );
      break;
    case GBC_STEP_TIINT:
      ret= PyObject_CallMethod ( _tracer.obj, "cpu_step", "iH",
                                 GBC_STEP_TIINT, nextaddr );
      break;
    case GBC_STEP_SEINT:
      ret= PyObject_CallMethod ( _tracer.obj, "cpu_step", "iH",
                                 GBC_STEP_SEINT, nextaddr );
      break;
    case GBC_STEP_JOINT:
      ret= PyObject_CallMethod ( _tracer.obj, "cpu_step", "iH",
                                 GBC_STEP_JOINT, nextaddr );
      break;
    default: ret= NULL;
    }
  Py_XDECREF ( ret );
  
} /* end cpu_step */




/******************/
/* FUNCIONS MÒDUL */
/******************/

static PyObject *
GBC_close (
           PyObject *self,
           PyObject *args
           )
{
  
  if ( !_initialized ) Py_RETURN_NONE;
  
  close_audio ();
  glDeleteTextures ( 1, &_screen.textureid );
  SDL_Quit ();
  if ( _rom.banks != NULL ) GBC_rom_free ( _rom );
  if ( _eram.ram != NULL ) free ( _eram.ram );
  _initialized= FALSE;
  Py_XDECREF ( _tracer.obj );
  
  Py_RETURN_NONE;
  
} /* end GBC_close */


static PyObject *
GBC_get_bank1 (
               PyObject *self,
               PyObject *args
               )
{
  
  CHECK_INITIALIZED;
  CHECK_ROM;
  
  return PyLong_FromLong ( GBC_mapper_get_bank1 () );
  
} /* end GBC_get_bank1 */


static PyObject *
GBC_get_cpal (
              PyObject *self,
              PyObject *args
              )
{
  
  static int bg[8][4];
  static int ob[8][4];
  
  PyObject *dict, *aux, *aux2, *aux3;
  int i, j;
  
  
  CHECK_INITIALIZED;
  CHECK_ROM;
  
  GBC_lcd_get_cpal ( bg, ob );
  dict= PyDict_New ();
  if ( dict == NULL ) return NULL;
  
  aux= PyTuple_New ( 8 );
  if ( aux == NULL ) goto error;
  if ( PyDict_SetItemString ( dict, "bg", aux ) == -1 )
    { Py_DECREF ( aux ); goto error; }
  for ( i= 0; i < 8; ++i )
    {
      aux2= PyTuple_New ( 4 );
      if ( aux2 == NULL ) goto error;
      PyTuple_SET_ITEM ( aux, i, aux2 );
      for ( j= 0; j < 4;  ++j )
        {
          aux3= PyLong_FromLong ( bg[i][j] );
          if ( aux3 == NULL ) goto error;
          PyTuple_SET_ITEM ( aux2, j, aux3 );
        }
    }
  
  aux= PyTuple_New ( 8 );
  if ( aux == NULL ) goto error;
  if ( PyDict_SetItemString ( dict, "ob", aux ) == -1 )
    { Py_DECREF ( aux ); goto error; }
  for ( i= 0; i < 8; ++i )
    {
      aux2= PyTuple_New ( 4 );
      if ( aux2 == NULL ) goto error;
      PyTuple_SET_ITEM ( aux, i, aux2 );
      for ( j= 0; j < 4;  ++j )
        {
          aux3= PyLong_FromLong ( ob[i][j] );
          if ( aux3 == NULL ) goto error;
          PyTuple_SET_ITEM ( aux2, j, aux3 );
        }
    }
  
  return dict;
  
 error:
  Py_DECREF ( dict );
  return NULL;
  
} /* end GBC_get_cpal */


static PyObject *
GBC_get_vram (
              PyObject *self,
              PyObject *args
              )
{
  
  PyObject *ret, *aux;
  const GBCu8 *vram;
  
  
  CHECK_INITIALIZED;
  CHECK_ROM;
  
  vram= GBC_lcd_get_vram ();
  ret= PyTuple_New ( 2 );
  if ( ret == NULL ) goto error;
  aux= PyBytes_FromStringAndSize ( (const char *) vram, 8192 );
  if ( aux == NULL ) goto error;
  PyTuple_SET_ITEM ( ret, 0, aux );
  aux= PyBytes_FromStringAndSize ( (const char *) &(vram[8192]), 8192 );
  if ( aux == NULL ) goto error;
  PyTuple_SET_ITEM ( ret, 1, aux );
  
  return ret;
  
 error:
  Py_DECREF ( ret );
  return NULL;
  
} /* end GBC_get_vram */


static PyObject *
GBC_get_rom (
             PyObject *self,
             PyObject *args
             )
{
  
  static const char *cgbflg2str[3]=
    {
      "Game works on CGB only",
      "Game supports CGB functions",
      "Old GB game"
    };
  
  PyObject *dict, *aux, *aux2;
  int n;
  GBC_RomHeader header;
  
  
  CHECK_INITIALIZED;
  CHECK_ROM;
  
  dict= PyDict_New ();
  if ( dict == NULL ) return NULL;
  
  /* Número de Pàgines. */
  aux= PyLong_FromLong ( _rom.nbanks );
  if ( aux == NULL ) goto error;
  if ( PyDict_SetItemString ( dict, "nbanks", aux ) == -1 )
    { Py_XDECREF ( aux ); goto error; }
  Py_XDECREF ( aux );
  
  /* Pàgines. */
  aux= PyTuple_New ( _rom.nbanks );
  if ( aux == NULL ) goto error;
  if ( PyDict_SetItemString ( dict, "banks", aux ) == -1 )
    { Py_XDECREF ( aux ); goto error; }
  Py_XDECREF ( aux );
  for ( n= 0; n < _rom.nbanks; ++n )
    {
      aux2= PyBytes_FromStringAndSize ( ((const char *) _rom.banks[n]),
                                        GBC_BANK_SIZE );
      if ( aux2 == NULL ) goto error;
      PyTuple_SET_ITEM ( aux, n, aux2 );
    }
  
  /* Obté la capçalera. */
  GBC_rom_get_header ( &_rom, &header );
  
  /* Comprovació logo nintendo. */
  aux= PyBool_FromLong ( GBC_rom_check_nintendo_logo ( &_rom ) );
  if ( PyDict_SetItemString ( dict, "logo_ok", aux ) == -1 )
    { Py_XDECREF ( aux ); goto error; }
  Py_XDECREF ( aux );
  
  /* Títol. */
  aux= PyUnicode_FromString ( header.title );
  if ( aux == NULL ) goto error;
  if ( PyDict_SetItemString ( dict, "title", aux ) == -1 )
    { Py_XDECREF ( aux ); goto error; }
  Py_XDECREF ( aux );
  
  /* Manufacturer code. */
  aux= PyUnicode_FromString ( header.manufacturer );
  if ( aux == NULL ) goto error;
  if ( PyDict_SetItemString ( dict, "manufacturer", aux ) == -1 )
    { Py_XDECREF ( aux ); goto error; }
  Py_XDECREF ( aux );
  
  /* Flag CGB. */
  aux= PyUnicode_FromString ( cgbflg2str[header.cgb_flag] );
  if ( aux == NULL ) goto error;
  if ( PyDict_SetItemString ( dict, "cgb_flag", aux ) == -1 )
    { Py_XDECREF ( aux ); goto error; }
  Py_XDECREF ( aux );
  
  /* License code. */
  if ( header.old_license == 0x33 )
    {
      aux= PyUnicode_FromString ( header.new_license );
      if ( aux == NULL ) goto error;
    }
  else
    {
      aux= PyLong_FromLong ( header.old_license );
      if ( aux == NULL ) goto error;
    }
  if ( PyDict_SetItemString ( dict, "license", aux ) == -1 )
    { Py_XDECREF ( aux ); goto error; }
  Py_XDECREF ( aux );
  
  /* SGB flag. */
  aux= PyBool_FromLong ( header.sgb_flag );
  if ( PyDict_SetItemString ( dict, "sgb_flag", aux ) == -1 )
    { Py_XDECREF ( aux ); goto error; }
  Py_XDECREF ( aux );
  
  /* Mapper. */
  aux= PyUnicode_FromString ( header.mapper );
  if ( aux == NULL ) goto error;
  if ( PyDict_SetItemString ( dict, "mapper", aux ) == -1 )
    { Py_XDECREF ( aux ); goto error; }
  Py_XDECREF ( aux );
  
  /* Rom size. */
  aux= PyLong_FromLong ( header.rom_size );
  if ( aux == NULL ) goto error;
  if ( PyDict_SetItemString ( dict, "rom_size", aux ) == -1 )
    { Py_XDECREF ( aux ); goto error; }
  Py_XDECREF ( aux );
  
  /* Ram size. */
  aux= PyLong_FromLong ( header.ram_size );
  if ( aux == NULL ) goto error;
  if ( PyDict_SetItemString ( dict, "ram_size", aux ) == -1 )
    { Py_XDECREF ( aux ); goto error; }
  Py_XDECREF ( aux );
  
  /* Japanse ROM. */
  aux= PyBool_FromLong ( header.japanese_rom );
  if ( PyDict_SetItemString ( dict, "japanese_rom", aux ) == -1 )
    { Py_XDECREF ( aux ); goto error; }
  Py_XDECREF ( aux );
  
  /* Version. */
  aux= PyLong_FromLong ( header.version );
  if ( aux == NULL ) goto error;
  if ( PyDict_SetItemString ( dict, "version", aux ) == -1 )
    { Py_XDECREF ( aux ); goto error; }
  Py_XDECREF ( aux );
  
  /* Checksum. */
  aux= PyLong_FromLong ( header.checksum );
  if ( aux == NULL ) goto error;
  if ( PyDict_SetItemString ( dict, "checksum", aux ) == -1 )
    { Py_XDECREF ( aux ); goto error; }
  Py_XDECREF ( aux );
  
  /* Comprovació checksum. */
  aux= PyBool_FromLong ( GBC_rom_check_checksum ( &_rom ) );
  if ( PyDict_SetItemString ( dict, "checksum_ok", aux ) == -1 )
    { Py_XDECREF ( aux ); goto error; }
  Py_XDECREF ( aux );
  
  /* Checksum global. */
  aux= PyLong_FromLong ( header.global_checksum );
  if ( aux == NULL ) goto error;
  if ( PyDict_SetItemString ( dict, "global_checksum", aux ) == -1 )
    { Py_XDECREF ( aux ); goto error; }
  Py_XDECREF ( aux );
  
  /* Comprovació checksum global. */
  aux= PyBool_FromLong ( GBC_rom_check_global_checksum ( &_rom ) );
  if ( PyDict_SetItemString ( dict, "global_checksum_ok", aux ) == -1 )
    { Py_XDECREF ( aux ); goto error; }
  Py_XDECREF ( aux );
  
  return dict;
  
 error:
  Py_XDECREF ( dict );
  return NULL;
  
} /* end GBC_get_rom */


static PyObject *
GBC_init_module (
        	 PyObject *self,
        	 PyObject *args
        	 )
{
  
  const char *err;
  Py_ssize_t size, n;
  PyObject *bios;
  const char *data;
  
  
  if ( _initialized ) Py_RETURN_NONE;
  
  bios= NULL;
  if ( !PyArg_ParseTuple ( args, "|O!", &PyBytes_Type, &bios ) )
    return NULL;
  if ( bios != NULL )
    {
      size= PyBytes_Size ( bios );
      if ( size != 0x900 )
        {
          PyErr_SetString ( GBCError, "Invalid BIOS size" );
          return NULL;
        }
      data= PyBytes_AS_STRING ( bios );
      for ( n= 0; n < size; ++n ) _bios.data[n]= (GBCu8) data[n];
      _bios.active= 1;
    }
  else _bios.active= 0;
  
  /* SDL */
  if ( SDL_Init ( SDL_INIT_VIDEO |
                  SDL_INIT_NOPARACHUTE |
                  SDL_INIT_AUDIO ) == -1 )
    {
      PyErr_SetString ( GBCError, SDL_GetError () );
      return NULL;
    }
  _screen.width= WIDTH;
  _screen.height= HEIGHT;
  _screen.surface= SDL_SetVideoMode ( _screen.width, _screen.height, 32,
                                      SDL_HWSURFACE | SDL_GL_DOUBLEBUFFER |
                                      SDL_OPENGL );
  if ( _screen.surface == NULL )
    {
      PyErr_SetString ( GBCError, SDL_GetError () );
      SDL_Quit ();
      return NULL;
    }
  init_GL ();
  init_palette ();
  screen_clear ();
  SDL_WM_SetCaption ( "GBC", "GBC" );
  if ( (err= init_audio ()) != NULL )
    {
      PyErr_SetString ( GBCError, err );
      SDL_Quit ();
      return NULL; 
    }
  
  /* ROM */
  _rom.banks= NULL;
  
  /* ERAM. */
  _eram.ram= NULL;
  
  /* Tracer. */
  _tracer.obj= NULL;
  
  _initialized= TRUE;
  
  Py_RETURN_NONE;
  
} /* end GBC_init_module */


static PyObject *
GBC_is_bios_mapped (
        	    PyObject *self,
        	    PyObject *args
        	    )
{
  
  CHECK_INITIALIZED;
  
  return PyBool_FromLong ( GBC_mem_is_bios_mapped () );
  
} /* end GBC_is_bios_mapped */


static PyObject *
GBC_loop_module (
        	 PyObject *self,
        	 PyObject *args
        	 )
{
  
  int n;
  
  
  CHECK_INITIALIZED;
  CHECK_ROM;
  
  for ( n= 0; n < NBUFF; ++n ) _audio.buffers[n].full= 0;
  SDL_PauseAudio ( 0 );
  GBC_loop ();
  SDL_PauseAudio ( 1 );
  
  Py_RETURN_NONE;
  
} /* end GBC_loop_module */


static PyObject *
GBC_set_rom (
             PyObject *self,
             PyObject *args
             )
{
  
  static const GBC_TraceCallbacks trace_callbacks=
    {
      mem_access,
      mapper_changed,
      cpu_step
    };
  static const GBC_Frontend frontend=
    {
      warning,
      get_external_ram,
      update_screen,
      check_signals,
      check_buttons,
      play_sound,
      update_rumble,
      &trace_callbacks
    };
  
  PyObject *bytes;
  Py_ssize_t size, n;
  char *banks;
  const char *data;
  GBC_Error err;
  
  
  CHECK_INITIALIZED;
  if ( !PyArg_ParseTuple ( args, "O!", &PyBytes_Type, &bytes ) )
    return NULL;
  
  size= PyBytes_Size ( bytes );
  if ( size <= 0 || size%(GBC_BANK_SIZE) != 0 )
    {
      PyErr_SetString ( GBCError, "Invalid ROM size" );
      return NULL;
    }
  if ( _rom.banks != NULL ) GBC_rom_free ( _rom );
  _rom.nbanks= size/(GBC_BANK_SIZE);
  GBC_rom_alloc ( _rom );
  if ( _rom.banks == NULL ) return PyErr_NoMemory ();
  data= PyBytes_AS_STRING ( bytes );
  banks= (char *) (_rom.banks);
  for ( n= 0; n < size; ++n ) banks[n]= data[n];
  
  /* Inicialitza el simulador. */
  screen_clear ();
  _control= 0;
  err= GBC_init ( _bios.active?_bios.data:NULL, &_rom, &frontend, NULL );
  if ( err != GBC_NOERROR )
    {
      switch ( err )
        {
        case GBC_EUNKMAPPER:
          PyErr_SetString ( GBCError, "Unknown mapper" );
          break;
        case GBC_WRONGLOGO:
          PyErr_SetString ( GBCError, "Nintendo logo test failed" );
          break;
        case GBC_WRONGCHKS:
          PyErr_SetString ( GBCError, "Checksum test failed" );
          break;
        case GBC_WRONGRAMSIZE:
          PyErr_SetString ( GBCError, "RAM size not supported" );
          break;
        case GBC_WRONGROMSIZE:
          PyErr_SetString ( GBCError, "ROM size not supported" );
          break;
        default:
          PyErr_SetString ( GBCError, "Unknown error" );
        }
      GBC_rom_free ( _rom );
      _rom.banks= NULL;
      return NULL;
    }
  
  Py_RETURN_NONE;
  
} /* end GBC_set_rom */


static PyObject *
GBC_set_tracer (
        	PyObject *self,
        	PyObject *args
        	)
{
  
  PyObject *aux;
  
  
  CHECK_INITIALIZED;
  if ( !PyArg_ParseTuple ( args, "O", &aux ) )
    return NULL;
  Py_XDECREF ( _tracer.obj );
  _tracer.obj= aux;
  Py_INCREF ( _tracer.obj );
  
  if ( _tracer.obj != NULL )
    {
      _tracer.has_mem_access= has_method ( _tracer.obj, "mem_access" );
      _tracer.has_cpu_step= has_method ( _tracer.obj, "cpu_step" );
      _tracer.has_mapper_changed= has_method ( _tracer.obj, "mapper_changed" );
    }
  
  Py_RETURN_NONE;
  
} /* end GBC_set_tracer */


static PyObject *
GBC_trace_module (
        	  PyObject *self,
        	  PyObject *args
        	  )
{
  
  int cc;
  
  
  CHECK_INITIALIZED;
  CHECK_ROM;
  
  SDL_PauseAudio ( 0 );
  cc= GBC_trace ();
  SDL_PauseAudio ( 1 );
  if ( PyErr_Occurred () != NULL ) return NULL;
  
  return PyLong_FromLong ( cc );
  
} /* end GBC_trace_module */




/************************/
/* INICIALITZACIÓ MÒDUL */
/************************/

static PyMethodDef GBCMethods[]=
  {
    { "close", GBC_close, METH_VARARGS,
      "Free module resources and close the module" },
    { "get_bank1", GBC_get_bank1, METH_VARARGS,
      "Get the number of the bank mapped to the bank 1" },
    { "get_cpal", GBC_get_cpal, METH_VARARGS,
      "Get a copy of the color palettes" },
    { "get_rom", GBC_get_rom, METH_VARARGS,
      "Get the ROM structured into a dictionary" },
    { "get_vram", GBC_get_vram, METH_VARARGS,
      "Get a copy of the VRAM" },
    { "init", GBC_init_module, METH_VARARGS,
      "Initialize the module. It's possible to specify"
      " a BIOS (type bytes) of size 0x900" },
    { "is_bios_mapped", GBC_is_bios_mapped, METH_VARARGS,
      "Return True if the BIOS is mapped" },
    { "loop", GBC_loop_module, METH_VARARGS,
      "Run the simulator into a loop and block" },
    { "set_rom", GBC_set_rom, METH_VARARGS,
      "Set a ROM into the simulator. The ROM should be of type bytes" },
    { "set_tracer", GBC_set_tracer, METH_VARARGS,
      "Set a python object to trace the execution. The object can"
      " implement one of these methods:\n"
      " - cpu_step: To trace the cpu execution. The first and second"
      " parameter are always the step identifier (INST|NMI|IRQ) and"
      " the next memory address. The other parameters depends on the"
      " identifier:\n"
      " -- INST: instruction id., operator 1 id., operator 2 id., bytes,"
      " tuple with extra info containing: (byte,desp,address/word,"
      "(branch.desp,branch.address))\n"
      " -- VBINT,LSINT,TIINT,SEINT,JOINT: nothing\n"
      " - mapper_changed: Called every time the memory mapper is changed."
      " No arguments are passed\n"
      " - mem_access: Called every time a memory access is done. The type"
      " of access, the address and the transmitted data "
      "is passed as arguments" },
    { "trace", GBC_trace_module, METH_VARARGS,
      "Executes the next instruction or interruption in trace mode" },
    { NULL, NULL, 0, NULL }
  };


static struct PyModuleDef GBCmodule=
  {
    PyModuleDef_HEAD_INIT,
    "GBC",
    NULL,
    -1,
    GBCMethods
  };


PyMODINIT_FUNC
PyInit_GBC (void)
{
  
  PyObject *m;
  
  
  m= PyModule_Create ( &GBCmodule );
  if ( m == NULL ) return NULL;
  
  _initialized= FALSE;
  GBCError= PyErr_NewException ( "GBC.error", NULL, NULL );
  Py_INCREF ( GBCError );
  PyModule_AddObject ( m, "error", GBCError );
  
  /* Tipus de pasos. */
  PyModule_AddIntConstant ( m, "INST", GBC_STEP_INST );
  PyModule_AddIntConstant ( m, "VBINT", GBC_STEP_VBINT );
  PyModule_AddIntConstant ( m, "LSINT", GBC_STEP_LSINT );
  PyModule_AddIntConstant ( m, "TIINT", GBC_STEP_TIINT );
  PyModule_AddIntConstant ( m, "SEINT", GBC_STEP_SEINT );
  PyModule_AddIntConstant ( m, "JOINT", GBC_STEP_JOINT );
  
  /* Mnemonics. */
  PyModule_AddIntConstant ( m, "UNK", GBC_UNK );
  PyModule_AddIntConstant ( m, "LD", GBC_LD );
  PyModule_AddIntConstant ( m, "PUSH", GBC_PUSH );
  PyModule_AddIntConstant ( m, "POP", GBC_POP );
  PyModule_AddIntConstant ( m, "LDI", GBC_LDI );
  PyModule_AddIntConstant ( m, "LDD", GBC_LDD );
  PyModule_AddIntConstant ( m, "ADD", GBC_ADD );
  PyModule_AddIntConstant ( m, "ADC", GBC_ADC );
  PyModule_AddIntConstant ( m, "SUB", GBC_SUB );
  PyModule_AddIntConstant ( m, "SBC", GBC_SBC );
  PyModule_AddIntConstant ( m, "AND", GBC_AND );
  PyModule_AddIntConstant ( m, "OR", GBC_OR );
  PyModule_AddIntConstant ( m, "XOR", GBC_XOR );
  PyModule_AddIntConstant ( m, "CP", GBC_CP );
  PyModule_AddIntConstant ( m, "INC", GBC_INC );
  PyModule_AddIntConstant ( m, "DEC", GBC_DEC );
  PyModule_AddIntConstant ( m, "DAA", GBC_DAA );
  PyModule_AddIntConstant ( m, "CPL", GBC_CPL );
  PyModule_AddIntConstant ( m, "CCF", GBC_CCF );
  PyModule_AddIntConstant ( m, "SCF", GBC_SCF );
  PyModule_AddIntConstant ( m, "NOP", GBC_NOP );
  PyModule_AddIntConstant ( m, "HALT", GBC_HALT );
  PyModule_AddIntConstant ( m, "DI", GBC_DI );
  PyModule_AddIntConstant ( m, "EI", GBC_EI );
  PyModule_AddIntConstant ( m, "RLCA", GBC_RLCA );
  PyModule_AddIntConstant ( m, "RLA", GBC_RLA );
  PyModule_AddIntConstant ( m, "RRCA", GBC_RRCA );
  PyModule_AddIntConstant ( m, "RRA", GBC_RRA );
  PyModule_AddIntConstant ( m, "RLC", GBC_RLC );
  PyModule_AddIntConstant ( m, "RL", GBC_RL );
  PyModule_AddIntConstant ( m, "RRC", GBC_RRC );
  PyModule_AddIntConstant ( m, "RR", GBC_RR );
  PyModule_AddIntConstant ( m, "SLA", GBC_SLA );
  PyModule_AddIntConstant ( m, "SRA", GBC_SRA );
  PyModule_AddIntConstant ( m, "SRL", GBC_SRL );
  PyModule_AddIntConstant ( m, "RLD", GBC_RLD );
  PyModule_AddIntConstant ( m, "RRD", GBC_RRD );
  PyModule_AddIntConstant ( m, "BIT", GBC_BIT );
  PyModule_AddIntConstant ( m, "SET", GBC_SET );
  PyModule_AddIntConstant ( m, "RES", GBC_RES );
  PyModule_AddIntConstant ( m, "JP", GBC_JP );
  PyModule_AddIntConstant ( m, "JR", GBC_JR );
  PyModule_AddIntConstant ( m, "CALL", GBC_CALL );
  PyModule_AddIntConstant ( m, "RET", GBC_RET );
  PyModule_AddIntConstant ( m, "RETI", GBC_RETI );
  PyModule_AddIntConstant ( m, "RST00", GBC_RST00 );
  PyModule_AddIntConstant ( m, "RST08", GBC_RST08 );
  PyModule_AddIntConstant ( m, "RST10", GBC_RST10 );
  PyModule_AddIntConstant ( m, "RST18", GBC_RST18 );
  PyModule_AddIntConstant ( m, "RST20", GBC_RST20 );
  PyModule_AddIntConstant ( m, "RST28", GBC_RST28 );
  PyModule_AddIntConstant ( m, "RST30", GBC_RST30 );
  PyModule_AddIntConstant ( m, "RST38", GBC_RST38 );
  PyModule_AddIntConstant ( m, "STOP", GBC_STOP );
  PyModule_AddIntConstant ( m, "SWAP", GBC_SWAP );
  
  /* MODES */
  PyModule_AddIntConstant ( m, "NONE", GBC_NONE );
  PyModule_AddIntConstant ( m, "A", GBC_A );
  PyModule_AddIntConstant ( m, "B", GBC_B );
  PyModule_AddIntConstant ( m, "C", GBC_C );
  PyModule_AddIntConstant ( m, "D", GBC_D );
  PyModule_AddIntConstant ( m, "E", GBC_E );
  PyModule_AddIntConstant ( m, "H", GBC_H );
  PyModule_AddIntConstant ( m, "L", GBC_L );
  PyModule_AddIntConstant ( m, "BYTE", GBC_BYTE );
  PyModule_AddIntConstant ( m, "DESP", GBC_DESP );
  PyModule_AddIntConstant ( m, "SPdd", GBC_SPdd );
  PyModule_AddIntConstant ( m, "pHL", GBC_pHL );
  PyModule_AddIntConstant ( m, "pBC", GBC_pBC );
  PyModule_AddIntConstant ( m, "pDE", GBC_pDE );
  PyModule_AddIntConstant ( m, "ADDR", GBC_ADDR );
  PyModule_AddIntConstant ( m, "BC", GBC_BC );
  PyModule_AddIntConstant ( m, "DE", GBC_DE );
  PyModule_AddIntConstant ( m, "HL", GBC_HL );
  PyModule_AddIntConstant ( m, "SP", GBC_SP );
  PyModule_AddIntConstant ( m, "AF", GBC_AF );
  PyModule_AddIntConstant ( m, "B0", GBC_B0 );
  PyModule_AddIntConstant ( m, "B1", GBC_B1 );
  PyModule_AddIntConstant ( m, "B2", GBC_B2 );
  PyModule_AddIntConstant ( m, "B3", GBC_B3 );
  PyModule_AddIntConstant ( m, "B4", GBC_B4 );
  PyModule_AddIntConstant ( m, "B5", GBC_B5 );
  PyModule_AddIntConstant ( m, "B6", GBC_B6 );
  PyModule_AddIntConstant ( m, "B7", GBC_B7 );
  PyModule_AddIntConstant ( m, "WORD", GBC_WORD );
  PyModule_AddIntConstant ( m, "F_NZ", GBC_F_NZ );
  PyModule_AddIntConstant ( m, "F_Z", GBC_F_Z );
  PyModule_AddIntConstant ( m, "F_NC", GBC_F_NC );
  PyModule_AddIntConstant ( m, "F_C", GBC_F_C );
  PyModule_AddIntConstant ( m, "BRANCH", GBC_BRANCH );
  PyModule_AddIntConstant ( m, "pB", GBC_pB );
  PyModule_AddIntConstant ( m, "pC", GBC_pC );
  PyModule_AddIntConstant ( m, "pD", GBC_pD );
  PyModule_AddIntConstant ( m, "pE", GBC_pE );
  PyModule_AddIntConstant ( m, "pH", GBC_pH );
  PyModule_AddIntConstant ( m, "pL", GBC_pL );
  PyModule_AddIntConstant ( m, "pA", GBC_pA );
  PyModule_AddIntConstant ( m, "pBYTE", GBC_pBYTE );
  PyModule_AddIntConstant ( m, "pFF00n", GBC_pFF00n );
  PyModule_AddIntConstant ( m, "pFF00C", GBC_pFF00C );
  
  /* TIPUS D'ACCESSOS A MEMÒRIA. */
  PyModule_AddIntConstant ( m, "READ", GBC_READ );
  PyModule_AddIntConstant ( m, "WRITE", GBC_WRITE );
  
  return m;

} /* end PyInit_GBC */
