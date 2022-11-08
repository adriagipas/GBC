from distutils.core import setup, Extension

module= Extension ( 'GBC',
                    sources= [ 'gbcmodule.c',
                               '../src/apu.c',
                               '../src/cpu.c',
                               '../src/cpu_dis.c',
                               '../src/joypad.c',
                               '../src/lcd.c',
                               '../src/main.c',
                               '../src/mem.c',
                               '../src/rom.c',
                               '../src/mapper.c',
                               '../src/timers.c' ],
                    depends= [ '../src/GBC.h' ],
                    libraries= [ 'SDL', 'GL' ],
                    include_dirs= [ '../src' ])

setup ( name= 'GBC',
        version= '1.0',
        description= 'GameBoy Color simulator',
        ext_modules= [module] )
